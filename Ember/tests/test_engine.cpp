// Plain C++ tests of the Ember DSP engine (no JUCE).  g++ -O2 -std=c++17 test_engine.cpp -o test_engine
#include "../Source/SatEngine.h"
#include <algorithm>
#include <chrono>
#include <complex>
#include <cstdio>
#include <random>
#include <vector>

using namespace ember;
static int fails = 0;
#define CHECK(c, ...) do { const bool ok_ = (c); std::printf ("%s  ", ok_ ? "PASS" : "FAIL"); std::printf (__VA_ARGS__); std::printf ("\n"); if (! ok_) ++fails; } while (0)

static const double PI = 3.14159265358979323846;

static GlobalParams identity (int bands)
{
    GlobalParams p; p.numBands = bands;
    for (auto& b : p.band) { b.driveDb = 0; b.mix = 0; b.levelDb = 0; b.on = true; b.solo = false; b.style = 12; }
    return p;
}

static std::vector<float> run (Engine& e, const GlobalParams& p, std::vector<float> l, std::vector<float>* r = nullptr, int blk = 512)
{
    std::vector<float> rr = r ? *r : l;
    for (size_t s = 0; s < l.size(); s += (size_t) blk)
    {
        const int n = (int) std::min ((size_t) blk, l.size() - s);
        float* io[2] = { l.data() + s, rr.data() + s };
        e.process (io, 2, n, p);
    }
    if (r) *r = rr;
    return l;
}

static std::vector<float> sine (double fs, double f, double amp, int n)
{
    std::vector<float> v ((size_t) n);
    for (int i = 0; i < n; ++i) v[(size_t) i] = (float) (amp * std::sin (2.0 * PI * f * i / fs));
    return v;
}
static double rms (const std::vector<float>& v, size_t a, size_t b) { double s = 0; for (size_t i = a; i < b; ++i) s += (double) v[i] * v[i]; return std::sqrt (s / (double) (b - a)); }
static double harm (const std::vector<float>& v, double fs, double f0, int k, size_t a, size_t b)
{
    std::complex<double> s = 0;
    for (size_t i = a; i < b; ++i) s += (double) v[i] * std::exp (std::complex<double> (0, -2.0 * PI * f0 * k * (double) i / fs));
    return 2.0 * std::abs (s) / (double) (b - a);
}

