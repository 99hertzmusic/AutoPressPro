// AutoPress Pro offline test app.
// Usage: AutoPressProTest <outdir> [name=key ...]
//  - learning per material, AUTO choices per type
//  - attack / release timing per step and type (time to 63 %)
//  - legacy AutoPress 1.1 timing for comparison
//  - clicks when switching steps / types / Pro features
//  - save -> new instance -> restore: no re-learn, mix + threshold survive
//  - Lite can't use Pro features (automation or saved state)
//  - type comparison on drums and bass/pad + WAV examples in <outdir>/sounds
//  - licence keys, UI snapshots
#include <juce_audio_utils/juce_audio_utils.h>
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include <cmath>
#include <map>
#include <deque>
#include "LicenceTests.h"

using APP = AutoPressProAudioProcessor;
static constexpr double SR = 48000.0;
static constexpr int BS = 256;
static int failures = 0;
static void check (bool ok, const juce::String& what)
{
    std::printf ("  [%s] %s\n", ok ? "PASS" : "FAIL", what.toRawUTF8());
    if (! ok) ++failures;
}

// ── Signals ──────────────────────────────────────────────────────────────────
using Buf = juce::AudioBuffer<float>;
static const double twoPi = juce::MathConstants<double>::twoPi;

static float kickAt (double t)
{
    if (t < 0 || t > 0.35) return 0.0f;
    const double f0 = 48.0, f1 = 170.0, k = 30.0;
    const double ph = twoPi * (f0 * t + (f1 - f0) * (1 - std::exp (-k * t)) / k);
    return (float) (std::sin (ph) * std::exp (-t / 0.11));
}

static Buf drumLoop (double secs, double bpm = 124.0)
{
    const int n = (int) (secs * SR);
    Buf b (2, n); b.clear();
    juce::Random rnd (42);
    const double step = 60.0 / bpm / 4.0;
    const int steps = (int) (secs / step) + 1;
    for (int s = 0; s < steps; ++s)
    {
        const int pos16 = s % 16, bar = s / 16;
        const float barGain = (bar % 2 == 1) ? 0.6f : 1.0f;              // quieter every other bar: real dynamics
        const float vel = barGain * (0.75f + 0.25f * rnd.nextFloat());
        const int start = (int) (s * step * SR);
        const bool kick = pos16 % 4 == 0, snare = pos16 == 4 || pos16 == 12, hat = pos16 % 2 == 0 ? false : true;
        for (int i = 0; i < (int) (0.4 * SR) && start + i < n; ++i)
        {
            const double t = i / SR;
            float l = 0, r = 0;
            if (kick) { const float k = 0.85f * vel * kickAt (t); l += k; r += k; }
            if (snare && t < 0.25)
            {
                const float nz = (rnd.nextFloat() * 2 - 1) * (float) std::exp (-t / 0.07) * 0.45f;
                const float tone = (float) (std::sin (twoPi * 190.0 * t) * std::exp (-t / 0.035)) * 0.35f;
                l += vel * (nz + tone); r += vel * (nz * 0.9f + tone);
            }
            if (hat && t < 0.08)
            {
                static float prev = 0; const float w = rnd.nextFloat() * 2 - 1; const float hp = w - prev; prev = w;
                const float h = hp * (float) std::exp (-t / 0.012) * 0.16f * vel;
                l += h * 0.7f; r += h;
            }
            b.addSample (0, start + i, l); b.addSample (1, start + i, r);
        }
    }
    b.applyGain (0.5f);   // peaks around -3 dBFS, like a real drum bus
    return b;
}

// Sustained bass notes + a slowly swelling pad chord.
static Buf bassPad (double secs, double bpm = 124.0)
{
    const int n = (int) (secs * SR);
    Buf b (2, n);
    const double bar = 4 * 60.0 / bpm;
    const double notes[] = { 55.0, 55.0, 73.42, 49.0 };
    const double chord[] = { 220.0, 261.63, 329.63, 392.0 };
    double bph = 0;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / SR;
        const int barIdx = (int) (t / bar);
        const double inBar = std::fmod (t, bar), half = bar / 2;
        const double f = notes[barIdx % 4];
        bph += f / SR;
        double saw = 0; for (int k = 1; k <= 8; ++k) saw += std::sin (twoPi * k * bph) / k;
        const double noteT = std::fmod (inBar, half);
        const double env = std::min (1.0, noteT / 0.008) * (noteT > half - 0.03 ? (half - noteT) / 0.03 : 1.0) * (std::fmod (inBar, half) < half ? 1.0 : 0.0);
        const double accent = (inBar < half ? 1.0 : 0.7);
        const float bass = (float) (0.32 * saw * env * accent);
        const double swell = 0.55 + 0.45 * std::sin (twoPi * t / 4.0);
        double pad = 0; for (int k = 0; k < 4; ++k) pad += std::sin (twoPi * chord[k] * t) + std::sin (twoPi * chord[k] * 1.004 * t + k);
        const float p = (float) (0.035 * pad * swell);
        b.setSample (0, i, bass + p); b.setSample (1, i, bass + p * 0.9f);
    }
    return b;
}

static Buf mixBus (double secs)
{
    auto d = drumLoop (secs), bp = bassPad (secs);
    for (int c = 0; c < 2; ++c) { d.applyGain (c, 0, d.getNumSamples(), 0.8f); d.addFrom (c, 0, bp, c, 0, d.getNumSamples(), 0.4f); }
    return d;
}

// ── Helpers ──────────────────────────────────────────────────────────────────
static void setParam (APP& p, const char* id, float plain)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (plain));
}

static void setPro (APP& p, int kind)
{
    hz::LicenceInfo li; li.kind = (hz::LicenceInfo::Kind) kind; li.email = "buyer@example.com";
    li.expires = kind == hz::LicenceInfo::trial ? juce::Time::currentTimeMillis() / 1000 + 5 * 86400 - 60 : 0;
    p.setLicenceInfo (li);
}

static std::unique_ptr<APP> makeProc (bool pro)
{
    auto p = std::make_unique<APP>();
    setPro (*p, pro ? hz::LicenceInfo::pro : hz::LicenceInfo::none);
    p->setRateAndBufferSizeDetails (SR, BS);
    p->prepareToPlay (SR, BS);
    return p;
}

// Process `in` (looped if needed) for `len` samples, returning the output.
static Buf run (APP& p, const Buf& in, int len = -1, int offset = 0)
{
    if (len < 0) len = in.getNumSamples();
    Buf out (2, len), blk (2, BS);
    juce::MidiBuffer m;
    for (int pos = 0; pos < len; pos += BS)
    {
        const int n = juce::jmin (BS, len - pos);
        blk.setSize (2, n, false, false, true);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i) blk.setSample (c, i, in.getSample (c, (offset + pos + i) % in.getNumSamples()));
        p.processBlock (blk, m);
        for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, blk, c, 0, n);
    }
    return out;
}

// Feed audio until the plugin has learned (worker thread runs the analysis).
static bool learn (APP& p, const Buf& sig)
{
    int pos = 0;
    for (int guard = 0; guard < 4000; ++guard)
    {
        if (p.getPhase() == APP::idle && p.getLearned().valid) return true;
        if (p.getPhase() == APP::analysing) { juce::Thread::sleep (20); continue; }
        run (p, sig, BS, pos); pos += BS;
    }
    return false;
}

static void writeWav (const juce::File& f, const Buf& b, double rate = SR)
{
    f.deleteFile();
    juce::WavAudioFormat w;
    if (auto os = std::unique_ptr<juce::OutputStream> (f.createOutputStream().release()))
        if (auto wr = std::unique_ptr<juce::AudioFormatWriter> (w.createWriterFor (os.get(), rate, 2, 24, {}, 0)))
        { os.release(); wr->writeFromAudioSampleBuffer (b, 0, b.getNumSamples()); }
}

