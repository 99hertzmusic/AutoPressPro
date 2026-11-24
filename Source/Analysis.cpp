#include "Analysis.h"
#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <deque>

namespace ap
{
// ── Loudness stats (BS.1770-style K-weighting, ported from AutoPress 1.1) ─────
namespace
{
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    float process (float x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return (float) y;
    }
    static Biquad highPass (double fs, double f, double q)
    {
        Biquad b; const double w = juce::MathConstants<double>::twoPi * f / fs, s = std::sin (w), c = std::cos (w), al = s / (2 * q), a0 = 1 + al;
        b.b0 = (1 + c) * 0.5 / a0; b.b1 = -(1 + c) / a0; b.b2 = b.b0; b.a1 = -2 * c / a0; b.a2 = (1 - al) / a0; return b;
    }
    static Biquad highShelf (double fs, double f, double gainDb, double q)
    {
        Biquad b; const double A = std::pow (10.0, gainDb / 40.0), w = juce::MathConstants<double>::twoPi * f / fs, s = std::sin (w), c = std::cos (w);
        const double al = s / (2 * q), be = 2 * std::sqrt (A) * al, a0 = (A + 1) - (A - 1) * c + be;
        b.b0 = A * ((A + 1) + (A - 1) * c + be) / a0; b.b1 = -2 * A * ((A - 1) + (A + 1) * c) / a0;
        b.b2 = A * ((A + 1) + (A - 1) * c - be) / a0; b.a1 = 2 * ((A - 1) - (A + 1) * c) / a0; b.a2 = ((A + 1) - (A - 1) * c - be) / a0; return b;
    }
};

double powerToLufs (double p) { return p <= 1.0e-16 ? -120.0 : -0.691 + 10.0 * std::log10 (p); }

double percentile (std::vector<float> v, double amount)
{
    if (v.empty()) return -60.0;
    const auto idx = (size_t) juce::jlimit (0.0, (double) v.size() - 1.0, std::floor (amount * ((double) v.size() - 1.0)));
    std::nth_element (v.begin(), v.begin() + (std::ptrdiff_t) idx, v.end());
    return v[idx];
}

struct Stats { double rmsDb = -120, drDb = 0, p50PeakDb = -120, p99PeakDb = -120, onsetDb = 0, flatness = 0; };   // onsetDb = % of time with a 6 dB+ jump

Stats measure (const std::vector<float>& audio, int ch, double fs)
{
    Stats st;
    const size_t frames = audio.size() / (size_t) ch;
    std::array<Biquad, 2> shelf, hp;
    for (int c = 0; c < 2; ++c) { shelf[(size_t) c] = Biquad::highShelf (fs, 1681.97, 4.0, 0.7072); hp[(size_t) c] = Biquad::highPass (fs, 38.135, 0.5003); }

    const size_t hop = (size_t) std::max (1.0, fs * 0.1), stFrames = (size_t) std::max (1.0, fs * 3.0), momFrames = (size_t) std::max (1.0, fs * 0.4);
    std::deque<float> stWin, momWin; double stSum = 0, momSum = 0, activeSq = 0; uint64_t activeN = 0;
    std::vector<float> shortTerm, momentary, peaks, onsets;
    peaks.reserve (frames);
    // Transient measure that bass can't hide: above 200 Hz, how far a 5 ms RMS jumps over a 150 ms
    // RMS (both RMS, so a steady waveform reads ~0 dB whatever its shape). Drums: big, frequent jumps.
    // Held notes / pads: none (a note start every half bar is under the 95th percentile).
    std::array<Biquad, 2> hp200;
    for (auto& b : hp200) b = Biquad::highPass (fs, 200.0, 0.707);
    float envFast = 0, envSlow = 0;
    const float fastC = std::exp (-1.0f / (0.005f * (float) fs)), slowC = std::exp (-1.0f / (0.150f * (float) fs));
    const size_t onsetHop = (size_t) std::max (1.0, fs * 0.001);
    const float gate = dbToGain (-60.0f);

    for (size_t f = 0; f < frames; ++f)
    {
        double pw = 0; float pk = 0;
        for (int c = 0; c < ch; ++c)
        {
            const float raw = audio[f * (size_t) ch + (size_t) c];
            const auto k = (size_t) std::min (c, 1);
            const float w = hp[k].process (shelf[k].process (raw));
            pw += (double) w * w; pk = std::max (pk, std::abs (raw));
        }
        pw /= ch;
        {
            float h = 0;
            for (int c = 0; c < ch; ++c) h = std::max (h, std::abs (hp200[(size_t) std::min (c, 1)].process (audio[f * (size_t) ch + (size_t) c])));
            envFast = fastC * envFast + (1 - fastC) * h * h;
            envSlow = slowC * envSlow + (1 - slowC) * h * h;
            if (f % onsetHop == 0 && envSlow > 1e-8f)
                onsets.push_back (10.0f * std::log10 ((envFast + 1e-12f) / envSlow));
        }
        stWin.push_back ((float) pw); stSum += pw; if (stWin.size() > stFrames) { stSum -= stWin.front(); stWin.pop_front(); }
        momWin.push_back ((float) pw); momSum += pw; if (momWin.size() > momFrames) { momSum -= momWin.front(); momWin.pop_front(); }
        if (pk > gate) { activeSq += pw * ch; activeN += (uint64_t) ch; peaks.push_back (gainToDb (pk)); }
        if (f % hop == 0)
        {
            if (stWin.size() >= stFrames) { const double l = powerToLufs (stSum / (double) stWin.size()); if (l > -60) shortTerm.push_back ((float) l); }
            if (momWin.size() >= momFrames) { const double l = powerToLufs (momSum / (double) momWin.size()); if (l > -60) momentary.push_back ((float) l); }
        }
    }
    st.rmsDb = activeN > 0 && activeSq > 1e-16 ? 10.0 * std::log10 (activeSq / (double) activeN) : -120.0;
    auto& src = shortTerm.size() >= 24 ? shortTerm : momentary;
    if (! src.empty()) st.drDb = std::max (0.0, percentile (src, 0.95) - percentile (src, 0.20));
    st.p50PeakDb = percentile (peaks, 0.50);
    st.p99PeakDb = percentile (peaks, 0.99);
    size_t jumps = 0;
    for (auto o : onsets) if (o > 6.0f) ++jumps;
    st.onsetDb = onsets.empty() ? 0.0 : 100.0 * (double) jumps / (double) onsets.size();

    // Spectral flatness above 200 Hz, loudness-weighted: noisy hits (snares, hats, claps) score high,
    // notes (piano, bass, vocals, synths) score near zero. Tells drums from staccato notes.
    {
        constexpr int order = 10, N = 1 << order;
        juce::dsp::FFT fft (order);
        juce::dsp::WindowingFunction<float> win ((size_t) N, juce::dsp::WindowingFunction<float>::hann, false);
        std::vector<float> buf ((size_t) N * 2);
        std::vector<std::pair<double, double>> fr;   // (energy, flatness)
        const int lo = (int) std::ceil (200.0 * N / fs);
        for (size_t f0 = 0; f0 + (size_t) N <= frames; f0 += (size_t) N / 2)
        {
            std::fill (buf.begin(), buf.end(), 0.0f);
            for (int i = 0; i < N; ++i)
            {
                float m = 0; for (int c = 0; c < ch; ++c) m += audio[(f0 + (size_t) i) * (size_t) ch + (size_t) c];
                buf[(size_t) i] = m / (float) ch;
            }
            win.multiplyWithWindowingTable (buf.data(), (size_t) N);
            fft.performFrequencyOnlyForwardTransform (buf.data());
            double e = 0, lg = 0, ar = 0; int cnt = 0;
            for (int k = 0; k <= N / 2; ++k) e += (double) buf[(size_t) k] * buf[(size_t) k];
            for (int k = lo; k <= N / 2; ++k) { const double pw = (double) buf[(size_t) k] * buf[(size_t) k] + 1e-12; lg += std::log (pw); ar += pw; ++cnt; }
            if (cnt > 0) fr.push_back ({ e, std::exp (lg / cnt) / (ar / cnt) });
        }
        double emax = 0; for (auto& v : fr) emax = std::max (emax, v.first);
        double sw = 0, sf = 0;
        for (auto& v : fr) if (v.first > emax * 1e-4) { sw += v.first; sf += v.first * v.second; }
        st.flatness = sw > 0 ? sf / sw : 0.0;
    }
    return st;
}

// Old AutoPress mapping: wider dynamics = more compression, 2..7 dB average GR.
double targetGrForDynamics (double dr)
{
    double t;
    if      (dr < 4.0)  t = 2.0;
    else if (dr < 8.0)  t = 2.0 + (dr - 4.0) * 0.50;
    else if (dr < 14.0) t = 4.0 + (dr - 8.0) * 0.28;
    else                t = 5.7 + (dr - 14.0) * 0.12;
    return juce::jlimit (2.0, 7.0, t);
}

struct SimResult { float avgGr = 0, rmsInDb = -120, rmsOutDb = -120, peakOutDb = -120; };

// Runs the real engine (AUTO timing) over the window; first `preroll` frames only settle the state.
SimResult simulate (const std::vector<float>& audio, int ch, double fs, size_t start, size_t len, size_t preroll,
                    int type, int material, const PerType& s)
{
    Engine e; e.setType (type); e.prepare (fs);
    EngineSettings es; es.attackMs = s.attackMs; es.autoRelease = true; es.relFastMs = s.relFastMs; es.relSlowMs = s.relSlowMs;
    memoryFor (type, material, es);
    e.setTiming (es);
    const float gate = dbToGain (-60.0f);
    const size_t frames = audio.size() / (size_t) ch;
    const size_t la = (size_t) std::lround (lookaheadMs * 0.001 * fs);   // same lookahead as live
    double grSum = 0, inSq = 0, outSq = 0; size_t n = 0; float pkOut = 0;
    const size_t from = start >= preroll ? start - preroll : 0;
    for (size_t f = from; f < std::min (frames, start + len + la); ++f)
    {
        float xAbs = 0;
        for (int c = 0; c < ch; ++c) xAbs = std::max (xAbs, std::abs (audio[f * (size_t) ch + (size_t) c]));
        const float gr = e.process (xAbs, xAbs * xAbs, s.threshDb, s.ratio, s.kneeDb);
        if (f < la || f - la < start) continue;
        const size_t k = f - la;                 // the gain lands on audio `la` frames back
        float xa = 0;
        for (int c = 0; c < ch; ++c) xa = std::max (xa, std::abs (audio[k * (size_t) ch + (size_t) c]));
        if (xa <= gate) continue;
        const float g = dbToGain (-gr);
        for (int c = 0; c < ch; ++c)
        {
            const float x = audio[k * (size_t) ch + (size_t) c];
            inSq += (double) x * x; outSq += (double) (x * g) * (x * g); pkOut = std::max (pkOut, std::abs (x * g));
        }
        grSum += gr; ++n;
    }
    SimResult r;
    if (n == 0) return r;
    r.avgGr = (float) (grSum / (double) n);
    r.rmsInDb = (float) (10.0 * std::log10 (inSq / (double) (n * (size_t) ch) + 1e-20));
    r.rmsOutDb = (float) (10.0 * std::log10 (outSq / (double) (n * (size_t) ch) + 1e-20));
    r.peakOutDb = gainToDb (pkOut);
    return r;
}
} // namespace

