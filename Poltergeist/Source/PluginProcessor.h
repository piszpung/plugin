#pragma once

#include <atomic>
#include <juce_audio_processors/juce_audio_processors.h>
#include "VibratoEngine.h"

class PoltergeistAudioProcessor : public juce::AudioProcessor
{
public:
    PoltergeistAudioProcessor();
    ~PoltergeistAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Poltergeist"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
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
    float getDeviationCents() const { return deviation.load (std::memory_order_relaxed); }     // pitch deviation right now
    float takeOutputPeak() { return outPeak.exchange (0.0f); }                                // loudest sample since the last call

private:
    polter::Vibrato engine;
    std::atomic<float>* pRate = nullptr;
    std::atomic<float>* pDepth = nullptr;
    std::atomic<float> deviation { 0.0f }, outPeak { 0.0f };
    double rate = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PoltergeistAudioProcessor)
};