struct Level { double peakDb, rmsDb, crestDb; };
static Level levelOf (const Buf& b, int from)
{
    double sq = 0; float pk = 0; size_t cnt = 0;
    for (int c = 0; c < 2; ++c)
        for (int i = from; i < b.getNumSamples(); ++i) { const float x = b.getSample (c, i); sq += x * x; pk = juce::jmax (pk, std::abs (x)); ++cnt; }
    const double rms = 10 * std::log10 (sq / (double) juce::jmax ((size_t) 1, cnt) + 1e-20), p = juce::Decibels::gainToDecibels (pk, -150.0f);
    return { p, rms, p - rms };
}

// Harmonic "colour": energy left after removing the input's own spectrum shape is hard offline, so use
// the residual between output and a gain-matched version of the input (distortion + gain movement).
static double residualDb (const Buf& in, const Buf& out, int from)
{
    // output is delayed by the 1.5 ms lookahead: line it up first
    const int d = (int) std::lround (ap::lookaheadMs * 0.001 * SR), n = in.getNumSamples() - d;
    double a = 0, b = 0;
    for (int c = 0; c < 2; ++c) for (int i = from; i < n; ++i) { a += in.getSample (c, i) * out.getSample (c, i + d); b += in.getSample (c, i) * in.getSample (c, i); }
    const double g = a / juce::jmax (1e-12, b);
    double e = 0, s = 0;
    for (int c = 0; c < 2; ++c) for (int i = from; i < n; ++i) { const double v = out.getSample (c, i + d) - g * in.getSample (c, i); e += v * v; s += out.getSample (c, i + d) * out.getSample (c, i + d); }
    return 10 * std::log10 (e / juce::jmax (1e-20, s));
}

static ap::Learned testLearned (float thr)
{
    auto l = ap::Learned::defaults();
    l.valid = true; l.peakDb = -6.0f;
    for (auto& t : l.t) { t.threshDb = thr; t.makeupDb = 0; t.refLevelDb = thr + 6; t.avgGrDb = 3; }
    return l;
}

static void printLearned (const char* name, const ap::Learned& l);

// BS.1770 integrated loudness (stereo sum of channel powers, 400 ms blocks, 75 % overlap, gates).
static double lufsOf (const Buf& b, double fs)
{
    struct BQ { double b0, b1, b2, a1, a2, z1 = 0, z2 = 0; double p (double x) { double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; } };
    auto shelf = [&] { const double A = std::pow (10.0, 4.0 / 40), w = twoPi * 1681.97 / fs, sn = std::sin (w), cs = std::cos (w), al = sn / (2 * 0.7072), be = 2 * std::sqrt (A) * al, a0 = (A + 1) - (A - 1) * cs + be;
        return BQ { A * ((A + 1) + (A - 1) * cs + be) / a0, -2 * A * ((A - 1) + (A + 1) * cs) / a0, A * ((A + 1) + (A - 1) * cs - be) / a0, 2 * ((A - 1) - (A + 1) * cs) / a0, ((A + 1) - (A - 1) * cs - be) / a0 }; };
    auto hp = [&] { const double w = twoPi * 38.135 / fs, sn = std::sin (w), cs = std::cos (w), al = sn / (2 * 0.5003), a0 = 1 + al;
        return BQ { (1 + cs) / 2 / a0, -(1 + cs) / a0, (1 + cs) / 2 / a0, -2 * cs / a0, (1 - al) / a0 }; };
    const int n = b.getNumSamples(); std::vector<double> pw ((size_t) n, 0.0);
    for (int c = 0; c < b.getNumChannels(); ++c) { auto s1 = shelf(); auto h1 = hp(); for (int i = 0; i < n; ++i) { const double y = h1.p (s1.p (b.getSample (c, i))); pw[(size_t) i] += y * y; } }
    const int blk = (int) (0.4 * fs), hop = blk / 4; std::vector<double> bl;
    for (int s0 = 0; s0 + blk <= n; s0 += hop) { double a = 0; for (int i = s0; i < s0 + blk; ++i) a += pw[(size_t) i]; bl.push_back (a / blk); }
    auto L = [] (double p) { return -0.691 + 10 * std::log10 (p + 1e-30); };
    double sum = 0; int cnt = 0; for (auto v : bl) if (L (v) > -70) { sum += v; ++cnt; }
    if (! cnt) return -120; const double rel = L (sum / cnt) - 10; sum = 0; cnt = 0;
    for (auto v : bl) if (L (v) > -70 && L (v) > rel) { sum += v; ++cnt; }
    return cnt ? L (sum / cnt) : -120;
}
static double plrOf (const Buf& b, double fs) { return juce::Decibels::gainToDecibels (b.getMagnitude (0, b.getNumSamples()), -150.0f) - lufsOf (b, fs); }

// Staccato tonal stabs with sharp starts (piano-chord / pluck-bass style): must NOT be treated as drums.
static Buf staccatoNotes (double secs, double bpm = 124.0)
{
    const int n = (int) (secs * SR); Buf b (2, n); b.clear();
    juce::Random rnd (7);
    const double step = 60.0 / bpm / 2.0;    // 8ths
    const double roots[] = { 110.0, 130.81, 98.0, 146.83 };
    for (int s = 0; s * step < secs; ++s)
    {
        if (rnd.nextFloat() < 0.3f) continue;                       // some gaps
        const float vel = 0.4f + 0.6f * rnd.nextFloat();
        const double f0 = roots[(s / 8) % 4];
        const int start = (int) (s * step * SR), len = (int) (step * 0.7 * SR);
        for (int i = 0; i < len && start + i < n; ++i)
        {
            const double t = i / SR;
            const double env = std::min (1.0, t / 0.0005) * std::exp (-t / 0.18);
            double v = 0; for (int k = 1; k <= 6; ++k) v += std::sin (twoPi * f0 * k * t) / (k * k) + 0.5 * std::sin (twoPi * f0 * 1.5 * k * t) / (k * k);
            const float x = (float) (0.3 * vel * env * v);
            b.addSample (0, start + i, x); b.addSample (1, start + i, x);
        }
    }
    return b;
}

static void instrumentTests()
{
    std::printf ("\n== Notes with sharp starts (piano stabs / staccato bass): controlled, not drum-style ==\n");
    auto sig = staccatoNotes (12);
    sig.applyGain (0.5f / sig.getMagnitude (0, sig.getNumSamples()));
    auto p0 = makeProc (true); learn (*p0, sig);
    const auto l = p0->getLearned();
    printLearned ("stabs", l);
    check (l.material == ap::rhythmic, juce::String ("staccato notes detected as ") + ap::materialName (l.material) + " (not DRUMS)");
    check (l.t[ap::clean].attackMs <= 10.0f && l.t[ap::punch].attackMs <= 1.0f, "notes get a fast AUTO attack (CLEAN " + juce::String (l.t[ap::clean].attackMs) + " ms)");
    const double dry = plrOf (sig, SR);
    double plr[ap::numTypes];
    for (int t = 0; t < ap::numTypes; ++t)
    {
        auto p = std::make_unique<APP>(); setPro (*p, hz::LicenceInfo::pro); p->setLearnedForTest (l); setParam (*p, "type", (float) t);
        p->setRateAndBufferSizeDetails (SR, BS); p->prepareToPlay (SR, BS);
        run (*p, sig); auto out = run (*p, sig);
        plr[t] = plrOf (out, SR);
        std::printf ("  %-7s PLR %5.1f (dry %5.1f)\n", ap::typeName (t), plr[t], dry);
        check (plr[t] <= dry + 0.3, juce::String (ap::typeName (t)) + " doesn't make the note peaks stick out (PLR " + juce::String (plr[t], 1) + " vs dry " + juce::String (dry, 1) + ")");
    }
    bool punchDensest = true; for (int t = 0; t < ap::numTypes; ++t) if (t != ap::punch && plr[t] < plr[ap::punch]) punchDensest = false;
    check (punchDensest, "PUNCH is the densest type on notes");
}

