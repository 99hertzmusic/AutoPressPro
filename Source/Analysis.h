#pragma once
// Learning: listens to a few seconds of audio, works out what it is, then for every compressor
// type picks AUTO attack / release / ratio and calibrates threshold + makeup by running the real
// engine over the captured audio.
#include <juce_data_structures/juce_data_structures.h>
#include "CompEngine.h"
#include <vector>

namespace ap
{
enum Material { mixed = 0, drums, sustained, rhythmic, numMaterials };
inline const char* materialName (int m)
{
    static const char* n[] = { "MIXED", "DRUMS", "SUSTAINED", "RHYTHMIC" };
    return n[m >= 0 && m < numMaterials ? m : 0];
}

struct PerType
{
    float threshDb = -18.0f, ratio = 2.0f, kneeDb = 6.0f;
    float attackMs = 10.0f, relFastMs = 100.0f, relSlowMs = 400.0f;
    float makeupDb = 0.0f, avgGrDb = 0.0f, refLevelDb = -12.0f;
};

struct Learned
{
    bool  valid = false;
    int   material = mixed;
    float flatness = 0.0f;              // spectral flatness above 200 Hz (noisy drums high, notes low)
    float onsetDb = 0.0f;               // % of time with a 6 dB+ jump above 200 Hz (transient share)
    float bpm = 120.0f, peakDb = -6.0f, rmsDb = -20.0f, crestDb = 12.0f, drDb = 6.0f, targetGrDb = 4.0f;
    PerType t[numTypes];

    static Learned defaults();                          // safe settings before anything is learned
    juce::ValueTree toTree() const;
    static Learned fromTree (const juce::ValueTree&);   // invalid if missing
};

// AUTO choices for a type on a material (no threshold: that comes from calibration).
PerType autoSettings (int type, int material, float bpm, float learnedCleanRatio, float learnedCleanKnee);

// AUTO release memory by material: on notes with sharp starts (and mixes) the slow stage holds
// more of the gain reduction and charges faster, so the GR is still there when the next note hits
// (an SSL-style auto release). Drums and sustained material keep the type's own memory.
inline void memoryFor (int type, int material, EngineSettings& es)
{
    const auto& m = model (type);
    es.slowShare = m.slowShare; es.slowChargeMs = m.slowChargeMs;
    if (material == rhythmic) { es.slowShare = std::max (m.slowShare, 0.85f); es.slowChargeMs = std::min (m.slowChargeMs, 100.0f); }
    else if (material == mixed) { es.slowShare = std::max (m.slowShare, 0.75f); es.slowChargeMs = std::min (m.slowChargeMs, 200.0f); }
}

// The whole learn: interleaved audio, `channels` wide.
Learned analyse (const std::vector<float>& audio, int channels, double sampleRate, double bpm);

// Detector feed used by both live and simulation: stereo-linked max |x| and max x^2.
inline void detectorFeed (const float* const* ch, int numCh, int i, float& xAbs, float& xSq)
{
    xAbs = 0.0f; xSq = 0.0f;
    for (int c = 0; c < numCh; ++c) { const float v = ch[c][i]; xAbs = std::max (xAbs, std::abs (v)); }
    xSq = xAbs * xAbs;
}
} // namespace ap
