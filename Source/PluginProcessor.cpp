#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

static constexpr double learnSeconds   = 6.0;    // seconds of real (non-silent) audio to learn from
static constexpr double maxCaptureSecs = 16.0;   // hard cap (also the longest loop it will learn)

// Background analysis thread: woken by the audio thread when a capture is complete.
class LearnWorker : public juce::Thread
{
public:
    explicit LearnWorker (AutoPressProAudioProcessor& p) : juce::Thread ("AutoPress Pro learn"), proc (p) {}
    void run() override
    {
        while (! threadShouldExit())
            if (wake.wait (250) && ! threadShouldExit())
                proc.runAnalysis();
    }
    juce::WaitableEvent wake;
private:
    AutoPressProAudioProcessor& proc;
};

AutoPressProAudioProcessor::AutoPressProAudioProcessor()
    : AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "AutoPressProState", createParameterLayout())
{
    pType    = apvts.getRawParameterValue ("type");
    pThresh  = apvts.getRawParameterValue ("threshold");
    pAttack  = apvts.getRawParameterValue ("attack");
    pRelease = apvts.getRawParameterValue ("release");
    pRatio   = apvts.getRawParameterValue ("ratio");
    pHpf     = apvts.getRawParameterValue ("schpf");
    pDrive   = apvts.getRawParameterValue ("drive");
    pMix     = apvts.getRawParameterValue ("mix");

    setLicenceInfo (licence.check());
    licenceWatcher = std::make_unique<hz::LicenceWatcher> (licence, licenceInfo, [this] (const hz::LicenceInfo& i) { setLicenceInfo (i); });
    worker = std::make_unique<LearnWorker> (*this);
    worker->startThread (juce::Thread::Priority::low);
}

bool AutoPressProAudioProcessor::usesUpgradeSettings() const
{
    using namespace ap::gate;
    const int t = juce::jlimit (0, ap::numTypes - 1, (int) pType->load());
    return proType[t]
        || (proAttack  && (int) pAttack->load()  != ap::attackAuto)
        || (proRelease && (int) pRelease->load() != ap::releaseAuto)
        || (proRatio   && (int) pRatio->load()   != ap::ratioAuto)
        || (proScHpf   && (int) pHpf->load()     != 0)
        || (proDrive   && pDrive->load() > 0.05f);
}

AutoPressProAudioProcessor::~AutoPressProAudioProcessor()
{
    worker->signalThreadShouldExit();
    static_cast<LearnWorker*> (worker.get())->wake.signal();
    worker->stopThread (10000);
}

juce::AudioProcessorValueTreeState::ParameterLayout AutoPressProAudioProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto id = [] (const char* s) { return ParameterID { s, 1 }; };

    l.add (std::make_unique<AudioParameterChoice> (id ("type"), "Type", StringArray { "Clean", "Glue", "Punch", "Smooth", "Warm" }, 0));
    l.add (std::make_unique<AudioParameterFloat> (id ("threshold"), "Threshold (vs learned)", NormalisableRange<float> (-18.0f, 18.0f, 0.1f), 0.0f,
               AudioParameterFloatAttributes().withLabel ("dB")
                   .withStringFromValueFunction ([] (float v, int) { return (v > 0.04f ? "+" : "") + String (v, 1); })));
    l.add (std::make_unique<AudioParameterChoice> (id ("attack"), "Attack",
               StringArray { "0.1 ms", "0.3 ms", "1 ms", "3 ms", "10 ms", "30 ms", "Auto" }, ap::attackAuto));
    l.add (std::make_unique<AudioParameterChoice> (id ("release"), "Release",
               StringArray { "100 ms", "300 ms", "600 ms", "1200 ms", "Auto" }, ap::releaseAuto));
    l.add (std::make_unique<AudioParameterChoice> (id ("ratio"), "Ratio",
               StringArray { "1.5:1", "2:1", "4:1", "10:1", "Auto" }, ap::ratioAuto));
    l.add (std::make_unique<AudioParameterChoice> (id ("schpf"), "Sidechain HPF", StringArray { "Off", "60 Hz", "90 Hz", "150 Hz" }, 0));
    l.add (std::make_unique<AudioParameterFloat> (id ("drive"), "Drive", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f,
               AudioParameterFloatAttributes().withLabel ("%")));
    l.add (std::make_unique<AudioParameterFloat> (id ("mix"), "Mix", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
               AudioParameterFloatAttributes().withLabel ("%")));
    return l;
}

