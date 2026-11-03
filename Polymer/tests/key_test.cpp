// Do the variations stay in the key of the sample?  g++ -O2 -std=c++17 tests/key_test.cpp
// For tonal test sounds the strongest spectral peak of every variation must be (an octave / harmonic of) one of the
// strongest peaks of the original. Shifted pitches (the old varispeed / spectral stretch) would show up as failures.
#ifndef HDR
#define HDR "../Source/Transmuter.h"
#endif
#include HDR
#include <complex>
#include <cstdio>

using namespace poly;
static const double SR = 48000.0;

static void fft (std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) { size_t bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap (a[i], a[j]); }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2 * 3.14159265358979 / (double) len; const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len) { std::complex<double> w (1); for (size_t j = 0; j < len / 2; ++j) { auto u = a[i + j], v = a[i + j + len / 2] * w; a[i + j] = u + v; a[i + j + len / 2] = u - v; w *= wl; } }
    }
}

// the `count` strongest spectral peaks between 50 Hz and 2 kHz (frequency, magnitude)
static std::vector<std::pair<double, double>> peaks (const Audio& a, int count)
{
    const size_t n = 131072; std::vector<std::complex<double>> x (n);
    for (size_t i = 0; i < n && i < (size_t) a.size(); ++i) x[i] = (double) (a.ch[0][i] + a.ch[1][i]) * 0.5 * (0.5 - 0.5 * std::cos (2 * 3.14159265358979 * (double) i / (double) std::min<size_t> (n, (size_t) a.size())));
    fft (x);
    std::vector<std::pair<double, double>> pk;
    for (size_t k = 2; k < n / 2 - 2; ++k)
    {
        const double f = (double) k * SR / (double) n; if (f < 50 || f > 2000) continue;
        const double m = std::abs (x[k]);
        if (m > std::abs (x[k - 1]) && m >= std::abs (x[k + 1]) && m > std::abs (x[k - 2]) && m >= std::abs (x[k + 2])) pk.push_back ({ f, m });
    }
    std::sort (pk.begin(), pk.end(), [] (const auto& p, const auto& q) { return p.second > q.second; });
    if ((int) pk.size() > count) pk.resize ((size_t) count);
    return pk;
}

static Audio pluck (std::initializer_list<double> notes, double sec)
{
    Audio a; a.sr = SR; a.resize ((int) (sec * SR));
    for (int i = 0; i < a.size(); ++i)
    {
        const double t = i / SR; double v = 0;
        for (double f : notes) for (int h = 1; h <= 8; ++h) v += std::sin (2 * 3.14159265358979 * f * h * t + f) / (h * h) * std::exp (-t * (1.2 + 0.4 * h));
        a.ch[0][(size_t) i] = a.ch[1][(size_t) i] = (float) (v * 0.4);
    }
    return a;
}

int main()
{
    struct Src { const char* name; Audio a; };
    std::vector<Src> srcs;
    srcs.push_back ({ "pluck A3 220 Hz", pluck ({ 220.0 }, 2.0) });
    srcs.push_back ({ "bass E1 41 Hz + harmonics", pluck ({ 41.2 }, 2.0) });
    srcs.push_back ({ "chord A3 + E4 + A4", pluck ({ 220.0, 329.63, 440.0 }, 2.5) });
    int fails = 0, total = 0;
    for (auto& s : srcs)
    {
        const float ref = activeRms (s.a);
        const auto base = peaks (s.a, 6);
        int ok = 0, n = 0;
        for (float c : { 0.3f, 0.6f, 1.0f })
            for (uint32_t seed = 1; seed <= 30; ++seed)
            {
                const Audio v = render (s.a, { seed * 7919u + 13u, c }, ref);
                const auto pk = peaks (v, 1);
                ++n;
                if (pk.empty()) continue;
                bool inKey = false;
                for (const auto& b : base)
                    for (double m : { 0.25, 0.5, 1.0, 2.0, 3.0, 4.0 })
                        if (std::fabs (std::log2 (pk[0].first / (b.first * m))) < 0.03) inKey = true;     // within ~36 cents
                ok += inKey ? 1 : 0;
            }
        std::printf ("%-28s in key: %d / %d (%.0f %%)\n", s.name, ok, n, 100.0 * ok / n);
        fails += n - ok; total += n;
    }
    const double frac = 1.0 - (double) fails / total;
    std::printf ("%s  strongest peak stays in key: %.1f %% of %d variations\n", frac >= 0.93 ? "PASS" : "FAIL", frac * 100, total);
    return frac >= 0.93 ? 0 : 1;
}
