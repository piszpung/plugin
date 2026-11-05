#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "SatEngine.h"
#include "Modulation.h"

class EmberAudioProcessor : public juce::AudioProcessor, private juce::AsyncUpdater
{
public:
    static constexpr int kAnalyzerSize = 4096;
    static constexpr int kChunk = 128;                 // control rate of the modulation: everything is processed in chunks of this many host samples
    static constexpr int kMaxOversampleStages = 5;     // 2^5 = 32x

    EmberAudioProcessor();
    ~EmberAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Ember"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.1; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    juce::AudioProcessorValueTreeState apvts { *this, nullptr, "STATE", createLayout() };

    // ---- for the editor (any thread, lock-free) ----
    // copies the newest `n` samples of the input (false) or output (true) signal, oldest first; n <= kAnalyzerSize
    void copyAnalyzer (bool output, float* dst, int n) const;
    float getBandLevel (int b) const { return bandMeter[(size_t) b].load (std::memory_order_relaxed); }
    double getSampleRateHz() const { return hostRate; }
    int getOversampleLatency() const { return latency; }

    // modulation: the live value of a source (LFO -1..1, envelope / slider 0..1), the LFO play-head, and the editable LFO steps
    float getSourceValue (int src) const { return srcValue[(size_t) std::clamp (src, 0, ember::kNumSources - 1)].load (std::memory_order_relaxed); }
    float getLfoPosition (int l) const { return lfoPos[(size_t) l].load (std::memory_order_relaxed); }
    float getLfoStep (int l, int i) const { return lfoSteps[(size_t) l][(size_t) i].load (std::memory_order_relaxed); }
    void setLfoStep (int l, int i, float v) { lfoSteps[(size_t) l][(size_t) i].store (std::clamp (v, -1.0f, 1.0f), std::memory_order_relaxed); }
    void setLfoShape (int l, int shape, unsigned seed = 1);
    // the parameter plain value including the modulation of the last chunk (for the GUI to show the moving knob), or the plain value if unmodulated
    float getModulatedNormalised (int target) const { return modShown[(size_t) std::clamp (target, 0, ember::kNumTargets - 1)].load (std::memory_order_relaxed); }

private:
    void handleAsyncUpdate() override { setLatencySamples (latency); }
    void switchOversampling (int idx);
    void readParameters (ember::GlobalParams&, const float* off) const;

    // parameter pointers
    struct BandPtrs { std::atomic<float> *on, *solo, *style, *drive, *level, *mix, *dyn, *tone, *fb, *fbf, *pan; };
    std::array<BandPtrs, ember::kMaxBands> pb {};
    std::array<std::atomic<float>*, 5> pXo {};
    std::atomic<float> *pIn = nullptr, *pOut = nullptr, *pMix = nullptr, *pBands = nullptr, *pOs = nullptr, *pMs = nullptr, *pLimit = nullptr, *pSlope = nullptr;

    // modulation
    struct Target { std::atomic<float>* raw = nullptr; juce::NormalisableRange<float> range; };
    std::array<Target, ember::kNumTargets> tgt {};
    struct LfoPtrs { std::atomic<float> *rate, *sync, *glide; };
    struct EnvPtrs { std::atomic<float> *atk, *rel, *gain; };
    struct RoutePtrs { std::atomic<float> *src, *tgt, *amt; };
    std::array<LfoPtrs, ember::kNumLfos> pLfo {};
    std::array<EnvPtrs, ember::kNumEnvs> pEnv {};
    std::array<std::atomic<float>*, ember::kNumSliders> pSlider {};
    std::array<RoutePtrs, ember::kNumRoutes> pRoute {};
    std::array<ember::Lfo, ember::kNumLfos> lfo;
    std::array<ember::Follower, ember::kNumEnvs> follower;
    std::array<std::array<std::atomic<float>, ember::kSteps>, ember::kNumLfos> lfoSteps;
    std::array<std::atomic<float>, ember::kNumSources> srcValue;
    std::array<std::atomic<float>, ember::kNumLfos> lfoPos;
    std::array<std::atomic<float>, ember::kNumTargets> modShown;

    ember::Engine engine;
    std::array<std::unique_ptr<juce::dsp::Oversampling<float>>, kMaxOversampleStages + 1> os;   // [0] unused (no oversampling)
    juce::AudioBuffer<float> scratch, dryRing;
    int ringPos = 0, ringSize = 8192, curOs = -1, latency = 0;
    double hostRate = 48000.0;
    float prevIn = 1.0f, prevOut = 1.0f, prevMix = 1.0f;
    ember::GlobalParams gp;

    std::array<std::atomic<float>, kAnalyzerSize> anaIn, anaOut;
    std::atomic<int> anaInPos { 0 }, anaOutPos { 0 };
    std::array<std::atomic<float>, ember::kMaxBands> bandMeter;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EmberAudioProcessor)
};