// ── Resolve: Lite/Pro gate + AUTO -> the numbers that actually run ────────────
AutoPressProAudioProcessor::Effective AutoPressProAudioProcessor::resolve (bool pro, const ap::Learned& Ld) const
{
    Effective e;
    e.type = ap::gate::effectiveType (juce::jlimit (0, ap::numTypes - 1, (int) pType->load()), pro);
    const auto& p = Ld.t[e.type];
    const auto& m = ap::model (e.type);

    const int a  = (pro || ! ap::gate::proAttack)  ? (int) pAttack->load()  : ap::attackAuto;
    const int r  = (pro || ! ap::gate::proRelease) ? (int) pRelease->load() : ap::releaseAuto;
    const int ra = (pro || ! ap::gate::proRatio)   ? (int) pRatio->load()   : ap::ratioAuto;

    e.attackAuto  = a < 0 || a >= ap::numAttackSteps;
    e.attackMs    = e.attackAuto ? p.attackMs : ap::attackStepsMs[a];
    e.releaseAuto = r < 0 || r >= ap::numReleaseSteps;
    e.releaseMs   = e.releaseAuto ? p.relSlowMs : ap::releaseStepsMs[r];
    e.relFastMs   = p.relFastMs;
    e.relSlowMs   = p.relSlowMs;
    e.ratioAuto   = ra < 0 || ra >= ap::numRatioSteps;
    e.ratio       = e.ratioAuto ? p.ratio : ap::ratioSteps[ra];
    e.kneeDb      = p.kneeDb;
    e.threshDb    = p.threshDb + pThresh->load();
    e.hpf         = (pro || ! ap::gate::proScHpf) ? juce::jlimit (0, ap::numHpfSteps - 1, (int) pHpf->load()) : 0;
    e.drive       = (pro || ! ap::gate::proDrive) ? juce::jlimit (0.0f, 1.0f, pDrive->load() / 100.0f) : 0.0f;
    e.mix         = juce::jlimit (0.0f, 1.0f, pMix->load() / 100.0f);

    // Auto makeup follows THRESHOLD / RATIO moves: compare the curve at the learned "typical" level.
    const float grNow     = ap::staticGr (p.refLevelDb, e.threshDb, e.ratio, e.kneeDb, m.variMu);
    const float grLearned = ap::staticGr (p.refLevelDb, p.threshDb, p.ratio, p.kneeDb, m.variMu);
    e.makeupDb = juce::jlimit (0.0f, 30.0f, p.makeupDb + juce::jlimit (-12.0f, 12.0f, 0.8f * (grNow - grLearned)));
    return e;
}

