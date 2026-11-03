// Headless test of PoltergeistAudioProcessor: parameters, state, processing, real-time safety.
#include "../Source/PluginProcessor.h"
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
static void check (const char* name, bool ok, const char* extra = "")
{
    std::printf ("%s  %s  %s\n", ok ? "PASS" : "FAIL", name, extra);
    if (! ok) ++failures;
}
static void setParam (PoltergeistAudioProcessor& p, const char* id, float plain)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (plain));
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    PoltergeistAudioProcessor proc;
    const double sr = 48000.0;
    proc.setPlayConfigDetails (2, 2, sr, 512);
    proc.prepareToPlay (sr, 512);

    // layouts
    {
        juce::AudioProcessor::BusesLayout st, mono, odd;
        st.inputBuses.add (juce::AudioChannelSet::stereo()); st.outputBuses.add (juce::AudioChannelSet::stereo());
        mono.inputBuses.add (juce::AudioChannelSet::mono()); mono.outputBuses.add (juce::AudioChannelSet::mono());
        odd.inputBuses.add (juce::AudioChannelSet::mono()); odd.outputBuses.add (juce::AudioChannelSet::stereo());
        check ("stereo and mono layouts supported, mono->stereo refused", proc.isBusesLayoutSupported (st) && proc.isBusesLayoutSupported (mono) && ! proc.isBusesLayoutSupported (odd));
    }

    // parameters
    {
        auto txt = [&] (const char* id, float plain) { auto* p = proc.apvts.getParameter (id); return p->getText (p->convertTo0to1 (plain), 0); };
        const auto r0 = txt ("rate", 0.5f), r1 = txt ("rate", 12.0f), d0 = txt ("depth", 0.0f), d1 = txt ("depth", 200.0f);
        std::printf ("     rate %s .. %s, depth %s .. %s\n", r0.toRawUTF8(), r1.toRawUTF8(), d0.toRawUTF8(), d1.toRawUTF8());
        check ("ranges: rate 0.5-12 Hz, depth 0-200 ct", r0.startsWith ("0.5") && r1.startsWith ("12.0") && d0.contains ("0 ct") && d1.contains ("200 ct"));
        check ("defaults: rate 5 Hz, depth 30 ct", std::fabs (proc.apvts.getRawParameterValue ("rate")->load() - 5.0f) < 1e-3f && std::fabs (proc.apvts.getRawParameterValue ("depth")->load() - 30.0f) < 1e-2f);
    }

    // depth 0 = transparent (3 samples of delay); depth > 0 changes the sound; the editor sees a sensible pitch deviation
    {
        setParam (proc, "depth", 0.0f);
        juce::AudioBuffer<float> in (2, 512), buf (2, 512); juce::MidiBuffer midi;
        double ph = 0.0; float hist[3] = {}; double err = 0; long cnt = 0;
        for (int b = 0; b < 100; ++b)
        {
            for (int i = 0; i < 512; ++i) { const float v = 0.5f * (float) std::sin (ph); ph += 2 * 3.14159265358979 * 1000.0 / sr; in.setSample (0, i, v); in.setSample (1, i, v); }
            buf.makeCopyOf (in); proc.processBlock (buf, midi);
            for (int i = 0; i < 512; ++i)
            {
                const float expected = hist[2];
                hist[2] = hist[1]; hist[1] = hist[0]; hist[0] = in.getSample (0, i);
                if (b > 20) { err = std::max (err, (double) std::fabs (buf.getSample (0, i) - expected)); ++cnt; }
            }
        }
        check ("depth 0 is transparent", err < 1e-4);

        setParam (proc, "depth", 150.0f); setParam (proc, "rate", 6.0f);
        float maxDev = 0.0f, minDev = 0.0f;
        for (int b = 0; b < 400; ++b)
        {
            for (int i = 0; i < 512; ++i) { const float v = 0.5f * (float) std::sin (ph); ph += 2 * 3.14159265358979 * 1000.0 / sr; in.setSample (0, i, v); in.setSample (1, i, v); }
            buf.makeCopyOf (in); proc.processBlock (buf, midi);
            if (b > 100) { maxDev = std::max (maxDev, proc.getDeviationCents()); minDev = std::min (minDev, proc.getDeviationCents()); }
        }
        char t[96]; std::snprintf (t, sizeof t, "deviation seen by the editor: %.0f .. %.0f ct (depth 150)", minDev, maxDev);
        check ("editor data: pitch deviation follows Depth", maxDev > 120.0f && maxDev < 175.0f && minDev < -120.0f && minDev > -190.0f, t);
        check ("editor data: output peak available", proc.takeOutputPeak() > 0.3f);
    }

    // state
    {
        setParam (proc, "rate", 8.3f); setParam (proc, "depth", 77.0f);
        juce::MemoryBlock mb; proc.getStateInformation (mb);
        PoltergeistAudioProcessor p2;
        p2.setStateInformation (mb.getData(), (int) mb.getSize());
        check ("state round trip", std::fabs (p2.apvts.getRawParameterValue ("rate")->load() - 8.3f) < 0.02f && std::fabs (p2.apvts.getRawParameterValue ("depth")->load() - 77.0f) < 0.2f);
        p2.setStateInformation ("garbage", 7);
        check ("garbage state is ignored", std::fabs (p2.apvts.getRawParameterValue ("rate")->load() - 8.3f) < 0.02f);
    }

    // mono and sample-rate changes
    {
        PoltergeistAudioProcessor p3;
        bool ok = true;
        for (double r : { 44100.0, 96000.0, 48000.0, 22050.0 })
        {
            p3.setPlayConfigDetails (1, 1, r, 256); p3.prepareToPlay (r, 256);
            juce::AudioBuffer<float> b3 (1, 256); juce::MidiBuffer m; double q = 0;
            for (int k = 0; k < 100; ++k) { for (int i = 0; i < 256; ++i) { b3.setSample (0, i, 0.4f * (float) std::sin (q)); q += 0.05; } p3.processBlock (b3, m); for (int i = 0; i < 256; ++i) if (! std::isfinite (b3.getSample (0, i))) ok = false; }
        }
        check ("mono buffers and sample rate changes", ok);
    }

    // real-time safety
    {
        juce::AudioBuffer<float> buf (2, 512); juce::MidiBuffer midi; std::mt19937 g (5); double ph = 0;
        long calls = 0;
        for (int b = 0; b < 1500; ++b)
        {
            for (int i = 0; i < 512; ++i) { const float v = 0.4f * (float) std::sin (ph); ph += 0.1; buf.setSample (0, i, v); buf.setSample (1, i, v); }
            if (b % 25 == 0) { setParam (proc, "rate", 0.5f + (float) (g() % 1150) / 100.0f); setParam (proc, "depth", (float) (g() % 200)); }
            gCount = true; proc.processBlock (buf, midi); gCount = false; ++calls;
        }
        char t[96]; std::snprintf (t, sizeof t, "%ld processBlock calls, heap allocations: %ld", calls, gAllocs.load());
        check ("no heap allocation in processBlock", gAllocs.load() == 0, t);
    }

    std::printf ("%s (%d failures)\n", failures ? "SOME TESTS FAILED" : "ALL TESTS PASSED", failures);
    return failures ? 1 : 0;
}
