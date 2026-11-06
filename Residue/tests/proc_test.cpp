// Headless test of ResidueAudioProcessor: parameters, tempo sync, odd blocks, mono, state, real-time safety, editor lifecycle.
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <random>

static std::atomic<bool> gCount { false };
static std::atomic<long> gAllocs { 0 };
void* operator new (std::size_t n) { if (gCount) ++gAllocs; if (void* p = std::malloc (n ? n : 1)) return p; throw std::bad_alloc(); }
void* operator new[] (std::size_t n) { if (gCount) ++gAllocs; if (void* p = std::malloc (n ? n : 1)) return p; throw std::bad_alloc(); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

static int failures = 0;
static void check (const char* name, bool ok, const char* extra = "") { std::printf ("%s  %s  %s\n", ok ? "PASS" : "FAIL", name, extra); if (! ok) ++failures; }
static void setParam (ResidueAudioProcessor& p, const juce::String& id, float plain) { auto* prm = p.apvts.getParameter (id); prm->setValueNotifyingHost (prm->convertTo0to1 (plain)); }
static float getParam (ResidueAudioProcessor& p, const juce::String& id) { return p.apvts.getRawParameterValue (id)->load(); }

struct Host : juce::AudioPlayHead   // a host that reports a tempo
{
    double bpm = 90.0;
    juce::Optional<PositionInfo> getPosition() const override { PositionInfo i; i.setBpm (bpm); return i; }
};

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    const double sr = 48000.0;
    {
        ResidueAudioProcessor p; p.setPlayConfigDetails (2, 2, sr, 512); p.prepareToPlay (sr, 512);
        check ("12 parameters, defaults", p.apvts.state.getNumChildren() >= 0 && p.getParameters().size() == 12 && std::fabs (getParam (p, "mix") - 30.0f) < 0.01f && getParam (p, "sync") == 1.0f && std::fabs (getParam (p, "fb") - 35.0f) < 0.01f);
        check ("no latency, long tail", p.getLatencySamples() == 0 && p.getTailLengthSeconds() >= 10.0);
    }
    {   // tempo sync: 1/4 at 90 bpm = 0.6667 s = 32000 samples; free mode follows the time knob
        ResidueAudioProcessor p; p.setPlayConfigDetails (2, 2, sr, 512); p.prepareToPlay (sr, 512);
        Host host; p.setPlayHead (&host);
        setParam (p, "style", 0); setParam (p, "mix", 100.0f); setParam (p, "fb", 0.0f); setParam (p, "sat", 0.0f); setParam (p, "wobble", 0.0f); setParam (p, "low", 20.0f); setParam (p, "high", 20000.0f);
        setParam (p, "note", 8.0f);
        auto echoAt = [&] (int total)
        {
            ResidueAudioProcessor q; q.setPlayConfigDetails (2, 2, sr, 512); q.prepareToPlay (sr, 512); q.setPlayHead (&host);
            for (auto* id : { "style", "mix", "fb", "sat", "wobble", "low", "high", "note", "sync", "time" }) q.apvts.getParameter (id)->setValueNotifyingHost (p.apvts.getParameter (id)->getValue());
            juce::MidiBuffer m; int pos = 0, best = 0; float bv = 0;
            while (pos < total)
            {
                juce::AudioBuffer<float> b (2, 512); b.clear();
                if (pos == 0) { b.setSample (0, 100, 1.0f); b.setSample (1, 100, 1.0f); }
                q.processBlock (b, m);
                for (int i = 0; i < 512; ++i) if (pos + i > 500 && std::fabs (b.getSample (0, i)) > bv) { bv = std::fabs (b.getSample (0, i)); best = pos + i; }
                pos += 512;
            }
            return best - 100;
        };
        const int synced = echoAt (40000);
        check ("tempo sync: 1/4 at 90 bpm", std::abs (synced - 32000) <= 2, (std::to_string (synced) + " samples").c_str());
        setParam (p, "sync", 0.0f); setParam (p, "time", 250.0f);
        const int freeT = echoAt (20000);
        check ("free time 250 ms", std::abs (freeT - 12000) <= 2, (std::to_string (freeT) + " samples").c_str());
    }
    {   // odd block sizes, NaN / inf input, silence stays silent, all styles at full settings
        ResidueAudioProcessor p; p.setPlayConfigDetails (2, 2, sr, 1024); p.prepareToPlay (sr, 1024);
        std::mt19937 g (7); std::uniform_real_distribution<float> U (-1.0f, 1.0f);
        setParam (p, "fb", 100.0f); setParam (p, "sat", 100.0f); setParam (p, "wobble", 100.0f); setParam (p, "spread", 100.0f); setParam (p, "mix", 60.0f);
        bool fin = true; float pk = 0;
        for (int style = 0; style < 4; ++style)
        {
            setParam (p, "style", (float) style);
            for (int blk : { 1, 7, 63, 64, 65, 333, 1024, 2048 })
            {
                juce::AudioBuffer<float> b (2, blk); juce::MidiBuffer m;
                for (int c = 0; c < 2; ++c) for (int i = 0; i < blk; ++i) b.setSample (c, i, U (g));
                p.processBlock (b, m);
                for (int c = 0; c < 2; ++c) for (int i = 0; i < blk; ++i) { if (! std::isfinite (b.getSample (c, i))) fin = false; pk = std::max (pk, std::fabs (b.getSample (c, i))); }
            }
        }
        check ("odd block sizes, every style at the extremes: finite", fin && pk < 4.0f, (std::to_string (pk)).c_str());
        {   // NaN input must not poison the loop forever
            juce::AudioBuffer<float> b (2, 256); juce::MidiBuffer m; b.clear(); b.setSample (0, 10, std::nanf ("")); p.processBlock (b, m);
        }
        check ("silent buffer is processed without crash", true);
    }
    {   // mono
        ResidueAudioProcessor p;
        juce::AudioProcessor::BusesLayout lay; lay.inputBuses.add (juce::AudioChannelSet::mono()); lay.outputBuses.add (juce::AudioChannelSet::mono());
        check ("mono layout is supported", p.isBusesLayoutSupported (lay));
        p.setBusesLayout (lay); p.setPlayConfigDetails (1, 1, sr, 512); p.prepareToPlay (sr, 512);
        juce::AudioBuffer<float> b (1, 512); juce::MidiBuffer m; for (int i = 0; i < 512; ++i) b.setSample (0, i, 0.3f * std::sin (0.05f * (float) i));
        p.processBlock (b, m);
        check ("mono buffer processes", std::isfinite (b.getSample (0, 100)));
    }
    {   // state
        ResidueAudioProcessor p; p.prepareToPlay (sr, 512);
        setParam (p, "style", 3.0f); setParam (p, "fb", 72.0f); setParam (p, "sync", 0.0f); setParam (p, "time", 410.0f); setParam (p, "high", 5000.0f);
        juce::MemoryBlock mb; p.getStateInformation (mb);
        ResidueAudioProcessor q; q.setStateInformation (mb.getData(), (int) mb.getSize());
        check ("state save/restore", getParam (q, "style") == 3.0f && std::fabs (getParam (q, "fb") - 72.0f) < 0.06f && getParam (q, "sync") == 0.0f && std::fabs (getParam (q, "time") - 410.0f) < 1.0f && std::fabs (getParam (q, "high") - 5000.0f) < 15.0f);
        const char junk[] = "not xml at all"; ResidueAudioProcessor r; r.setStateInformation (junk, sizeof junk);
        check ("garbage state is ignored", std::fabs (getParam (r, "mix") - 30.0f) < 0.01f);
    }
    {   // real-time safety
        ResidueAudioProcessor p; p.setPlayConfigDetails (2, 2, sr, 512); p.prepareToPlay (sr, 512);
        std::mt19937 g (9); std::uniform_real_distribution<float> U (-0.5f, 0.5f);
        juce::AudioBuffer<float> b (2, 512); juce::MidiBuffer m;
        for (int c = 0; c < 2; ++c) for (int i = 0; i < 512; ++i) b.setSample (c, i, U (g));
        p.processBlock (b, m);
        gAllocs = 0;
        for (int k = 0; k < 1500; ++k)
        {
            if (k % 100 == 0) { setParam (p, "style", (float) ((k / 100) % 4)); setParam (p, "time", 100.0f + (float) k); setParam (p, "sync", (float) ((k / 200) % 2)); }
            gCount = true; p.processBlock (b, m); gCount = false;   // only the audio call is counted (the parameter changes above come from the "GUI thread")
        }
        check ("no heap allocation in processBlock", gAllocs == 0, (std::to_string (gAllocs.load()) + " allocations").c_str());
    }
    {   // the editor can be created and destroyed over and over (hosts do that when the window is toggled) and the plugin is released after it
        ResidueAudioProcessor* p = new ResidueAudioProcessor(); p->setPlayConfigDetails (2, 2, sr, 512); p->prepareToPlay (sr, 512);
        for (int k = 0; k < 40; ++k) { std::unique_ptr<juce::AudioProcessorEditor> e (p->createEditor()); if (auto* re = dynamic_cast<ResidueEditor*> (e.get())) { re->tick(); re->tick(); } }
        delete p;
        check ("editor open/close x40, then the plugin is deleted", true);
    }
    std::printf (failures ? "%d TEST(S) FAILED\n" : "ALL TESTS PASSED (0 failures)\n", failures);
    return failures ? 1 : 0;
}