// ── AUTO choices ────────────────────────────────────────────────────────────────
PerType autoSettings (int type, int material, float bpm, float cleanRatio, float cleanKnee)
{
    const float beat = 60.0f / juce::jlimit (60.0f, 200.0f, bpm);
    const int mt = juce::jlimit (0, numMaterials - 1, material);
    PerType p;
    p.kneeDb = model (type).kneeDb;

    // AUTO timing per [type][material]. Material order: MIXED, DRUMS, SUSTAINED, RHYTHMIC.
    //  attack ms | slow (memory) release in beats, clamped to [lo, hi] seconds | fast release = factor x slow
    //
    //  DRUMS get the slower attack so each hit's transient gets through (the punch) and a release
    //  that is back before the next beat.
    //  RHYTHMIC = notes with sharp starts (piano stabs, staccato bass, a full mix): an engineer would
    //  catch the note peaks (3-10 ms) and keep the gain reduction engaged between notes with a long,
    //  program-dependent release. A drum-style 30 ms attack + short release here lets every note start
    //  through and squashes the body = peakier than the dry (measured on Sam's piano + bass loops).
    //  SUSTAINED (pads, legato vocals): fast enough attack to hold the level, slow smooth release.
    struct T { float atk, beats, lo, hi, fast; };
    static const T tab[numTypes][numMaterials] = {
        //            MIXED                          DRUMS                          SUSTAINED                       RHYTHMIC
        /* CLEAN  */ { { 8.0f, 2.0f, 0.4f, 1.0f, 0.3f }, { 30.0f, 0.5f, 0.12f, 0.5f, 0.35f }, { 3.0f, 1.5f, 0.3f, 1.2f, 0.35f }, { 3.0f, 2.0f, 0.5f, 1.2f, 0.25f } },
        /* GLUE   */ { {10.0f, 2.0f, 0.4f, 1.2f, 0.1f }, { 30.0f, 1.0f, 0.25f, 0.6f, 0.15f }, {10.0f, 2.0f, 0.6f, 1.2f, 0.1f  }, { 3.0f, 2.5f, 0.6f, 1.2f, 0.1f  } },
        /* PUNCH  */ { { 0.5f, 1.0f, 0.2f, 0.5f, 0.4f }, {  3.5f, 0.25f,0.06f,0.15f,0.5f }, { 0.8f, 1.0f, 0.2f, 0.6f, 0.4f  }, { 0.3f, 1.0f, 0.25f, 0.6f, 0.35f } },
        /* SMOOTH */ { {10.0f, 2.0f, 0.8f, 1.2f, 0.06f}, {  5.0f, 1.0f, 0.5f, 0.5f, 0.12f }, {10.0f, 3.0f, 1.5f, 1.5f, 0.04f }, { 3.0f, 2.0f, 1.0f, 1.2f, 0.06f } },
        /* WARM   */ { {20.0f, 1.5f, 0.4f, 1.0f, 0.3f }, { 30.0f, 1.0f, 0.3f, 0.6f, 0.3f  }, {15.0f, 2.0f, 0.6f, 1.2f, 0.3f  }, { 3.0f, 2.0f, 0.6f, 1.2f, 0.3f  } },
    };
    const auto& e = tab[type][mt];
    const float slow = juce::jlimit (e.lo, e.hi, e.beats * beat);
    p.attackMs  = e.atk;
    p.relSlowMs = slow * 1000.0f;
    p.relFastMs = juce::jmax (40.0f, e.fast * slow * 1000.0f);

    switch (type)
    {
        case clean:  p.ratio = cleanRatio; p.kneeDb = cleanKnee; break;
        case glue:   p.ratio = 2.0f;  break;
        case punch:  p.ratio = 4.0f;  break;
        case smooth: p.ratio = 3.0f;  break;
        case warm:   p.ratio = 6.0f;  break;   // the most it climbs to; vari-mu starts gentler
        default: break;
    }
    return p;
}

