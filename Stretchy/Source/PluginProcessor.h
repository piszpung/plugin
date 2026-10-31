#pragma once

#include <atomic>
#include <juce_audio_processors/juce_audio_processors.h>
#include "StretchEngine.h"

class ChronosphereAudioProcessor : public juce::AudioProcessor
{
public:
    ChronosphereAudioProcessor();
    ~ChronosphereAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Chronosphere"; }
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

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    juce::AudioProcessorValueTreeState apvts { *this, nullptr, "STATE", createLayout() };

private:
    static constexpr int kChunk = 4096;

    slowmo::StretchEngine engine;
    double currentSampleRate = 0.0;

    std::vector<float> dryL, dryR, wetL, wetR;

    std::atomic<float>* pStretch = nullptr;
    std::atomic<float>* pLpf = nullptr;
    std::atomic<float>* pKey = nullptr;
    std::atomic<float>* pScale = nullptr;
    std::atomic<float>* pMix = nullptr;
    std::atomic<float>* pOutput = nullptr;
    std::atomic<float>* pAutoReset = nullptr;
    std::atomic<float>* pSafe = nullptr;

    float lastMixGain = 1.0f, lastOutGain = 1.0f;
    float lastGate = 1.0f;               // 1 while the host transport plays, fades to 0 when it stops
    bool wasPlaying = true;
    bool resetWhenSilent = false;        // engine is reset once the fade-out after a stop has finished
    juce::int64 lastTimeInSamples = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChronosphereAudioProcessor)
};
