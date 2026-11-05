#pragma once
// Ember DSP: multiband saturation engine.
//   * up to 6 bands split by Linkwitz-Riley 4th-order crossovers (the bands sum back to a flat magnitude response)
//   * per band: style (16 shapers), drive, level, dry/wet mix, dynamics (compress/expand before the shaper),
//     tone (tilt EQ before the shaper), feedback (resonant loop around the shaper)
//   * global mid/side mode
// The engine runs at whatever rate it is prepared for (the processor feeds it oversampled audio).
// Everything is allocation-free after prepare().
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace ember
{
constexpr int kMaxBands = 6;
constexpr int kNumStyles = 28;

inline const char* styleName (int i)
{
    static const char* const n[kNumStyles] = {
        "Subtle Tube", "Clean Tube", "Warm Tube", "Hot Tube",
        "Subtle Tape", "Clean Tape", "Warm Tape", "Hot Tape",
        "Subtle Saturation", "Saturation", "Soft Clip", "Overdrive", "Hard Clip",
        "Transformer Soft", "Transformer Warm", "Transformer Hard",
        "American Clean", "American Crunch", "British Crunch", "British Lead", "Fuzz",
        "Foldback", "Sine Fold", "Half Rectify", "Full Rectify", "Bit Crusher", "Breakdown", "Smear" };
    return n[std::clamp (i, 0, kNumStyles - 1)];
}
// menu groups: first style of each group
constexpr int kNumGroups = 6;
inline const char* groupName (int g) { static const char* const n[kNumGroups] = { "Tube", "Tape", "Saturation", "Transformer", "Amp", "FX" }; return n[std::clamp (g, 0, kNumGroups - 1)]; }
inline int groupStart (int g) { static const int s[kNumGroups + 1] = { 0, 4, 8, 13, 16, 21, 28 }; return s[std::clamp (g, 0, kNumGroups)]; }
inline int groupOf (int style) { int g = 0; while (g + 1 < kNumGroups && style >= groupStart (g + 1)) ++g; return g; }

// ---- shapers -------------------------------------------------------------------------------------------------------
// tanh as a 7/6 Pade: error < 1e-5 for |x| < 5, clamped to +-1 outside; ~10x cheaper than std::tanh
inline float fastTanh (float x)
{
    if (x > 4.97f) return 1.0f;
    if (x < -4.97f) return -1.0f;
    const float x2 = x * x;
    const float num = x * (135135.0f + x2 * (17325.0f + x2 * (378.0f + x2)));
    const float den = 135135.0f + x2 * (62370.0f + x2 * (3150.0f + x2 * 28.0f));
    return std::clamp (num / den, -1.0f, 1.0f);
}

// The memoryless non-linearity of every style (what the transfer curve in the GUI shows). Small-signal slope ~1 for most styles
// (so 0 dB of drive hardly changes quiet material); the output is bounded. Filtering / memory of a style lives in Band::tick.
inline float shape (int style, float x)
{
    switch (style)
    {
        case 0:  return (fastTanh (0.6f * x + 0.12f) - 0.119427f) / 0.591442f;                                // Subtle Tube: a little asymmetry
        case 1:  { const float u = x + 0.06f; return (u / std::sqrt (1.0f + u * u) - 0.059892f) / 0.99461f; } // Clean Tube
        case 2:  return (fastTanh (x + 0.30f) - 0.291313f) / 0.915137f;                                       // Warm Tube: asymmetric -> even harmonics
        case 3:  return (fastTanh (1.6f * x + 0.288f) - 0.28030f) / 0.921432f;                                // Hot Tube
        case 4:  return fastTanh (0.7f * x) * (1.0f / 0.7f);                                                  // Subtle Tape (+ HF loss)
        case 5:  return fastTanh (x);                                                                         // Clean Tape (+ HF loss)
        case 6:  return 0.636619772f * std::atan (1.570796327f * x);                                          // Warm Tape (+ more HF loss)
        case 7:  return fastTanh (2.0f * x);                                                                  // Hot Tape (+ HF loss)
        case 8:  return x / (1.0f + 0.5f * std::fabs (x));                                                    // Subtle Saturation
        case 9:  return x / (1.0f + std::fabs (x));                                                           // Saturation: rational soft knee
        case 10: { const float c = std::clamp (x, -1.5f, 1.5f); return c - 0.148148148f * c * c * c; }        // Soft Clip: cubic
        case 11: { const float a = std::fabs (0.5f * x); float y;                                              // Overdrive: three-piece soft clip
                   if (a < 1.0f / 3.0f) y = 2.0f * a; else if (a < 2.0f / 3.0f) { const float t = 2.0f - 3.0f * a; y = (3.0f - t * t) / 3.0f; } else y = 1.0f;
                   return x < 0.0f ? -y : y; }
        case 12: return std::clamp (x, -1.0f, 1.0f);                                                          // Hard Clip
        case 13: return fastTanh (0.8f * x) * 1.25f;                                                          // Transformer Soft (core)
        case 14: return (fastTanh (x + 0.15f) - 0.148885f) / 0.97787f;                                        // Transformer Warm (core)
        case 15: { const float x2 = x * x; return x / std::sqrt (std::sqrt (1.0f + x2 * x2)); }              // Transformer Hard (core)
        case 16: return (fastTanh (1.5f * x + 0.05f) - 0.049958f) / 1.4963f;                                  // American Clean (power stage)
        case 17: return fastTanh (2.5f * fastTanh (2.0f * x));                                                // American Crunch: two stages
        case 18: { const float u = fastTanh (4.0f * x + 0.25f) - 0.244919f; return fastTanh (1.5f * u); }     // British Crunch: asymmetric first stage
        case 19: return fastTanh (3.0f * fastTanh (4.0f * fastTanh (5.0f * x + 0.1f)));                        // British Lead: three stages
        case 20: return x >= 0.0f ? fastTanh (6.0f * x) : 0.8f * fastTanh (3.0f * x);                        // Fuzz: very hard, asymmetric
        case 21: { float u = std::fmod (x + 1.0f, 4.0f); if (u < 0.0f) u += 4.0f; return u < 2.0f ? u - 1.0f : 3.0f - u; } // Foldback: triangle fold
        case 22: return std::sin (1.570796327f * x);                                                          // Sine Fold: smooth fold
        case 23: return x > 0.0f ? fastTanh (x) : 0.0f;                                                       // Half-wave rectifier (soft)
        case 24: return fastTanh (std::fabs (x));                                                             // Full-wave rectifier (soft)
        case 25: return std::clamp (std::round (x * 31.0f) * (1.0f / 31.0f), -1.0f, 1.0f);                    // Bit Crusher (illustration: 6 bit)
        case 26: return 0.8f * fastTanh (3.0f * x);                                                           // Breakdown (+ octave-down in Band::tick)
        case 27: return fastTanh (x);                                                                         // Smear (+ all-pass chain in Band::tick)
        default: return x;
    }
}

// level trim after the shaper of the styles whose cores have a lot of gain
inline float stylePost (int style)
{
    switch (style) { case 17: return 0.6f; case 18: return 0.7f; case 19: return 0.55f; case 20: return 0.7f; default: return 1.0f; }
}

// the HF roll-off (Hz) of the tape styles (after the shaper); 0 = none
inline float styleLowpassHz (int style) { return style == 4 ? 14000.0f : style == 5 ? 9000.0f : style == 6 ? 5000.0f : style == 7 ? 7500.0f : 0.0f; }

// ---- parameters ----------------------------------------------------------------------------------------------------
struct BandParams
{
    bool on = true, solo = false;
    int style = 0;
    float driveDb = 12.0f, levelDb = 0.0f, mix = 1.0f;   // mix 0..1
    float dyn = 0.0f, tone = 0.0f, fb = 0.0f;            // -1..1, -1..1, 0..1
    float fbHz = 1000.0f;
    float pan = 0.0f;                                    // -1..1 balance
};

struct GlobalParams
{
    int numBands = 3;
    int slope = 2;                                       // crossover slope: 0 = 6, 1 = 12, 2 = 24, 3 = 36, 4 = 48 dB/oct
    std::array<float, kMaxBands - 1> xoHz { 120.0f, 800.0f, 3000.0f, 7000.0f, 12000.0f };
    bool midSide = false;
    std::array<BandParams, kMaxBands> band;
};

// ---- crossover filters ----------------------------------------------------------------------------------------------
// Linkwitz-Riley crossovers built from Butterworth sections (TPT state-variable / one-pole):
//   6 dB/oct  : 1st order, complementary (lo + hi = input)
//   12 dB/oct : Butterworth-1 squared (HP inverted),  24: Butterworth-2 squared,  36: Butterworth-3 squared (HP inverted),  48: Butterworth-4 squared
// lo + hi is an all-pass, so bands that lie below a crossover are passed through a copy of it ("compensation") and the bands add up flat.
struct XoCoefs
{
    int n = 2;
    double sign = 1.0;
    bool one[4] = { false, false, false, false };
    double a1[4] = {}, a2[4] = {}, a3[4] = {}, k[4] = {}, G[4] = {};

    void set (int slope, double g)   // g = tan (pi * fc / fs)
    {
        static const double kBw3 = 1.0, kBw2 = 1.4142135623730951, kBw4a = 0.7653668647301796, kBw4b = 1.8477590650225735;
        switch (slope)
        {
            case 0:  n = 1; sign = 1.0;  one[0] = true; break;
            case 1:  n = 2; sign = -1.0; one[0] = one[1] = true; break;
            case 2:  n = 2; sign = 1.0;  one[0] = one[1] = false; k[0] = k[1] = kBw2; break;
            case 3:  n = 4; sign = -1.0; one[0] = one[2] = true; one[1] = one[3] = false; k[1] = k[3] = kBw3; break;
            default: n = 4; sign = 1.0;  for (int i = 0; i < 4; ++i) one[i] = false; k[0] = k[2] = kBw4a; k[1] = k[3] = kBw4b; break;
        }
        for (int i = 0; i < n; ++i)
        {
            if (one[i]) G[i] = g / (1.0 + g);
            else { a1[i] = 1.0 / (1.0 + g * (g + k[i])); a2[i] = g * a1[i]; a3[i] = g * a2[i]; }
        }
    }
};

struct XoState
{
    double lp[4][2] = {}, hp[4][2] = {};

    static inline double sec (double (&s)[2], const XoCoefs& c, int i, double x, bool wantHp)
    {
        if (c.one[i])
        {
            const double v = (x - s[0]) * c.G[i], lpv = v + s[0];
            s[0] = lpv + v;
            return wantHp ? x - lpv : lpv;
        }
        const double v3 = x - s[1], v1 = c.a1[i] * s[0] + c.a2[i] * v3, v2 = s[1] + c.a2[i] * s[0] + c.a3[i] * v3;
        s[0] = 2.0 * v1 - s[0]; s[1] = 2.0 * v2 - s[1];
        return wantHp ? x - c.k[i] * v1 - v2 : v2;
    }
    inline void process (const XoCoefs& c, double x, double& lo, double& hi)
    {
        double a = x, b = x;
        for (int i = 0; i < c.n; ++i) a = sec (lp[i], c, i, a, false);
        for (int i = 0; i < c.n; ++i) b = sec (hp[i], c, i, b, true);
        lo = a; hi = b * c.sign;
    }
};

// ---- the engine ----------------------------------------------------------------------------------------------------
class Engine
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        smoothC = 1.0 - std::exp (-1.0 / (0.012 * fs));          // parameter smoothing, 12 ms
        envA = (float) (1.0 - std::exp (-1.0 / (0.004 * fs)));   // detector attack 4 ms
        envR = (float) (1.0 - std::exp (-1.0 / (0.080 * fs)));   // release 80 ms
        dynC = (float) (1.0 - std::exp (-1.0 / (0.003 * fs)));
        toneA = (float) (1.0 - std::exp (-6.283185307 * 800.0 / fs));
        dcR = (float) (1.0 - 6.283185307 * 10.0 / fs);
        reset();
        for (int k = 0; k < kMaxBands - 1; ++k) xoSm[(size_t) k] = -1.0;
        for (auto& b : bands) b.styleCoefFor = -1;
    }

    void reset()
    {
        xo = {}; ap = {};
        for (auto& b : bands) b.resetState();
        snapNext = true;
    }

    // io: nch (1 or 2) channels of n samples at the prepared rate, processed in place
    void process (float* const* io, int nch, int n, const GlobalParams& p)
    {
        nch = std::clamp (nch, 1, 2);
        N = std::clamp (p.numBands, 1, kMaxBands);
        const int slope = std::clamp (p.slope, 0, 4);
        if (slope != curSlope || N != curN) { xo = {}; ap = {}; curSlope = slope; curN = N; }
        const bool ms = p.midSide && nch == 2;
        bool anySolo = false;
        for (int b = 0; b < N; ++b) anySolo = anySolo || p.band[(size_t) b].solo;

        for (int s0 = 0; s0 < n; s0 += kSub)
        {
            const int len = std::min (kSub, n - s0);
            updateControl (p, anySolo, len, slope);
            if (snapNext) { for (int b = 0; b < N; ++b) bands[(size_t) b].snap(); snapNext = false; }   // first block after reset: no glide from stale values
            for (int i = s0; i < s0 + len; ++i)
            {
                float in[2] = { io[0][i], nch > 1 ? io[1][i] : 0.0f };
                if (ms) { const float m = 0.5f * (in[0] + in[1]), s = 0.5f * (in[0] - in[1]); in[0] = m; in[1] = s; }

                float bandIn[kMaxBands][2];
                for (int c = 0; c < nch; ++c) split (c, in[c], bandIn, N, slope);

                float out[2] = { 0.0f, 0.0f };
                for (int b = 0; b < N; ++b)
                {
                    Band& bd = bands[(size_t) b];
                    bd.smooth ((float) smoothC);
                    // stereo-linked detector
                    float a = std::fabs (bandIn[b][0]);
                    if (nch > 1) a = std::max (a, std::fabs (bandIn[b][1]));
                    bd.env += (a - bd.env) * (a > bd.env ? envA : envR);
                    bd.dynGain += (bd.dynTarget - bd.dynGain) * dynC;
                    for (int c = 0; c < nch; ++c) out[c] += bd.tick (c, bandIn[b][c], toneA, dcR) * (c == 0 ? bd.gPanL : bd.gPanR);
                }
                if (ms) { const float m = out[0], s = out[1]; out[0] = m + s; out[1] = m - s; }
                io[0][i] = out[0];
                if (nch > 1) io[1][i] = out[1];
            }
        }
    }

    float bandLevel (int b) const { return bands[(size_t) b].env; }   // detector value, for the meters

    static constexpr int kSub = 32;                                   // control-rate block (coefficients, dynamics target)

