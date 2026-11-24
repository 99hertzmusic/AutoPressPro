#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <vector>
#include "Features.h"
#include "CompEngine.h"
#include "Analysis.h"
#include "Licence/Licence.h"

// AutoPress Pro: the auto-learning compressor.
//  Lite (free): learns on first insert (LEARN to re-learn), THRESHOLD, MIX, CLEAN + GLUE, auto makeup.
//  Pro (key or 7-day trial): PUNCH / SMOOTH / WARM types, stepped ATTACK / RELEASE / RATIO,
//  SIDECHAIN HPF and DRIVE. The split itself lives in Features.h (ap::gate).
class AutoPressProAudioProcessor : public juce::AudioProcessor
{
public:
    AutoPressProAudioProcessor();
    ~AutoPressProAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "AutoPress Pro"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // ── Licence ─────────────────────────────────────────────────────────────
    hz::Licence licence { "autopresspro" };
    hz::LicenceInfo licenceInfo;                 // message thread
    std::atomic<bool> proUnlocked { false };     // audio thread reads this
    std::atomic<int> licenceVersion { 0 };       // bumps on every change, so open editors can follow
    void setLicenceInfo (const hz::LicenceInfo& i) { licenceInfo = i; proUnlocked.store (i.unlocked()); ++licenceVersion; }
    std::unique_ptr<hz::LicenceWatcher> licenceWatcher;   // picks up keys saved by other instances / trial expiry
    // True if the current settings include upgrade-only values (kept in the session, played as free).
    bool usesUpgradeSettings() const;

    // ── What actually runs, after the Lite/Pro gate and AUTO are resolved ───
    struct Effective
    {
        int   type = ap::clean;
        bool  attackAuto = true;  float attackMs = 10.0f;
        bool  releaseAuto = true; float releaseMs = 300.0f, relFastMs = 100.0f, relSlowMs = 400.0f;
        bool  ratioAuto = true;   float ratio = 2.0f;
        float kneeDb = 6.0f, threshDb = -18.0f, makeupDb = 0.0f;
        int   hpf = 0;
        float drive = 0.0f, mix = 1.0f;
    };
    Effective resolve (bool pro, const ap::Learned& L) const;

    // ── Learning ────────────────────────────────────────────────────────────
    enum Phase { idle = 0, capturing, analysing };
    void requestLearn();                         // LEARN key (message thread)
    int   getPhase() const       { return phase.load(); }
    float getLearnProgress() const { return learnProgress.load(); }
    bool  hasHeardSignal() const { return heardSignal.load(); }
    ap::Learned getLearned() const;              // copy, any thread but the audio thread

    // ── Meters / scope for the editor ──────────────────────────────────────
    std::atomic<float> inPeak { 0.0f }, outPeak { 0.0f }, grNow { 0.0f };
    static constexpr int scopeSize = 400;        // 5 ms per column = 2 s
    struct ScopeColumn { std::atomic<float> level { 0 }, gr { 0 }; };
    std::array<ScopeColumn, scopeSize> scope;
    std::atomic<int> scopeWrite { 0 };

    // ── Test hooks ─────────────────────────────────────────────────────────
    std::vector<float>* grTrace = nullptr;       // when set, per-sample GR (dB) is appended (not real-time safe)
    void setLearnedForTest (const ap::Learned& l);
    int  learnCount = 0;                         // completed analyses (tests check nothing re-learns)

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    friend class LearnWorker;
    void runAnalysis();
    void captureLearning (const juce::AudioBuffer<float>& buffer, int numCh, int n);

    std::atomic<float>* pType = nullptr; std::atomic<float>* pThresh = nullptr; std::atomic<float>* pAttack = nullptr;
    std::atomic<float>* pRelease = nullptr; std::atomic<float>* pRatio = nullptr; std::atomic<float>* pHpf = nullptr;
    std::atomic<float>* pDrive = nullptr; std::atomic<float>* pMix = nullptr;

    double sr = 44100.0;

    // Learned settings: `shared` is the truth (guarded by lock); the audio thread works from `L`.
    mutable juce::SpinLock learnLock;
    ap::Learned shared = ap::Learned::defaults();
    std::atomic<int> sharedVersion { 1 };
    ap::Learned L = ap::Learned::defaults();
    int localVersion = 0;

    // Capture (audio thread fills, worker copies). Never reallocated on the audio thread.
    std::vector<float> capture;
    int captureCh = 2;
    std::atomic<size_t> captureFrames { 0 };
    std::atomic<bool> captureReset { true }, workerBusy { false };
    std::atomic<int> phase { capturing }, gen { 0 };
    int captureGen = 0;
    std::atomic<float> learnProgress { 0.0f };
    std::atomic<bool> heardSignal { false };
    std::atomic<double> hostBpm { 120.0 };
    uint64_t activeFrames = 0;
    bool loopStarted = false; double lastPpq = -1.0;
    std::unique_ptr<juce::Thread> worker;

    // DSP state
    ap::Engine eng[2]; int cur = 0;
    float typeFade = 1.0f, typeFadeStep = 0.001f;
    int lastType = -1;
    Effective prevE;
    juce::SmoothedValue<float> sThresh, sRatio, sKnee, sMakeup, sMix, sDrive;
    juce::dsp::IIR::Filter<float> hpf[3][2];
    int hpfCur = 0, hpfPrev = 0; float hpfFade = 1.0f, hpfFadeStep = 0.001f;
    float dcX[2] = {}, dcY[2] = {}, dcR = 0.9995f;
    float engage = 0.0f, engageStep = 0.0f;
    std::vector<float> la[2]; int laPos = 0, laN = 1;    // lookahead delay for the audio path
    float atkLog = 0.0f, relLog = 0.0f, timeSmooth = 0.1f;   // attack/release glide (log ms) so step changes never click
    float drvPre = 0.0f, drvPost = 0.0f, drvAvg = 0.999f;     // drive auto level compensation
    bool wasValid = false;
    int scopeCount = 0, scopeHop = 240; float scopeLevel = 0.0f, scopeGr = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutoPressProAudioProcessor)
};