Learned Learned::defaults()
{
    Learned l;
    for (int t = 0; t < numTypes; ++t)
    {
        l.t[t] = autoSettings (t, mixed, 120.0f, 2.5f, 5.0f);
        l.t[t].threshDb = -18.0f;
    }
    return l;
}

Learned analyse (const std::vector<float>& audio, int ch, double fs, double bpm)
{
    Learned L = Learned::defaults();
    L.valid = true;
    L.bpm = (float) juce::jlimit (60.0, 200.0, bpm);
    const size_t frames = ch > 0 ? audio.size() / (size_t) ch : 0;
    if (frames < (size_t) (fs * 0.5)) return L;

    const auto st = measure (audio, ch, fs);
    if (st.rmsDb < -100.0) return L;

    const double crest  = st.p99PeakDb - st.rmsDb;
    const double spread = st.p99PeakDb - st.p50PeakDb;
    // Drums: lots of 6 dB+ jumps above 200 Hz (or very peaky). Sustained: almost none.
    // (The old crest-only test called a full mix with a loud bass "sustained" and gave it a 3 ms attack.)
    // Sharp starts (6 dB+ jumps) + noisy spectrum = DRUMS. Sharp starts on notes = RHYTHMIC
    // (piano stabs, staccato bass, a full mix). Few jumps = SUSTAINED. Otherwise MIXED.
    const bool sharp   = st.onsetDb > 3.5 || ((crest > 16.0 || spread > 18.0) && st.onsetDb > 2.0);
    const bool noisy   = st.flatness > 0.016;
    const bool transient = sharp && noisy;
    const bool rhythm    = sharp && ! noisy;
    const bool sustain   = ! sharp && st.onsetDb < 1.5 && crest < 12.0;
    L.material = transient ? drums : rhythm ? rhythmic : sustain ? sustained : mixed;
    L.flatness = (float) st.flatness;
    L.peakDb = (float) st.p99PeakDb; L.rmsDb = (float) st.rmsDb; L.crestDb = (float) crest; L.drDb = (float) st.drDb; L.onsetDb = (float) st.onsetDb;

    double target = targetGrForDynamics (st.drDb);
    if (transient) target = std::max (target, juce::jlimit (2.5, 6.0, (crest - 10.0) * 0.30));
    // Drums: a little lighter by default (2.5 dB floor) so the hits stay open, like a drum-bus SSL.
    target = juce::jlimit (transient ? 2.5 : 3.0, transient ? 6.0 : (sustain ? 7.0 : 6.5), target);
    L.targetGrDb = (float) target;

    const float cleanRatio = (float) juce::jlimit (2.0, 5.0, transient ? 2.5 : sustain ? 4.0 : rhythm ? 3.0 : 2.5 + (target - 2.5) * 0.22);
    const float cleanKnee  = transient ? 8.0f : (sustain ? 3.0f : 5.0f);

    // Loudest 4 s window, so calibration sees real material rather than a gap between hits.
    const size_t win = std::min (frames, (size_t) (fs * 4.0));
    size_t start = 0;
    if (frames > win)
    {
        const size_t hop = std::max ((size_t) 1, (size_t) (fs * 0.1));
        std::vector<double> e ((frames + hop - 1) / hop, 0.0);
        for (size_t f = 0; f < frames; ++f)
            for (int c = 0; c < ch; ++c) e[f / hop] += std::abs (audio[f * (size_t) ch + (size_t) c]);
        const size_t span = win / hop;
        double run = 0, best = -1;
        for (size_t i = 0; i < e.size(); ++i)
        {
            run += e[i];
            if (i >= span) run -= e[i - span];
            if (i + 1 >= span && run > best && (i + 1 - span) * hop + win <= frames) { best = run; start = (i + 1 - span) * hop; }
        }
    }
    const size_t preroll = (size_t) (fs * 0.5);

    for (int t = 0; t < numTypes; ++t)
    {
        PerType p = autoSettings (t, L.material, L.bpm, cleanRatio, cleanKnee);
        double tgt = target;
        switch (t)
        {
            case glue:  tgt = juce::jlimit (2.0, 4.0, target * 0.8); break;   // glue = gentle
            case punch: tgt = L.material == drums ? target + 1.0             // FET on drums: lets the crack through
                                           : std::min (9.0, target + 2.5); break;   // on notes / mixes: densest
            case warm:  tgt = std::max (2.5, target * 0.9); break;
            default: break;
        }

        double lo = st.rmsDb - 30.0, hi = std::min (0.0, st.p99PeakDb);
        SimResult r;
        for (int it = 0; it < 14; ++it)
        {
            p.threshDb = (float) ((lo + hi) * 0.5);
            r = simulate (audio, ch, fs, start, win, preroll, t, L.material, p);
            if      (r.avgGr < tgt - 0.2) hi = p.threshDb;   // too little GR -> lower threshold
            else if (r.avgGr > tgt + 0.2) lo = p.threshDb;
            else break;
        }
        p.avgGrDb = r.avgGr;
        // Makeup: match loudness, but leave the compressed peaks of the WHOLE capture under -1.5 dBFS,
        // so the -1 dBFS safety ceiling stays a rare net and never shaves drum hits.
        const auto all = simulate (audio, ch, fs, 0, frames, 0, t, L.material, p);
        const float pk = std::max (r.peakOutDb, all.peakOutDb);
        p.makeupDb = juce::jlimit (0.0f, std::max (0.0f, -1.5f - pk), r.rmsInDb - r.rmsOutDb);
        // Level at which the static curve gives the average GR: used to keep auto makeup right
        // when THRESHOLD or RATIO are moved off the learned values.
        float a = p.threshDb - 20.0f, b = p.threshDb + 60.0f;
        for (int it = 0; it < 30; ++it)
        {
            const float m = 0.5f * (a + b);
            if (staticGr (m, p.threshDb, p.ratio, p.kneeDb, model (t).variMu) < p.avgGrDb) a = m; else b = m;
        }
        p.refLevelDb = 0.5f * (a + b);
        L.t[t] = p;
    }
    return L;
}

