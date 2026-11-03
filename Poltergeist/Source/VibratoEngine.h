// Poltergeist - vibrato engine (header-only, no JUCE, no allocation after prepare()).
//
// A vibrato is a delay line whose delay time is swung by a slow sine LFO. While the delay grows the sound is
// played slightly slower (lower pitch), while it shrinks it is played faster (higher pitch):
//     pitch ratio = 1 - d(delay)/dt
// With delay = D0 * (1 - cos(phase)) the ratio is 1 - delta * sin(phase), where delta = D0 * omega is the largest
// speed change. Depth is the pitch swing in cents (the upward and the downward peak average to it; delta = tanh(cents * ln2 / 1200)), D0 = delta / omega.
// (Deep and slow vibrato needs long delays: 200 cents at 0.5 Hz is about 39 ms of swing - the buffer holds 0.3 s.)
#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace polter
{
constexpr double kPi = 3.14159265358979323846;
constexpr float kMinRate = 0.5f, kMaxRate = 12.0f;       // Hz
constexpr float kMaxDepth = 200.0f;                      // cents (peak, +-)

class Vibrato
{
public:
    static constexpr int kMaxChannels = 2;
    static constexpr float kBaseDelay = 3.0f;            // samples; keeps the interpolator inside the written part of the buffer

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        int len = 1;
        while ((double) len < 0.3 * sr + 16.0) len <<= 1;
        for (auto& b : buf) b.assign ((size_t) len, 0.0f);
        mask = len - 1;
        aRate = 1.0 - std::exp (-1.0 / (0.04 * sr));
        aDepth = 1.0 - std::exp (-1.0 / (0.03 * sr));
        reset();
    }

    void reset()
    {
        for (auto& b : buf) std::fill (b.begin(), b.end(), 0.0f);
        w = 0; phase = 0.0; rateS = depthS = -1.0; deviation = 0.0f;
    }

    // io[c] is processed in place
    void process (float* const* io, int numChannels, int n, float rateHz, float depthCents)
    {
        const int nch = std::min (numChannels, kMaxChannels);
        rateHz = std::clamp (rateHz, kMinRate, kMaxRate);
        depthCents = std::clamp (depthCents, 0.0f, kMaxDepth);
        if (rateS < 0.0) { rateS = rateHz; depthS = depthCents; }

        for (int i = 0; i < n; ++i)
        {
            rateS += aRate * ((double) rateHz - rateS);
            depthS += aDepth * ((double) depthCents - depthS);
            const double om = 2.0 * kPi * rateS / sr;
            phase += om;
            if (phase >= 2.0 * kPi) phase -= 2.0 * kPi;
            const double delta = std::tanh (depthS * 0.693147180559945 / 1200.0);     // up/down swing averages exactly `depth` cents
            const double D0 = delta / om;
            const float delay = kBaseDelay + (float) (D0 * (1.0 - std::cos (phase)));

            for (int c = 0; c < nch; ++c)
            {
                auto& b = buf[(size_t) c];
                b[(size_t) (w & mask)] = io[c][i];
                const double p = (double) w - (double) delay;
                const long long i0 = (long long) std::floor (p);
                const float f = (float) (p - (double) i0);
                const float y0 = b[(size_t) ((i0 - 1) & mask)], y1 = b[(size_t) (i0 & mask)], y2 = b[(size_t) ((i0 + 1) & mask)], y3 = b[(size_t) ((i0 + 2) & mask)];
                const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
                io[c][i] = ((c3 * f + c2) * f + c1) * f + y1;
            }
            ++w;
            deviation = (float) (1200.0 * std::log2 (std::max (0.2, 1.0 - delta * std::sin (phase))));
        }
    }

    // pitch deviation right now, in cents (what the listener hears at this moment)
    float currentDeviationCents() const { return deviation; }

private:
    double sr = 48000.0, phase = 0.0, rateS = -1.0, depthS = -1.0, aRate = 0.001, aDepth = 0.001;
    long long w = 0, mask = 0;
    float deviation = 0.0f;
    std::vector<float> buf[kMaxChannels];
};
} // namespace polter
