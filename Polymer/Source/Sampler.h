// Sampler.h - polyphonic one-shot sample player for Polymer (no JUCE dependency).
// Plays a `Sound` pitched by the MIDI note (note 60 = original pitch), cubic interpolation.
#pragma once

#include "Transmuter.h"
#include <array>
#include <atomic>
#include <memory>

namespace poly
{

struct Sound
{
    int id = 0;
    int parent = -1;
    int generation = 0;
    Dna dna;
    Audio audio;
    float hue = 0.6f;
    float seconds = 0.0f;
    std::vector<float> peaks;                 // waveform thumbnail
    std::atomic<bool> retired { false };      // set when the sound is about to be freed -> voices fade out
};

inline std::vector<float> makePeaks (const Audio& a, int count = 160)
{
    std::vector<float> p ((size_t) count, 0.0f);
    const int n = a.size();
    if (n == 0) return p;
    for (int i = 0; i < count; ++i)
    {
        const int b = (int) ((long long) n * i / count), e = std::max (b + 1, (int) ((long long) n * (i + 1) / count));
        float m = 0.0f;
        for (int k = b; k < std::min (e, n); ++k) m = std::max (m, std::max (std::fabs (a.ch[0][(size_t) k]), std::fabs (a.ch[1][(size_t) k])));
        p[(size_t) i] = m;
    }
    return p;
}

class Sampler
{
public:
    static constexpr int kVoices = 16;

    void prepare (double hostRate) { sr = hostRate; allOff(); }

    void allOff() { for (auto& v : voices) v.active = false; }

    // returns the voice index used
    void noteOn (int note, float velocity, const Sound* s, bool audition)
    {
        if (s == nullptr || s->audio.size() < 2) return;
        Voice* v = nullptr;
        for (auto& c : voices) if (! c.active) { v = &c; break; }
        if (v == nullptr)    // steal the oldest
        {
            v = &voices[0];
            for (auto& c : voices) if (c.age < v->age) v = &c;
        }
        v->active = true;
        v->snd = s;
        v->pos = 0.0;
        v->note = note;
        v->held = true;
        v->audition = audition;
        v->gain = 0.25f + 0.75f * velocity;
        v->env = 0.0f;
        v->releasing = false;
        v->relStep = 0.0f;
        v->age = ++counter;
    }

    void noteOff (int note, bool cutOnNoteOff)
    {
        for (auto& v : voices)
            if (v.active && v.held && v.note == note && ! v.audition)
            {
                v.held = false;
                if (cutOnNoteOff) startRelease (v, 0.15f);
            }
    }

    void stopAudition()
    {
        for (auto& v : voices) if (v.active && v.audition) startRelease (v, 0.02f);
    }

    void setPitchBend (float semitones) { bend = semitones; }

    // adds into L / R
    void render (float* L, float* R, int n)
    {
        const float bendRatio = std::pow (2.0f, bend / 12.0f);
        for (auto& v : voices)
        {
            if (! v.active) continue;
            const Sound* s = v.snd;
            if (s->retired.load (std::memory_order_relaxed) && ! v.releasing) startRelease (v, 0.005f);
            const int len = s->audio.size();
            const double ratio = s->audio.sr / sr * std::pow (2.0, (v.note - 60) / 12.0) * bendRatio;
            const float attackStep = 1.0f / (float) std::max (1.0, 0.0015 * sr);
            const std::vector<float>& xl = s->audio.ch[0];
            const std::vector<float>& xr = s->audio.ch[1];
            for (int i = 0; i < n; ++i)
            {
                if (v.pos >= len - 1) { v.active = false; break; }
                if (v.releasing) { v.env -= v.relStep; if (v.env <= 0.0f) { v.active = false; break; } }
                else if (v.env < 1.0f) v.env = std::min (1.0f, v.env + attackStep);
                const float g = v.gain * v.env;
                L[i] += detail::hermite (xl, v.pos) * g;
                R[i] += detail::hermite (xr, v.pos) * g;
                v.pos += ratio;
            }
        }
    }

    int activeVoices() const { int c = 0; for (auto& v : voices) c += v.active ? 1 : 0; return c; }

private:
    struct Voice
    {
        bool active = false, held = false, releasing = false, audition = false;
        const Sound* snd = nullptr;
        double pos = 0.0;
        int note = 60;
        float gain = 1.0f, env = 0.0f, relStep = 0.0f;
        uint64_t age = 0;
    };

    void startRelease (Voice& v, float seconds)
    {
        if (v.releasing) return;
        v.releasing = true;
        v.relStep = std::max (v.env, 0.01f) / (float) std::max (1.0, seconds * sr);
    }

    std::array<Voice, kVoices> voices;
    double sr = 44100.0;
    float bend = 0.0f;
    uint64_t counter = 0;
};

} // namespace poly
