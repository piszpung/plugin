// Headless test of EmberAudioProcessor: parameters, state, latency alignment, aliasing, band switches, real-time safety.
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
static void setParam (EmberAudioProcessor& p, const juce::String& id, float plain)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (plain));
}
static float getParam (EmberAudioProcessor& p, const juce::String& id) { return p.apvts.getRawParameterValue (id)->load(); }

static void resetAll (EmberAudioProcessor& p)
{
    for (auto* prm : p.getParameters()) prm->setValueNotifyingHost (prm->getDefaultValue());
}

// runs `n` samples (blocks of `blk`) of the supplied mono-ish signal through the processor, returns the left channel
static std::vector<float> run (EmberAudioProcessor& p, const std::vector<float>& l, const std::vector<float>* r = nullptr, int blk = 512, std::vector<float>* outR = nullptr)
{
    std::vector<float> out (l.size());
    if (outR) outR->assign (l.size(), 0.0f);
    juce::MidiBuffer midi;
    for (size_t s = 0; s < l.size(); s += (size_t) blk)
    {
        const int n = (int) std::min ((size_t) blk, l.size() - s);
        juce::AudioBuffer<float> b (2, n);
        for (int i = 0; i < n; ++i) { b.setSample (0, i, l[s + (size_t) i]); b.setSample (1, i, r ? (*r)[s + (size_t) i] : l[s + (size_t) i]); }
        p.processBlock (b, midi);
        for (int i = 0; i < n; ++i) { out[s + (size_t) i] = b.getSample (0, i); if (outR) (*outR)[s + (size_t) i] = b.getSample (1, i); }
    }
    return out;
}
static std::vector<float> sine (double fs, double f, double a, int n) { std::vector<float> v ((size_t) n); for (int i = 0; i < n; ++i) v[(size_t) i] = (float) (a * std::sin (6.283185307179586 * f * i / fs)); return v; }
static double rms (const std::vector<float>& v, size_t a, size_t b) { double s = 0; for (size_t i = a; i < b; ++i) s += (double) v[i] * v[i]; return std::sqrt (s / (double) (b - a)); }

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    const double sr = 48000.0;
    EmberAudioProcessor proc;
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
        int n = 0; for (auto* p : proc.getParameters()) { (void) p; ++n; }
        auto txt = [&] (const char* id, float plain) { auto* p = proc.apvts.getParameter (id); return p->getText (p->convertTo0to1 (plain), 0); };
        std::printf ("     %d parameters; drive 0..60: %s .. %s; xo1 20 Hz..20 kHz: %s .. %s\n", n, txt ("b1_drive", 0).toRawUTF8(), txt ("b1_drive", 60).toRawUTF8(), txt ("xo1", 20).toRawUTF8(), txt ("xo1", 20000).toRawUTF8());
        check ("117 parameters, ranges and defaults", n == 8 + 5 + 6 * 11 + 6 + 6 + 2 + 24 && getParam (proc, "b3_drive") == 12.0f && getParam (proc, "bands") == 2.0f && getParam (proc, "os") == 1.0f && getParam (proc, "mix") == 100.0f);
    }

    // one band, mix 0, no oversampling = the signal passes untouched
    {
        resetAll (proc); setParam (proc, "bands", 0); setParam (proc, "os", 0); setParam (proc, "b1_mix", 0.0f);
        proc.prepareToPlay (sr, 512);
        auto in = sine (sr, 440.0, 0.5, 4800);
        auto out = run (proc, in);
        double err = 0; for (size_t i = 0; i < in.size(); ++i) err = std::max (err, (double) std::fabs (in[i] - out[i]));
        check ("1 band, mix 0, no oversampling: bit-transparent", err < 1e-6, (std::string ("max error ") + std::to_string (err)).c_str());
    }

    // oversampling: latency is reported and the dry path is aligned with it (no comb filtering in the global mix)
    for (int os = 1; os <= 5; ++os)
    {
        resetAll (proc); setParam (proc, "bands", 0); setParam (proc, "os", (float) os); setParam (proc, "b1_mix", 0.0f);
        proc.prepareToPlay (sr, 512);
        const int lat = proc.getOversampleLatency();
        const int n = 48000;
        auto in = sine (sr, 1000.0, 0.5, n);
        auto wet = run (proc, in);
        // compare with the input delayed by the reported latency (steady state)
        double err = 0; for (int i = 4000; i < n; ++i) err = std::max (err, (double) std::fabs (wet[(size_t) i] - in[(size_t) (i - lat)]));
        setParam (proc, "mix", 50.0f);
        proc.prepareToPlay (sr, 512);
        auto half = run (proc, in);
        const double ratio = rms (half, 8000, n) / rms (wet, 8000, n);
        char buf[160]; std::snprintf (buf, sizeof buf, "latency %d samples, error vs delayed input %.2e, mix 50%% keeps level (x%.4f)", lat, err, ratio);
        check ((std::string ("oversampling ") + (os == 1 ? "2x" : os == 2 ? "4x" : os == 3 ? "8x" : os == 4 ? "16x" : "32x") + ": latency reported and dry aligned").c_str(), err < 5e-3 && std::fabs (ratio - 1.0) < 0.01, buf);
    }

    // aliasing: a hard-clipped 8.789 kHz sine folds back into the audio band; oversampling removes most of it
    {
        const int N = 1 << 15; const double f0 = 3000.0 * sr / 16384.0 / 2.0;   // an exact FFT bin of the 2^15 analysis
        juce::dsp::FFT fft (15);
        auto alias = [&] (int os)
        {
            resetAll (proc); setParam (proc, "bands", 0); setParam (proc, "os", (float) os);
            setParam (proc, "b1_style", 11); setParam (proc, "b1_drive", 24.0f);
            proc.prepareToPlay (sr, 512);
            auto in = sine (sr, f0, 0.5, N + 8000);
            auto out = run (proc, in);
            std::vector<float> w (2 * (size_t) N, 0.0f);
            for (int i = 0; i < N; ++i) w[(size_t) i] = out[(size_t) (8000 + i)] * (0.5f - 0.5f * std::cos (6.283185307f * (float) i / (float) (N - 1)));
            fft.performFrequencyOnlyForwardTransform (w.data());
            const double binHz = sr / N; double legit = 0, bad = 0, fund = 0;
            for (int b = 3; b < N / 2; ++b)
            {
                const double f = b * binHz, m = f / f0, nearInt = std::fabs (m - std::round (m));
                const double e = (double) w[(size_t) b] * w[(size_t) b];
                if (std::fabs (f - f0) < 40.0) fund += e;
                else if (nearInt < 0.03) legit += e; else bad += e;
            }
            return std::make_pair (10.0 * std::log10 (bad / fund), 10.0 * std::log10 (legit / fund));
        };
        const auto a1 = alias (0), a4 = alias (2);
        char buf[200]; std::snprintf (buf, sizeof buf, "non-harmonic (aliased) energy vs fundamental: no OS %.1f dB, 4x %.1f dB", a1.first, a4.first);
        check ("oversampling cuts aliasing by more than 15 dB", a4.first < a1.first - 15.0, buf);
    }

    // band switches
    {
        resetAll (proc); setParam (proc, "bands", 1); setParam (proc, "os", 0); setParam (proc, "xo1", 1000.0f);
        setParam (proc, "b1_mix", 0.0f); setParam (proc, "b2_mix", 0.0f);
        proc.prepareToPlay (sr, 512);
        const int n = 24000;
        auto lvl = [&] (double f) { auto in = sine (sr, f, 0.3, n); auto out = run (proc, in); return 20 * std::log10 (rms (out, 12000, n) / rms (in, 12000, n)); };
        setParam (proc, "b2_on", 0.0f);
        const double hiOff = lvl (4000.0), loOff = lvl (150.0);
        setParam (proc, "b2_on", 1.0f); setParam (proc, "b2_solo", 1.0f);
        const double loSolo = lvl (150.0), hiSolo = lvl (4000.0);
        char buf[200]; std::snprintf (buf, sizeof buf, "band 2 off: 4k %.1f dB / 150 Hz %.1f dB; band 2 solo: 150 Hz %.1f dB / 4k %.1f dB", hiOff, loOff, loSolo, hiSolo);
        check ("on/solo switches silence the right bands", hiOff < -25 && loOff > -0.5 && loSolo < -25 && hiSolo > -0.5, buf);
    }

    // crossover slope and band pan reach the engine
    {
        auto lowBandAt2k = [&] (int slope)
        {
            resetAll (proc); setParam (proc, "bands", 1); setParam (proc, "os", 0); setParam (proc, "slope", (float) slope); setParam (proc, "xo1", 1000.0f);
            setParam (proc, "b1_mix", 0.0f); setParam (proc, "b2_mix", 0.0f); setParam (proc, "b2_on", 0.0f);
            proc.prepareToPlay (sr, 512);
            const int n = 24000; auto in = sine (sr, 2000.0, 0.3, n); auto out = run (proc, in);
            return 20 * std::log10 (rms (out, 12000, n) / rms (in, 12000, n));
        };
        const double s0 = lowBandAt2k (0), s2 = lowBandAt2k (2), s4 = lowBandAt2k (4);
        char buf[160]; std::snprintf (buf, sizeof buf, "low band at 2 kHz (fc 1 kHz): 6 dB/oct %.1f dB, 24 dB/oct %.1f dB, 48 dB/oct %.1f dB", s0, s2, s4);
        check ("slope parameter selects the crossover steepness", std::fabs (s0 + 7.0) < 0.6 && std::fabs (s2 + 24.6) < 0.8 && std::fabs (s4 + 48.2) < 1.5, buf);

        resetAll (proc); setParam (proc, "bands", 0); setParam (proc, "os", 0); setParam (proc, "b1_mix", 0.0f); setParam (proc, "b1_pan", 100.0f);
        proc.prepareToPlay (sr, 512);
        auto in = sine (sr, 500.0, 0.4, 9600); std::vector<float> r;
        auto l = run (proc, in, nullptr, 512, &r);
        check ("band pan +100% silences the left channel only", rms (l, 4800, 9600) < 1e-4 && rms (r, 4800, 9600) > 0.25);
    }

    // modulation
    {
        auto windowsRatio = [&] (const std::vector<float>& o)
        {
            double lo = 1e9, hi = 0; const size_t w = 960;
            for (size_t s = 9600; s + w <= o.size(); s += w) { const double r = rms (o, s, s + w); lo = std::min (lo, r); hi = std::max (hi, r); }
            return hi / std::max (lo, 1e-9);
        };
        auto base = [&] (float lfoRate)
        {
            resetAll (proc); setParam (proc, "bands", 0); setParam (proc, "os", 0); setParam (proc, "b1_style", 12); setParam (proc, "b1_drive", 0.0f); setParam (proc, "b1_mix", 100.0f);
            setParam (proc, "lfo1_rate", lfoRate); setParam (proc, "lfo1_glide", 100.0f);
            proc.setLfoShape (0, ember::kShapeSine);
            proc.prepareToPlay (sr, 512);
        };
        const int n = 96000; auto in = sine (sr, 440.0, 0.2, n);
        base (5.0f);
        const double flat = windowsRatio (run (proc, in));
        base (5.0f); setParam (proc, "m1_src", 1); setParam (proc, "m1_tgt", 1); setParam (proc, "m1_amt", 100.0f);     // LFO 1 -> band 1 drive
        proc.prepareToPlay (sr, 512);
        const double moved = windowsRatio (run (proc, in));
        char buf[120]; std::snprintf (buf, sizeof buf, "level ratio over time: no route %.2f, LFO -> drive %.2f", flat, moved);
        check ("LFO 1 -> band 1 drive moves the sound", flat < 1.05 && moved > 1.6, buf);
        check ("the GUI can read the modulated position (target 1 shown, others not)", proc.getModulatedNormalised (1) >= 0.0f && proc.getModulatedNormalised (2) < 0.0f);

        // envelope follower -> output level
        auto outLevel = [&] (bool routed)
        {
            resetAll (proc); setParam (proc, "bands", 0); setParam (proc, "os", 0); setParam (proc, "b1_mix", 0.0f);
            if (routed) { setParam (proc, "m1_src", 3); setParam (proc, "m1_tgt", (float) ember::kTargetOut); setParam (proc, "m1_amt", 60.0f); }
            proc.prepareToPlay (sr, 512);
            auto o = run (proc, sine (sr, 300.0, 0.05, 48000)); return rms (o, 24000, 48000);
        };
        const double un = outLevel (false), mo = outLevel (true);
        std::snprintf (buf, sizeof buf, "output level without route %.4f, with envelope -> output %.4f (x%.1f)", un, mo, mo / un);
        check ("envelope follower -> output gain follows the input level", mo > 3.0 * un, buf);

        // slider -> global mix: halfway between dry and wet
        auto lvlMix = [&] (float slider, bool routed)
        {
            resetAll (proc); setParam (proc, "bands", 0); setParam (proc, "os", 0); setParam (proc, "b1_style", 12); setParam (proc, "b1_drive", 30.0f);
            setParam (proc, "sl1", slider);
            if (routed) { setParam (proc, "m1_src", 5); setParam (proc, "m1_tgt", (float) ember::kTargetMix); setParam (proc, "m1_amt", -100.0f); }
            proc.prepareToPlay (sr, 512);
            auto o = run (proc, sine (sr, 300.0, 0.2, 24000)); return rms (o, 12000, 24000);
        };
        const double wet = lvlMix (0.0f, true), half = lvlMix (50.0f, true), dry = lvlMix (100.0f, true);
        std::snprintf (buf, sizeof buf, "slider 0%% (mix 100%%): %.3f, 50%%: %.3f, 100%% (mix 0%%): %.3f", wet, half, dry);
        check ("slider -> global mix moves between wet and dry", wet > 0.6 && dry < 0.16 && half < wet - 0.1 && half > dry + 0.1, buf);
    }

    // mono, odd block sizes, blocks larger than prepared, NaN/inf input
    {
        resetAll (proc); setParam (proc, "bands", 3); setParam (proc, "os", 2); setParam (proc, "b1_fb", 80.0f);
        proc.prepareToPlay (sr, 256);
        std::mt19937 rng (3); std::uniform_real_distribution<float> U (-1.0f, 1.0f);
        std::vector<float> in (20000); for (auto& v : in) v = 0.7f * U (rng);
        in[5000] = std::nanf (""); in[5001] = INFINITY; in[5002] = -INFINITY;
        bool ok = true;
        for (int blk : { 1, 7, 100, 256, 1000, 3000 })
        {
            proc.prepareToPlay (sr, 256);
            auto out = run (proc, in, nullptr, blk);
            for (float v : out) ok = ok && std::isfinite (v);
        }
        check ("odd and oversized blocks, NaN/inf input: output stays finite", ok);
        // mono buffer
        juce::AudioBuffer<float> mono (1, 512); juce::MidiBuffer m;
        for (int i = 0; i < 512; ++i) mono.setSample (0, i, 0.4f * std::sin (0.05f * (float) i));
        proc.setPlayConfigDetails (1, 1, sr, 512); proc.prepareToPlay (sr, 512);
        proc.processBlock (mono, m);
        bool fin = true; for (int i = 0; i < 512; ++i) fin = fin && std::isfinite (mono.getSample (0, i));
        check ("mono buffer processes", fin);
        proc.setPlayConfigDetails (2, 2, sr, 512);
    }

    // state round-trip
    {
        resetAll (proc); setParam (proc, "b4_style", 12); setParam (proc, "b2_drive", 33.3f); setParam (proc, "xo3", 2500.0f); setParam (proc, "ms", 1); setParam (proc, "os", 3);
        juce::MemoryBlock mb; proc.getStateInformation (mb);
        EmberAudioProcessor other; other.setStateInformation (mb.getData(), (int) mb.getSize());
        check ("state save/restore", getParam (other, "b4_style") == 12.0f && std::fabs (getParam (other, "b2_drive") - 33.3f) < 0.06f && std::fabs (getParam (other, "xo3") - 2500.0f) < 1.0f && getParam (other, "ms") == 1.0f && getParam (other, "os") == 3.0f);
        proc.setLfoStep (0, 3, 0.37f); proc.setLfoStep (1, 15, -0.81f);
        juce::MemoryBlock mb2; proc.getStateInformation (mb2);
        EmberAudioProcessor third; third.setStateInformation (mb2.getData(), (int) mb2.getSize());
        check ("LFO steps are saved and restored", std::fabs (third.getLfoStep (0, 3) - 0.37f) < 1e-3f && std::fabs (third.getLfoStep (1, 15) + 0.81f) < 1e-3f);
        juce::MemoryBlock junk; junk.append ("garbage", 7); other.setStateInformation (junk.getData(), (int) junk.getSize());
        check ("garbage state is ignored", getParam (other, "os") == 3.0f);
    }

    // real-time safety: no heap allocation in processBlock (fixed settings, six bands, every style, feedback, M/S, limiter)
    {
        resetAll (proc); setParam (proc, "bands", 5); setParam (proc, "os", 2); setParam (proc, "ms", 1); setParam (proc, "limit", 1);
        for (int b = 1; b <= 6; ++b) { setParam (proc, "b" + juce::String (b) + "_style", (float) ((b * 5) % 28)); setParam (proc, "b" + juce::String (b) + "_fb", 40.0f); setParam (proc, "b" + juce::String (b) + "_dyn", -50.0f); setParam (proc, "b" + juce::String (b) + "_tone", 30.0f); }
        setParam (proc, "m1_src", 1); setParam (proc, "m1_tgt", 1); setParam (proc, "m2_src", 3); setParam (proc, "m2_tgt", (float) ember::crossoverTarget (1)); setParam (proc, "m3_src", 6); setParam (proc, "m3_tgt", 20); setParam (proc, "sl2", 40.0f);
        proc.prepareToPlay (sr, 512);
        juce::AudioBuffer<float> buf (2, 512); juce::MidiBuffer midi;
        std::mt19937 rng (5); std::uniform_real_distribution<float> U (-1.0f, 1.0f);
        for (int i = 0; i < 20; ++i) { for (int c = 0; c < 2; ++c) for (int k = 0; k < 512; ++k) buf.setSample (c, k, 0.3f * U (rng)); proc.processBlock (buf, midi); }
        long total = 0;
        for (int i = 0; i < 1500; ++i)
        {
            for (int c = 0; c < 2; ++c) for (int k = 0; k < 512; ++k) buf.setSample (c, k, 0.3f * U (rng));
            if (i % 50 == 0) { setParam (proc, "b1_drive", 6.0f + (float) (i % 7) * 5.0f); setParam (proc, "xo2", 600.0f + (float) i); }
            gAllocs = 0; gCount = true;
            proc.processBlock (buf, midi);
            gCount = false; total += gAllocs;
        }
        char b2[80]; std::snprintf (b2, sizeof b2, "1500 processBlock calls, heap allocations: %ld", total);
        check ("no heap allocation in processBlock", total == 0, b2);
    }

    std::printf (failures ? "\n%d TEST(S) FAILED\n" : "\nALL TESTS PASSED (0 failures)\n", failures);
    return failures ? 1 : 0;
}
