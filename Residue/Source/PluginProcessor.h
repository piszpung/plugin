#pragma once

#include <atomic>
#include <juce_audio_processors/juce_audio_processors.h>
#include "EchoEngine.h"

class ResidueAudioProcessor : public juce::AudioProcessor
{
public:
    static constexpr int kChunk = 64;
    static constexpr double kMaxDelaySeconds = 4.0;

    ResidueAudioProcessor();
    ~ResidueAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Residue"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 12.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    juce::AudioProcessorValueTreeState apvts { *this, nullptr, "STATE", createLayout() };

    // for the editor (any thread)
    float getInputLevel() const { return inLevel.load (std::memory_order_relaxed); }
    float getOutputLevel() const { return outLevel.load (std::memory_order_relaxed); }
    float getDelaySeconds() const { return delaySec.load (std::memory_order_relaxed); }

private:
    std::atomic<float> *pTime, *pSync, *pNote, *pFb, *pSat, *pWob, *pDuck, *pSpread, *pLow, *pHigh, *pMix, *pStyle;
    residue::Engine engine;
    juce::AudioBuffer<float> scratch;
    double hostRate = 48000.0;
    std::atomic<float> inLevel { 0.0f }, outLevel { 0.0f }, delaySec { 0.3f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ResidueAudioProcessor)
};
