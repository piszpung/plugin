// StretchEngine.h
// Paulstretch-style extreme time-stretch engine ("ambient pad from any sound").
// Header-only, no JUCE dependency, so it can be unit-tested on its own.
//
// How it works (per channel):
//   * the input is recorded continuously into a long ring buffer,
//   * every hop (half a window of output samples) a window of input samples is taken at a read
//     position that advances only (N/2)/stretch samples per hop,
//   * the window is FFT'd, only the magnitudes are kept, phases are randomised,
//     (optionally: low-pass and key/scale mask are applied to the magnitudes),
//   * IFFT, sine synthesis window, overlap-add.
// Random phases are independent per channel, which gives a wide stereo pad.
//
// Fast start: the very first grains use short windows (~21 ms) that double in length
// as more input becomes available, until the full ~0.7 s window is reached. So the
// output starts ~21 ms after the input starts instead of ~0.7 s.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace slowmo
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

// Transparent below `knee`, smoothly saturates towards +-1.0 above it (derivative continuous at the knee).
inline float softClip (float x, float knee = 0.7f)
{
    const float ax = std::fabs (x);
    if (ax <= knee) return x;
    const float y = knee + (1.0f - knee) * std::tanh ((ax - knee) / (1.0f - knee));
    return x < 0.0f ? -y : y;
}

// ---------------------------------------------------------------------------------
// Scales
// ---------------------------------------------------------------------------------
inline const std::vector<std::vector<int>>& scaleTable()
{
    static const std::vector<std::vector<int>> t = {
        { 0, 2, 4, 5, 7, 9, 11 },   // 0 Major
        { 0, 2, 3, 5, 7, 8, 10 },   // 1 Minor
        { 0, 2, 3, 5, 7, 9, 10 },   // 2 Dorian
        { 0, 1, 3, 5, 7, 8, 10 },   // 3 Phrygian
        { 0, 2, 4, 6, 7, 9, 11 },   // 4 Lydian
        { 0, 2, 4, 5, 7, 9, 10 },   // 5 Mixolydian
        { 0, 1, 3, 5, 6, 8, 10 },   // 6 Locrian
        { 0, 2, 3, 5, 7, 8, 11 },   // 7 Harmonic Minor
        { 0, 2, 4, 7, 9 },          // 8 Pentatonic Major
        { 0, 3, 5, 7, 10 },         // 9 Pentatonic Minor
        { 0, 3, 5, 6, 7, 10 },      // 10 Blues
    };
    return t;
}

// ---------------------------------------------------------------------------------
// Engine
// ---------------------------------------------------------------------------------
class StretchEngine
{
public:
    static constexpr int kChannels = 2;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        // full window of ~0.7 s (32768 @ 44.1/48k, 65536 @ 96k)
        N = 1;
        while (N < (int) (sampleRate * 0.5)) N <<= 1;
        minSize = std::max (256, N >> 5);                  // ~21 ms
        numLevels = 0;
        for (int s = minSize; s <= N; s <<= 1) ++numLevels;

        ffts.assign ((size_t) numLevels, FFT());
        hannL.assign ((size_t) numLevels, std::vector<float>());
        sineL.assign ((size_t) numLevels, std::vector<float>());
        for (int l = 0; l < numLevels; ++l)
        {
            const int n = minSize << l;
            ffts[(size_t) l].init (n);
            auto& h = hannL[(size_t) l];
            auto& s = sineL[(size_t) l];
            h.resize ((size_t) n);
            s.resize ((size_t) n);
            for (int i = 0; i < n; ++i)
            {
                h[(size_t) i] = 0.5f - 0.5f * (float) std::cos (2.0 * kPi * i / n);
                s[(size_t) i] = (float) std::sin (kPi * (i + 0.5) / n);
            }
        }
        // Hann has mean power 3/8; compensate so that output level ~ input level
        outGain = (float) std::sqrt (8.0 / 3.0);

        ringSize = 1;
        while (ringSize < (int64_t) (sampleRate * 30.0)) ringSize <<= 1;   // ~30-45 s of history
        ringMask = ringSize - 1;
        for (auto& r : ring) r.assign ((size_t) ringSize, 0.0f);