int main()
{
    const double fs = 96000.0;

    // 1. the bands sum to a flat magnitude for every slope (LR split + all-pass compensation), also with crossovers close together
    {
        double worst = 0;
        for (int slope = 0; slope < 5; ++slope)
            for (int nb : { 2, 4, 6 })
                for (int tight = 0; tight < 2; ++tight)
                {
                    Engine e; e.prepare (fs);
                    auto p = identity (nb); p.slope = slope;
                    p.xoHz = tight ? std::array<float, 5> { 1000.0f, 1130.0f, 1280.0f, 1450.0f, 1640.0f } : std::array<float, 5> { 90.0f, 400.0f, 1500.0f, 5000.0f, 11000.0f };
                    double w = 0;
                    for (double f : { 30.0, 60.0, 90.0, 140.0, 250.0, 400.0, 700.0, 1050.0, 1200.0, 1360.0, 1500.0, 2500.0, 4000.0, 5000.0, 7000.0, 11000.0, 14000.0, 20000.0 })
                    {
                        e.reset();
                        const int n = (int) (fs * 1.2);
                        auto in = sine (fs, f, 0.5, n);
                        auto out = run (e, p, in);
                        const double g = 20.0 * std::log10 (rms (out, (size_t) (n / 2), (size_t) n) / rms (in, (size_t) (n / 2), (size_t) n));
                        w = std::max (w, std::fabs (g));
                    }
                    if (w > 0.05) std::printf ("      slope %d, %d bands%s: worst deviation %.4f dB\n", slope, nb, tight ? " (tight)" : "", w);
                    worst = std::max (worst, w);
                }
        CHECK (worst < 0.05, "band sum is flat for all 5 slopes, 2/4/6 bands, wide and tight crossovers (worst %.4f dB)", worst);
    }

    // 2. a single band: the textbook attenuation at the crossover and one octave above it, for every slope
    {
        const double atFc[5] = { -3.01, -6.02, -6.02, -6.02, -6.02 }, atOct[5] = { -6.99, -13.98, -24.61, -36.3, -48.2 };
        for (int slope = 0; slope < 5; ++slope)
        {
            Engine e; e.prepare (fs);
            auto p = identity (2); p.slope = slope; p.xoHz[0] = 1000.0f; p.band[1].on = false;      // only the low band is audible
            auto meas = [&] (double f) { e.reset(); const int n = (int) (fs * 0.8); auto in = sine (fs, f, 0.5, n); auto out = run (e, p, in); return 20.0 * std::log10 (rms (out, (size_t) (n / 2), (size_t) n) / rms (in, (size_t) (n / 2), (size_t) n)); };
            const double a = meas (1000.0), b = meas (2000.0), pass = meas (60.0);
            CHECK (std::fabs (a - atFc[slope]) < 0.3 && std::fabs (b - atOct[slope]) < 0.6 && std::fabs (pass) < 0.05,
                   "slope %d: low band at fc %.2f dB (%.2f), one octave up %.2f dB (%.2f), passband %.3f dB", slope, a, atFc[slope], b, atOct[slope], pass);
        }
    }

    // 3. every style: bounded, finite, DC-free, harmonic content
    std::printf ("      style table (1 kHz, 0.5 amp, drive +12 dB): H2/H3/H5 relative to H1 in dB, THD %%\n");
    for (int st = 0; st < kNumStyles; ++st)
    {
        Engine e; e.prepare (fs);
        auto p = identity (1); p.band[0].style = st; p.band[0].driveDb = 12.0f; p.band[0].mix = 1.0f;
        const int n = (int) (fs * 1.5);
        auto in = sine (fs, 1000.0, 0.5, n);
        auto out = run (e, p, in);
        const size_t a = (size_t) (n / 2), b = (size_t) n;
        double mx = 0, dc = 0; bool finite = true;
        for (size_t i = a; i < b; ++i) { mx = std::max (mx, (double) std::fabs (out[i])); dc += out[i]; finite = finite && std::isfinite (out[i]); }
        dc /= (double) (b - a);
        const double h1 = harm (out, fs, 1000.0, 1, a, b), h2 = harm (out, fs, 1000.0, 2, a, b), h3 = harm (out, fs, 1000.0, 3, a, b), h5 = harm (out, fs, 1000.0, 5, a, b);
        double thd = 0; for (int k = 2; k <= 9; ++k) { const double h = harm (out, fs, 1000.0, k, a, b); thd += h * h; }
        thd = std::sqrt (thd) / std::max (1e-9, h1) * 100.0;
        std::printf ("      %-18s H2 %6.1f  H3 %6.1f  H5 %6.1f  THD %6.1f %%  peak %.2f  dc %.4f\n", styleName (st),
                     20 * std::log10 (h2 / h1 + 1e-12), 20 * std::log10 (h3 / h1 + 1e-12), 20 * std::log10 (h5 / h1 + 1e-12), thd, mx, dc);
        CHECK (finite && mx < 2.0 && std::fabs (dc) < 0.01, "%s: finite, peak %.2f < 2.0, DC %.4f", styleName (st), mx, dc);
    }
    {   // character: tubes lean on the 2nd harmonic, symmetric styles do not make one
        auto h2h3 = [&] (int st) { Engine e; e.prepare (fs); auto p = identity (1); p.band[0].style = st; p.band[0].driveDb = 12.0f; p.band[0].mix = 1.0f;
            const int n = (int) (fs * 1.5); auto out = run (e, p, sine (fs, 1000.0, 0.5, n)); return harm (out, fs, 1000.0, 2, (size_t) (n / 2), (size_t) n) / harm (out, fs, 1000.0, 3, (size_t) (n / 2), (size_t) n); };
        CHECK (h2h3 (2) > 0.5, "Warm Tube makes a strong 2nd harmonic (H2/H3 = %.2f)", h2h3 (2));
        CHECK (h2h3 (5) < 0.01, "Clean Tape (symmetric) makes almost no 2nd harmonic (H2/H3 = %.4f)", h2h3 (5));
        CHECK (h2h3 (14) > 0.2, "Transformer Warm (asymmetric core) makes a 2nd harmonic (H2/H3 = %.2f)", h2h3 (14));
        CHECK (h2h3 (3) > 0.25, "Hot Tube makes a strong 2nd harmonic (H2/H3 = %.2f)", h2h3 (3));
    }

    // 3b. style behaviour that is more than a static curve
    {
        auto thd = [&] (int st, double f, double amp, double drive) { Engine e; e.prepare (fs); auto p = identity (1); p.band[0].style = st; p.band[0].driveDb = (float) drive; p.band[0].mix = 1.0f;
            const int n = (int) (fs * 1.5); auto out = run (e, p, sine (fs, f, amp, n)); const size_t a0 = (size_t) (n / 2), b0 = (size_t) n;
            const double h1 = harm (out, fs, f, 1, a0, b0); double s = 0; for (int k = 2; k <= 7; ++k) { const double h = harm (out, fs, f, k, a0, b0); s += h * h; } return std::sqrt (s) / h1 * 100.0; };
        for (int st : { 13, 14, 15 })
        {
            const double lo = thd (st, 60.0, 0.4, 6.0), hi = thd (st, 4000.0, 0.4, 6.0);
            CHECK (lo > 2.0 * hi, "%s: bass saturates first (THD %.1f %% at 60 Hz vs %.1f %% at 4 kHz)", styleName (st), lo, hi);
        }
        // crusher: very few output levels at full crush
        {
            Engine e; e.prepare (fs); auto p = identity (1); p.band[0].style = 25; p.band[0].driveDb = 60.0f; p.band[0].mix = 1.0f;
            auto out = run (e, p, sine (fs, 300.0, 0.9, (int) fs));
            std::vector<int> q; for (size_t i = 20000; i < out.size(); ++i) q.push_back ((int) std::lround (out[i] * 1000.0f));
            std::sort (q.begin(), q.end()); const int distinct = (int) (std::unique (q.begin(), q.end()) - q.begin());
            CHECK (distinct <= 9 && distinct >= 3, "Bit Crusher at full crush: %d distinct output levels (3 bits)", distinct);
        }
        // breakdown: octave-down component at half the input frequency
        {
            Engine e; e.prepare (fs); auto p = identity (1); p.band[0].style = 26; p.band[0].driveDb = 18.0f; p.band[0].mix = 1.0f;
            const int n = (int) (fs * 1.5); auto out = run (e, p, sine (fs, 300.0, 0.5, n));
            const double sub = harm (out, fs, 150.0, 1, (size_t) (n / 2), (size_t) n), fund = harm (out, fs, 300.0, 1, (size_t) (n / 2), (size_t) n);
            CHECK (sub > 0.15 * fund && sub > 0.05, "Breakdown: sub-octave (150 Hz) %.3f vs input octave %.3f", sub, fund);
        }
        // smear: flat magnitude for small signals (all-pass chain), only phase moves
        {
            double worst = 0;
            for (double f : { 100.0, 400.0, 1500.0, 5000.0, 12000.0 })
            {
                Engine e; e.prepare (fs); auto p = identity (1); p.band[0].style = 27; p.band[0].driveDb = 0.0f; p.band[0].mix = 1.0f;
                const int n = (int) (fs * 0.8); auto in = sine (fs, f, 0.02, n); auto out = run (e, p, in);
                worst = std::max (worst, std::fabs (20.0 * std::log10 (rms (out, (size_t) (n / 2), (size_t) n) / rms (in, (size_t) (n / 2), (size_t) n))));
            }
            CHECK (worst < 0.2, "Smear keeps the magnitude of small signals (worst %.3f dB)", worst);
        }
        // pan: balance
        {
            Engine e; e.prepare (fs); auto p = identity (1); p.band[0].mix = 0.0f;
            const int n = (int) (fs * 0.6); auto l = sine (fs, 500.0, 0.4, n), r = l;
            auto lvl = [&] (float pan, int ch) { e.reset(); p.band[0].pan = pan; auto a = l, b = r; auto o = run (e, p, a, &b); return rms (ch ? b : o, (size_t) (n / 2), (size_t) n) / rms (l, (size_t) (n / 2), (size_t) n); };
            CHECK (std::fabs (lvl (0.0f, 0) - 1.0) < 0.01 && lvl (1.0f, 0) < 0.01 && std::fabs (lvl (1.0f, 1) - 1.0) < 0.01 && lvl (-1.0f, 1) < 0.01 && std::fabs (lvl (0.5f, 0) - 0.5) < 0.01, "pan: centre untouched, +100%% silences the left, -100%% the right, +50%% halves the left");
        }
    }

    // 4. extreme input and settings never produce NaN/inf or runaway
    {
        std::mt19937 rng (7); std::uniform_real_distribution<float> U (-1.0f, 1.0f);
        bool ok = true; double peak = 0;
        for (int st = 0; st < kNumStyles; ++st)
        {
            Engine e; e.prepare (fs);
            auto p = identity (6);
            for (auto& b : p.band) { b.style = st; b.driveDb = 60.0f; b.mix = 1.0f; b.dyn = -1.0f; b.tone = 1.0f; b.fb = 1.0f; b.fbHz = 300.0f; b.levelDb = 24.0f; }
            std::vector<float> l ((size_t) fs), r ((size_t) fs);
            for (size_t i = 0; i < l.size(); ++i) { l[i] = 50.0f * U (rng); r[i] = 50.0f * U (rng); }
            auto out = run (e, p, l, &r);
            for (float v : out) { ok = ok && std::isfinite (v); peak = std::max (peak, (double) std::fabs (v)); }
            for (float v : r) ok = ok && std::isfinite (v);
        }
        CHECK (ok && peak < 400.0, "worst case (+-50 input, 60 dB drive, 100%% feedback, +24 dB level, all styles): finite, peak %.1f", peak);
    }

    // 5. feedback does not drone on silence
    {
        Engine e; e.prepare (fs);
        auto p = identity (1); p.band[0].style = 0; p.band[0].driveDb = 30.0f; p.band[0].mix = 1.0f; p.band[0].fb = 1.0f; p.band[0].fbHz = 440.0f;
        const int n = (int) (fs * 4.0);
        auto in = sine (fs, 440.0, 0.4, n); for (size_t i = (size_t) (fs * 1.0); i < in.size(); ++i) in[i] = 0.0f;
        auto out = run (e, p, in);
        const double during = rms (out, (size_t) (fs * 0.4), (size_t) (fs * 0.9)), tail = rms (out, (size_t) (fs * 3.0), (size_t) (fs * 4.0));
        CHECK (during > 0.05 && tail < 1e-4, "feedback sings with the note (%.3f) and is silent after it (%.2e)", during, tail);
        // and it really is a resonance: more feedback -> more output near the filter frequency
        auto lvl = [&] (float fb) { Engine e2; e2.prepare (fs); auto q = p; q.band[0].fb = fb; q.band[0].driveDb = 6.0f; auto o = run (e2, q, sine (fs, 440.0, 0.1, (int) fs)); return rms (o, (size_t) (fs * 0.5), (size_t) fs); };
        CHECK (lvl (0.9f) > 1.15 * lvl (0.0f), "feedback adds energy at its frequency (%.3f vs %.3f)", lvl (0.9f), lvl (0.0f));
    }

    // 6. dynamics: negative compresses (levels get closer), positive expands
    {
        auto ratio = [&] (float dyn)
        {
            Engine e; e.prepare (fs);
            auto p = identity (1); p.band[0].style = 12; p.band[0].dyn = dyn; p.band[0].mix = 1.0f; p.band[0].driveDb = 0.0f;
            const int seg = (int) (fs * 1.0);
            std::vector<float> in;
            for (int k = 0; k < 2; ++k) { auto s = sine (fs, 500.0, k == 0 ? 0.03 : 0.5, seg); in.insert (in.end(), s.begin(), s.end()); }
            auto out = run (e, p, in);
            return rms (out, (size_t) (seg * 1.5), (size_t) (seg * 2)) / rms (out, (size_t) (seg * 0.5), (size_t) seg);
        };
        const double rc = ratio (-1.0f), rn = ratio (0.0f), re = ratio (1.0f);
        std::printf ("      loud/quiet level ratio: compress %.1f, neutral %.1f, expand %.1f\n", rc, rn, re);
        CHECK (rc < 0.6 * rn && re > 1.4 * rn, "dynamics compress < neutral < expand");
    }

    // 7. tone is a tilt: +100%% brightens, -100%% darkens
    {
        auto g = [&] (float tone, double f) { Engine e; e.prepare (fs); auto p = identity (1); p.band[0].style = 12; p.band[0].mix = 1.0f; p.band[0].tone = tone;
            const int n = (int) (fs * 0.8); auto in = sine (fs, f, 0.1, n); auto o = run (e, p, in); return 20 * std::log10 (rms (o, (size_t) (n / 2), (size_t) n) / rms (in, (size_t) (n / 2), (size_t) n)); };
        const double hi = g (1.0f, 8000.0), lo = g (1.0f, 100.0), z = g (0.0f, 8000.0);
        CHECK (hi > 6.0 && lo < -6.0 && std::fabs (z) < 0.05, "tone +100%%: 8 kHz %+.1f dB, 100 Hz %+.1f dB; tone 0: %+.3f dB", hi, lo, z);
    }

    // 8. mid/side: identity passes both a mono and a side-only signal
    {
        Engine e; e.prepare (fs);
        auto p = identity (3); p.midSide = true;
        for (int mode = 0; mode < 2; ++mode)
        {
            e.reset();
            const int n = (int) (fs * 0.6);
            auto l = sine (fs, 700.0, 0.4, n); auto r = l; if (mode) for (auto& v : r) v = -v;
            auto l0 = l, r0 = r;
            auto o = run (e, p, l, &r);
            double err = 0; for (size_t i = (size_t) (n / 2); i < (size_t) n; ++i) err = std::max (err, (double) std::fabs (r[i] - o[i] * (mode ? -1.0f : 1.0f)));
            const double lvl = rms (o, (size_t) (n / 2), (size_t) n) / rms (l0, (size_t) (n / 2), (size_t) n);
            CHECK (std::fabs (lvl - 1.0) < 0.01 && err < 1e-3, "M/S %s: level %.4f, L/R match error %.5f", mode ? "side-only" : "mono", lvl, err);
        }
    }

    // 9. CPU: 6 bands, stereo, at 8x of 48 kHz
    {
        const double fsOs = 48000.0 * 8.0;
        Engine e; e.prepare (fsOs);
        GlobalParams p; p.numBands = 6;
        for (int b = 0; b < 6; ++b) { p.band[(size_t) b].style = (b * 3) % kNumStyles; p.band[(size_t) b].driveDb = 18.0f; p.band[(size_t) b].dyn = -0.5f; p.band[(size_t) b].fb = 0.3f; }
        const int n = (int) fsOs * 4;
        std::vector<float> l = sine (fsOs, 300.0, 0.3, n), r = l;
        const auto t0 = std::chrono::steady_clock::now();
        for (size_t s = 0; s < l.size(); s += 4096) { const int m = (int) std::min<size_t> (4096, l.size() - s); float* io[2] = { l.data() + s, r.data() + s }; e.process (io, 2, m, p); }
        const double sec = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        std::printf ("      6 bands stereo at 8x (384 kHz): %.2f s CPU for 4 s of audio -> %.1f %% of one core\n", sec, 100.0 * sec / 4.0);
    }

    std::printf (fails ? "\n%d TEST(S) FAILED\n" : "\nALL TESTS PASSED\n", fails);
    return fails ? 1 : 0;
}
