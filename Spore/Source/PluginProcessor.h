#pragma once

#include <array>
#include <atomic>
#include <juce_audio_processors/juce_audio_processors.h>
#include "GrainEngine.h"

class SporeAudioProcessor : public juce::AudioProcessor
{
public:
    SporeAudioProcessor();
    ~SporeAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Spore"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }
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
    static constexpr int kGrains = spore::Engine::kMaxGrains;
    struct GrainView { float pan, depth, env; bool active; };
    GrainView getGrain (int i) const
    {
        return { vizPan[(size_t) i].load (std::memory_order_relaxed), vizDepth[(size_t) i].load (std::memory_order_relaxed),
                 vizEnv[(size_t) i].load (std::memory_order_relaxed), vizOn[(size_t) i].load (std::memory_order_relaxed) };
    }
    float takeInputPeak()  { return inPeak.exchange (0.0f); }
    float takeOutputPeak() { return outPeak.exchange (0.0f); }
    bool isFrozen() const { return pFreeze != nullptr && pFreeze->load() > 0.5f; }

private:
    spore::Engine engine;
    spore::VizGrain viz[spore::Engine::kMaxGrains];
    std::array<std::atomic<float>, kGrains> vizPan {}, vizDepth {}, vizEnv {};
    std::array<std::atomic<bool>, kGrains> vizOn {};
    std::atomic<float> inPeak { 0.0f }, outPeak { 0.0f };

    std::atomic<float>* pDensity = nullptr;
    std::atomic<float>* pSize = nullptr;
    std::atomic<float>* pBlur = nullptr;
    std::atomic<float>* pDecay = nullptr;
    std::atomic<float>* pFreeze = nullptr;
    double rate = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SporeAudioProcessor)
};