// Sam's real stems if they're on this Mac: PLR must be at or below SSL Bus Comp 2 (measured by the coordinator).
static void realStemTests()
{
    const auto R = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("99hertz/ads/how-to-make/out/stems");
    if (! R.isDirectory()) { std::printf ("\n(real stems not found, skipping)\n"); return; }
    std::printf ("\n== Sam's real loops: PLR per type on AUTO (lower = denser; SSL Bus Comp 2 reference) ==\n");
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    struct Src { const char* name; juce::StringArray files; double ssl; double maxForType; bool drums; };
    std::vector<Src> srcs = {
        { "drums", { "gta6-01-stussy-ninety/groove.wav", "gta6-01-stussy-ninety/hat.wav" }, 14.3, 99.0, true },
        { "piano", { "90s-piano-house-15/piano.wav" }, 12.8, 12.8, false },
        { "bass",  { "90s-piano-house-15/bass.wav" }, 15.6, 15.6, false },
        { "full mix", { "gta6-01-stussy-ninety/groove.wav", "gta6-01-stussy-ninety/hat.wav", "gta6-01-stussy-ninety/bass.wav", "gta6-01-stussy-ninety/synth.wav" }, 17.2, 17.2, false },
    };
    for (auto& s : srcs)
    {
        Buf x; double fs = 44100;
        for (auto& f : s.files)
        {
            std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (R.getChildFile (f)));
            if (! r) return;
            fs = r->sampleRate; const int len = (int) r->lengthInSamples;
            Buf b (2, len); r->read (&b, 0, len, 0, true, true); if (r->numChannels == 1) b.copyFrom (1, 0, b, 0, 0, len);
            if (x.getNumSamples() == 0) x = b; else { const int m = juce::jmin (len, x.getNumSamples()); x.setSize (2, m, true); for (int c = 0; c < 2; ++c) x.addFrom (c, 0, b, c, 0, m); }
        }
        x.applyGain (0.5f / x.getMagnitude (0, x.getNumSamples()));
        double plr[ap::numTypes]; ap::Learned l;
        for (int t = 0; t < ap::numTypes; ++t)
        {
            auto p = std::make_unique<APP>(); setPro (*p, hz::LicenceInfo::pro); setParam (*p, "type", (float) t);
            p->setRateAndBufferSizeDetails (fs, BS); p->prepareToPlay (fs, BS);
            int pos = 0;
            while (! (p->getPhase() == APP::idle && p->getLearned().valid))
            { if (p->getPhase() == APP::analysing) { juce::Thread::sleep (10); continue; } run (*p, x, BS, pos); pos += BS; }
            l = p->getLearned();
            auto out = run (*p, x, x.getNumSamples(), 0);
            plr[t] = plrOf (out, fs);
        }
        std::printf ("  %-8s %-9s dry %5.1f  SSL %5.1f |", s.name, ap::materialName (l.material), plrOf (x, fs), s.ssl);
        for (int t = 0; t < ap::numTypes; ++t) std::printf ("  %s %5.1f", ap::typeName (t), plr[t]);
        std::printf ("\n");
        if (! s.drums)
        {
            bool ok = true, punch = true;
            for (int t = 0; t < ap::numTypes; ++t) { if (plr[t] > s.maxForType + 0.05) ok = false; if (t != ap::punch && plr[t] <= plr[ap::punch]) punch = false; }
            check (ok, juce::String (s.name) + ": every type at or below SSL's PLR");
            check (punch, juce::String (s.name) + ": PUNCH densest");
        }
        else
        {
            check (l.material == ap::drums, "drums: still DRUMS");
            const double dry = plrOf (x, fs);
            for (int t = 0; t < ap::numTypes; ++t)
                check (plr[t] >= dry - 0.1 && plr[t] <= 15.6, juce::String ("drums ") + ap::typeName (t) + ": hits kept, in SSL/Pro-C's range (PLR " + juce::String (plr[t], 1) + ", dry " + juce::String (dry, 1) + ")");
        }
    }
}

// Clean drums: added distortion per type at DRIVE 0, and how often the -1 dBFS ceiling touches.
static void cleanDrumTests (const ap::Learned& drumsLearned)
{
    std::printf ("\n== Clean drums: no added distortion at DRIVE 0, ceiling stays a safety net ==\n");
    // 1) On the drum loop: output vs "the same compressor with saturation disabled", i.e. the dry times
    //    the plugin's own per-sample gain (grTrace) and makeup, through the same DC blocker.
    //    Residual / output = harmonics the plugin adds on top of the compression itself.
    const int la = (int) std::lround (ap::lookaheadMs * 0.001 * SR), from = (int) SR;
    auto loop = drumLoop (8);
    for (int drive : { 0, 100 })
        for (int t = 0; t < ap::numTypes; ++t)
        {
            auto p = std::make_unique<APP>(); setPro (*p, hz::LicenceInfo::pro); p->setLearnedForTest (drumsLearned);
            setParam (*p, "type", (float) t); setParam (*p, "drive", (float) drive);
            p->setRateAndBufferSizeDetails (SR, BS); p->prepareToPlay (SR, BS);
            std::vector<float> tr; tr.reserve ((size_t) loop.getNumSamples() + 10); p->grTrace = &tr;
            auto out = run (*p, loop);
            const double R = 1.0 - twoPi * 5.0 / SR; double x1 = 0, y1 = 0, num = 0, den = 0;
            std::vector<double> ref ((size_t) out.getNumSamples(), 0.0);
            for (int i = 0; i < out.getNumSamples(); ++i)
            {
                const double x = (i >= la ? loop.getSample (0, i - la) : 0.0) * std::pow (10.0, -tr[(size_t) i] / 20.0);
                const double y = x - x1 + R * y1; x1 = x; y1 = y; ref[(size_t) i] = y;
                if (i >= from) { num += out.getSample (0, i) * y; den += y * y; }
            }
            const double k = num / juce::jmax (1e-20, den); double e = 0, s2 = 0;
            for (int i = from; i < out.getNumSamples(); ++i) { const double d = out.getSample (0, i) - k * ref[(size_t) i]; e += d * d; s2 += out.getSample (0, i) * out.getSample (0, i); }
            const double added = 100.0 * std::sqrt (e / juce::jmax (1e-20, s2));
            std::printf ("  drum loop  drive %3d  %-7s added distortion %7.4f %%\n", drive, ap::typeName (t), added);
            if (drive == 0) check (added < 0.3, juce::String (ap::typeName (t)) + " adds < 0.3 % distortion on the drum loop at DRIVE 0 (" + juce::String (added, 4) + " %)");
            else if (t == ap::clean) check (added > 1.0, "sanity: DRIVE 100 does add harmonics (" + juce::String (added, 2) + " %)");
        }

    // Classic steady-tone THD at drive 0 (55 Hz kick fundamental and 100 Hz), level set for ~6 dB GR.
    for (double f0 : { 55.0, 100.0 })
        for (int t = 0; t < ap::numTypes; ++t)
        {
            const int len = (int) (3 * SR); Buf sine (2, len);
            auto p = std::make_unique<APP>(); setPro (*p, hz::LicenceInfo::pro); p->setLearnedForTest (drumsLearned); setParam (*p, "type", (float) t);
            p->setRateAndBufferSizeDetails (SR, BS); p->prepareToPlay (SR, BS);
            const float amp = juce::Decibels::decibelsToGain (juce::jmin (-1.0f, drumsLearned.t[t].threshDb + 3.0f + 12.0f));
            for (int i = 0; i < len; ++i) { const float v = amp * (float) std::sin (twoPi * f0 * i / SR); sine.setSample (0, i, v); sine.setSample (1, i, v); }
            std::vector<float> tr; tr.reserve ((size_t) len + 10); p->grTrace = &tr;
            auto out = run (*p, sine);
            const int a0 = 2 * (int) SR; double ss = 0, sc = 0, cc = 0, ys = 0, yc = 0, mean = 0;
            for (int i = a0; i < len; ++i) mean += out.getSample (0, i); mean /= (len - a0);
            for (int i = a0; i < len; ++i) { const double ph = twoPi * f0 * i / SR, s1 = std::sin (ph), c1 = std::cos (ph), y = out.getSample (0, i) - mean;
                ss += s1 * s1; cc += c1 * c1; sc += s1 * c1; ys += y * s1; yc += y * c1; }
            const double det = ss * cc - sc * sc, A = (ys * cc - yc * sc) / det, B = (yc * ss - ys * sc) / det;
            double res = 0, tot = 0;
            for (int i = a0; i < len; ++i) { const double ph = twoPi * f0 * i / SR, fit = A * std::sin (ph) + B * std::cos (ph), d = out.getSample (0, i) - mean - fit; res += d * d; tot += fit * fit; }
            const double thd = 100.0 * std::sqrt (res / tot);
            std::printf ("  steady %3.0f Hz  %-7s GR %4.1f dB  THD+N %6.3f %%\n", f0, ap::typeName (t), tr.back(), thd);
            check (thd < 0.3, juce::String ("steady ") + juce::String ((int) f0) + " Hz " + ap::typeName (t) + " THD+N < 0.3 % at DRIVE 0 (" + juce::String (thd, 3) + " %)");
        }

    // Ceiling: on the drum loop at normal (-6 dBFS) and hot (-0.3 dBFS) input, count output samples the
    // ceiling touches where the input itself was under -1 dBFS (so the plugin pushed them there).
    for (float peakDb : { -6.0f, -0.3f })
    {
        auto d = drumLoop (12); d.applyGain (juce::Decibels::decibelsToGain (peakDb) / d.getMagnitude (0, d.getNumSamples()));
        auto pl = makeProc (true); learn (*pl, d); const auto l = pl->getLearned();
        for (int t = 0; t < ap::numTypes; ++t)
        {
            auto p = std::make_unique<APP>(); setPro (*p, hz::LicenceInfo::pro); p->setLearnedForTest (l); setParam (*p, "type", (float) t);
            p->setRateAndBufferSizeDetails (SR, BS); p->prepareToPlay (SR, BS);
            run (*p, d); auto out = run (*p, d);
            const int la = (int) std::lround (ap::lookaheadMs * 0.001 * SR);
            size_t touch = 0, cnt = 0;
            for (int i = la; i < out.getNumSamples(); ++i)
                for (int c = 0; c < 2; ++c)
                {
                    const float x = std::abs (d.getSample (c, i - la)), y = std::abs (out.getSample (c, i));
                    if (x < 1e-4f) continue; ++cnt;
                    if (y > 0.891f && x <= 0.891f) ++touch;
                }
            const double pct = 100.0 * (double) touch / (double) juce::jmax ((size_t) 1, cnt);
            std::printf ("  input peak %5.1f dBFS  %-7s makeup %4.1f dB  ceiling touches %.4f %% of samples  out peak %5.1f dBFS\n", peakDb, ap::typeName (t),
                         l.t[t].makeupDb, pct, juce::Decibels::gainToDecibels (out.getMagnitude (0, out.getNumSamples())));
            check (pct < 0.01, juce::String ("ceiling is a rare safety net on drums (") + ap::typeName (t) + ", input " + juce::String (peakDb, 1) + " dBFS)");
        }
    }

    // PUNCH on drums keeps the hits: PLR not below the dry.
    {
        auto d = drumLoop (12);
        auto p = std::make_unique<APP>(); setPro (*p, hz::LicenceInfo::pro); p->setLearnedForTest (drumsLearned); setParam (*p, "type", (float) ap::punch);
        p->setRateAndBufferSizeDetails (SR, BS); p->prepareToPlay (SR, BS);
        run (*p, d); auto out = run (*p, d);
        const double dry = plrOf (d, SR), wet = plrOf (out, SR);
        std::printf ("  PUNCH on the drum loop: PLR %.1f vs dry %.1f\n", wet, dry);
        check (wet >= dry - 0.1, "PUNCH on drums keeps the transients (PLR " + juce::String (wet, 1) + " >= dry " + juce::String (dry, 1) + ")");
    }
}

