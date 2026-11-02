// Offline tests of the granular engine.  g++ -O2 -std=c++17 [-fsanitize=address,undefined] tests/test_engine.cpp
#include "../Source/GrainEngine.h"
#include <chrono>
#include <complex>
#include <cstdio>
#include <random>

using namespace spore;
static int failures = 0;
static void check (const char* n, bool ok, const char* extra = "") { std::printf ("%s  %s  %s\n", ok ? "PASS" : "FAIL", n, extra); if (! ok) ++failures; }

static const double SR = 48000.0;

struct Sig { std::vector<float> l, r; };

static Sig noise (double sec, float amp, uint32_t seed = 7)
{
    std::mt19937 g (seed); std::normal_distribution<float> nd (0.0f, 1.0f);
    Sig s; const int n = (int) (sec * SR); s.l.resize ((size_t) n); s.r.resize ((size_t) n);
    float lp1 = 0, lp2 = 0;
    for (int i = 0; i < n; ++i) { lp1 += 0.3f * (nd (g) - lp1); lp2 += 0.3f * (nd (g) - lp2); s.l[(size_t) i] = amp * lp1 * 1.6f; s.r[(size_t) i] = amp * lp2 * 1.6f; }
    return s;
}
static Sig sine (double sec, double hz, float amp, bool stereoSame = true)
{
    Sig s; const int n = (int) (sec * SR); s.l.resize ((size_t) n); s.r.resize ((size_t) n);
    for (int i = 0; i < n; ++i) { s.l[(size_t) i] = amp * (float) std::sin (2 * kPi * hz * i / SR); s.r[(size_t) i] = stereoSame ? s.l[(size_t) i] : amp * (float) std::sin (2 * kPi * hz * 1.5 * i / SR); }
    return s;
}
static Sig silence (double sec) { Sig s; s.l.assign ((size_t) (sec * SR), 0.0f); s.r = s.l; return s; }

static Sig run (Engine& e, const Sig& in, const Params& p, int block = 512)
{
    Sig o; o.l.resize (in.l.size()); o.r.resize (in.l.size());
    for (size_t i = 0; i < in.l.size(); i += (size_t) block)
    {
        const int n = (int) std::min<size_t> ((size_t) block, in.l.size() - i);
        e.process (in.l.data() + i, in.r.data() + i, o.l.data() + i, o.r.data() + i, n, p);
    }
    return o;
}
static double rms (const std::vector<float>& v, size_t a, size_t b)
{
    double s = 0; for (size_t i = a; i < b; ++i) s += (double) v[i] * v[i]; return std::sqrt (s / std::max<size_t> (1, b - a));
}
static bool finiteBounded (const Sig& s) { for (size_t i = 0; i < s.l.size(); ++i) if (! std::isfinite (s.l[i]) || ! std::isfinite (s.r[i]) || std::fabs (s.l[i]) > 1.0001f || std::fabs (s.r[i]) > 1.0001f) return false; return true; }

static void fft (std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) { size_t bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap (a[i], a[j]); }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2 * kPi / (double) len; const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len) { std::complex<double> w (1); for (size_t j = 0; j < len / 2; ++j) { auto u = a[i + j], v = a[i + j + len / 2] * w; a[i + j] = u + v; a[i + j + len / 2] = u - v; w *= wl; } }
    }
}
// fraction of spectral energy between lo and hi Hz (window: 1 s starting at `from`)
static double bandFraction (const std::vector<float>& x, size_t from, double lo, double hi)
{
    const size_t n = 65536; std::vector<std::complex<double>> a (n);
    for (size_t i = 0; i < n; ++i) a[i] = (double) x[from + i] * (0.5 - 0.5 * std::cos (2 * kPi * (double) i / (double) n));
    fft (a);
    double tot = 0, in = 0;
    for (size_t k = 1; k < n / 2; ++k) { const double f = (double) k * SR / (double) n, e = std::norm (a[k]); tot += e; if (f >= lo && f <= hi) in += e; }
    return in / std::max (tot, 1e-30);
}

