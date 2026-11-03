#pragma once

#include <atomic>
#include <deque>
#include <memory>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "Sampler.h"

// What the editor needs to know about one bubble in the cloud.
struct OrbView
{
    int id = 0;
    int parent = 0;          // 0 = the original sample (the vial)
    int generation = 1;
    float hue = 0.6f;
    float seconds = 1.0f;
};

class PolymerAudioProcessor : public juce::AudioProcessor
{
public:
    static constexpr int kMaxOrbs = 36;       // bubbles kept in the cloud (oldest leaves are dissolved)
    static constexpr int kBatch = 6;          // variations per Transmute

    PolymerAudioProcessor();
    ~PolymerAudioProcessor() override;

    // ---- juce::AudioProcessor -------------------------------------------------------------------
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Polymer"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- API used by the editor (message thread) -----------------------------------------------------
    bool loadSampleFile (const juce::File&);          // false = could not read / silent
    bool transmute();                                  // variations of the selected sound; false = nothing to do / busy
    void selectOrb (int id, bool audition, bool bloom);
    bool hasSource() const;
    int  getSelected() const;
    int  getPending() const { return pending.load(); }
    std::vector<OrbView> getOrbs() const;              // all bubbles (without the original)
    std::vector<float> getPeaks (int id) const;        // waveform thumbnail (id 0 = original)
    float getSourceSeconds() const;
    void resetCloud();                                 // drops all variations, keeps the sample (back to the state before the first Transmute)
    juce::File getExportDir() const;                   // where "grab sample" writes its .wav files
    void setExportDir (const juce::File&);             // remembered in the project and as the default for new ones
    juce::String getExportDirName() const;
    // writes a sound (0 = original) as a 24-bit WAV into a temp folder, for dragging it out of the plug-in; invalid File on failure
    juce::File exportSound (int id, const juce::String& baseName) const;
    int  getSampleVersion() const { return sampleVersion.load(); }
    bool sourceWasTrimmed() const { return trimmed.load(); }
    uint32_t getTriggerCount() const { return triggerCount.load(); }
    float takeOutputPeak() { return outPeak.exchange (0.0f); }         // loudest sample since the last call (for the editor's glow)
    int  getLastTriggerId() const { return lastTriggerId.load(); }

    juce::AudioProcessorValueTreeState apvts;

    // test hook: blocks until all pending variations are rendered
    bool waitUntilIdle (int timeoutMs) const;

private:
    struct Spec { int id; int parentId; uint32_t seed; float chaos; };
    struct Job  { int epoch = 0; bool restore = false; std::vector<Spec> specs; };

    class Worker : public juce::Thread
    {
    public:
        explicit Worker (PolymerAudioProcessor& o) : juce::Thread ("Polymer worker"), owner (o) {}
        void run() override { owner.workerLoop (*this); }
    private:
        PolymerAudioProcessor& owner;
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    void workerLoop (juce::Thread&);
    void processJob (const Job&, juce::Thread&);
    bool publish (std::shared_ptr<poly::Sound>, int epoch);
    std::shared_ptr<poly::Sound> findLocked (int id) const;
    void retireLocked (const std::shared_ptr<poly::Sound>&);
    int  findPrunableLocked (int protectedParent) const;
    void setSource (poly::Audio&&, bool wasTrimmed, const juce::MemoryBlock* encoded = nullptr);
    void purgeGraveyard (bool all);
    std::shared_ptr<poly::Sound> makeSound (int id, int parent, int gen, poly::Dna, poly::Audio&&, float parentHue = 0.0f) const;

    // tree (guarded by `lock`; the audio thread never touches it)
    mutable juce::CriticalSection lock;
    std::vector<std::shared_ptr<poly::Sound>> sounds;          // sounds[0] = original, rest in creation order
    std::vector<std::pair<std::shared_ptr<poly::Sound>, juce::uint32>> graveyard;
    int selected = 0, nextId = 1, epoch = 0, wantSelected = -1;
    float refRms = 0.0f;
    juce::String exportDirPath;      // empty = default (Documents/Polymer exports); guarded by lock
    juce::MemoryBlock sourceFlac;   // the source as saved in the project (16-bit FLAC), guarded by lock

    juce::CriticalSection queueLock;
    std::deque<Job> jobs;
    Worker worker { *this };
    std::atomic<int> pending { 0 };

    // audio thread side
    std::atomic<const poly::Sound*> active { nullptr };
    std::atomic<const poly::Sound*> auditionReq { nullptr };
    std::atomic<int> sampleVersion { 0 }, lastTriggerId { 0 };
    std::atomic<uint32_t> triggerCount { 0 };
    std::atomic<bool> trimmed { false };
    std::atomic<float> outPeak { 0.0f };
    poly::Sampler sampler;
    double hostRate = 44100.0;
    std::atomic<float>* pChaos = nullptr;
    std::atomic<float>* pGate = nullptr;
    juce::AudioFormatManager formats;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PolymerAudioProcessor)
};