        accSize = 2 * (int64_t) N;
        accMask = accSize - 1;
        for (auto& a : acc) a.assign ((size_t) accSize, 0.0f);

        re.resize ((size_t) N);
        im.resize ((size_t) N);
        magMask.assign ((size_t) N / 2 + 1, 1.0f);
        maskDirty = true;
        reset();
    }

    void reset()
    {
        writePos = 0;
        readPos = 0.0;
        started = false;
        outPos = 0;
        nextGrainStart = 0;
        for (auto& a : acc) std::fill (a.begin(), a.end(), 0.0f);
        rng[0] = 0x9E3779B97F4A7C15ull;
        rng[1] = 0xD1B54A32D192ED03ull;
    }

    // stretch factor: 1 = 100% ... e.g. 5 = 500%
    void setStretch (float factor) { stretch = std::max (1.0f, factor); }

    void setLowpass (float hz)
    {
        hz = std::min (std::max (hz, 20.0f), 22000.0f);
        if (hz != lpfHz) { lpfHz = hz; maskDirty = true; }
    }

    // key: -1 = off, 0..11 = C..B; scaleIdx: index into scaleTable()
    void setScale (int key, int scaleIdx)
    {
        scaleIdx = std::min (std::max (scaleIdx, 0), (int) scaleTable().size() - 1);
        if (key != keyIdx || scaleIdx != scaleSel) { keyIdx = key; scaleSel = scaleIdx; maskDirty = true; }
    }

    int windowSize() const { return N; }
    int shortestWindow() const { return minSize; }
    bool isStarted() const { return started; }

    // Process n samples.
    void process (const float* inL, const float* inR, float* outL, float* outR, int n)
    {
        if (maskDirty) rebuildMask();

        for (int i = 0; i < n; ++i)
        {
            const size_t w = (size_t) (writePos & ringMask);
            ring[0][w] = inL[i];
            ring[1][w] = inR[i];
            ++writePos;

            if (! started && writePos >= minSize)
            {
                started = true;
                outPos = 0;
                nextGrainStart = 0;
                readPos = 0.0;
            }

            if (started)
            {
                // every grain that overlaps this output sample must already be added
                while (nextGrainStart <= outPos) addGrain();

                const size_t idx = (size_t) (outPos & accMask);
                outL[i] = acc[0][idx];
                outR[i] = acc[1][idx];
                acc[0][idx] = 0.0f;
                acc[1][idx] = 0.0f;
                ++outPos;
            }
            else
            {
                outL[i] = 0.0f;
                outR[i] = 0.0f;
            }
        }
    }

