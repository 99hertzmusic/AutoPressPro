#pragma once
// AutoPress Pro compressor core. The SAME engine runs live and inside the learning simulation,
// so the threshold the learner picks gives the gain reduction it aimed for.
//
// Signal flow (per type): detector -> static curve -> attack/release ballistics (dB domain)
//                         -> gain -> type saturation -> (processor: drive, makeup, mix, ceiling)
#include "Features.h"
#include <juce_core/juce_core.h>
#include <algorithm>
#include <cmath>

namespace ap
{
inline float dbToGain (float db) { return std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float g)  { return 20.0f * std::log10 (std::max (g, 1.0e-9f)); }

// Character of each type. Detector, knee, curve, auto-release model and saturation all differ.
struct TypeModel
{
    // Detector: peak-hold. It holds each peak for longer than half a bass cycle, so the level it
    // reads doesn't ripple with the waveform (no gain wobble = no distortion on kicks and bass),
    // yet it reacts instantly, so the attack steps stay exact.
    float holdMs;            // peak hold
    float offsetDb;          // level offset: -3 dB reads a sine like an RMS meter; 0 = pure peak
    float kneeDb;            // soft knee width (CLEAN's comes from learning)
    bool  variMu;            // ratio rises with level (vari-mu)
    float slowShare;         // AUTO release: share of the GR held by the slow (memory) stage
    float slowChargeMs;      // AUTO release: how fast the slow stage charges under sustained compression
    float satBase, satGr;    // saturation amount = base + gr * (GR / 12 dB)
    float satBias;           // asymmetry -> even harmonics
};

inline const TypeModel& model (int t)
{
    static const TypeModel m[numTypes] = {
        // No built-in saturation on any type (Sam: "don't be distorting drums"). DRIVE is the only
        // thing that adds harmonics, and only when it's turned up. The types differ in dynamics.
        //  hold   offset  knee   variMu share charge  satB   satGr  bias
        { 15.0f, -3.0f,  5.0f, false, 0.60f, 250.0f, 0.00f, 0.00f, 0.00f },  // CLEAN: transparent VCA
        { 15.0f, -3.0f,  6.0f, false, 0.70f, 350.0f, 0.00f, 0.00f, 0.00f },  // GLUE: SSL bus VCA, auto release
        { 12.0f,  0.0f,  1.5f, false, 0.40f, 120.0f, 0.00f, 0.00f, 0.00f },  // PUNCH: FET-style peak, hard knee, fast
        { 15.0f, -4.0f, 12.0f, false, 0.55f, 500.0f, 0.00f, 0.00f, 0.00f },  // SMOOTH: opto, very soft, 2-stage release
        { 15.0f, -4.0f,  0.0f, true,  0.75f, 300.0f, 0.00f, 0.00f, 0.00f },  // WARM: vari-mu (ratio rises with level), slow
    };
    return m[t >= 0 && t < numTypes ? t : 0];
}

// Gain reduction (dB, >= 0) for a detector level.
inline float staticGr (float levelDb, float threshDb, float ratio, float kneeDb, bool variMu)
{
    const float over  = levelDb - threshDb;
    const float slope = 1.0f - 1.0f / std::max (1.0f, ratio);
    if (variMu)
    {
        // Ratio climbs from 1:1 at 6 dB under the threshold to the full ratio 12 dB over it.
        if (over <= -6.0f) return 0.0f;
        if (over <= 12.0f) return slope * (over + 6.0f) * (over + 6.0f) / 36.0f;
        return slope * (9.0f + over - 12.0f);
    }
    if (kneeDb <= 0.01f) return over > 0.0f ? over * slope : 0.0f;
    const float h = kneeDb * 0.5f;
    if (over <= -h) return 0.0f;
    if (over >= h)  return over * slope;
    const float k = over + h;
    return slope * k * k / (2.0f * kneeDb);
}

// Small-signal-unity asymmetric soft saturator (tanh with bias = even + odd harmonics).
inline float asymSat (float u, float drive, float bias)
{
    const float tb = std::tanh (drive * bias);
    const float norm = drive * (1.0f - tb * tb);
    return (std::tanh (drive * (u + bias)) - tb) / norm;
}

// Every type looks 1.5 ms ahead (reported to the host as latency): the gain can start coming down
// just before a note's very first peak, which no attack time can catch after the fact.
inline constexpr float lookaheadMs = 1.5f;

struct EngineSettings
{
    float attackMs = 10.0f;
    float releaseMs = 300.0f;   // used when autoRelease == false
    bool  autoRelease = true;
    float relFastMs = 100.0f, relSlowMs = 400.0f;
    float slowShare = -1.0f, slowChargeMs = -1.0f;   // AUTO release memory; < 0 = the type's own
};

class Engine
{
public:
    void prepare (double sampleRate) { fs = (float) sampleRate; reset(); setType (type); }
    void reset() { det = curMax = prevMax = 0.0f; holdCnt = 0; gr = grFast = grSlow = grS = 0.0f; }
    void setType (int t)
    {
        type = t; m = &model (t);
        holdN = std::max (1, (int) (m->holdMs * 0.001f * fs));
        aDec = coef (3.0f);
    }
    int  getType() const { return type; }
    float getGr() const { return grS; }