private:
    void split (int c, float x, float (&out)[kMaxBands][2], int nb, int slope)
    {
        if (nb == 1) { out[0][c] = x; return; }
        double sig = x;
        for (int m = 0; m < nb - 1; ++m)
        {
            double lo, hi;
            xo[(size_t) c][(size_t) m].process (xoC[(size_t) m], sig, lo, hi);
            // the all-pass (lo + hi) of this crossover for every band below it (not needed for the 6 dB split: lo + hi == input)
            if (slope > 0)
                for (int j = 0; j < m; ++j)
                {
                    double l2, h2;
                    ap[(size_t) c][(size_t) m][(size_t) j].process (xoC[(size_t) m], (double) out[j][c], l2, h2);
                    out[j][c] = (float) (l2 + h2);
                }
            out[m][c] = (float) lo;
            sig = hi;
        }
        out[nb - 1][c] = (float) sig;
    }

    // ---- per-band processing ----
    struct Band
    {
        // smoothed values and their targets
        float drive = 4, driveT = 4, level = 1, levelT = 1, mix = 1, mixT = 1, gL = 1, gLT = 1, gH = 1, gHT = 1, fb = 0, fbT = 0, on = 1, onT = 1;
        float panL = 1, panR = 1, panLT = 1, panRT = 1, gPanL = 1, gPanR = 1;
        float env = 0, dynGain = 1, dynTarget = 1, dynAmt = 0;
        int style = 0; float tapeA = 1.0f;
        float fbG = 0.1f, fbA1 = 1, fbA2 = 0, fbA3 = 0;       // feedback band-pass coefficients (float SVF below)
        // per-style filter coefficients (recomputed when the style or the sample rate changes)
        int styleCoefFor = -1;
        float lfA = 0.01f, hpA = 0.01f, presA = 0.3f, m1A = 0.1f, m2A = 0.05f, cabA = 0.3f, cab2A = 0.3f, bdA = 0.03f, smC[4] = { 0, 0, 0, 0 };
        float crushLevels = 128.0f; int crushHold = 1;
        struct Ch
        {
            float toneLp = 0, tapeLp = 0, dcX = 0, dcY = 0, fbSig = 0, fbIc1 = 0, fbIc2 = 0;
            float lf = 0, hpLp = 0, m1 = 0, m2 = 0, cab = 0, cab2 = 0;            // transformer / amp filters
            float hold = 0; int holdCnt = 0;                                       // bit crusher
            float flip = 1.0f, subLp = 0, prevSub = 0, sub = 0;                    // breakdown
            float ap[4] = { 0, 0, 0, 0 };                                          // smear
        } ch[2];

        void resetState() { ch[0] = {}; ch[1] = {}; env = 0; dynGain = dynTarget = 1; }
        void snap() { drive = driveT; level = levelT; mix = mixT; gL = gLT; gH = gHT; fb = fbT; on = onT; dynGain = dynTarget; panL = panLT; panR = panRT; gPanL = panL; gPanR = panR; }

        inline void smooth (float c)
        {
            drive += (driveT - drive) * c; level += (levelT - level) * c; mix += (mixT - mix) * c;
            gL += (gLT - gL) * c; gH += (gHT - gH) * c; fb += (fbT - fb) * c; on += (onT - on) * c;
            panL += (panLT - panL) * c; panR += (panRT - panR) * c;
            gPanL = panL; gPanR = panR;
        }

        static inline float onePole (float& st, float x, float a) { st += (x - st) * a; return st; }

        inline float tick (int c, float x, float toneA, float dcR)
        {
            Ch& s = ch[c];
            // tone: tilt around 800 Hz
            s.toneLp += (x - s.toneLp) * toneA;
            const float toned = s.toneLp * gL + (x - s.toneLp) * gH;
            // feedback only lives while there is signal (so the loop cannot drone on silence)
            const float gate = std::min (1.0f, env * 200.0f);
            const float base = toned * dynGain;
            const float fbv = fb * gate * s.fbSig;
            float y;
            switch (style)
            {
                case 13: case 14: case 15:        // transformers: the low end saturates first (iron core), the highs stay clean
                {
                    const float v = base * drive + fbv;
                    const float lfv = onePole (s.lf, v, lfA);
                    constexpr float kLow = 2.5f;
                    y = shape (style, v - lfv) + shape (style, lfv * kLow) * (1.0f / kLow);
                    break;
                }
                case 16: case 17: case 18: case 19: case 20:    // guitar amps: tight low end, mid voicing, power stage, cab roll-off
                {
                    float a = base * drive + fbv;
                    a -= onePole (s.hpLp, a, hpA);
                    if (style == 16) a += 0.3f * (a - onePole (s.m1, a, presA));                                  // presence
                    else if (style == 17) a -= 0.35f * (onePole (s.m1, a, m1A) - onePole (s.m2, a, m2A));         // scooped mids
                    else if (style == 18 || style == 19) a += 0.5f * (onePole (s.m1, a, m1A) - onePole (s.m2, a, m2A));   // mid hump
                    y = shape (style, a) * stylePost (style);
                    y = onePole (s.cab, y, cabA);
                    if (style != 16) y = onePole (s.cab2, y, cab2A);                                              // steeper cab
                    break;
                }
                case 25:                          // bit crusher: Drive = crush amount (bits and sample-rate reduction), no extra gain
                {
                    if (s.holdCnt <= 0)
                    {
                        const float v = std::clamp (base + fbv, -1.0f, 1.0f);
                        s.hold = std::round (v * crushLevels) * (1.0f / crushLevels);
                        s.holdCnt = crushHold;
                    }
                    --s.holdCnt;
                    y = s.hold;
                    break;
                }
                case 26:                          // breakdown: heavy clip + an octave-down square (flip-flop on the zero crossings of the low-passed input)
                {
                    const float v = base * drive + fbv;
                    s.prevSub = s.subLp;
                    onePole (s.subLp, v, bdA);
                    if (s.prevSub < 0.0f && s.subLp >= 0.0f) s.flip = -s.flip;
                    const float sq = s.flip * std::clamp (s.subLp * 12.0f, -1.0f, 1.0f);
                    s.sub += (sq - s.sub) * 0.15f;                                                                 // soften the square
                    y = 0.6f * shape (style, v) + 0.55f * s.sub;
                    break;
                }
                case 27:                          // smear: a chain of all-passes (smears the transients in time) into a soft clipper
                {
                    float a = base * drive + fbv;
                    for (int i = 0; i < 4; ++i) { const float t = smC[i] * a + s.ap[i]; s.ap[i] = a - smC[i] * t; a = t; }
                    y = shape (style, a);
                    break;
                }
                default:
                    y = shape (style, base * drive + fbv);
                    break;
            }
            s.tapeLp += (y - s.tapeLp) * tapeA;            // tapeA == 1 -> bypass
            y = s.tapeLp;
            // feedback tap: unity-peak band-pass of the shaped signal, read on the next sample
            {
                const float v3 = y - s.fbIc2;
                const float v1 = fbA1 * s.fbIc1 + fbA2 * v3;
                const float v2 = s.fbIc2 + fbA2 * s.fbIc1 + fbA3 * v3;
                s.fbIc1 = 2.0f * v1 - s.fbIc1; s.fbIc2 = 2.0f * v2 - s.fbIc2;
                s.fbSig = fbG * v1;                        // fbG = 1/Q -> unity peak gain
            }
            // DC blocker (asymmetric shapers and rectifiers create DC)
            const float dc = y - s.dcX + dcR * s.dcY;
            s.dcX = y; s.dcY = dc;
            const float wet = style == 25 ? y : dc;      // the crusher is symmetric: keep its staircase exact
            return ((x * (1.0f - mix) + wet * mix)) * level * on;
        }
    };

    static float hzA (double hz, double fs) { return (float) (1.0 - std::exp (-6.283185307 * std::min (hz, fs * 0.45) / fs)); }

    void styleCoefs (Band& bd, int style) const
    {
        bd.lfA = hzA (160.0, fs);
        bd.hpA = hzA (80.0, fs);
        bd.presA = hzA (3200.0, fs);
        bd.m1A = hzA (style == 17 ? 900.0 : 1800.0, fs);
        bd.m2A = hzA (style == 17 ? 250.0 : 500.0, fs);
        bd.cabA = hzA (style == 16 ? 9000.0 : 5600.0, fs);
        bd.cab2A = hzA (style == 19 ? 4200.0 : 6500.0, fs);
        bd.bdA = hzA (260.0, fs);
        static const double smHz[4] = { 180.0, 650.0, 2300.0, 7000.0 };
        for (int i = 0; i < 4; ++i)
        {
            const double t = std::tan (3.14159265358979323846 * std::min (smHz[i], fs * 0.45) / fs);
            bd.smC[i] = (float) ((t - 1.0) / (t + 1.0));
        }
        bd.styleCoefFor = style;
    }

    void updateControl (const GlobalParams& p, bool anySolo, int len, int slope)
    {
        // crossover frequencies: ordered, smoothed once per control block
        for (int k = 0; k < kMaxBands - 1; ++k)
        {
            float t = std::clamp (p.xoHz[(size_t) k], 20.0f, 20000.0f);
            if (k > 0) t = std::max (t, xoOrd[(size_t) k - 1] * 1.12f);
            xoOrd[(size_t) k] = t;
            const double nyq = fs * 0.45;
            const double target = std::min ((double) t, nyq);
            if (xoSm[(size_t) k] < 0.0) xoSm[(size_t) k] = target;
            else xoSm[(size_t) k] += (target - xoSm[(size_t) k]) * (1.0 - std::exp (-(double) len / (0.02 * fs)));
            xoC[(size_t) k].set (slope, std::tan (3.14159265358979323846 * xoSm[(size_t) k] / fs));
        }

        for (int b = 0; b < N; ++b)
        {
            const BandParams& bp = p.band[(size_t) b];
            Band& bd = bands[(size_t) b];
            bd.style = std::clamp (bp.style, 0, kNumStyles - 1);
            if (bd.styleCoefFor != bd.style) styleCoefs (bd, bd.style);
            bd.driveT = bd.style == 25 ? 1.0f : std::pow (10.0f, bp.driveDb * (1.0f / 20.0f));
            if (bd.style == 25)    // crusher: Drive 0..60 dB -> 16..3 bits, and a sample-rate reduction that grows with the square of it
            {
                const float k = std::clamp (bp.driveDb * (1.0f / 60.0f), 0.0f, 1.0f);
                bd.crushLevels = std::exp2 (std::round (16.0f - 13.0f * k) - 1.0f);
                bd.crushHold = std::max (1, (int) std::lround (k * k * 36.0 * fs / 48000.0));
            }
            bd.levelT = std::pow (10.0f, bp.levelDb * (1.0f / 20.0f));
            bd.mixT = std::clamp (bp.mix, 0.0f, 1.0f);
            bd.gHT = std::pow (10.0f, bp.tone * (9.0f / 20.0f));
            bd.gLT = 1.0f / bd.gHT;
            bd.fbT = std::clamp (bp.fb, 0.0f, 1.0f) * 0.92f;
            const bool audible = bp.on && (!anySolo || bp.solo);
            bd.onT = audible ? 1.0f : 0.0f;
            const float pn = std::clamp (bp.pan, -1.0f, 1.0f);
            bd.panLT = pn > 0.0f ? 1.0f - pn : 1.0f;
            bd.panRT = pn < 0.0f ? 1.0f + pn : 1.0f;
            const float lpHz = styleLowpassHz (bd.style);
            bd.tapeA = lpHz > 0.0f ? hzA (lpHz, fs) : 1.0f;
            // feedback filter: Q = 3
            const double fg = std::tan (3.14159265358979323846 * std::min ((double) std::clamp (bp.fbHz, 20.0f, 20000.0f), fs * 0.45) / fs);
            const double kq = 1.0 / 3.0;
            bd.fbG = (float) kq;
            const double a1 = 1.0 / (1.0 + fg * (fg + kq));
            bd.fbA1 = (float) a1; bd.fbA2 = (float) (fg * a1); bd.fbA3 = (float) (fg * fg * a1);
            // dynamics: negative = compress (quiet gets louder, loud gets quieter), positive = expand; pivot -18 dB
            const float envDb = 20.0f * std::log10 (bd.env + 1.0e-5f);
            const float gdb = std::clamp (bp.dyn * 0.75f * (envDb + 18.0f), -24.0f, 24.0f);
            bd.dynTarget = std::pow (10.0f, gdb * (1.0f / 20.0f));
        }
    }

    double fs = 48000.0, smoothC = 0.001;
    float envA = 0.01f, envR = 0.001f, dynC = 0.01f, toneA = 0.1f, dcR = 0.999f;
    int N = 1, curSlope = -1, curN = -1;
    bool snapNext = true;
    std::array<XoCoefs, kMaxBands - 1> xoC {};
    std::array<double, kMaxBands - 1> xoSm {};
    std::array<float, kMaxBands - 1> xoOrd {};
    std::array<std::array<XoState, kMaxBands - 1>, 2> xo {};
    std::array<std::array<std::array<XoState, kMaxBands - 1>, kMaxBands - 1>, 2> ap {};
    std::array<Band, kMaxBands> bands {};
};
} // namespace ember