// ── Lifecycle ─────────────────────────────────────────────────────────────────
void AutoPressProAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    while (workerBusy.load()) juce::Thread::sleep (1);   // never resize while the worker copies
    const bool rateChanged = std::abs (sampleRate - sr) > 0.5 || capture.empty();
    sr = sampleRate;
    captureCh = juce::jlimit (1, 2, getTotalNumInputChannels());
    if (rateChanged || capture.size() != (size_t) (maxCaptureSecs * sr) * (size_t) captureCh)
    {
        capture.assign ((size_t) (maxCaptureSecs * sr) * (size_t) captureCh, 0.0f);
        captureReset.store (true);
    }

    for (auto& e : eng) e.prepare (sampleRate);
    laN = juce::jmax (1, (int) std::lround (ap::lookaheadMs * 0.001 * sampleRate));
    for (auto& d : la) d.assign ((size_t) laN, 0.0f);
    laPos = 0;
    setLatencySamples (laN);
    typeFade = 1.0f; typeFadeStep = (float) (1.0 / (0.04 * sampleRate));
    lastType = -1;

    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) juce::jmax (1, samplesPerBlock), 1 };
    for (int k = 0; k < 3; ++k)
        for (int c = 0; c < 2; ++c)
        {
            hpf[k][c].prepare (spec);
            hpf[k][c].coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass (sampleRate, ap::hpfStepsHz[k + 1], 0.707f);
            hpf[k][c].reset();
        }
    hpfFade = 1.0f; hpfFadeStep = (float) (1.0 / (0.03 * sampleRate));
    dcR = (float) (1.0 - juce::MathConstants<double>::twoPi * 5.0 / sampleRate);
    for (int c = 0; c < 2; ++c) dcX[c] = dcY[c] = 0.0f;

    auto initSmooth = [&] (juce::SmoothedValue<float>& s, double secs) { s.reset (sampleRate, secs); };
    initSmooth (sThresh, 0.05); initSmooth (sRatio, 0.05); initSmooth (sKnee, 0.05);
    initSmooth (sMakeup, 0.1);  initSmooth (sMix, 0.05);   initSmooth (sDrive, 0.03);

    // Settle everything on the current values (no ramps on load).
    {
        const juce::SpinLock::ScopedLockType lk (learnLock);
        L = shared; localVersion = sharedVersion.load();
    }
    const auto e = resolve (proUnlocked.load(), L);
    sThresh.setCurrentAndTargetValue (e.threshDb); sRatio.setCurrentAndTargetValue (e.ratio);
    sKnee.setCurrentAndTargetValue (e.kneeDb);     sMakeup.setCurrentAndTargetValue (e.makeupDb);
    sMix.setCurrentAndTargetValue (e.mix);         sDrive.setCurrentAndTargetValue (e.drive);
    hpfCur = hpfPrev = e.hpf;
    atkLog = std::log (e.attackMs); relLog = std::log (e.releaseMs);
    timeSmooth = (float) (1.0 - std::exp (-32.0 / (0.04 * sampleRate)));
    drvAvg = (float) std::exp (-1.0 / (0.4 * sampleRate)); drvPre = drvPost = 0.0f;
    wasValid = L.valid;
    engage = 0.0f;                                   // always fade the compressor in on load (no click)
    engageStep = (float) (1.0 / (0.5 * sampleRate));
    scopeHop = juce::jmax (1, (int) (0.005 * sampleRate));
}

bool AutoPressProAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return layouts.getMainInputChannelSet() == out;
}

// ── Learning ──────────────────────────────────────────────────────────────────
void AutoPressProAudioProcessor::requestLearn()
{
    if (workerBusy.load() || phase.load() == analysing) return;
    gen.fetch_add (1);
    captureReset.store (true);
    learnProgress.store (0.0f);
    heardSignal.store (false);
    phase.store (capturing);
    // A new learn starts from the new learned threshold.
    if (auto* t = apvts.getParameter ("threshold")) t->setValueNotifyingHost (t->convertTo0to1 (0.0f));
}

ap::Learned AutoPressProAudioProcessor::getLearned() const
{
    const juce::SpinLock::ScopedLockType lk (learnLock);
    return shared;
}

void AutoPressProAudioProcessor::setLearnedForTest (const ap::Learned& l)
{
    const juce::SpinLock::ScopedLockType lk (learnLock);
    shared = l;
    sharedVersion.fetch_add (1);
    gen.fetch_add (1);
    phase.store (l.valid ? idle : capturing);
    captureReset.store (true);
}

