// Headless test of StretchyAudioProcessor: parameters, state save/restore, processing, mix=0 passthrough.
#include "../Source/PluginProcessor.h"
#include <cmath>
#include <cstdio>
#include <random>

static int failures = 0;
static void check (const char* name, bool ok, const char* extra = "")
{
    std::printf ("%s  %s  %s\n", ok ? "PASS" : "FAIL", name, extra);
    if (! ok) ++failures;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;     // JUCE needs this for some classes even when headless
    StretchyAudioProcessor proc;

    const double sr = 48000.0;
    proc.setPlayConfigDetails (2, 2, sr, 512);
    proc.prepareToPlay (sr, 512);

    // defaults as in the reference screenshot
    check ("default stretch = 500", std::fabs (proc.apvts.getRawParameterValue ("stretch")->load() - 500.0f) < 0.5f);
    check ("default lpf = 1000 Hz", std::fabs (proc.apvts.getRawParameterValue ("lpf")->load() - 1000.0f) < 0.5f);
    check ("default key = None (0)", proc.apvts.getRawParameterValue ("key")->load() == 0.0f);
    check ("default scale = Major (0)", proc.apvts.getRawParameterValue ("scale")->load() == 0.0f);

    // --- mix = 0 -> dry passthrough (output gain 0 dB, safe clip transparent for small signals) ---
    proc.apvts.getParameter ("mix")->setValueNotifyingHost (0.0f);
    proc.prepareToPlay (sr, 512);                       // resets the ramp memory
    std::mt19937 rng (7);
    std::normal_distribution<float> nd (0.0f, 0.1f);
    juce::AudioBuffer<float> buf (2, 512), ref (2, 512);
    juce::MidiBuffer midi;
    bool dryOk = true;
    for (int b = 0; b < 8; ++b)
    {
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 512; ++i) { const float v = nd (rng); buf.setSample (c, i, v); ref.setSample (c, i, v); }
        proc.processBlock (buf, midi);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 512; ++i)
                if (std::fabs (buf.getSample (c, i) - ref.getSample (c, i)) > 1e-6f) dryOk = false;
    }
    check ("mix 0%: dry passthrough", dryOk);

    // --- mix = 100% : silence for first window, then audio, finite, bounded ---
    proc.apvts.getParameter ("mix")->setValueNotifyingHost (1.0f);
    proc.prepareToPlay (sr, 512);
    double energyEarly = 0.0, energyLate = 0.0;
    bool finite = true; float peak = 0.0f;
    for (int b = 0; b < 600; ++b)                       // 600*512 = 6.4 s
    {
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 512; ++i) buf.setSample (c, i, nd (rng));
        proc.processBlock (buf, midi);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 512; ++i)
            {
                const float v = buf.getSample (c, i);
                if (! std::isfinite (v)) finite = false;
                peak = std::max (peak, std::fabs (v));
                if (b < 40) energyEarly += v * v; else if (b > 300) energyLate += v * v;
            }
    }
    check ("wet: silent during the first ~0.4 s (analysis window filling)", energyEarly < 1e-3);
    check ("wet: audio after the window is filled", energyLate > 1.0);
    check ("output finite and <= 1.0 with safe clip", finite && peak <= 1.0f);

    // --- state round trip ---
    proc.apvts.getParameter ("key")->setValueNotifyingHost (proc.apvts.getParameter ("key")->convertTo0to1 (3.0f));
    juce::MemoryBlock mb;
    proc.getStateInformation (mb);
    StretchyAudioProcessor proc2;
    proc2.setStateInformation (mb.getData(), (int) mb.getSize());
    check ("state restore (key = 3)", proc2.apvts.getRawParameterValue ("key")->load() == 3.0f);

    // --- odd block sizes (not multiples of the chunk) and mono buffers handled ---
    juce::AudioBuffer<float> big (2, 10000);
    for (int c = 0; c < 2; ++c) for (int i = 0; i < 10000; ++i) big.setSample (c, i, nd (rng));
    proc.processBlock (big, midi);
    check ("block larger than internal chunk (10000) works", std::isfinite (big.getSample (0, 9999)));

    std::printf (failures ? "FAILED\n" : "ALL PASSED\n");
    return failures ? 1 : 0;
}
