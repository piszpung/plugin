// Headless test of SporeAudioProcessor: parameters, state, processing, freeze, real-time safety (no heap use in processBlock).
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

static void setParam (SporeAudioProcessor& p, const char* id, float v01) { p.apvts.getParameter (id)->setValueNotifyingHost (v01); }

static double fillSine (juce::AudioBuffer<float>& b, double& ph, double hz, float amp, double sr)
{
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const float v = amp * (float) std::sin (ph); ph += 2.0 * 3.14159265358979 * hz / sr;
        b.setSample (0, i, v); b.setSample (1, i, v * 0.9f);
    }
    return ph;
}
static double rmsOf (const juce::AudioBuffer<float>& b) { double s = 0; for (int c = 0; c < 2; ++c) for (int i = 0; i < b.getNumSamples(); ++i) s += (double) b.getSample (c, i) * b.getSample (c, i); return std::sqrt (s / (2.0 * b.getNumSamples())); }

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    SporeAudioProcessor proc;
    const double sr = 48000.0;
    proc.setPlayConfigDetails (2, 2, sr, 512);
    proc.prepareToPlay (sr, 512);

    // layout
    {
        juce::AudioProcessor::BusesLayout st, mono;
        st.inputBuses.add (juce::AudioChannelSet::stereo()); st.outputBuses.add (juce::AudioChannelSet::stereo());
        mono.inputBuses.add (juce::AudioChannelSet::mono()); mono.outputBuses.add (juce::AudioChannelSet::mono());
        check ("stereo layout supported, mono refused", proc.isBusesLayoutSupported (st) && ! proc.isBusesLayoutSupported (mono));
    }

    // parameter texts
    {
        auto txt = [&] (const char* id, float v) { return proc.apvts.getParameter (id)->getText (v, 0); };
        const auto d0 = txt ("density", 0.0f), d1 = txt ("density", 1.0f), s0 = txt ("size", 0.0f), s1 = txt ("size", 1.0f), b1 = txt ("blur", 1.0f);
        std::printf ("     density %s .. %s, size %s .. %s, blur %s\n", d0.toRawUTF8(), d1.toRawUTF8(), s0.toRawUTF8(), s1.toRawUTF8(), b1.toRawUTF8());
        check ("parameter ranges: 2..64 grains, 10..500 ms, +-1200 ct", d0.startsWith ("2 ") && d1.startsWith ("64 ") && s0.startsWith ("10 ") && s1.startsWith ("500 ") && b1.contains ("1200"));
    }

    // processing
    juce::AudioBuffer<float> buf (2, 512);
    double ph = 0.0;
    bool finite = true;
    double lastRms = 0;
    for (int b = 0; b < 400; ++b)
    {
        fillSine (buf, ph, 330.0, 0.4f, sr);
        juce::MidiBuffer midi;
        proc.processBlock (buf, midi);
        for (int c = 0; c < 2; ++c) for (int i = 0; i < 512; ++i) { const float v = buf.getSample (c, i); if (! std::isfinite (v) || std::fabs (v) > 1.0f) finite = false; }
        lastRms = rmsOf (buf);
    }
    check ("output is finite and within +-1", finite);
    check ("a texture comes out", lastRms > 0.05, (std::to_string (lastRms)).c_str());
    int active = 0; for (int i = 0; i < SporeAudioProcessor::kGrains; ++i) active += proc.getGrain (i).active ? 1 : 0;
    check ("grain display data available", active > 0 && proc.takeInputPeak() > 0.3f && proc.takeOutputPeak() > 0.0f);

    // freeze through the parameter
    {
        setParam (proc, "freeze", 1.0f);
        check ("freeze parameter on", proc.isFrozen());
        juce::MidiBuffer midi;
        double r1 = 0, r2 = 0; int n1 = 0, n2 = 0;
        for (int b = 0; b < 600; ++b)
        {
            for (int c = 0; c < 2; ++c) buf.clear (c, 0, 512);          // input stops completely
            proc.processBlock (buf, midi);
            if (b >= 100 && b < 160) { r1 += rmsOf (buf) * rmsOf (buf); ++n1; }
            if (b >= 540) { r2 += rmsOf (buf) * rmsOf (buf); ++n2; }
        }
        r1 = std::sqrt (r1 / n1); r2 = std::sqrt (r2 / n2);
        char t[96]; std::snprintf (t, sizeof t, "after 2 s %.3f, after 12 s %.3f (input silent)", r1, r2);
        check ("frozen texture keeps sounding without input", r1 > 0.05 && r2 > 0.05 && std::fabs (20 * std::log10 (r2 / r1)) < 3.0, t);
        setParam (proc, "freeze", 0.0f);
    }

    // state
    {
        setParam (proc, "density", 0.9f); setParam (proc, "size", 0.2f); setParam (proc, "blur", 0.77f); setParam (proc, "decay", 0.6f); setParam (proc, "freeze", 1.0f);
        juce::MemoryBlock mb; proc.getStateInformation (mb);
        SporeAudioProcessor p2;
        p2.setStateInformation (mb.getData(), (int) mb.getSize());
        auto g = [&] (SporeAudioProcessor& p, const char* id) { return p.apvts.getParameter (id)->getValue(); };
        check ("state round trip", std::fabs (g (p2, "density") - 0.9f) < 1e-3 && std::fabs (g (p2, "size") - 0.2f) < 1e-3 && std::fabs (g (p2, "blur") - 0.77f) < 1e-3
                                   && std::fabs (g (p2, "decay") - 0.6f) < 1e-3 && p2.isFrozen());
        p2.setStateInformation ("garbage", 7);
        check ("garbage state is ignored", std::fabs (g (p2, "density") - 0.9f) < 1e-3);
    }

    // sample rate change
    {
        SporeAudioProcessor p3;
        bool ok = true;
        for (double r : { 44100.0, 96000.0, 48000.0, 22050.0 })
        {
            p3.setPlayConfigDetails (2, 2, r, 256); p3.prepareToPlay (r, 256);
            juce::AudioBuffer<float> b3 (2, 256); double q = 0; juce::MidiBuffer m;
            for (int k = 0; k < 100; ++k) { fillSine (b3, q, 500, 0.5f, r); p3.processBlock (b3, m); for (int i = 0; i < 256; ++i) if (! std::isfinite (b3.getSample (0, i))) ok = false; }
        }
        check ("sample rate changes", ok);
    }

    // real-time safety: no heap use inside processBlock, with parameters moving and freeze toggling
    {
        setParam (proc, "freeze", 0.0f); setParam (proc, "density", 1.0f); setParam (proc, "decay", 1.0f); setParam (proc, "blur", 1.0f);
        juce::MidiBuffer midi; std::mt19937 g (5);
        long calls = 0;
        for (int b = 0; b < 1500; ++b)
        {
            fillSine (buf, ph, 220.0, 0.5f, sr);
            if (b % 50 == 0) { setParam (proc, "size", (float) (g() % 100) / 100.0f); setParam (proc, "freeze", (float) (g() % 2)); }
            gCount = true; proc.processBlock (buf, midi); gCount = false; ++calls;
        }
        char t[96]; std::snprintf (t, sizeof t, "%ld processBlock calls, heap allocations: %ld", calls, gAllocs.load());
        check ("no heap allocation in processBlock", gAllocs.load() == 0, t);
    }

    std::printf ("%s (%d failures)\n", failures ? "SOME TESTS FAILED" : "ALL TESTS PASSED", failures);
    return failures ? 1 : 0;
}
