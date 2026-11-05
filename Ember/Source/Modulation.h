#pragma once
// Ember modulation: sources and the target table (pure C++, no JUCE).
//   * 2 step LFOs (16 steps, free-running in Hz or locked to the host tempo, with glide between the steps)
//   * 2 envelope followers (attack / release / gain)
//   * 2 manual sliders
//   * 8 routes: source -> target with a bipolar amount; the amount is added in the 0..1 domain of the target parameter
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace ember
{
constexpr int kNumLfos = 2, kNumEnvs = 2, kNumSliders = 2, kNumRoutes = 8, kSteps = 16;

enum Source { kSrcNone = 0, kSrcLfo1, kSrcLfo2, kSrcEnv1, kSrcEnv2, kSrcSlider1, kSrcSlider2, kNumSources };
inline const char* sourceName (int s)
{
    static const char* const n[kNumSources] = { "None", "LFO 1", "LFO 2", "Envelope 1", "Envelope 2", "Slider 1", "Slider 2" };
    return n[std::clamp (s, 0, kNumSources - 1)];
}
inline bool sourceIsBipolar (int s) { return s == kSrcLfo1 || s == kSrcLfo2; }

// ---- targets: 0 = none, 1..48 = band (6) x parameter (8), 49..53 = crossovers, 54..56 = global input / output / mix
constexpr int kBandTargetParams = 8;
constexpr int kNumTargets = 1 + 6 * kBandTargetParams + 5 + 3;
inline const char* bandParamName (int j) { static const char* const n[kBandTargetParams] = { "Drive", "Level", "Mix", "Dynamics", "Tone", "Feedback", "Fb Freq", "Pan" }; return n[std::clamp (j, 0, kBandTargetParams - 1)]; }
inline const char* bandParamId (int j) { static const char* const n[kBandTargetParams] = { "drive", "level", "mix", "dyn", "tone", "fb", "fbf", "pan" }; return n[std::clamp (j, 0, kBandTargetParams - 1)]; }
inline int bandTarget (int band, int param) { return 1 + band * kBandTargetParams + param; }
inline int crossoverTarget (int k) { return 1 + 6 * kBandTargetParams + k; }
constexpr int kTargetIn = 1 + 6 * kBandTargetParams + 5, kTargetOut = kTargetIn + 1, kTargetMix = kTargetIn + 2;

// "Band 3 - Drive", "Crossover 2", "Output" ...
inline void targetName (int t, char* buf, size_t n)
{
    if (t <= 0 || t >= kNumTargets) std::snprintf (buf, n, "None");
    else if (t < crossoverTarget (0)) std::snprintf (buf, n, "Band %d  %s", (t - 1) / kBandTargetParams + 1, bandParamName ((t - 1) % kBandTargetParams));
    else if (t < kTargetIn) std::snprintf (buf, n, "Crossover %d", t - crossoverTarget (0) + 1);
    else std::snprintf (buf, n, "%s", t == kTargetIn ? "Input" : t == kTargetOut ? "Output" : "Mix");
}

// ---- LFO -------------------------------------------------------------------------------------------------------------
constexpr int kNumSyncModes = 7;
inline const char* syncName (int i) { static const char* const n[kNumSyncModes] = { "Free", "1 beat", "2 beats", "1 bar", "2 bars", "4 bars", "8 bars" }; return n[std::clamp (i, 0, kNumSyncModes - 1)]; }
inline double syncBeats (int i) { static const double b[kNumSyncModes] = { 0.0, 1.0, 2.0, 4.0, 8.0, 16.0, 32.0 }; return b[std::clamp (i, 0, kNumSyncModes - 1)]; }   // one full 16-step cycle

struct LfoSettings
{
    float rateHz = 1.0f;       // cycles per second (free mode)
    int sync = 0;              // 0 = free, else tempo-locked
    float glide = 0.0f;        // 0 = hard steps, 1 = fully smoothed
    std::array<float, kSteps> steps {};
};

class Lfo
{
public:
    void reset() { phase = 0.0; y = 0.0f; started = false; }
    // dt: seconds since the last call; ppq < 0 = no transport position available; bpm: host tempo (or 120)
    float run (const LfoSettings& s, double dt, double ppq, double bpm)
    {
        const double beats = syncBeats (s.sync);
        if (beats > 0.0)
        {
            if (ppq >= 0.0) phase = std::fmod (ppq / beats, 1.0);
            else phase += dt * bpm / 60.0 / beats;
        }
        else phase += dt * std::clamp ((double) s.rateHz, 0.01, 100.0);
        phase -= std::floor (phase);
        const int idx = std::clamp ((int) (phase * kSteps), 0, kSteps - 1);
        const float target = std::clamp (s.steps[(size_t) idx], -1.0f, 1.0f);
        if (! started) { y = target; started = true; }
        if (s.glide <= 0.001f) y = target;
        else
        {
            const double cycleSec = beats > 0.0 ? beats * 60.0 / bpm : 1.0 / std::clamp ((double) s.rateHz, 0.01, 100.0);
            const double tau = std::max (1.0e-4, (double) s.glide * (cycleSec / kSteps) * 0.7);
            y += (float) ((target - y) * (1.0 - std::exp (-dt / tau)));
        }
        return y;
    }
    float position() const { return (float) phase; }
private:
    double phase = 0.0;
    float y = 0.0f;
    bool started = false;
};

// ---- envelope follower -----------------------------------------------------------------------------------------------
class Follower
{
public:
    void reset() { env = 0.0f; }
    // peak: the loudest |sample| of the chunk; returns 0..1 (the level in dB mapped from -48 dB .. 0 dB, after the gain)
    float run (float peak, double dt, float attackMs, float releaseMs, float gainDb)
    {
        const double tau = 0.001 * (peak > env ? attackMs : releaseMs);
        env += (float) ((peak - env) * (1.0 - std::exp (-dt / std::max (1.0e-4, tau))));
        const float db = 20.0f * std::log10 (env + 1.0e-6f) + gainDb;
        return std::clamp ((db + 48.0f) * (1.0f / 48.0f), 0.0f, 1.0f);
    }
private:
    float env = 0.0f;
};

// ---- the default LFO shapes and the editor's shape presets ------------------------------------------------------------
enum Shape { kShapeSine, kShapeTriangle, kShapeSawUp, kShapeSawDown, kShapeSquare, kShapeRandom, kShapeFlat, kNumShapes };
inline const char* shapeName (int s) { static const char* const n[kNumShapes] = { "Sine", "Triangle", "Saw up", "Saw down", "Square", "Random", "Flat" }; return n[std::clamp (s, 0, kNumShapes - 1)]; }
inline void fillShape (std::array<float, kSteps>& st, int shape, unsigned seed = 1)
{
    unsigned r = seed * 2654435761u + 12345u;
    for (int i = 0; i < kSteps; ++i)
    {
        const float t = (float) i / (float) kSteps;
        switch (shape)
        {
            case kShapeSine:     st[(size_t) i] = std::sin (6.283185307f * t); break;
            case kShapeTriangle: st[(size_t) i] = 1.0f - 4.0f * std::fabs (std::fmod (t + 0.25f, 1.0f) - 0.5f) ; break;
            case kShapeSawUp:    st[(size_t) i] = -1.0f + 2.0f * t; break;
            case kShapeSawDown:  st[(size_t) i] = 1.0f - 2.0f * t; break;
            case kShapeSquare:   st[(size_t) i] = i < kSteps / 2 ? 1.0f : -1.0f; break;
            case kShapeRandom:   r = r * 1664525u + 1013904223u; st[(size_t) i] = (float) (r >> 8) / 8388608.0f - 1.0f; break;
            default:             st[(size_t) i] = 0.0f; break;
        }
    }
}
} // namespace ember