void AutoPressProAudioProcessor::captureLearning (const juce::AudioBuffer<float>& buffer, int numCh, int n)
{
    if (phase.load() != capturing || workerBusy.load() || capture.empty()) return;
    if (captureReset.exchange (false))
    {
        captureFrames.store (0); activeFrames = 0; loopStarted = false;
        heardSignal.store (false); learnProgress.store (0.0f);
    }

    bool looping = false, wrapped = false; double ppq = -1.0, ls = 0.0, le = 0.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) hostBpm.store (*b);
            looping = pos->getIsLooping() && pos->getIsPlaying();
            if (auto p = pos->getPpqPosition()) ppq = *p;
            if (auto lp = pos->getLoopPoints()) { ls = lp->ppqStart; le = lp->ppqEnd; }
        }
    looping = looping && ppq >= 0.0 && le > ls;
    if (looping)
    {
        wrapped = lastPpq >= 0.0 && ppq + 1.0e-4 < lastPpq;
        if (! loopStarted && (wrapped || std::abs (ppq - ls) < 0.02))
        {
            loopStarted = true; captureFrames.store (0); activeFrames = 0;
            wrapped = false;
        }
    }
    lastPpq = ppq;
    if (looping && ! loopStarted) return;

    const size_t cap = capture.size() / (size_t) captureCh;
    size_t frames = captureFrames.load();
    const float gate = ap::dbToGain (-60.0f);
    bool heard = heardSignal.load();
    const int chIn = juce::jmin (numCh, buffer.getNumChannels());
    for (int i = 0; i < n && frames < cap; ++i)
    {
        float xAbs = 0.0f;
        for (int c = 0; c < chIn; ++c) xAbs = juce::jmax (xAbs, std::abs (buffer.getSample (c, i)));
        if (! heard && xAbs <= gate) continue;
        heard = true;
        for (int c = 0; c < captureCh; ++c)
            capture[frames * (size_t) captureCh + (size_t) c] = buffer.getSample (juce::jmin (c, chIn - 1), i);
        ++frames;
        if (xAbs > gate) ++activeFrames;
    }
    captureFrames.store (frames);
    heardSignal.store (heard);

    const auto need = (uint64_t) (learnSeconds * sr);
    bool done = frames >= cap;
    float progress = (float) juce::jlimit (0.0, 1.0, (double) activeFrames / (double) need);
    if (looping)
    {
        if (wrapped && frames > (size_t) (0.5 * sr)) done = true;
        progress = (float) juce::jlimit (0.0, 1.0, (ppq - ls) / (le - ls));
    }
    else if (activeFrames >= need) done = true;
    learnProgress.store (done ? 1.0f : progress);

    if (done)
    {
        captureGen = gen.load();
        workerBusy.store (true);
        phase.store (analysing);
        static_cast<LearnWorker*> (worker.get())->wake.signal();
    }
}

void AutoPressProAudioProcessor::runAnalysis()
{
    if (! workerBusy.load()) return;
    const int myGen = captureGen, ch = captureCh;
    const size_t frames = juce::jmin (captureFrames.load(), capture.size() / (size_t) ch);
    std::vector<float> audio (capture.begin(), capture.begin() + (std::ptrdiff_t) (frames * (size_t) ch));
    const double rate = sr, bpm = hostBpm.load();
    workerBusy.store (false);

    auto result = ap::analyse (audio, ch, rate, bpm);

    const juce::SpinLock::ScopedLockType lk (learnLock);
    if (gen.load() == myGen)
    {
        shared = result;
        sharedVersion.fetch_add (1);
        phase.store (idle);
        ++learnCount;
    }
}