// ── Timing ───────────────────────────────────────────────────────────────────
static Buf toneSteps (double lowDb, double highDb, double t1, double t2, double total, double freq = 1000.0)
{
    const int n = (int) (total * SR);
    Buf b (2, n);
    for (int i = 0; i < n; ++i)
    {
        const double t = i / SR;
        const double db = (t >= t1 && t < t2) ? highDb : lowDb;
        const float v = (float) (juce::Decibels::decibelsToGain (db) * std::sin (twoPi * freq * t));
        b.setSample (0, i, v); b.setSample (1, i, v);
    }
    return b;
}

// Time (ms) for GR to cover 63 % of a step, from a per-sample GR trace.
static double t63 (const std::vector<float>& gr, int stepAt, int endAt, bool rising)
{
    const float startV = gr[(size_t) juce::jmax (0, stepAt - 1)];
    float finalV = 0; int cnt = 0;
    for (int i = endAt - (endAt - stepAt) / 10; i < endAt; ++i) { finalV += gr[(size_t) i]; ++cnt; }
    finalV /= (float) cnt;
    const float target = startV + 0.632f * (finalV - startV);
    for (int i = stepAt; i < endAt; ++i)
        if (rising ? gr[(size_t) i] >= target : gr[(size_t) i] <= target) return (i - stepAt) * 1000.0 / SR;
    return -1;
}

static void timingTests()
{
    std::printf ("\n== Attack / release timing (1 kHz tone, ratio 10:1, threshold -30 dB; time to 63 %% of the GR step) ==\n");
    std::printf ("%-7s %-9s %9s %9s\n", "type", "step", "measured", "ok");
    for (int type = 0; type < ap::numTypes; ++type)
    {
        for (int a = 0; a < ap::numAttackSteps; ++a)
        {
            auto p = std::make_unique<APP>(); setPro (*p, hz::LicenceInfo::pro);
            p->setLearnedForTest (testLearned (-30.0f));
            setParam (*p, "type", (float) type); setParam (*p, "attack", (float) a); setParam (*p, "release", 1.0f); setParam (*p, "ratio", 3.0f);
            p->setRateAndBufferSizeDetails (SR, BS); p->prepareToPlay (SR, BS);
            std::vector<float> tr; tr.reserve (100000); p->grTrace = &tr;
            run (*p, toneSteps (-50, -6, 0.3, 1.3, 1.3));
            const double m = t63 (tr, (int) (0.3 * SR), (int) (1.3 * SR), true);
            const double want = ap::attackStepsMs[a];
            const bool ok = m >= want * 0.7 && m <= want * 1.5 + 0.15;
            std::printf ("%-7s atk %-5.1f %7.2f ms %s\n", ap::typeName (type), want, m, ok ? "ok" : "OUT");
            check (ok, juce::String (ap::typeName (type)) + " attack " + juce::String (want) + " ms -> " + juce::String (m, 2) + " ms");
        }
        for (int r = 0; r < ap::numReleaseSteps; ++r)
        {
            auto p = std::make_unique<APP>(); setPro (*p, hz::LicenceInfo::pro);
            p->setLearnedForTest (testLearned (-30.0f));
            setParam (*p, "type", (float) type); setParam (*p, "attack", 0.0f); setParam (*p, "release", (float) r); setParam (*p, "ratio", 3.0f);
            p->setRateAndBufferSizeDetails (SR, BS); p->prepareToPlay (SR, BS);
            std::vector<float> tr; tr.reserve (600000); p->grTrace = &tr;
            run (*p, toneSteps (-70, -6, 0.2, 1.2, 11.2));
            const double m = t63 (tr, (int) (1.2 * SR), (int) (11.2 * SR), false);
            const double want = ap::releaseStepsMs[r];
            const bool ok = m >= want * 0.8 && m <= want * 1.25 + 3.0;
            std::printf ("%-7s rel %-5.0f %7.1f ms %s\n", ap::typeName (type), want, m, ok ? "ok" : "OUT");
            check (ok, juce::String (ap::typeName (type)) + " release " + juce::String (want) + " ms -> " + juce::String (m, 1) + " ms");
        }
    }
}