    // Take over another engine's state (used when switching type, so the crossfade starts level).
    void copyStateFrom (const Engine& o)
    {
        gr = o.gr; grFast = o.grFast; grSlow = o.grSlow; grS = o.grS;
        det = o.det; holdCnt = std::min (o.holdCnt, holdN);
    }

    void setTiming (const EngineSettings& s)
    {
        if (coefsValid && juce::exactlyEqual (s.attackMs, cur.attackMs) && juce::exactlyEqual (s.releaseMs, cur.releaseMs) && s.autoRelease == cur.autoRelease
            && juce::exactlyEqual (s.relFastMs, cur.relFastMs) && juce::exactlyEqual (s.relSlowMs, cur.relSlowMs)
            && juce::exactlyEqual (s.slowShare, cur.slowShare) && juce::exactlyEqual (s.slowChargeMs, cur.slowChargeMs)) return;
        cur = s; coefsValid = true;
        aA  = coef (s.attackMs);
        // The peak hold already keeps the level up for holdMs (+ ~3 ms fall), so take that off the
        // release: a 100 ms step then measures 100 ms, not 118.
        const float holdComp = m->holdMs + 3.0f;
        aR  = coef (std::max (5.0f, s.releaseMs - holdComp));
        aRF = coef (std::max (5.0f, s.relFastMs - holdComp));
        aRS = coef (s.relSlowMs);
        aC  = coef (s.slowChargeMs > 0.0f ? s.slowChargeMs : m->slowChargeMs);
        share = s.slowShare >= 0.0f ? s.slowShare : m->slowShare;
    }
    void invalidate() { coefsValid = false; }

    // xAbs / xSq: stereo-linked detector input (max over channels). Returns gain reduction in dB.
    float process (float xAbs, float xSq, float threshDb, float ratio, float kneeDb)
    {
        juce::ignoreUnused (xSq);
        if (xAbs >= det)                { det = xAbs; holdCnt = holdN; }
        else if (xAbs >= 0.85f * det)   holdCnt = holdN;    // a peak within 1.4 dB keeps the hold: no mid-cycle dips
        else if (holdCnt > 0)           --holdCnt;
        else                    det = std::max (xAbs, det * aDec);
        const float levelDb = gainToDb (det) + m->offsetDb;
        const float target = std::min (40.0f, staticGr (levelDb, threshDb, ratio, kneeDb, m->variMu));

        if (! cur.autoRelease)
        {
            gr = target > gr ? aA * gr + (1.0f - aA) * target : aR * gr + (1.0f - aR) * target;
            grFast = grSlow = gr;
        }
        else
        {
            // Program-dependent release: a fast stage for transients and a slow "memory" stage that
            // only charges under sustained compression. Short hits recover fast, long squeezes slowly.
            grFast = target > grFast ? aA * grFast + (1.0f - aA) * target : aRF * grFast + (1.0f - aRF) * target;
            const float st = target * share;
            grSlow = st > grSlow ? aC * grSlow + (1.0f - aC) * st : aRS * grSlow + (1.0f - aRS) * st;
            gr = std::max (grFast, grSlow);
        }
        grS = gr;
        return gr;
    }

    float detectorLevelDb() const { return gainToDb (det) + m->offsetDb; }

private:
    float coef (float ms) const { return std::exp (-1.0f / (std::max (0.001f, ms) * 0.001f * fs)); }

    float fs = 48000.0f;
    int type = clean;
    const TypeModel* m = &model (clean);
    EngineSettings cur;
    bool coefsValid = false;
    float aA = 0, aR = 0, aRF = 0, aRS = 0, aC = 0, aDec = 0, aS = 0, grS = 0.0f;
    int holdN = 1, holdCnt = 0;
    float share = 0.6f, curMax = 0.0f, prevMax = 0.0f;
    float det = 0.0f, gr = 0.0f, grFast = 0.0f, grSlow = 0.0f;
};
} // namespace ap