// ── State ───────────────────────────────────────────────────────────────────────
juce::ValueTree Learned::toTree() const
{
    juce::ValueTree v ("Learned");
    v.setProperty ("valid", valid, nullptr);
    v.setProperty ("material", material, nullptr);
    v.setProperty ("bpm", bpm, nullptr);
    v.setProperty ("peakDb", peakDb, nullptr);
    v.setProperty ("rmsDb", rmsDb, nullptr);
    v.setProperty ("crestDb", crestDb, nullptr);
    v.setProperty ("onsetDb", onsetDb, nullptr);
    v.setProperty ("flatness", flatness, nullptr);
    v.setProperty ("drDb", drDb, nullptr);
    v.setProperty ("targetGrDb", targetGrDb, nullptr);
    for (int i = 0; i < numTypes; ++i)
    {
        const auto& p = t[i];
        juce::StringArray s;
        for (float f : { p.threshDb, p.ratio, p.kneeDb, p.attackMs, p.relFastMs, p.relSlowMs, p.makeupDb, p.avgGrDb, p.refLevelDb })
            s.add (juce::String (f, 4));
        v.setProperty (juce::String ("t") + juce::String (i), s.joinIntoString (","), nullptr);
    }
    return v;
}

Learned Learned::fromTree (const juce::ValueTree& v)
{
    Learned l = defaults();
    if (! v.isValid() || ! v.hasType ("Learned")) return l;
    l.valid      = (bool) v.getProperty ("valid", false);
    l.material   = juce::jlimit (0, (int) numMaterials - 1, (int) v.getProperty ("material", 0));
    l.bpm        = (float) v.getProperty ("bpm", 120.0f);
    l.peakDb     = (float) v.getProperty ("peakDb", -6.0f);
    l.rmsDb      = (float) v.getProperty ("rmsDb", -20.0f);
    l.crestDb    = (float) v.getProperty ("crestDb", 12.0f);
    l.onsetDb    = (float) v.getProperty ("onsetDb", 0.0f);
    l.flatness   = (float) v.getProperty ("flatness", 0.0f);
    l.drDb       = (float) v.getProperty ("drDb", 6.0f);
    l.targetGrDb = (float) v.getProperty ("targetGrDb", 4.0f);
    for (int i = 0; i < numTypes; ++i)
    {
        auto parts = juce::StringArray::fromTokens (v.getProperty (juce::String ("t") + juce::String (i)).toString(), ",", "");
        if (parts.size() != 9) continue;
        auto f = [&] (int k) { return parts[k].getFloatValue(); };
        auto& p = l.t[i];
        p.threshDb = juce::jlimit (-90.0f, 0.0f, f (0)); p.ratio = juce::jlimit (1.0f, 20.0f, f (1)); p.kneeDb = juce::jlimit (0.0f, 24.0f, f (2));
        p.attackMs = juce::jlimit (0.05f, 100.0f, f (3)); p.relFastMs = juce::jlimit (10.0f, 3000.0f, f (4)); p.relSlowMs = juce::jlimit (10.0f, 5000.0f, f (5));
        p.makeupDb = juce::jlimit (0.0f, 24.0f, f (6)); p.avgGrDb = f (7); p.refLevelDb = f (8);
    }
    return l;
}
} // namespace ap
