// Headless test of ChronosphereAudioProcessor: parameters, state, processing, mix, transport gating.
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

struct FakePlayHead : juce::AudioPlayHead
{
    bool playing = true;
    juce::int64 samples = 0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setIsPlaying (playing);
        p.setTimeInSamples (samples);
        return p;
    }
};

static double energy (const juce::AudioBuffer<float>& b)
{
    double e = 0.0;
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = 0; i < b.getNumSamples(); ++i) e += (double) b.getSample (c, i) * b.getSample (c, i);
    return e;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    ChronosphereAudioProcessor proc;

    const double sr = 48000.0;
    proc.setPlayConfigDetails (2, 2, sr, 512);
    proc.prepareToPlay (sr, 512);

    check ("name is Chronosphere", proc.getName() == "Chronosphere");
    check ("default stretch = 500", std::fabs (proc.apvts.getRawParameterValue ("stretch")->load() - 500.0f) < 0.5f);
    check ("default lpf = 1000 Hz", std::fabs (proc.apvts.getRawParameterValue ("lpf")->load() - 1000.0f) < 0.5f);
    check ("default key = None (0)", proc.apvts.getRawParameterValue ("key")->load() == 0.0f);

    // stretch range is limited to 3000 %
    auto* sp = proc.apvts.getParameter ("stretch");
    sp->setValueNotifyingHost (1.0f);
    check ("stretch max = 3000", std::fabs (proc.apvts.getRawParameterValue ("stretch")->load() - 3000.0f) < 0.5f);
    sp->setValueNotifyingHost (0.0f);
    check ("stretch min = 100", std::fabs (proc.apvts.getRawParameterValue ("stretch")->load() - 100.0f) < 0.5f);
    sp->setValueNotifyingHost (sp->convertTo0to1 (500.0f));

    // --- mix = 0 -> dry passthrough ---
    proc.apvts.getParameter ("mix")->setValueNotifyingHost (0.0f);
    proc.prepareToPlay (sr, 512);
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

    // --- mix = 100%: sound starts almost immediately ---
    proc.apvts.getParameter ("mix")->setValueNotifyingHost (1.0f);
    proc.prepareToPlay (sr, 512);
    double eFirst = 0.0, eLate = 0.0;
    bool finite = true; float peak = 0.0f;
    for (int b = 0; b < 400; ++b)
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
            }
        if (b >= 1 && b < 6) eFirst += energy (buf);          // 10 - 60 ms
        if (b > 300) eLate += energy (buf);
    }
    check ("wet: audible within the first 60 ms", eFirst > 1.0, "");
    check ("wet: audio later on", eLate > 1.0);
    check ("output finite and <= 1.0 with safe clip", finite && peak <= 1.0f);

    // --- transport: pause mutes the wet signal, play brings it back ---
    FakePlayHead ph;
    proc.setPlayHead (&ph);
    proc.prepareToPlay (sr, 512);
    ph.playing = true;
    for (int b = 0; b < 200; ++b)                                   // ~2 s of playing
    {
        for (int c = 0; c < 2; ++c) for (int i = 0; i < 512; ++i) buf.setSample (c, i, nd (rng));
        proc.processBlock (buf, midi);
        ph.samples += 512;
    }
    {
        // white noise through the default 1 kHz low-pass is ~14 dB quieter than the input (energy ~0.4), so use a low threshold
        char msg[64];
        std::snprintf (msg, sizeof msg, "energy of last block = %.3f (input would be ~10)", energy (buf));
        check ("playing: wet signal present", energy (buf) > 0.05, msg);
    }

    ph.playing = false;                                            // pause
    double ePaused = 0.0;
    for (int b = 0; b < 20; ++b)
    {
        for (int c = 0; c < 2; ++c) for (int i = 0; i < 512; ++i) buf.setSample (c, i, 0.0f);   // silence in, like a stopped project
        proc.processBlock (buf, midi);
        if (b >= 2) ePaused += energy (buf);                      // after the ~11 ms fade-out
    }
    check ("paused: output is silent after the fade-out", ePaused < 1e-9, "");

    ph.playing = true;                                             // play again
    ph.samples = 0;
    double eResumed = 0.0;
    for (int b = 0; b < 20; ++b)
    {
        for (int c = 0; c < 2; ++c) for (int i = 0; i < 512; ++i) buf.setSample (c, i, nd (rng));
        proc.processBlock (buf, midi);
        ph.samples += 512;
        if (b >= 2 && b < 8) eResumed += energy (buf);
    }
    check ("resumed: pad is back within ~100 ms", eResumed > 1.0);
    proc.setPlayHead (nullptr);

    // --- state round trip ---
    proc.apvts.getParameter ("key")->setValueNotifyingHost (proc.apvts.getParameter ("key")->convertTo0to1 (3.0f));
    juce::MemoryBlock mb;
    proc.getStateInformation (mb);
    ChronosphereAudioProcessor proc2;
    proc2.setStateInformation (mb.getData(), (int) mb.getSize());
    check ("state restore (key = 3)", proc2.apvts.getRawParameterValue ("key")->load() == 3.0f);

    // --- block larger than the internal chunk ---
    juce::AudioBuffer<float> big (2, 10000);
    for (int c = 0; c < 2; ++c) for (int i = 0; i < 10000; ++i) big.setSample (c, i, nd (rng));
    proc.processBlock (big, midi);
    check ("block larger than internal chunk (10000) works", std::isfinite (big.getSample (0, 9999)));

    std::printf (failures ? "FAILED\n" : "ALL PASSED\n");
    return failures ? 1 : 0;
}