// The 1.1 detector + gain computer, reproduced to measure what its knobs really did.
static void legacyTiming()
{
    std::printf ("\n== Legacy AutoPress 1.1 timing (same tone test; knobs at attack 10 ms, release 120/420 ms) ==\n");
    for (int mode = 0; mode < 2; ++mode)   // 0 = default hybrid detector, 1 = transient-heavy detector
    {
        auto sig = toneSteps (-50, -6, 0.3, 2.3, 7.3);
        std::deque<float> mom, st; double momSum = 0, stSum = 0;
        const size_t momN = (size_t) (0.4 * SR), stN = (size_t) (3.0 * SR);
        float gr = 0; std::vector<float> tr;
        for (int i = 0; i < sig.getNumSamples(); ++i)
        {
            const float x = sig.getSample (0, i);
            const double pw = x * x;
            mom.push_back ((float) pw); momSum += pw; if (mom.size() > momN) { momSum -= mom.front(); mom.pop_front(); }
            st.push_back ((float) pw); stSum += pw; if (st.size() > stN) { stSum -= st.front(); st.pop_front(); }
            auto lufs = [] (double s, size_t n) { return s <= 1e-16 ? -120.0 : -0.691 + 10 * std::log10 (s / (double) n); };
            const double momL = lufs (momSum, mom.size()), stL = lufs (stSum, st.size());
            const double momR = momSum <= 1e-16 ? -120.0 : 10 * std::log10 (momSum / (double) mom.size());
            const double pk = 20 * std::log10 (std::max (1e-9f, std::abs (x)));
            const double body = momR + juce::jlimit (0.0, 4.0, (pk - momR - 8.0) * 0.25);
            const double det = mode == 0 ? std::max ({ pk - 1.0, body, std::max (stL, momL) }) : std::max (body, std::max (stL, momL));
            const double target = juce::jlimit (0.0, 30.0, (double) ap::staticGr ((float) det, -30.0f, 10.0f, 0.0f, false));
            const double relMs = 120.0 + (420.0 - 120.0) * juce::jlimit (0.0, 1.0, gr / 12.0);
            const double tms = target > gr ? 10.0 : relMs;
            const double c = std::exp (-1.0 / (tms * 0.001 * SR));
            gr = (float) (c * gr + (1 - c) * target);
            tr.push_back (gr);
        }
        const double a = t63 (tr, (int) (0.3 * SR), (int) (2.3 * SR), true);
        const double r = t63 (tr, (int) (2.3 * SR), (int) (7.3 * SR), false);
        std::printf ("  %s detector: attack knob 10 ms -> measured %.1f ms; release knob 120-420 ms -> measured %.0f ms\n",
                     mode == 0 ? "default  " : "transient", a, r);
    }
}

// ── Clicks ───────────────────────────────────────────────────────────────────
struct HF { juce::dsp::IIR::Filter<float> a, b; HF() { a.coefficients = b.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass (SR, 2500.0f); } float operator() (float x) { return b.processSample (a.processSample (x)); } };

static void clickTests()
{
    std::printf ("\n== Clicks when switching (80 Hz bass tone; HF > 2.5 kHz right after each switch vs steady state) ==\n");
    struct Cat { const char* name; std::function<void (APP&, int)> change; bool liteToggle = false; };
    std::vector<Cat> cats = {
        { "attack steps",  [] (APP& p, int k) { setParam (p, "attack", (float) (k % 7)); } },
        { "release steps", [] (APP& p, int k) { setParam (p, "release", (float) (k % 5)); } },
        { "ratio steps",   [] (APP& p, int k) { setParam (p, "ratio", (float) (k % 5)); } },
        { "type",          [] (APP& p, int k) { setParam (p, "type", (float) (k % 5)); } },
        { "glue toggle",   [] (APP& p, int k) { setParam (p, "type", (float) (k % 2)); } },
        { "sc filter",     [] (APP& p, int k) { setParam (p, "schpf", (float) (k % 4)); } },
        { "drive 0/100",   [] (APP& p, int k) { setParam (p, "drive", k % 2 ? 100.0f : 0.0f); } },
        { "mix 0/100",     [] (APP& p, int k) { setParam (p, "mix", k % 2 ? 0.0f : 100.0f); } },
        { "threshold",     [] (APP& p, int k) { setParam (p, "threshold", k % 2 ? -18.0f : 18.0f); } },
        { "Pro on/off",    [] (APP& p, int k) { setPro (p, k % 2 ? hz::LicenceInfo::none : hz::LicenceInfo::pro); } },
    };
    const double secs = 6.0; const int total = (int) (secs * SR);
    Buf tone (2, total);
    for (int i = 0; i < total; ++i) { const float v = 0.4f * (float) std::sin (twoPi * 80.0 * i / SR); tone.setSample (0, i, v); tone.setSample (1, i, v); }
    std::printf ("%-14s %12s %12s %8s\n", "switch", "steady dB", "switch dB", "click");
    for (auto& cat : cats)
    {
        auto p = std::make_unique<APP>(); setPro (*p, hz::LicenceInfo::pro);
        p->setLearnedForTest (testLearned (-24.0f));
        if (juce::String (cat.name) == "Pro on/off") { setParam (*p, "type", 2); setParam (*p, "drive", 60); setParam (*p, "ratio", 3); setParam (*p, "attack", 2); }
        p->setRateAndBufferSizeDetails (SR, BS); p->prepareToPlay (SR, BS);
        HF hf; float steady = 0, sw = 0;
        const int every = (int) (0.25 * SR) / BS * BS, after = (int) (0.06 * SR);
        int k = 0, lastChange = -1000000;
        Buf blk (2, BS); juce::MidiBuffer m;
        for (int pos = 0; pos < total; pos += BS)
        {
            if (pos >= (int) (1.0 * SR) && pos % every == 0) { cat.change (*p, ++k); lastChange = pos; }
            for (int c = 0; c < 2; ++c) blk.copyFrom (c, 0, tone, c, pos, BS);
            p->processBlock (blk, m);
            for (int i = 0; i < BS; ++i)
            {
                const float y = std::abs (hf (blk.getSample (0, i)));
                if (pos + i < (int) (0.8 * SR)) continue;
                if (pos + i - lastChange < after) sw = juce::jmax (sw, y);
                else steady = juce::jmax (steady, y);
            }
        }
        const float sDb = juce::Decibels::gainToDecibels (steady, -150.0f), wDb = juce::Decibels::gainToDecibels (sw, -150.0f);
        const bool ok = wDb < sDb + 6.0f || wDb < -80.0f;
        std::printf ("%-14s %12.1f %12.1f %8s\n", cat.name, sDb, wDb, ok ? "none" : "CLICK");
        check (ok, juce::String ("no click: ") + cat.name);
    }
}

// ── Learning per material + state ────────────────────────────────────────────
static void printLearned (const char* name, const ap::Learned& l)
{
    std::printf ("  %-9s material=%-9s transients=%.1f%% flat=%.3f crest=%.1f dB DR=%.1f dB peak=%.1f dB target GR=%.1f dB\n", name, ap::materialName (l.material), l.onsetDb, l.flatness, l.crestDb, l.drDb, l.peakDb, l.targetGrDb);
    for (int t = 0; t < ap::numTypes; ++t)
    {
        const auto& p = l.t[t];
        std::printf ("     %-7s thr %6.1f  ratio %4.1f  knee %4.1f  AUTO atk %5.1f ms  rel %4.0f/%4.0f ms  makeup %4.1f  avgGR %4.1f\n",
                     ap::typeName (t), p.threshDb, p.ratio, p.kneeDb, p.attackMs, p.relFastMs, p.relSlowMs, p.makeupDb, p.avgGrDb);
    }
}

static bool sameLearned (const ap::Learned& a, const ap::Learned& b)
{
    if (a.valid != b.valid || a.material != b.material) return false;
    for (int t = 0; t < ap::numTypes; ++t)
        if (std::abs (a.t[t].threshDb - b.t[t].threshDb) > 1e-3f || std::abs (a.t[t].attackMs - b.t[t].attackMs) > 1e-3f
            || std::abs (a.t[t].relSlowMs - b.t[t].relSlowMs) > 1e-2f || std::abs (a.t[t].makeupDb - b.t[t].makeupDb) > 1e-3f) return false;
    return true;
}

