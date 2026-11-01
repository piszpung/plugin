// Dsp.h - small DSP building blocks shared by the Polymer engine (no JUCE dependency).
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace poly
{

constexpr double kPi = 3.14159265358979323846;

// ---------------------------------------------------------------------------------
// Minimal radix-2 complex FFT (split real/imag arrays)
// ---------------------------------------------------------------------------------
class FFT
{
public:
    void init (int n)
    {
        N = n;
        int lg = 0;
        while ((1 << lg) < n) ++lg;
        cosT.assign ((size_t) n / 2, 0.0f);
        sinT.assign ((size_t) n / 2, 0.0f);
        for (int k = 0; k < n / 2; ++k)
        {
            const double a = -2.0 * kPi * k / n;
            cosT[(size_t) k] = (float) std::cos (a);
            sinT[(size_t) k] = (float) std::sin (a);
        }
        rev.assign ((size_t) n, 0);
        for (int i = 1; i < n; ++i)
            rev[(size_t) i] = (rev[(size_t) (i >> 1)] >> 1) | ((i & 1) << (lg - 1));
    }

    // forward transform, in place
    void forward (float* re, float* im) const
    {
        for (int i = 0; i < N; ++i)
        {
            const int j = rev[(size_t) i];
            if (j > i) { std::swap (re[i], re[j]); std::swap (im[i], im[j]); }
        }
        for (int size = 2; size <= N; size <<= 1)
        {
            const int half = size >> 1, step = N / size;
            for (int start = 0; start < N; start += size)
                for (int k = 0; k < half; ++k)
                {
                    const float wr = cosT[(size_t) (k * step)], wi = sinT[(size_t) (k * step)];
                    const int a = start + k, b = a + half;
                    const float tr = re[b] * wr - im[b] * wi;
                    const float ti = re[b] * wi + im[b] * wr;
                    re[b] = re[a] - tr; im[b] = im[a] - ti;
                    re[a] += tr;        im[a] += ti;
                }
        }
    }

    // inverse transform (scaled by 1/N), in place; uses the swap trick
    void inverse (float* re, float* im) const
    {
        forward (im, re);
        const float s = 1.0f / (float) N;
        for (int i = 0; i < N; ++i) { re[i] *= s; im[i] *= s; }
    }

    int size() const { return N; }

private:
    int N = 0;
    std::vector<float> cosT, sinT;
    std::vector<int> rev;
};

// Transparent below `knee`, smoothly saturates towards +-1.0 above it.
inline float softClip (float x, float knee = 0.7f)
{
    const float ax = std::fabs (x);
    if (ax <= knee) return x;
    const float y = knee + (1.0f - knee) * std::tanh ((ax - knee) / (1.0f - knee));
    return x < 0.0f ? -y : y;
}

// ---------------------------------------------------------------------------------
// Deterministic PRNG (xorshift64*): identical results on every platform / compiler,
// so a variation can be re-created from (parent, seed, chaos) alone.
// ---------------------------------------------------------------------------------
struct Rng
{
    uint64_t s;
    explicit Rng (uint64_t seed = 1) : s (seed * 0x9E3779B97F4A7C15ull + 0x1234567ull) { if (s == 0) s = 1; next(); next(); }
    uint64_t next() { s ^= s >> 12; s ^= s << 25; s ^= s >> 27; return s * 0x2545F4914F6CDD1Dull; }
    float uni()                      { return (float) ((next() >> 40) * (1.0 / 16777216.0)); }   // [0,1)
    float range (float a, float b)   { return a + (b - a) * uni(); }
    int   below (int n)              { return n <= 1 ? 0 : std::min (n - 1, (int) (uni() * (float) n)); }
    bool  chance (float p)           { return uni() < p; }
    float sign()                     { return uni() < 0.5f ? -1.0f : 1.0f; }
};

} // namespace poly