private:
    float nextRand (int ch)
    {
        // xorshift64*
        uint64_t x = rng[ch];
        x ^= x >> 12; x ^= x << 25; x ^= x >> 27;
        rng[ch] = x;
        const uint64_t r = x * 0x2545F4914F6CDD1Dull;
        return (float) ((r >> 40) * (1.0 / 16777216.0));   // [0,1)
    }

    void addGrain()
    {
        // keep the read position inside the data that is still in the ring buffer
        const double minStart = std::max (0.0, (double) (writePos - ringSize + 2 * (int64_t) N));
        if (readPos < minStart) readPos = minStart;

        // largest window (minSize .. N) for which input data is already available
        int64_t rpi = (int64_t) readPos;
        const int64_t avail = writePos - rpi;
        int lvl = numLevels - 1;
        while (lvl > 0 && ((int64_t) minSize << lvl) > avail) --lvl;
        const int Ns = minSize << lvl;
        const bool full = (lvl == numLevels - 1);
        if (full)
        {
            readPos = std::min (readPos, (double) (writePos - N));
            rpi = (int64_t) readPos;
        }

        const int stride = N / Ns;               // bin k of this FFT == bin k*stride of the full grid
        const int halfBins = Ns / 2;
        const FFT& f = ffts[(size_t) lvl];
        const float* hw = hannL[(size_t) lvl].data();
        const float* sw = sineL[(size_t) lvl].data();

        for (int c = 0; c < kChannels; ++c)
        {
            float* r = re.data();
            float* m = im.data();
            for (int k = 0; k < Ns; ++k)
            {
                r[k] = ring[c][(size_t) ((rpi + k) & ringMask)] * hw[k];
                m[k] = 0.0f;
            }
            f.forward (r, m);

            // keep magnitudes (times mask), randomise phases, build conjugate-symmetric spectrum
            for (int k = 0; k <= halfBins; ++k)
            {
                const float mag = std::sqrt (r[k] * r[k] + m[k] * m[k]) * magMask[(size_t) (k * stride)];
                if (k == 0 || k == halfBins)
                {
                    r[k] = mag * ((nextRand (c) < 0.5f) ? 1.0f : -1.0f);   // real-valued bins
                    m[k] = 0.0f;
                }
                else
                {
                    const float ph = 6.28318530718f * nextRand (c);
                    const float cr = mag * std::cos (ph), ci = mag * std::sin (ph);
                    r[k] = cr;           m[k] = ci;
                    r[Ns - k] = cr;      m[Ns - k] = -ci;
                }
            }
            f.inverse (r, m);

            float* a = acc[c].data();
            for (int k = 0; k < Ns; ++k)
                a[(size_t) ((nextGrainStart + k) & accMask)] += r[k] * sw[k] * outGain;
        }

        nextGrainStart += Ns / 2;
        // the read position only moves once the full window is in use
        if (full) readPos += (double) (Ns / 2) / (double) stretch;
    }

    void rebuildMask()
    {
        maskDirty = false;
        const int halfBins = N / 2;
        const float binHz = (float) (sr / N);

        // key / scale mask
        std::vector<float> keyMask ((size_t) halfBins + 1, 1.0f);
        if (keyIdx >= 0)
        {
            const auto& sc = scaleTable()[(size_t) scaleSel];
            const double sigma = 0.25, floorGain = 0.02, minF = 60.0;
            auto gainForPc = [&] (double pc)
            {
                double d = 99.0;
                for (int s : sc)
                {
                    double x = std::fabs (pc - s);
                    x = std::min (x, 12.0 - x);
                    d = std::min (d, x);
                }
                return std::max (floorGain, std::exp (-0.5 * (d / sigma) * (d / sigma)));
            };
            // makeup so that the average power over one octave is preserved
            double pw = 0.0;
            const int steps = 1200;
            for (int s = 0; s < steps; ++s) { const double g = gainForPc (12.0 * s / steps); pw += g * g; }
            pw /= steps;
            const float makeup = (float) std::min (2.8, 1.0 / std::sqrt (pw));   // max ~ +9 dB

            for (int k = 0; k <= halfBins; ++k)
            {
                const double f = (double) k * binHz;
                if (f < minF) continue;
                const double midi = 69.0 + 12.0 * std::log2 (f / 440.0);
                double pc = std::fmod (midi - keyIdx, 12.0);
                if (pc < 0.0) pc += 12.0;
                keyMask[(size_t) k] = (float) gainForPc (pc) * makeup;
            }
        }

        // low-pass (24 dB/oct on magnitudes, zero phase)
        for (int k = 0; k <= halfBins; ++k)
        {
            const float f = (float) k * binHz;
            const float x = f / lpfHz;
            const float x2 = x * x;
            const float lp = 1.0f / std::sqrt (1.0f + x2 * x2 * x2 * x2);
            magMask[(size_t) k] = lp * keyMask[(size_t) k];
        }
    }

    double sr = 48000.0;
    int N = 32768, minSize = 1024, numLevels = 6;
    std::vector<FFT> ffts;
    std::vector<std::vector<float>> hannL, sineL;
    std::vector<float> re, im, magMask;
    float outGain = 1.0f;

    std::vector<float> ring[kChannels];
    int64_t ringSize = 0, ringMask = 0, writePos = 0;
    double readPos = 0.0;
    bool started = false;

    std::vector<float> acc[kChannels];
    int64_t accSize = 0, accMask = 0;
    int64_t outPos = 0, nextGrainStart = 0;

    float stretch = 5.0f;
    float lpfHz = 20000.0f;
    int keyIdx = -1, scaleSel = 0;
    bool maskDirty = true;
    uint64_t rng[kChannels] = { 0x9E3779B97F4A7C15ull, 0xD1B54A32D192ED03ull };
};

} // namespace slowmo