static std::map<std::string, ap::Learned> learnTests()
{
    std::printf ("\n== Learning per material ==\n");
    std::map<std::string, ap::Learned> out;
    struct M { const char* name; Buf sig; int expect; };
    std::vector<M> mats = { { "drums", drumLoop (12), ap::drums }, { "bass/pad", bassPad (12), ap::sustained }, { "mix bus", mixBus (12), ap::mixed } };
    for (auto& m : mats)
    {
        auto p = makeProc (true);
        const bool ok = learn (*p, m.sig);
        check (ok, juce::String ("learns on ") + m.name);
        const auto l = p->getLearned();
        printLearned (m.name, l);
        if (m.expect >= 0) check (l.material == m.expect, juce::String (m.name) + " detected as " + ap::materialName (l.material));
        out[m.name] = l;
    }
    // Sense checks on AUTO timing
    const auto& d = out["drums"]; const auto& s = out["bass/pad"];
    check (d.t[ap::clean].attackMs >= 10.0f, "drums: CLEAN AUTO attack slow enough to keep the punch (" + juce::String (d.t[ap::clean].attackMs) + " ms)");
    check (s.t[ap::clean].attackMs < d.t[ap::clean].attackMs, "sustained material gets a faster attack than drums");
    check (d.t[ap::clean].relSlowMs < s.t[ap::clean].relSlowMs, "drums recover faster than sustained material");
    return out;
}

static void stateTests (const Buf& drums)
{
    std::printf ("\n== Save -> new instance -> restore ==\n");
    auto a = makeProc (true);
    learn (*a, drums);
    setParam (*a, "mix", 37.0f); setParam (*a, "threshold", 2.5f); setParam (*a, "type", 1.0f); setParam (*a, "attack", 3.0f);
    juce::MemoryBlock mb; a->getStateInformation (mb);
    const auto la = a->getLearned();

    auto b = std::make_unique<APP>(); setPro (*b, hz::LicenceInfo::pro);
    b->setStateInformation (mb.getData(), (int) mb.getSize());
    check (b->getLearned().valid && b->getPhase() == APP::idle, "restored session is learned right away (no learning screen)");
    b->setRateAndBufferSizeDetails (SR, BS); b->prepareToPlay (SR, BS);
    run (*b, bassPad (4.0));                 // different audio: must NOT trigger a re-learn
    juce::Thread::sleep (200);
    check (b->learnCount == 0 && b->getPhase() == APP::idle, "no re-learn after restore, even on different audio");
    check (sameLearned (la, b->getLearned()), "learned settings identical after restore");
    check (std::abs (b->apvts.getRawParameterValue ("mix")->load() - 37.0f) < 0.05f, "mix 37 % survives save/restore (" + juce::String (b->apvts.getRawParameterValue ("mix")->load()) + ")");
    check (std::abs (b->apvts.getRawParameterValue ("threshold")->load() - 2.5f) < 0.05f, "threshold offset survives");
    check ((int) b->apvts.getRawParameterValue ("type")->load() == 1 && (int) b->apvts.getRawParameterValue ("attack")->load() == 3, "type + attack step survive");

    // Window reopen: editor created / destroyed twice, still no re-learn.
    for (int i = 0; i < 2; ++i) { std::unique_ptr<juce::AudioProcessorEditor> ed (b->createEditor()); }
    run (*b, drums, (int) SR);
    check (b->learnCount == 0 && b->getPhase() == APP::idle, "closing / reopening the window doesn't re-learn");

    // Same output from two restored instances (state fully captured)
    auto c = std::make_unique<APP>(); setPro (*c, hz::LicenceInfo::pro);
    c->setStateInformation (mb.getData(), (int) mb.getSize());
    c->setRateAndBufferSizeDetails (SR, BS); c->prepareToPlay (SR, BS);
    auto b2 = std::make_unique<APP>(); setPro (*b2, hz::LicenceInfo::pro);
    b2->setStateInformation (mb.getData(), (int) mb.getSize());
    b2->setRateAndBufferSizeDetails (SR, BS); b2->prepareToPlay (SR, BS);
    auto o1 = run (*c, drums, (int) (3 * SR)), o2 = run (*b2, drums, (int) (3 * SR));
    float diff = 0; for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < o1.getNumSamples(); ++i) diff = juce::jmax (diff, std::abs (o1.getSample (ch, i) - o2.getSample (ch, i)));
    check (diff < 1e-6f, "two restored instances sound identical");

    // LEARN on demand works and keeps compressing meanwhile
    b->requestLearn();
    check (b->getPhase() == APP::capturing, "LEARN starts a new learn");
    learn (*b, drums);
    check (b->learnCount == 1, "LEARN re-learned once");
}

static void liteTests (const ap::Learned& l, const Buf& drums)
{
    std::printf ("\n== Lite can't use Pro features ==\n");
    auto mk = [&] (bool pro)
    {
        auto p = std::make_unique<APP>(); setPro (*p, pro ? hz::LicenceInfo::pro : hz::LicenceInfo::none);
        p->setLearnedForTest (l); p->setRateAndBufferSizeDetails (SR, BS); p->prepareToPlay (SR, BS); return p;
    };
    auto proSettings = [] (APP& p, int type)
    {
        setParam (p, "type", (float) type); setParam (p, "attack", 0); setParam (p, "release", 3); setParam (p, "ratio", 3);
        setParam (p, "schpf", 3); setParam (p, "drive", 100);
    };
    const int len = (int) (4 * SR);
    auto ref = mk (false);
    const auto refOut = run (*ref, drums, len);
    auto maxDiff = [&] (const Buf& o) { float d = 0; for (int c = 0; c < 2; ++c) for (int i = 0; i < len; ++i) d = juce::jmax (d, std::abs (o.getSample (c, i) - refOut.getSample (c, i))); return d; };

    for (int type : { (int) ap::clean, (int) ap::punch, (int) ap::smooth, (int) ap::warm })
    {
        auto lite = mk (false); proSettings (*lite, type);
        const float d = maxDiff (run (*lite, drums, len));
        check (d == 0.0f, juce::String ("Lite + automation to ") + ap::typeName (type) + " / 0.1 ms / 1.2 s / 10:1 / 150 Hz / drive 100 = plain Lite (max diff " + juce::String (d) + ")");
    }
    // Saved Pro session opened in Lite
    {
        auto pro = mk (true); proSettings (*pro, ap::warm);
        juce::MemoryBlock mb; pro->getStateInformation (mb);
        auto lite = std::make_unique<APP>(); setPro (*lite, hz::LicenceInfo::none);
        lite->setStateInformation (mb.getData(), (int) mb.getSize());
        lite->setLearnedForTest (l);
        lite->setRateAndBufferSizeDetails (SR, BS); lite->prepareToPlay (SR, BS);
        const float d = maxDiff (run (*lite, drums, len));
        check (d == 0.0f, "Pro session opened in Lite falls back to plain Lite (max diff " + juce::String (d) + ")");
        // expired trial behaves like Lite too
        auto exp = std::make_unique<APP>(); setPro (*exp, hz::LicenceInfo::trialExpired);
        exp->setStateInformation (mb.getData(), (int) mb.getSize()); exp->setLearnedForTest (l);
        exp->setRateAndBufferSizeDetails (SR, BS); exp->prepareToPlay (SR, BS);
        check (maxDiff (run (*exp, drums, len)) == 0.0f, "expired trial = Lite");
    }
    {
        auto pro = mk (true); proSettings (*pro, ap::punch);
        check (maxDiff (run (*pro, drums, len)) > 0.01f, "sanity: the same settings DO change the sound in Pro");
        auto glue = mk (false); setParam (*glue, "type", 1);
        check (maxDiff (run (*glue, drums, len)) > 0.001f, "GLUE works in Lite");
    }
}