int main()
{
    Engine eng; eng.prepare (SR);

    // 1. level: wet loudness relative to the input over the whole parameter space (filter off)
    {
        const Sig in = noise (6.0, 0.2f);
        const double ref = rms (in.l, (size_t) SR, in.l.size());
        double worstLo = 99, worstHi = -99;
        for (float den : { 0.0f, 0.5f, 1.0f }) for (float sz : { 0.0f, 0.5f, 1.0f }) for (float bl : { 0.0f, 1.0f })
        {
            eng.reset(); Params p; p.density = den; p.size = sz; p.blur = bl; p.decay = 0.0f;
            const Sig o = run (eng, in, p);
            const double db = 20 * std::log10 (rms (o.l, (size_t) SR, o.l.size()) / ref);
            worstLo = std::min (worstLo, db); worstHi = std::max (worstHi, db);
        }
        char b[96]; std::snprintf (b, sizeof b, "output vs input: %.1f .. %.1f dB", worstLo, worstHi);
        check ("loudness stays close to the input (decay 0)", worstLo > -4.0 && worstHi < 3.0, b);

        double lo = 99, hi = -99;
        for (float d : { 0.25f, 0.6f, 1.0f })
        {
            eng.reset(); Params p; p.decay = d; p.blur = 0.5f; p.density = 0.6f;
            const Sig o = run (eng, in, p);
            const double db = 20 * std::log10 (rms (o.l, (size_t) SR, o.l.size()) / ref);
            lo = std::min (lo, db); hi = std::max (hi, db);
        }
        std::snprintf (b, sizeof b, "with decay: %.1f .. %.1f dB (darker = quieter)", lo, hi);
        check ("decay does not blow up the level", hi < 4.0, b);
    }

    // 2. safety with loud input and wild parameters
    {
        std::mt19937 g (3); std::uniform_real_distribution<float> u (0, 1);
        const Sig in = noise (3.0, 1.0f);
        bool ok = true;
        for (int t = 0; t < 40; ++t)
        {
            eng.reset(); Params p; p.density = u (g); p.size = u (g); p.blur = u (g); p.decay = u (g); p.freeze = false;
            Sig o; o.l.resize (in.l.size()); o.r.resize (in.l.size());
            size_t i = 0;
            while (i < in.l.size()) { const int n = (int) std::min<size_t> (1 + (size_t) (u (g) * 700), in.l.size() - i); if (u (g) < 0.1f) p.freeze = ! p.freeze; eng.process (in.l.data() + i, in.r.data() + i, o.l.data() + i, o.r.data() + i, n, p); i += (size_t) n; }
            ok = ok && finiteBounded (o);
        }
        check ("40 random runs (loud input, freeze toggled, odd block sizes): finite and within +-1", ok);
    }

    // 3. density = number of overlapping grains
    {
        const Sig in = noise (6.0, 0.2f);
        for (float den : { 0.0f, 0.5f, 1.0f })
        {
            eng.reset(); Params p; p.density = den; p.size = 0.6f;
            double acc = 0; int cnt = 0;
            for (size_t i = 0; i + 256 <= in.l.size(); i += 256)
            {
                std::vector<float> a (256), b (256);
                eng.process (in.l.data() + i, in.r.data() + i, a.data(), b.data(), 256, p);
                if (i > (size_t) SR * 2) { acc += eng.activeGrains(); ++cnt; }
            }
            const double mean = acc / cnt, want = grainCount (den);
            char b[96]; std::snprintf (b, sizeof b, "density %.1f -> mean %.1f grains (want %.1f)", den, mean, want);
            check ("grain count follows density", std::fabs (mean - want) < 0.2 * want + 0.7, b);
        }
    }

    // 4. pitch: blur 0 keeps the pitch, blur 1 spreads within +-1200 cents
    {
        const Sig in = sine (5.0, 2000.0, 0.5f);
        eng.reset(); Params p; p.blur = 0.0f; p.decay = 0.0f; p.density = 0.7f; p.size = 0.5f;
        Sig o = run (eng, in, p);
        const double f0 = bandFraction (o.l, (size_t) (2.5 * SR), 1940.0, 2060.0);
        char b[96]; std::snprintf (b, sizeof b, "%.1f %% of energy within +-3 %% of 2 kHz", f0 * 100);
        check ("blur 0: pitch unchanged", f0 > 0.9, b);

        eng.reset(); p.blur = 1.0f; p.size = 0.5f;
        o = run (eng, in, p);
        const double inRange = bandFraction (o.l, (size_t) (2.5 * SR), 950.0, 4200.0);
        const double spread = 1.0 - bandFraction (o.l, (size_t) (2.5 * SR), 1900.0, 2100.0);
        std::snprintf (b, sizeof b, "%.1f %% inside 1-4 kHz (+-1200 ct), %.1f %% away from 2 kHz", inRange * 100, spread * 100);
        check ("blur 1: pitches spread but stay within +-1200 cents", inRange > 0.93 && spread > 0.7, b);
    }

    // 5. pan
    {
        const Sig in = sine (4.0, 300.0, 0.4f);
        eng.reset(); Params p; p.blur = 0.0f; p.decay = 0.0f;
        Sig o = run (eng, in, p);
        double dmax = 0; for (size_t i = (size_t) SR; i < o.l.size(); ++i) dmax = std::max (dmax, (double) std::fabs (o.l[i] - o.r[i]));
        check ("blur 0: mono in -> identical L and R", dmax < 1e-5);

        eng.reset(); p.blur = 1.0f; p.density = 0.1f; p.size = 0.2f;
        o = run (eng, in, p);
        double mn = 99, mx = -99;
        const size_t w = (size_t) (0.05 * SR);
        for (size_t i = (size_t) SR; i + w < o.l.size(); i += w) { const double d = 20 * std::log10 ((rms (o.l, i, i + w) + 1e-9) / (rms (o.r, i, i + w) + 1e-9)); mn = std::min (mn, d); mx = std::max (mx, d); }
        char b[96]; std::snprintf (b, sizeof b, "L/R balance of 50 ms windows: %.1f .. %.1f dB", mn, mx);
        check ("blur 1: grains land at random places in the stereo field", mn < -4 && mx > 4, b);
    }

    // 6. freeze: the texture keeps living on the frozen audio, new input is ignored
    {
        Params p; p.blur = 0.0f; p.decay = 0.0f; p.density = 0.6f; p.size = 0.5f;
        eng.reset();
        run (eng, sine (3.0, 440.0, 0.4f), p);
        p.freeze = true;
        const Sig o = run (eng, sine (14.0, 1500.0, 0.4f), p);           // different tone arrives while frozen
        const double early = rms (o.l, (size_t) (1.0 * SR), (size_t) (2.0 * SR)), late = rms (o.l, (size_t) (12.0 * SR), (size_t) (13.0 * SR));
        char b[96]; std::snprintf (b, sizeof b, "level 1-2 s: %.3f, 12-13 s: %.3f", early, late);
        check ("freeze: texture sustains indefinitely", early > 0.15 && std::fabs (20 * std::log10 (late / early)) < 1.5, b);
        const double at440 = bandFraction (o.l, (size_t) (6.0 * SR), 420.0, 460.0), at1500 = bandFraction (o.l, (size_t) (6.0 * SR), 1450.0, 1550.0);
        std::snprintf (b, sizeof b, "440 Hz: %.1f %%, 1.5 kHz: %.2f %%", at440 * 100, at1500 * 100);
        check ("freeze: plays the frozen sound, ignores new input", at440 > 0.85 && at1500 < 0.01, b);

        p.freeze = false;
        const Sig o2 = run (eng, sine (4.0, 1500.0, 0.4f), p);
        check ("unfreeze: follows the input again", bandFraction (o2.l, (size_t) (2.5 * SR), 1450.0, 1550.0) > 0.8);
    }

    // 7. no clicks: very short, dense grains on a pure tone
    {
        const Sig in = sine (4.0, 220.0, 0.5f);
        double worst = 0;
        for (float sz : { 0.0f, 0.3f }) for (float bl : { 0.0f, 1.0f })
        {
            eng.reset(); Params p; p.density = 1.0f; p.size = sz; p.blur = bl; p.decay = 0.0f;
            const Sig o = run (eng, in, p);
            for (size_t i = (size_t) SR; i + 1 < o.l.size(); ++i) worst = std::max (worst, (double) std::fabs (o.l[i + 1] - o.l[i]));
        }
        char b[96]; std::snprintf (b, sizeof b, "largest sample-to-sample step %.3f (tone itself: %.3f)", worst, 2 * kPi * 220 / SR * 0.5);
        check ("no clicks (windowed grains)", worst < 0.15, b);
    }

    // 8. decay: darker and resonant, but alive
    {
        const Sig in = noise (6.0, 0.2f);
        Params p; p.density = 0.6f; p.size = 0.6f; p.blur = 0.3f;
        eng.reset(); p.decay = 0.0f; const Sig a = run (eng, in, p);
        eng.reset(); p.decay = 1.0f; const Sig b2 = run (eng, in, p);
        const double hiA = bandFraction (a.l, (size_t) (2.0 * SR), 3000.0, 20000.0), hiB = bandFraction (b2.l, (size_t) (2.0 * SR), 3000.0, 20000.0);
        char b[96]; std::snprintf (b, sizeof b, "energy above 3 kHz: decay 0 = %.3f, decay 1 = %.3f", hiA, hiB);
        check ("decay 1 sweeps the grains' filters (less highs)", hiB < hiA * 0.7, b);
        check ("decay 1 stays finite and alive", finiteBounded (b2) && rms (b2.l, (size_t) (2.0 * SR), b2.l.size()) > 0.01);
    }

    // 9. cost
    {
        const Sig in = noise (10.0, 0.2f);
        Params p; p.density = 1.0f; p.size = 0.9f; p.blur = 1.0f; p.decay = 1.0f;
        eng.reset();
        const auto t0 = std::chrono::steady_clock::now();
        run (eng, in, p, 256);
        const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
        char b[96]; std::snprintf (b, sizeof b, "worst case (64 grains, filters on): %.1f ms per 10 s of audio = %.1f %% of one core", ms, ms / 100.0);
        #if defined(__SANITIZE_ADDRESS__)
        check ("cost is reasonable (not measured under ASAN)", true, b);
#else
        check ("cost is reasonable", ms < 1500, b);
#endif
    }

    // 10. other sample rates
    for (double sr2 : { 44100.0, 96000.0 })
    {
        Engine e2; e2.prepare (sr2);
        std::vector<float> l ((size_t) (3 * sr2), 0.2f), r = l, ol (l.size()), orr (l.size());
        for (size_t i = 0; i < l.size(); ++i) l[i] = r[i] = 0.3f * (float) std::sin (2 * kPi * 500 * (double) i / sr2);
        Params p; p.blur = 1.0f; p.decay = 0.7f; p.density = 1.0f; p.size = 1.0f;
        e2.process (l.data(), r.data(), ol.data(), orr.data(), (int) l.size(), p);
        bool ok = true; for (float v : ol) ok = ok && std::isfinite (v) && std::fabs (v) <= 1.0001f;
        char b[64]; std::snprintf (b, sizeof b, "%.0f Hz", sr2);
        check ("other sample rates run fine", ok, b);
    }

    std::printf ("%s (%d failures)\n", failures ? "SOME TESTS FAILED" : "ALL TESTS PASSED", failures);
    return failures ? 1 : 0;
}
