// Real-time safety + leak check. Counts heap allocations made inside processBlock (operator new and malloc).
// usage: rt_test sample.wav
#include "../Source/PluginProcessor.h"
#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>
#include <new>

static thread_local bool g_audio = false;
static std::atomic<long> g_allocs { 0 };

void* operator new (std::size_t n) { if (g_audio) ++g_allocs; if (void* p = std::malloc (n)) return p; throw std::bad_alloc(); }
void* operator new[] (std::size_t n) { if (g_audio) ++g_allocs; if (void* p = std::malloc (n)) return p; throw std::bad_alloc(); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

int main (int argc, char** argv)
{
    if (argc < 2) return 2;
    int rc = 0;
    {
        juce::ScopedJuceInitialiser_GUI init;
        PolymerAudioProcessor proc;
        proc.setPlayConfigDetails (0, 2, 48000.0, 512);
        proc.prepareToPlay (48000.0, 512);
        if (! proc.loadSampleFile (juce::File (argv[1]))) return 3;
        proc.transmute(); proc.waitUntilIdle (30000);
        auto orbs = proc.getOrbs();

        long blocks = 0;
        for (int round = 0; round < 40; ++round)
        {
            if (round % 5 == 1 && ! orbs.empty()) proc.selectOrb (orbs[(size_t) round % orbs.size()].id, true, false);   // audition + select while audio runs
            if (round % 10 == 9) { proc.selectOrb (0, false, false); proc.transmute(); }                                  // worker adds/prunes sounds meanwhile
            for (int b = 0; b < 40; ++b)
            {
                juce::AudioBuffer<float> buf (2, 512);
                juce::MidiBuffer midi;
                if (b % 8 == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 40 + (b + round) % 36, (juce::uint8) 100), 3);
                if (b % 8 == 5) midi.addEvent (juce::MidiMessage::allNotesOff (1), 3);
                g_audio = true;
                proc.processBlock (buf, midi);
                g_audio = false;
                ++blocks;
            }
            juce::Thread::sleep (5);
        }
        proc.waitUntilIdle (30000);
        std::printf ("processBlock calls: %ld, heap allocations inside them: %ld\n", blocks, g_allocs.load());
        if (g_allocs.load() != 0) rc = 1;

        juce::MemoryBlock st; proc.getStateInformation (st);
        PolymerAudioProcessor p2; p2.setPlayConfigDetails (0, 2, 48000.0, 512); p2.prepareToPlay (48000.0, 512);
        p2.setStateInformation (st.getData(), (int) st.getSize()); p2.waitUntilIdle (60000);
        p2.loadSampleFile (juce::File (argv[1]));            // replace the sample (retires every sound)
    }
    std::puts (rc == 0 ? "RT OK" : "RT FAIL");
    return rc;
}
