#pragma once
// Residue echo engine (pure C++, no JUCE).
//   stereo delay line (cubic interpolation) -> echo colour of the style -> low cut / high cut -> soft limiter
//   the processed echo is both the output tap and the feedback (so every repeat gets darker / dirtier, as on a real echo)
//   extras: input saturation, wobble (tape flutter / BBD chorus), ping-pong (cross feedback), ducking
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace residue
{
enum Style { kDigital = 0, kTape, kAnalog, kLoFi, kNumStyles };
inline const char* styleName (int s) { static const char* const n[kNumStyles] = { "DIGITAL", "TAPE", "ANALOG", "LO-FI" }; return n[std::clamp (s, 0, kNumStyles - 1)]; }

constexpr int kNumNotes = 14;
inline const char* noteName (int i)
{
    static const char* const n[kNumNotes] = { "1/32", "1/16T", "1/16", "1/8T", "1/16.", "1/8", "1/4T", "1/8.", "1/4", "1/2T", "1/4.", "1/2", "1/2.", "1/1" };
    return n[std::clamp (i, 0, kNumNotes - 1)];
}
inline double noteBeats (int i)   // length in quarter notes
{
    static const double b[kNumNotes] = { 0.125, 1.0 / 6.0, 0.25, 1.0 / 3.0, 0.375, 0.5, 2.0 / 3.0, 0.75, 1.0, 4.0 / 3.0, 1.5, 2.0, 3.0, 4.0 };
    return b[std::clamp (i, 0, kNumNotes - 1)];
}

struct Params
{
    float timeSamples = 14400.0f;   // echo time
    float fb = 0.35f;               // 0..1
    float sat = 0.0f;               // 0..1  drive of the input
    float wobble = 0.0f;            // 0..1
    float duck = 0.0f;              // 0..1
    float spread = 0.0f;            // 0..1  0 = parallel stereo, 1 = full ping-pong
    float mix = 0.3f;               // 0..1
    float lowHz = 20.0f, highHz = 20000.0f;
    int style = kTape;
};

class Engine
{
public:
    static constexpr int kMaxCh = 2;

    void prepare (double sampleRate, double maxDelaySeconds)
    {
        fs = sampleRate;
        maxDelay = (int) (maxDelaySeconds * sampleRate);
        for (auto& l : line) l.prepare (maxDelay + 16);
        nzA = (float) (1.0 - std::exp (-6.283185307 * 3.0 / fs));
        hold = std::max (1, (int) (fs / 12000.0));
        reset();
    }

    void reset()
    {
        for (auto& l : line) l.clear();
        for (auto& c : ch) c = Chan {};
        first = true; env = 0.0f; ph1 = ph2 = ph3 = 0.0; nz = 0.0f; rng = 22222u;
        inPeak = outPeak = 0.0f;
        fbS = prm.fb; satS = prm.sat; wobS = prm.wobble; duckS = prm.duck; sprS = prm.spread; mixS = prm.mix;
    }

    void setParams (const Params& p) { prm = p; }
    int maxDelaySamples() const { return maxDelay; }
    float lastInputPeak() const { return inPeak; }
    float lastOutputPeak() const { return outPeak; }

    void process (float* L, float* R, int n)
    {
        updateCoefs();
        if (first) { tCur = prm.timeSamples; fbS = prm.fb; satS = prm.sat; wobS = prm.wobble; duckS = prm.duck; sprS = prm.spread; mixS = prm.mix; first = false; }
        const float tA = 1.0f - std::exp (-1.0f / (float) (fs * timeTau (prm.style)));
        const float envAtk = 1.0f - std::exp (-1.0f / (float) (fs * 0.003)), envRel = 1.0f - std::exp (-1.0f / (float) (fs * 0.18));
        constexpr float sm = 0.002f;
        float inPk = 0.0f, outPk = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            const float inL = L[i], inR = R[i];
            const float ap = std::max (std::fabs (inL), std::fabs (inR));
            inPk = std::max (inPk, ap);
            env += (ap - env) * (ap > env ? envAtk : envRel);

            fbS += (prm.fb - fbS) * sm;       satS += (prm.sat - satS) * sm;       wobS += (prm.wobble - wobS) * sm;
            duckS += (prm.duck - duckS) * sm; sprS += (prm.spread - sprS) * sm;   mixS += (prm.mix - mixS) * sm;
            tCur += (prm.timeSamples - tCur) * tA;

            float mL = 0.0f, mR = 0.0f;
            modulation (mL, mR);
            const float dMax = (float) (maxDelay - 4);
            const float dL = std::clamp (tCur + mL, 4.0f, dMax), dR = std::clamp (tCur + mR, 4.0f, dMax);

            const float pl = loop (0, line[0].read (dL));
            const float pr = loop (1, line[1].read (dR));

            // input: saturation, then the ping-pong routing (spread 1: the mono sum goes into the left line only)
            const float mono = 0.5f * (inL + inR);
            const float fL = satIn (inL + (mono - inL) * sprS), fR = satIn (inR * (1.0f - sprS));
            const float cl = pl + (pr - pl) * sprS, cr = pr + (pl - pr) * sprS;
            line[0].push (fL + fbS * cl);
            line[1].push (fR + fbS * cr);

            const float dryG = std::min (1.0f, 2.0f * (1.0f - mixS)), wetG = std::min (1.0f, 2.0f * mixS);
            const float duckG = 1.0f - duckS * std::clamp (env * 4.0f, 0.0f, 1.0f);
            const float oL = inL * dryG + pl * wetG * duckG, oR = inR * dryG + pr * wetG * duckG;
            L[i] = oL; R[i] = oR;
            outPk = std::max (outPk, std::max (std::fabs (oL), std::fabs (oR)));
        }
        inPeak = inPk; outPeak = outPk;
    }

private:
    // ---- building blocks ----
    struct Line
    {
        std::vector<float> buf; int mask = 0, w = 0;
        void prepare (int n) { int s = 1; while (s < n + 8) s <<= 1; buf.assign ((size_t) s, 0.0f); mask = s - 1; w = 0; }
        void clear() { std::fill (buf.begin(), buf.end(), 0.0f); w = 0; }
        void push (float x) { buf[(size_t) w] = x; w = (w + 1) & mask; }
        // delay d >= 4 samples (fractional), 4-point Hermite
        float read (float d) const
        {
            const int n = (int) d; const float t = d - (float) n;
            const float ym1 = buf[(size_t) ((w - n + 1) & mask)], y0 = buf[(size_t) ((w - n) & mask)];
            const float y1 = buf[(size_t) ((w - n - 1) & mask)], y2 = buf[(size_t) ((w - n - 2) & mask)];
            const float c1 = 0.5f * (y1 - ym1), c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2, c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
            return ((c3 * t + c2) * t + c1) * t + y0;
        }
    };
    struct Pole   // TPT one-pole low-pass (the high-pass is x - lp)
    {
        float s = 0.0f;
        float lp (float x, float G) { const float v = (x - s) * G; const float y = v + s; s = y + v; return y; }
    };
    struct Chan { Pole low[2], high[2], col[2]; float held = 0.0f; int cnt = 0; };

    static float G (double hz, double fs) { const double g = std::tan (3.14159265358979 * std::min (hz, 0.45 * fs) / fs); return (float) (g / (1.0 + g)); }
    static float timeTau (int style) { return style == kDigital ? 0.03f : style == kTape ? 0.12f : style == kAnalog ? 0.08f : 0.05f; }

    void updateCoefs()
    {
        gLow = G (std::max (prm.lowHz, 20.0f), fs);
        gHigh = G (std::min (prm.highHz, 20000.0f), fs);
        highOn = prm.highHz < 19990.0f;
        gTape = G (6500.0, fs); gAna = G (4200.0, fs); gLofi = G (5200.0, fs);
    }

    float satIn (float x) const
    {
        if (satS < 1.0e-4f) return x;
        const float g = 1.0f + satS * 5.0f, mk = 1.0f + (g - 1.0f) * 0.45f;
        const float shaped = std::tanh (g * x) / g * mk;
        return x + satS * (shaped - x);
    }

    // processes one echo (colour of the style, the user's cuts, soft limiter); small signals keep unity gain
    float loop (int c, float v)
    {
        Chan& s = ch[c];
        float y = v;
        switch (prm.style)
        {
            case kTape:   y = std::tanh (1.5f * y) * (1.0f / 1.5f); y = s.col[0].lp (y, gTape); break;
            case kAnalog: y = std::tanh (2.0f * y) * 0.5f; y = s.col[1].lp (s.col[0].lp (y, gAna), gAna); break;
            case kLoFi:
                if (++s.cnt >= hold) { s.cnt = 0; s.held = std::round (y * 512.0f) * (1.0f / 512.0f); }
                y = s.col[0].lp (s.held, gLofi); break;
            default: break;
        }
        { const float h1 = y - s.high[0].lp (y, gLow); y = h1 - s.high[1].lp (h1, gLow); }
        if (highOn) y = s.low[1].lp (s.low[0].lp (y, gHigh), gHigh);
        const float a = std::fabs (y);
        if (a > 0.8f) y = std::copysign (0.8f + 0.2f * std::tanh ((a - 0.8f) * 5.0f), y);
        return y;
    }

    float noise() { rng = rng * 1664525u + 1013904223u; return (float) (rng >> 8) * (1.0f / 8388608.0f) - 1.0f; }

    void modulation (float& mL, float& mR)
    {
        const float depth = wobS * (float) (0.003 * fs);
        if (depth < 1.0e-6f) { mL = mR = 0.0f; return; }
        const double inv = 1.0 / fs;
        float a, b;
        switch (prm.style)
        {
            case kAnalog: ph1 += 0.9 * inv; ph1 -= std::floor (ph1); a = std::sin (6.2831853f * (float) ph1); b = std::sin (6.2831853f * (float) (ph1 + 0.25)); mL = depth * 0.9f * a; mR = depth * 0.9f * b; break;
            case kDigital: ph1 += 0.4 * inv; ph1 -= std::floor (ph1); mL = depth * 0.5f * std::sin (6.2831853f * (float) ph1); mR = depth * 0.5f * std::sin (6.2831853f * (float) (ph1 + 0.3)); break;
            default:   // tape / lo-fi: slow wow + fast flutter + a random drift
            {
                ph1 += 0.7 * inv; ph1 -= std::floor (ph1); ph2 += 6.3 * inv; ph2 -= std::floor (ph2);
                nz += (noise() - nz) * nzA;
                const float drift = 0.3f * 35.0f * nz;
                mL = depth * (0.7f * std::sin (6.2831853f * (float) ph1) + 0.04f * std::sin (6.2831853f * (float) ph2) + drift);
                mR = depth * (0.7f * std::sin (6.2831853f * (float) (ph1 + 0.2)) + 0.04f * std::sin (6.2831853f * (float) (ph2 + 0.5)) - drift);
                break;
            }
        }
        (void) ph3;
    }

    double fs = 48000.0;
    int maxDelay = 0, hold = 4;
    std::array<Line, kMaxCh> line;
    std::array<Chan, kMaxCh> ch;
    Params prm;
    bool first = true, highOn = false;
    float tCur = 14400.0f, env = 0.0f, nz = 0.0f, nzA = 0.0f;
    float fbS = 0, satS = 0, wobS = 0, duckS = 0, sprS = 0, mixS = 0;
    float gLow = 0, gHigh = 0, gTape = 0, gAna = 0, gLofi = 0;
    double ph1 = 0, ph2 = 0, ph3 = 0;
    uint32_t rng = 22222u;
    float inPeak = 0.0f, outPeak = 0.0f;
};
} // namespace residue