// ── Audio ─────────────────────────────────────────────────────────────────────
void AutoPressProAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int numCh = juce::jmin (2, getTotalNumInputChannels(), getTotalNumOutputChannels(), buffer.getNumChannels());
    for (int c = numCh; c < buffer.getNumChannels(); ++c) buffer.clear (c, 0, n);
    if (numCh <= 0 || n <= 0) return;

    captureLearning (buffer, numCh, n);

    // Pick up newly learned / restored settings.
    if (sharedVersion.load() != localVersion)
    {
        const juce::SpinLock::ScopedTryLockType lk (learnLock);
        if (lk.isLocked()) { L = shared; localVersion = sharedVersion.load(); }
    }
    if (L.valid && ! wasValid) engage = 0.0f;   // first learn: fade the compressor in over 0.5 s
    wasValid = L.valid;

    // Locked features fall back to their Lite values, so automation or an old session can't bypass the lock.
    const bool pro = proUnlocked.load();
    const Effective e = resolve (pro, L);

    if (lastType < 0) { eng[cur].setType (e.type); eng[cur].invalidate(); lastType = e.type; prevE = e; }
    if (e.type != lastType)
    {
        // Type change: keep the old engine running and crossfade 40 ms into the new one (no clicks).
        const int other = 1 - cur;
        eng[other] = eng[cur];
        eng[cur].setType (e.type);
        eng[cur].copyStateFrom (eng[other]);
        eng[cur].invalidate();
        typeFade = 0.0f;
        lastType = e.type;
    }
    ap::EngineSettings es;
    es.autoRelease = e.releaseAuto; es.relFastMs = e.relFastMs; es.relSlowMs = e.relSlowMs;
    ap::memoryFor (e.type, L.material, es);
    const float atkTarget = std::log (e.attackMs), relTarget = std::log (e.releaseMs);
    if (typeFade >= 1.0f) prevE = e;

    sThresh.setTargetValue (e.threshDb); sRatio.setTargetValue (e.ratio); sKnee.setTargetValue (e.kneeDb);
    sMakeup.setTargetValue (e.makeupDb); sMix.setTargetValue (e.mix);     sDrive.setTargetValue (e.drive);
    if (e.hpf != hpfCur) { hpfPrev = hpfCur; hpfCur = e.hpf; hpfFade = 0.0f; }

    const float refGain = 0.8f / ap::dbToGain (juce::jlimit (-40.0f, 0.0f, L.peakDb));
    auto typeSat = [refGain] (float x, int type, float gr)
    {
        const auto& m = ap::model (type);
        const float k = m.satBase + m.satGr * juce::jmin (1.0f, gr / 12.0f);
        if (k <= 0.0f) return x;
        return ap::asymSat (x * refGain, 0.6f + 2.4f * k, m.satBias) / refGain;
    };

    const bool active = L.valid;   // before the first learn, audio passes through untouched
    float blkIn = 0.0f, blkOut = 0.0f, blkGr = 0.0f;
    float* data[2] = { buffer.getWritePointer (0), buffer.getWritePointer (numCh > 1 ? 1 : 0) };

    for (int i = 0; i < n; ++i)
    {
        const float thr = sThresh.getNextValue(), ratio = sRatio.getNextValue(), knee = sKnee.getNextValue();
        const float makeup = ap::dbToGain (sMakeup.getNextValue());
        const float mix = sMix.getNextValue(), drive = sDrive.getNextValue();
        if ((i & 31) == 0)
        {
            // Attack / release glide to a new step over ~40 ms (in log time): no jump in the GR = no click.
            atkLog += (atkTarget - atkLog) * timeSmooth;
            relLog += (relTarget - relLog) * timeSmooth;
            es.attackMs = std::exp (atkLog); es.releaseMs = std::exp (relLog);
            eng[cur].setTiming (es);
        }

        // The detector hears the input now; the audio it acts on comes out of the lookahead delay.
        const float in[2] = { data[0][i], numCh > 1 ? data[1][i] : data[0][i] };
        float dry[2];
        for (int c = 0; c < 2; ++c) { dry[c] = la[c][(size_t) laPos]; la[c][(size_t) laPos] = in[c]; }
        if (++laPos >= laN) laPos = 0;

        // Detector feed with the sidechain high-pass (detector only). All filters always run.
        float xAbs = 0.0f;
        if (hpfFade < 1.0f) hpfFade = juce::jmin (1.0f, hpfFade + hpfFadeStep);
        for (int c = 0; c < numCh; ++c)
        {
            float f[4] = { in[c], 0, 0, 0 };
            for (int k = 0; k < 3; ++k) f[k + 1] = hpf[k][c].processSample (in[c]);
            const float d = f[hpfPrev] + (f[hpfCur] - f[hpfPrev]) * hpfFade;
            xAbs = juce::jmax (xAbs, std::abs (d));
        }

        float gr = eng[cur].process (xAbs, xAbs * xAbs, thr, ratio, knee);
        float grPrev = 0.0f;
        if (typeFade < 1.0f) grPrev = eng[1 - cur].process (xAbs, xAbs * xAbs, prevE.threshDb, prevE.ratio, prevE.kneeDb);
        const float g = ap::dbToGain (-gr), gPrev = ap::dbToGain (-grPrev);
        const float fade = typeFade;
        if (typeFade < 1.0f) typeFade = juce::jmin (1.0f, typeFade + typeFadeStep);
        const float grShown = fade < 1.0f ? grPrev + (gr - grPrev) * fade : gr;

        if (active && engage < 1.0f) engage = juce::jmin (1.0f, engage + engageStep);
        const float eg = active ? engage : 0.0f;

        for (int c = 0; c < numCh; ++c)
        {
            float wet = typeSat (dry[c] * g, e.type, gr);
            if (fade < 1.0f)
                wet = typeSat (dry[c] * gPrev, eng[1 - cur].getType(), grPrev) + (wet - typeSat (dry[c] * gPrev, eng[1 - cur].getType(), grPrev)) * fade;

            // DRIVE: analogue-style soft saturation after the compressor, level-compensated.
            if (drive > 1.0e-4f)
            {
                const float u = wet * refGain;
                const float s = ap::asymSat (u, 1.0f + 2.5f * drive, 0.12f);   // gentle: colour, not fuzz
                const float driven = wet + (s - u) / refGain * drive;
                if (c == 0)
                {
                    drvPre  = drvAvg * drvPre  + (1.0f - drvAvg) * wet * wet;
                    drvPost = drvAvg * drvPost + (1.0f - drvAvg) * driven * driven;
                }
                // slow loudness match (0.4 s) so more drive = more colour, not just louder or quieter
                const float comp = juce::jlimit (1.0f, 2.5f, std::sqrt ((drvPre + 1.0e-9f) / (drvPost + 1.0e-9f)));
                wet = driven * (1.0f + (comp - 1.0f) * juce::jmin (1.0f, drive * 4.0f));
            }
            else if (c == 0) { drvPre = drvPost = 0.0f; }
            // DC blocker (saturation asymmetry), then makeup.
            const float y = wet - dcX[c] + dcR * dcY[c];
            dcX[c] = wet; dcY[c] = y;
            wet = y * makeup;

            wet = dry[c] + (wet - dry[c]) * eg;
            float out = dry[c] + (wet - dry[c]) * mix;

            // Safety ceiling: soft above -1 dBFS, never past 0 dBFS.
            const float a = std::abs (out), t = 0.891f;
            if (a > t) out = std::copysign (t + (1.0f - t) * std::tanh ((a - t) / (1.0f - t)), out);

            blkIn = juce::jmax (blkIn, std::abs (dry[c]));
            blkOut = juce::jmax (blkOut, std::abs (out));
            data[c][i] = out;
        }

        const float grOut = grShown * eg;
        blkGr = juce::jmax (blkGr, grOut);
        if (grTrace != nullptr) grTrace->push_back (grShown);

        scopeLevel = juce::jmax (scopeLevel, std::abs (dry[0]));
        scopeGr = juce::jmax (scopeGr, grOut);
        if (++scopeCount >= scopeHop)
        {
            const int w = scopeWrite.load();
            scope[(size_t) w].level.store (scopeLevel);
            scope[(size_t) w].gr.store (scopeGr);
            scopeWrite.store ((w + 1) % scopeSize);
            scopeCount = 0; scopeLevel = scopeGr = 0.0f;
        }
    }

    inPeak.store (blkIn); outPeak.store (blkOut); grNow.store (blkGr);
}

// ── State ─────────────────────────────────────────────────────────────────────
void AutoPressProAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.removeChild (state.getChildWithName ("Learned"), nullptr);
    state.appendChild (getLearned().toTree(), nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void AutoPressProAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (! xml || ! xml->hasTagName (apvts.state.getType())) return;
    auto tree = juce::ValueTree::fromXml (*xml);
    const auto learnedTree = tree.getChildWithName ("Learned");
    const auto l = ap::Learned::fromTree (learnedTree);
    tree.removeChild (learnedTree, nullptr);
    apvts.replaceState (tree);

    // A saved, learned session is restored as-is: it never re-learns by itself.
    const juce::SpinLock::ScopedLockType lk (learnLock);
    shared = l;
    sharedVersion.fetch_add (1);
    gen.fetch_add (1);                       // drops any analysis still running
    if (l.valid) { phase.store (idle); learnProgress.store (1.0f); }
    else         { phase.store (capturing); captureReset.store (true); }
}

juce::AudioProcessorEditor* AutoPressProAudioProcessor::createEditor() { return new AutoPressProAudioProcessorEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new AutoPressProAudioProcessor(); }
