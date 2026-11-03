// Spore - granular texture engine (header-only, no JUCE, no allocation after prepare()).
//
//  * the input is written into a circular buffer (4 s of history is used);
//  * a scheduler starts grains so that about `density` (2..64) of them overlap at any time;
//  * every grain reads the buffer at a random place, with a Hann-like window (no clicks); its start is nudged so that it
//    continues the waveform that is already playing (phase alignment -> a steady pad, not a noisy shimmer);
//    blur adds musical pitch shifts (octaves, fourths, fifths, up to +-1200 cents), slight detune and random pan;
//  * freeze stops writing to the buffer: grains keep reading the frozen audio forever;
//  * decay shapes each grain's life: the window gets a fast attack and a long tail, and the grain passes through
//    its own low-pass (or high-pass) filter whose cutoff sweeps and whose resonance swells while the grain lives.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace spore
{
constexpr double kPi = 3.14159265358979323846;

struct Rng
{
    uint32_t s = 0x1234567u;
    explicit Rng (uint32_t seed = 1) : s (seed ? seed : 1u) {}
    uint32_t bits() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float next() { return (float) (bits() >> 8) * (1.0f / 16777216.0f); }       // [0,1)
    float range (float a, float b) { return a + (b - a) * next(); }
};

struct Params
{
    float density = 0.6f;   // 0..1 -> 2..64 simultaneous grains
    float size    = 0.8f;   // 0..1 -> 10..500 ms
    float blur    = 0.3f;   // 0..1 -> random pitch +-1200 ct and random pan, also widens the read position
    float decay   = 0.2f;   // 0..1 -> grain lifespan shape + filter sweep
    bool  freeze  = false;
};

inline float grainCount (float d)  { return 2.0f * std::pow (32.0f, std::clamp (d, 0.0f, 1.0f)); }
inline float grainMs (float s)     { return 10.0f * std::pow (50.0f, std::clamp (s, 0.0f, 1.0f)); }
inline float blurCents (float b)   { return 1200.0f * std::clamp (b, 0.0f, 1.0f); }

struct VizGrain { float pan = 0.5f; float depth = 0.0f; float env = 0.0f; bool active = false; };   // pan 0..1, depth 0..1 (how far back in time)

class Engine
{
public:
    static constexpr int kMaxGrains = 128;
    static constexpr int kTable = 1024;

    void prepare (double sampleRate, uint32_t seed = 0xC0FFEEu)
    {
        sr = sampleRate;
        int len = 1;
        while ((double) len < 4.0 * sr + 4096.0) len <<= 1;      // power of two, so wrapping is a mask
        bufL.assign ((size_t) len, 0.0f);
        bufR.assign ((size_t) len, 0.0f);
        mask = len - 1;
        maxDepth = (int64_t) (4.0 * sr);
        for (int i = 0; i <= kTable; ++i) hann[i] = 0.5f - 0.5f * (float) std::cos (2.0 * kPi * (double) i / (double) kTable);
        for (int k = 0; k <= 16; ++k)
        {
            const float pk = peakFor ((float) k / 16.0f);
            double acc = 0.0; const int n = 512;
            for (int i = 0; i < n; ++i) { const float w = window ((float) (i + 0.5) / (float) n, pk); acc += (double) w * w; }
            msq[k] = (float) (acc / n);
        }
        seedValue = seed;
        aPow = 1.0f - (float) std::exp (-1.0 / (0.08 * sr));
        aGain = 1.0f - (float) std::exp (-1.0 / (0.05 * sr));
        reset();
    }

    void reset()
    {
        std::fill (bufL.begin(), bufL.end(), 0.0f);
        std::fill (bufR.begin(), bufR.end(), 0.0f);
        head = 0; countdown = 0.0; rng = Rng (seedValue);
        std::fill (hist.begin(), hist.end(), 0.0f); histPos = 0; refS = outS = 0.0f; agc = 1.0f;
        for (auto& g : grains) g.active = false;
    }

    // in and out may be the same arrays (every input sample is read before the output sample is written)
    void process (const float* inL, const float* inR, float* outL, float* outR, int n, const Params& p)
    {
        const bool frozen = p.freeze;
        for (int i = 0; i < n; ++i)
        {
            const float xl = inL[i], xr = inR[i];
            if (! frozen)
            {
                bufL[(size_t) (head & mask)] = std::isfinite (xl) ? xl : 0.0f;
                bufR[(size_t) (head & mask)] = std::isfinite (xr) ? xr : 0.0f;
                ++head;
            }

            countdown -= 1.0;
            if (countdown <= 0.0)
            {
                const double L = std::max (64.0, (double) grainMs (p.size) * 0.001 * sr);
                const float N = grainCount (p.density);
                spawn (p, (int) L, N);
                countdown += L / (double) N * (double) rng.range (0.5f, 1.5f);
            }

            float sl = 0.0f, sr_ = 0.0f, refAcc = 0.0f;
            for (auto& g : grains)
            {
                if (! g.active) continue;
                const float u = (float) g.age / (float) g.len;
                const float w = window (u, g.peak) * g.gain;

                const double pos = g.pos;
                const int64_t i0 = (int64_t) std::floor (pos);
                const float f = (float) (pos - (double) i0);
                float yl = cubic (bufL, i0, f), yr = cubic (bufR, i0, f);

                if (g.filt)
                {
                    if ((g.age & 15) == 0) updateFilter (g, u);
                    yl = svf (g, yl, g.s1L, g.s2L);
                    yr = svf (g, yr, g.s1R, g.s2R);
                }
                const float ol = yl * w * g.gL, orr = yr * w * g.gR;
                sl += ol; sr_ += orr;
                refAcc += ol * ol + orr * orr;          // what the level would be if the grains did not add up in phase

                g.env = w;
                g.pos += g.rate;
                if (++g.age >= g.len) g.active = false;
            }
            // Phase-aligned grains add up coherently (louder than the random-phase estimate used for the per-grain gain),
            // so a slow level follower keeps the texture at the loudness of the material it was made from.
            refS += aPow * (refAcc - refS);
            outS += aPow * ((sl * sl + sr_ * sr_) - outS);
            if (refS > 1e-9f && outS > 1e-9f) agc += aGain * (std::clamp (std::sqrt (refS / outS), 0.06f, 1.5f) - agc);
            sl *= agc; sr_ *= agc;
            hist[(size_t) (histPos++ & kHistMask)] = 0.5f * (sl + sr_);
            outL[i] = softClip (sl);
            outR[i] = softClip (sr_);
        }
    }

    int activeGrains() const { int c = 0; for (const auto& g : grains) c += g.active ? 1 : 0; return c; }

    void snapshot (VizGrain* out) const   // kMaxGrains entries, call from the audio thread after process()
    {
        for (int i = 0; i < kMaxGrains; ++i)
        {
            const auto& g = grains[(size_t) i];
            out[i] = { g.pan01, g.depth01, g.env, g.active };
        }
    }

private:
    struct Grain
    {
        bool active = false, filt = false, hp = false;
        double pos = 0.0, rate = 1.0;
        int age = 0, len = 1;
        float gL = 1.0f, gR = 1.0f, gain = 1.0f, peak = 0.5f, env = 0.0f, pan01 = 0.5f, depth01 = 0.0f;
        float lf0 = 0.0f, dlf = 0.0f, qMax = 0.707f;
        float a1 = 0.0f, a2 = 0.0f, a3 = 0.0f, k = 1.4f;
        float s1L = 0.0f, s2L = 0.0f, s1R = 0.0f, s2R = 0.0f;
    };

    static float peakFor (float decay) { return 0.5f - 0.38f * decay; }      // where in the grain the window peaks

    float window (float u, float pk) const
    {
        u = std::clamp (u, 0.0f, 1.0f);
        const float v = u < pk ? 0.5f * u / pk : 0.5f + 0.5f * (u - pk) / (1.0f - pk);
        const float x = v * (float) kTable;
        const int i = std::min (kTable - 1, (int) x);
        return hann[i] + (hann[i + 1] - hann[i]) * (x - (float) i);
    }

    float cubic (const std::vector<float>& b, int64_t i, float f) const
    {
        const float y0 = b[(size_t) ((i - 1) & mask)], y1 = b[(size_t) (i & mask)], y2 = b[(size_t) ((i + 1) & mask)], y3 = b[(size_t) ((i + 2) & mask)];
        const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + y1;
    }

    void updateFilter (Grain& g, float u) const
    {
        const float fc = std::min (std::exp (g.lf0 + g.dlf * u), (float) (0.45 * sr));
        const float s = std::sin ((float) kPi * u);
        const float q = 0.707f + (g.qMax - 0.707f) * s * s;               // resonance swells mid-life
        const float gg = std::tan ((float) kPi * fc / (float) sr);
        g.k = 1.0f / q;
        g.a1 = 1.0f / (1.0f + gg * (gg + g.k));
        g.a2 = gg * g.a1;
        g.a3 = gg * g.a2;
    }

    static float svf (const Grain& g, float x, float& ic1, float& ic2)
    {
        const float v3 = x - ic2;
        const float v1 = g.a1 * ic1 + g.a2 * v3;
        const float v2 = ic2 + g.a2 * ic1 + g.a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        return g.hp ? x - g.k * v1 - v2 : v2;
    }

    static float softClip (float x)
    {
        const float a = std::fabs (x);
        if (a <= 0.8f) return x;
        const float y = 0.8f + 0.2f * std::tanh ((a - 0.8f) / 0.2f);
        return x < 0.0f ? -y : y;
    }

    // Phase alignment ("waveform similarity"): the grain may start up to 20 ms later or earlier than planned; pick the
    // start whose preceding 10 ms looks most like the last 10 ms of the texture that is playing right now. Grains that
    // continue the same waveform add up coherently, so a held chord stays a clean, steady pad instead of a noisy shimmer.
    void alignToTexture (double& pos, double latest) const
    {
        const int S = std::max (64, (int) (0.010 * sr)), maxShift = (int) (0.020 * sr), step = 4;
        float refE = 0.0f;
        for (int k = 0; k < S; k += step) { const float v = hist[(size_t) ((histPos - S + k) & kHistMask)]; refE += v * v; }
        if (refE < 1e-8f) return;
        const int64_t p0 = (int64_t) std::floor (pos);
        float best = 0.25f; int bestShift = -1;
        for (int sh = 0; sh <= maxShift; sh += step)
        {
            if ((double) (p0 + sh) > latest) break;
            float xy = 0.0f, yy = 1e-12f;
            for (int k = 0; k < S; k += step)
            {
                const float y = bufL[(size_t) ((p0 + sh - S + k) & mask)] + bufR[(size_t) ((p0 + sh - S + k) & mask)];
                const float x = hist[(size_t) ((histPos - S + k) & kHistMask)];
                xy += x * y; yy += y * y;
            }
            const float c = xy / std::sqrt (refE * yy * 2.0f);
            if (c > best) { best = c; bestShift = sh; }
        }
        if (bestShift >= 0) pos += (double) bestShift;
    }

    // Pitch of a grain. A continuous random pitch turns any tone into noise ("wind"), so the shifts are musical:
    // the grain is either left alone or moved by an octave / fourth / fifth, plus a little detune for width.
    // Blur opens the intervals one after another (octaves first), up to +-1200 cents at full blur.
    float pickCents (float b)
    {
        struct Iv { float st, w, gate; };
        static const Iv iv[] = { { -12.0f, 1.0f, 0.20f }, { 12.0f, 1.0f, 0.20f }, { -5.0f, 0.6f, 0.38f }, { 5.0f, 0.7f, 0.38f },
                                 { -7.0f, 0.8f, 0.55f }, { 7.0f, 1.0f, 0.55f } };
        float w[6], total = 2.6f;
        for (int i = 0; i < 6; ++i) { w[i] = iv[i].w * std::clamp ((b - iv[i].gate) / 0.12f, 0.0f, 1.0f); total += w[i]; }
        float pick = rng.next() * total, st = 0.0f;
        for (int i = 0; i < 6; ++i) { if (pick < w[i]) { st = iv[i].st; break; } pick -= w[i]; }
        return st * 100.0f + (rng.next() * 2.0f - 1.0f) * 22.0f * b;
    }

    void spawn (const Params& p, int len, float N)
    {
        Grain* slot = nullptr;
        for (auto& g : grains) if (! g.active) { slot = &g; break; }
        if (slot == nullptr) return;
        Grain& g = *slot;
        const float b = std::clamp (p.blur, 0.0f, 1.0f), d = std::clamp (p.decay, 0.0f, 1.0f);

        g = Grain();
        g.len = len;
        const float cents = pickCents (b);
        g.rate = std::pow (2.0, (double) cents / 1200.0);

        // read position: never closer to the write head than the grain needs (it would run into it), never further than the buffer holds
        const double minD = g.rate * (double) len + 64.0;
        const double maxD = std::max (minD, (double) std::min<int64_t> (maxDepth, (int64_t) mask) - 2.0 * (double) len - 512.0);
        const double span = (0.03 + 0.8 * (double) b) * sr;
        const double dist = std::min (maxD, minD + 0.02 * sr + (double) rng.next() * span);
        g.pos = (double) head - dist;
        if (std::fabs (cents) < 60.0f) alignToTexture (g.pos, (double) head - minD);
        g.depth01 = (float) std::clamp (((double) head - g.pos) / (double) maxDepth, 0.0, 1.0);

        const float th = (float) (kPi / 4.0) + (rng.next() - 0.5f) * (float) (kPi / 2.0) * b;
        g.gL = 1.41421356f * std::cos (th);
        g.gR = 1.41421356f * std::sin (th);
        g.pan01 = th / (float) (kPi / 2.0);

        g.peak = peakFor (d);
        const float fk = d * 16.0f; const int k0 = std::min (15, (int) fk);
        const float m = msq[k0] + (msq[k0 + 1] - msq[k0]) * (fk - (float) k0);
        g.gain = 1.0f / std::sqrt (std::max (1e-3f, m * N));          // overlapping grains add up in power

        if (d > 0.02f)
        {
            g.filt = true;
            g.hp = rng.next() < 0.15f;
            const float f0 = g.hp ? 60.0f : std::min (9000.0f, (float) (0.45 * sr));
            const float f1 = g.hp ? f0 * std::pow (1200.0f / f0, d) : f0 * std::pow (180.0f / f0, d);
            g.lf0 = std::log (f0);
            g.dlf = std::log (f1 / f0);
            g.qMax = 0.707f + 1.8f * d * rng.range (0.4f, 1.0f);
            updateFilter (g, 0.0f);
        }
        g.active = true;
    }

    double sr = 48000.0;
    std::vector<float> bufL, bufR;
    int64_t mask = 0, head = 0, maxDepth = 0;
    double countdown = 0.0;
    Rng rng;
    uint32_t seedValue = 1;
    static constexpr int kHistMask = 4095;
    std::vector<float> hist = std::vector<float> (kHistMask + 1, 0.0f);
    int64_t histPos = 0;
    float refS = 0.0f, outS = 0.0f, agc = 1.0f, aPow = 0.001f, aGain = 0.001f;
    float hann[kTable + 1] = {};
    float msq[17] = {};
    std::vector<Grain> grains = std::vector<Grain> ((size_t) kMaxGrains);
};

} // namespace spore
