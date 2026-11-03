// Offline tests of the vibrato engine.  g++ -O2 -std=c++17 [-fsanitize=address,undefined] tests/test_engine.cpp
#include "../Source/VibratoEngine.h"
#include <complex>
#include <cstdio>
#include <random>

using namespace polter;
static int failures = 0;
static void check (const char* n, bool ok, const char* extra = "") { std::printf ("%s  %s  %s\n", ok ? "PASS" : "FAIL", n, extra); if (! ok) ++failures; }
static const double SR = 48000.0;

static std::vector<float> sine (double hz, double sec, float amp = 0.5f)
{
    std::vector<float> v ((size_t) (sec * SR));
    for (size_t i = 0; i < v.size(); ++i) v[i] = amp * (float) std::sin (2 * kPi * hz * (double) i / SR);
    return v;
}
static std::vector<float> run (const std::vector<float>& in, float rate, float depth, int block = 512)
{
    Vibrato v; v.prepare (SR);
    std::vector<float> a = in, b = in;
    for (size_t i = 0; i < in.size(); i += (size_t) block)
    {
        const int n = (int) std::min<size_t> ((size_t) block, in.size() - i);
        float* io[2] = { a.data() + i, b.data() + i };
        v.process (io, 2, n, rate, depth);
    }
    return a;
}

// instantaneous frequency of a (nearly) pure tone from the intervals between rising zero crossings; returns (time, cents vs f0)
static std::vector<std::pair<double, double>> pitchTrack (const std::vector<float>& x, double f0)
{
    std::vector<std::pair<double, double>> r;
    double last = -1.0;
    for (size_t i = 1; i < x.size(); ++i)
        if (x[i - 1] < 0.0f && x[i] >= 0.0f)
        {
            const double t = (double) (i - 1) + (double) (-x[i - 1]) / (double) (x[i] - x[i - 1]);
            if (last >= 0.0) { const double f = SR / (t - last); r.push_back ({ t / SR, 1200.0 * std::log2 (f / f0) }); }
            last = t;
        }
    return r;
}

int main()
{
    // 1. depth 0 is transparent (only the fixed 3-sample delay)
    {
        const auto in = sine (1000, 1.0);
        const auto out = run (in, 5.0f, 0.0f);
        double err = 0; for (size_t i = 10; i < in.size(); ++i) err = std::max (err, (double) std::fabs (out[i] - in[i - 3]));
        char b[64]; std::snprintf (b, sizeof b, "max error %.2e", err);
        check ("depth 0: output = input delayed by 3 samples", err < 1e-5, b);
    }

    // 2. the measured pitch swing equals the Depth setting and runs at the Rate setting
    for (float depth : { 25.0f, 100.0f, 200.0f })
        for (float rate : { 1.0f, 5.0f, 10.0f })
        {
            const auto out = run (sine (1000, 4.0), rate, depth);
            const auto tr = pitchTrack (out, 1000.0);
            double mx = -1e9, mn = 1e9;
            for (const auto& p : tr) if (p.first > 1.5) { mx = std::max (mx, p.second); mn = std::min (mn, p.second); }
            // modulation frequency: number of upward crossings of the mean in the pitch track
            int cyc = 0; bool above = false; double t0 = -1, t1 = -1;
            for (const auto& p : tr) if (p.first > 1.5) { const bool a = p.second > 0; if (a && ! above) { ++cyc; if (t0 < 0) t0 = p.first; t1 = p.first; } above = a; }
            const double mf = cyc > 1 ? (cyc - 1) / (t1 - t0) : 0.0;
            const double peak = 0.5 * (mx - mn);
            char b[128]; std::snprintf (b, sizeof b, "depth %.0f ct rate %.1f Hz -> measured +-%.1f ct, %.2f Hz", depth, rate, peak, mf);
            // zero-crossing tracking is coarse at 1 kHz (one value per cycle), so allow 15 % at fast rates
            check ("pitch swing and rate match the settings", std::fabs (peak - depth) < 0.15 * depth + 3.0 && std::fabs (mf - rate) < 0.08 * rate + 0.05, b);
        }

    // 3. level is not changed by the effect (a sine stays a sine of the same amplitude), also at high frequencies
    {
        double worst = 0;
        for (double hz : { 200.0, 2000.0, 8000.0 })
        {
            const auto out = run (sine (hz, 2.0), 6.0f, 150.0f);
            double pk = 0; for (size_t i = (size_t) SR; i < out.size(); ++i) pk = std::max (pk, (double) std::fabs (out[i]));
            worst = std::max (worst, std::fabs (20 * std::log10 (pk / 0.5)));
        }
        char b[64]; std::snprintf (b, sizeof b, "worst level change %.2f dB", worst);
        check ("level stays put (200 Hz - 8 kHz)", worst < 1.0, b);
    }

    // 4. no clicks when the knobs are turned abruptly
    {
        const auto in = sine (440, 6.0);
        Vibrato v; v.prepare (SR);
        std::vector<float> a = in, b = in; double worst = 0; float prev = 0;
        std::mt19937 g (1); std::uniform_real_distribution<float> u (0, 1);
        float rate = 4, depth = 50;
        for (size_t i = 0; i < in.size(); i += 256)
        {
            if (u (g) < 0.25f) { rate = kMinRate + u (g) * (kMaxRate - kMinRate); depth = u (g) * kMaxDepth; }
            float* io[2] = { a.data() + i, b.data() + i };
            v.process (io, 2, 256, rate, depth);
            for (size_t k = 0; k < 256; ++k) { if (i + k > 48000) worst = std::max (worst, (double) std::fabs (a[i + k] - prev)); prev = a[i + k]; }
        }
        char bb[96]; std::snprintf (bb, sizeof bb, "largest sample step %.3f (440 Hz sine, amp 0.5: %.3f)", worst, 2 * kPi * 440 / SR * 0.5);
        check ("abrupt knob changes do not click", worst < 0.12, bb);
    }

    // 5. stereo channels get the same treatment; garbage in -> bounded out; other sample rates; odd block sizes
    {
        const auto in = sine (700, 2.0);
        Vibrato v; v.prepare (SR); std::vector<float> a = in, b = in;
        for (size_t i = 0; i < in.size(); i += 333) { const int n = (int) std::min<size_t> (333, in.size() - i); float* io[2] = { a.data() + i, b.data() + i }; v.process (io, 2, n, 7.0f, 120.0f); }
        double d = 0; for (size_t i = 0; i < in.size(); ++i) d = std::max (d, (double) std::fabs (a[i] - b[i]));
        check ("both channels identical for identical input", d < 1e-7);
        for (double r : { 22050.0, 44100.0, 96000.0, 192000.0 })
        {
            Vibrato v2; v2.prepare (r); std::vector<float> x ((size_t) (2 * r)); std::mt19937 g (3); std::normal_distribution<float> nd (0, 0.3f); for (auto& s : x) s = nd (g);
            std::vector<float> y = x; float* io[2] = { x.data(), y.data() };
            v2.process (io, 2, (int) x.size(), 0.5f, 200.0f);
            bool ok = true; for (float s : x) ok = ok && std::isfinite (s) && std::fabs (s) < 3.0f;
            char bb[32]; std::snprintf (bb, sizeof bb, "%.0f Hz", r);
            check ("noise at slowest rate, deepest depth stays finite", ok, bb);
        }
    }

    std::printf ("%s (%d failures)\n", failures ? "SOME TESTS FAILED" : "ALL TESTS PASSED", failures);
    return failures ? 1 : 0;
}