// ── Type comparison + sound examples ─────────────────────────────────────────
static void typeTests (const std::map<std::string, ap::Learned>& learned, const juce::File& soundDir)
{
    std::printf ("\n== Types compared (AUTO everything, after learning; 8 s, first 1 s skipped) ==\n");
    soundDir.createDirectory();
    struct S { const char* name; const char* file; Buf sig; };
    std::vector<S> sigs = { { "drums", "drums", drumLoop (8) }, { "bass/pad", "bass-pad", bassPad (8) }, { "mix bus", "mix-bus", mixBus (8) } };
    const int from = (int) SR;
    for (auto& s : sigs)
    {
        const auto& l = learned.at (s.name);
        writeWav (soundDir.getChildFile (juce::String (s.file) + "-0-DRY.wav"), s.sig);
        const auto in = levelOf (s.sig, from);
        std::printf ("  %-8s DRY      peak %6.1f  rms %6.1f  crest %5.1f\n", s.name, in.peakDb, in.rmsDb, in.crestDb);
        for (int t = 0; t < ap::numTypes; ++t)
        {
            auto p = std::make_unique<APP>(); setPro (*p, hz::LicenceInfo::pro);
            p->setLearnedForTest (l); setParam (*p, "type", (float) t);
            p->setRateAndBufferSizeDetails (SR, BS); p->prepareToPlay (SR, BS);
            std::vector<float> tr; tr.reserve ((size_t) s.sig.getNumSamples() + 10); p->grTrace = &tr;
            auto out = run (*p, s.sig);
            double grSum = 0, grMax = 0; for (size_t i = (size_t) from; i < tr.size(); ++i) { grSum += tr[i]; grMax = std::max (grMax, (double) tr[i]); }
            const auto o = levelOf (out, from);
            std::printf ("  %-8s %-7s  peak %6.1f  rms %6.1f  crest %5.1f (%+5.1f)  avgGR %4.1f  maxGR %4.1f  residual %6.1f dB\n",
                         s.name, ap::typeName (t), o.peakDb, o.rmsDb, o.crestDb, o.crestDb - in.crestDb,
                         grSum / (double) (tr.size() - (size_t) from), grMax, residualDb (s.sig, out, from));
            writeWav (soundDir.getChildFile (juce::String (s.file) + "-" + juce::String (t + 1) + "-" + ap::typeName (t) + ".wav"), out);
        }
    }
    // Harmonic colour per type: 100 Hz sine at the learned bass level, THD after the fundamental is removed.
    {
        const auto& l = learned.at ("bass/pad");
        std::printf ("  THD on a 100 Hz sine at the bass/pad level (%.1f dBFS peak):\n", l.peakDb);
        const int n = (int) (3 * SR);
        Buf sine (2, n);
        const float amp = juce::Decibels::decibelsToGain (l.peakDb);
        for (int i = 0; i < n; ++i) { const float v = amp * (float) std::sin (twoPi * 100.0 * i / SR); sine.setSample (0, i, v); sine.setSample (1, i, v); }
        for (int t = 0; t < ap::numTypes; ++t)
        {
            auto p = std::make_unique<APP>(); setPro (*p, hz::LicenceInfo::pro);
            p->setLearnedForTest (l); setParam (*p, "type", (float) t);
            p->setRateAndBufferSizeDetails (SR, BS); p->prepareToPlay (SR, BS);
            std::vector<float> tr; tr.reserve ((size_t) n + 10); p->grTrace = &tr;
            auto out = run (*p, sine);
            // least-squares fit of DC + sin + cos at 100 Hz over the last second
            const int a0 = 2 * (int) SR; double ss = 0, sc = 0, cc = 0, ys = 0, yc = 0, tot = 0;
            for (int i = a0; i < n; ++i) { const double ph = twoPi * 100.0 * i / SR, s1 = std::sin (ph), c1 = std::cos (ph), y = out.getSample (0, i);
                ss += s1 * s1; cc += c1 * c1; sc += s1 * c1; ys += y * s1; yc += y * c1; }
            const double det = ss * cc - sc * sc, A = (ys * cc - yc * sc) / det, B = (yc * ss - ys * sc) / det;
            double mean = 0; for (int i = a0; i < n; ++i) mean += out.getSample (0, i); mean /= (n - a0);
            double res = 0;
            for (int i = a0; i < n; ++i) { const double ph = twoPi * 100.0 * i / SR; const double d = out.getSample (0, i) - mean - A * std::sin (ph) - B * std::cos (ph); res += d * d; tot += (A * std::sin (ph) + B * std::cos (ph)) * (A * std::sin (ph) + B * std::cos (ph)); }
            std::printf ("     %-7s GR %4.1f dB  THD %6.2f %%\n", ap::typeName (t), tr.back(), 100.0 * std::sqrt (res / tot));
        }
    }
    // Drive level compensation
    {
        const auto& l = learned.at ("drums");
        auto sig = drumLoop (6);
        double rms[2];
        for (int k = 0; k < 2; ++k)
        {
            auto p = std::make_unique<APP>(); setPro (*p, hz::LicenceInfo::pro);
            p->setLearnedForTest (l); setParam (*p, "drive", k ? 100.0f : 0.0f);
            p->setRateAndBufferSizeDetails (SR, BS); p->prepareToPlay (SR, BS);
            auto out = run (*p, sig);
            rms[k] = levelOf (out, from).rmsDb;
            if (k) writeWav (soundDir.getChildFile ("drums-6-CLEAN-drive100.wav"), out);
        }
        std::printf ("  drive 100%% vs 0%% on drums: rms %+.2f dB\n", rms[1] - rms[0]);
        check (std::abs (rms[1] - rms[0]) < 1.5, "DRIVE is level-compensated (within 1.5 dB)");
    }
}

// ── UI ───────────────────────────────────────────────────────────────────────
static void snapshots (const juce::File& outDir, const Buf& drums)
{
    std::printf ("\n== UI snapshots ==\n");
    auto snap = [&] (const char* name, int licKind, const char* panel, std::function<void (APP&)> setup, bool learnFirst = true, int learnHalf = 0)
    {
        auto p = std::make_unique<APP>(); setPro (*p, licKind);
        p->setRateAndBufferSizeDetails (SR, BS); p->prepareToPlay (SR, BS);
        if (learnFirst) { learn (*p, drums); run (*p, drums, (int) (2.5 * SR)); }
        if (learnHalf > 0) run (*p, drums, learnHalf);
        if (setup) { setup (*p); if (learnFirst) run (*p, drums, (int) (2.2 * SR)); }
        if (learnFirst) run (*p, drums, BS * 4);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
        if (auto* t = dynamic_cast<juce::Timer*> (ed.get())) for (int i = 0; i < 20; ++i) t->timerCallback();
        if (panel != nullptr)
            for (auto* c : ed->getChildren())
                if (auto* pp = dynamic_cast<ProPanel*> (c)) pp->show (panel);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
        juce::FileOutputStream fo (outDir.getChildFile (name));
        fo.setPosition (0); fo.truncate();
        juce::PNGImageFormat().writeImageToStream (img, fo);
        std::printf ("  %s\n", name);
    };
    snap ("ui-1-lite.png", hz::LicenceInfo::none, nullptr, nullptr);
    snap ("ui-2-lite-glue.png", hz::LicenceInfo::none, nullptr, [] (APP& p) { setParam (p, "type", 1); });
    snap ("ui-3-lite-pro-panel.png", hz::LicenceInfo::none, "PUNCH IS AN UPGRADE COMPRESSOR TYPE", nullptr);
    snap ("ui-4-trial.png", hz::LicenceInfo::trial, nullptr, [] (APP& p) { setParam (p, "type", 2); });
    snap ("ui-5-trial-ended.png", hz::LicenceInfo::trialExpired, nullptr, nullptr);
    snap ("ui-6-trial-ended-panel.png", hz::LicenceInfo::trialExpired, "", nullptr);
    snap ("ui-7-pro.png", hz::LicenceInfo::pro, nullptr, [] (APP& p)
          { setParam (p, "type", 4); setParam (p, "attack", 3); setParam (p, "ratio", 2); setParam (p, "schpf", 2); setParam (p, "drive", 35); setParam (p, "mix", 80); setParam (p, "threshold", -2.0f); });
    snap ("ui-8-pro-panel.png", hz::LicenceInfo::pro, "", nullptr);
    snap ("ui-9-learning.png", hz::LicenceInfo::none, nullptr, nullptr, false, (int) (2.6 * SR));
    snap ("ui-10-waiting.png", hz::LicenceInfo::none, nullptr, nullptr, false, 0);

    // Licence states (Launch prep renders)
    auto upgradeSettings = [] (APP& p) { setParam (p, "type", 2); setParam (p, "attack", 3); setParam (p, "ratio", 2); setParam (p, "schpf", 2); setParam (p, "drive", 35); };
    snap ("licence-1-free.png", hz::LicenceInfo::none, nullptr, nullptr);
    snap ("licence-2-free-panel.png", hz::LicenceInfo::none, "", nullptr);
    snap ("licence-3-trial.png", hz::LicenceInfo::trial, nullptr, upgradeSettings);
    snap ("licence-4-trial-panel.png", hz::LicenceInfo::trial, "", nullptr);
    snap ("licence-5-trial-ended.png", hz::LicenceInfo::trialExpired, nullptr, nullptr);
    snap ("licence-6-trial-ended-panel.png", hz::LicenceInfo::trialExpired, "", nullptr);
    snap ("licence-7-licensed.png", hz::LicenceInfo::pro, nullptr, upgradeSettings);
    snap ("licence-8-licensed-panel.png", hz::LicenceInfo::pro, "", nullptr);
    snap ("licence-9-saved-upgrade-settings.png", hz::LicenceInfo::trialExpired, nullptr, upgradeSettings);
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc > 3 && juce::String (argv[1]) == "render")
    {
        // AutoPressProTest render <outdir> file.wav ... : learn on each file (Pro), then render every type on AUTO.
        juce::AudioFormatManager fm; fm.registerBasicFormats();
        const juce::File od { juce::String (argv[2]) }; od.createDirectory();
        for (int a = 3; a < argc; ++a)
        {
            const juce::File f { juce::String (argv[a]) };
            std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
            if (! r) continue;
            const int len = (int) r->lengthInSamples;
            Buf x (2, len); r->read (&x, 0, len, 0, true, true);
            if (r->numChannels == 1) x.copyFrom (1, 0, x, 0, 0, len);
            x.applyGain (0.5f / juce::jmax (1e-9f, x.getMagnitude (0, len)));
            const double fs = r->sampleRate;
            for (int t = 0; t < ap::numTypes; ++t)
            {
                auto p = std::make_unique<APP>(); setPro (*p, hz::LicenceInfo::pro);
                setParam (*p, "type", (float) t);
                p->setRateAndBufferSizeDetails (fs, BS); p->prepareToPlay (fs, BS);
                int pos = 0;
                while (! (p->getPhase() == APP::idle && p->getLearned().valid))
                {
                    if (p->getPhase() == APP::analysing) { juce::Thread::sleep (10); continue; }
                    run (*p, x, BS, pos); pos += BS;
                }
                const auto l = p->getLearned();
                if (t == 0) printLearned (f.getFileName().toRawUTF8(), l);
                auto out = run (*p, x, len, 0);
                writeWav (od.getChildFile (f.getParentDirectory().getFileName() + "-" + f.getFileNameWithoutExtension() + "-" + ap::typeName (t) + ".wav"), out, fs);
            }
        }
        return 0;
    }
    if (argc > 2 && juce::String (argv[1]) == "classify")
    {
        // Run the learner on real files: AutoPressProTest classify a.wav b.wav ...
        juce::AudioFormatManager fm; fm.registerBasicFormats();
        for (int a = 2; a < argc; ++a)
        {
            std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (juce::File (juce::String (argv[a]))));
            if (! r) continue;
            const int ch = juce::jmin (2, (int) r->numChannels), len = (int) r->lengthInSamples;
            Buf all (ch, len); r->read (&all, 0, len, 0, true, true);
            int st = 0;   // skip leading silence, like the plugin does
            while (st < len && std::abs (all.getSample (0, st)) < 1.0e-3f && std::abs (all.getSample (ch - 1, st)) < 1.0e-3f) ++st;
            const int n = juce::jmin (len - st, (int) (r->sampleRate * 16));
            std::vector<float> il ((size_t) n * (size_t) ch);
            for (int i = 0; i < n; ++i) for (int c = 0; c < ch; ++c) il[(size_t) i * (size_t) ch + (size_t) c] = all.getSample (c, st + i);
            const auto l = ap::analyse (il, ch, r->sampleRate, 124.0);
            std::printf ("%-60.60s %-9s transients %4.1f%%  crest %4.1f  CLEAN atk %4.1f ms rel %4.0f ms | PUNCH atk %.1f\n",
                         juce::File (juce::String (argv[a])).getFileName().toRawUTF8(), ap::materialName (l.material), l.onsetDb, l.crestDb,
                         l.t[ap::clean].attackMs, l.t[ap::clean].relSlowMs, l.t[ap::punch].attackMs);
        }
        return 0;
    }
    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    outDir.createDirectory();
    const bool quick = argc > 2 && juce::String (argv[2]) == "quick";

    const auto drums = drumLoop (12);
    auto learned = learnTests();
    if (! quick)
    {
        timingTests();
        legacyTiming();
        clickTests();
    }
    instrumentTests();
    cleanDrumTests (learned["drums"]);
    if (! quick) realStemTests();
    stateTests (drums);
    liteTests (learned["drums"], drums);
    typeTests (learned, outDir.getChildFile ("sounds"));
    snapshots (outDir, drums);

    std::printf ("\n== Licence keys ==\n");
    for (int a = 2; a < argc; ++a)
    {
        const juce::String arg (argv[a]);
        if (! arg.containsChar ('=')) continue;
        const auto name = arg.upToFirstOccurrenceOf ("=", false, false);
        const auto info = hz::Licence::verify (arg.fromFirstOccurrenceOf ("=", false, false), "autopresspro");
        const char* kinds[] = { "none", "trial", "pro", "trialExpired", "invalid", "saveFailed" };
        std::printf ("  key %-14s -> %-12s daysLeft=%d\n", name.toRawUTF8(), kinds[(int) info.kind], info.daysLeft());
        hz::LicenceInfo::Kind want = hz::LicenceInfo::invalid;
        if (name == "pro") want = hz::LicenceInfo::pro;
        else if (name == "trial") want = hz::LicenceInfo::trial;
        else if (name == "expired") want = hz::LicenceInfo::trialExpired;
        else if (name == "all") want = hz::LicenceInfo::pro;
        check (info.kind == want, "key " + name + " -> " + kinds[(int) info.kind]);
    }

    hztest::runLicenceTests<APP> ("autopresspro", [] (APP& p) { return p.proUnlocked.load(); });
    hztest::runSavedSettingsTests<APP> ([] (APP& p)
    {
        setParam (p, "type", (float) ap::punch); setParam (p, "attack", 3); setParam (p, "release", 1);
        setParam (p, "ratio", 2); setParam (p, "schpf", 2); setParam (p, "drive", 35);
    });
    {
        // and the audio still falls back to free: no licence + saved upgrade settings = CLEAN, AUTO, no HPF, no drive
        APP p; p.setLicenceInfo ({});
        setParam (p, "type", (float) ap::punch); setParam (p, "attack", 3); setParam (p, "schpf", 2); setParam (p, "drive", 35);
        const auto e = p.resolve (false, p.getLearned());
        hztest::expect (p.usesUpgradeSettings() && e.type == ap::clean && e.attackAuto && e.hpf == 0 && e.drive == 0.0f,
                        "no licence + saved upgrade settings: flagged, plays as free");
    }
    const int licFails = hztest::finish();

    std::printf ("\n%s: %d failure(s)\n", failures == 0 && licFails == 0 ? "ALL PASS" : "FAILURES", failures + hztest::failures);
    return failures == 0 && licFails == 0 ? 0 : 1;
}
