// Transmuter.h - the "variation engine" of Polymer.
//
// A variation is described by a tiny DNA: (seed, chaos). Given the parent sound, the DNA picks a
// random chain of 1-4 transformations (varispeed pitch, granular stretch, Paulstretch smear,
// slice scramble, reverse, spectral warp, filter sweep, resonator, bit crush, drive, envelope,
// reverb) and sets how strong each one is. "Chaos" (0..1) decides how many and how extreme.
// The result is always loudness-matched to the original, de-clicked and length-limited.
//
// Everything is deterministic (own PRNG), so a variation can be re-created from
// (parent, seed, chaos) - this is how the plug-in restores its cloud when a project is reloaded.
#pragma once

#include "Dsp.h"

namespace poly
{

constexpr double kMaxSeconds = 8.0;     // longest sound Polymer keeps (source and variations)

struct Audio
{
    std::vector<float> ch[2];
    double sr = 44100.0;

    int size() const { return (int) ch[0].size(); }
    void resize (int n) { ch[0].assign ((size_t) n, 0.0f); ch[1].assign ((size_t) n, 0.0f); }
    int maxLen() const { return (int) (kMaxSeconds * sr); }
};

struct Dna
{
    uint32_t seed = 1;
    float chaos = 0.5f;      // 0..1
};

namespace detail
{

inline float hermite (const std::vector<float>& x, double pos)
{
    const int n = (int) x.size();
    const int i = (int) std::floor (pos);
    const float f = (float) (pos - i);
    auto at = [&] (int k) { return (k < 0 || k >= n) ? 0.0f : x[(size_t) k]; };
    const float y0 = at (i - 1), y1 = at (i), y2 = at (i + 1), y3 = at (i + 2);
    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * f + c2) * f + c1) * f + y1;
}

// ratio > 1 -> faster / higher / shorter
inline std::vector<float> resample (const std::vector<float>& in, double ratio)
{
    const int n = (int) in.size();
    if (n < 2 || ratio <= 0.0) return in;
    const int m = std::max (1, (int) std::floor ((n - 1) / ratio) + 1);
    std::vector<float> out ((size_t) m);
    for (int i = 0; i < m; ++i) out[(size_t) i] = hermite (in, i * ratio);
    return out;
}

inline std::vector<float> sineWindow (int N)
{
    std::vector<float> w ((size_t) N);
    for (int i = 0; i < N; ++i) w[(size_t) i] = (float) std::sin (kPi * (i + 0.5) / N);
    return w;
}

inline std::vector<float> hannWindow (int N)
{
    std::vector<float> w ((size_t) N);
    for (int i = 0; i < N; ++i) w[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * kPi * (i + 0.5) / N));
    return w;
}

inline void extend (Audio& a, int newSize)
{
    for (auto& c : a.ch) c.resize ((size_t) newSize, 0.0f);
}

inline float lerp (float a, float b, float t) { return a + (b - a) * t; }

// ------------------------------------------------------------------------------------------
// Operations. Each takes amt in 0..1 (how strong) and draws its own random parameters.
// ------------------------------------------------------------------------------------------

// Varispeed pitch (changes length too, like a sampler).
inline void opPitch (Audio& a, Rng& r, float amt)
{
    const float semis = r.sign() * (0.4f + 0.6f * r.uni()) * std::pow (amt, 1.4f) * 14.0f;
    const double ratio = std::pow (2.0, semis / 12.0);
    for (auto& c : a.ch) c = resample (c, ratio);
}

// Granular time-stretch (pitch preserving, with jittered grains -> texture).
inline void opStretch (Audio& a, Rng& r, float amt)
{
    const int n = a.size();
    if (n < 2048) return;
    const double f = std::exp (r.sign() * (0.35f + 0.65f * r.uni()) * amt * std::log (2.6f));
    const int G = std::max (256, (int) (a.sr * 0.07) & ~1);
    const int Hs = G / 4;
    const int outLen = std::min ((int) (n * f), a.maxLen());
    if (outLen < G) return;
    const auto w = hannWindow (G);

    std::vector<float> jit;
    for (int o = 0; o < outLen; o += Hs) jit.push_back ((r.uni() - 0.5f) * 0.3f * (float) G * amt);

    Audio out; out.sr = a.sr; out.resize (outLen);
    for (int c = 0; c < 2; ++c)
    {
        double inPos = 0.0;
        size_t gi = 0;
        for (int o = 0; o < outLen; o += Hs, ++gi)
        {
            const int start = std::clamp ((int) (inPos + jit[gi]), 0, n - 1);
            for (int i = 0; i < G && o + i < outLen; ++i)
            {
                const int idx = start + i;
                if (idx >= n) break;
                out.ch[c][(size_t) (o + i)] += a.ch[c][(size_t) idx] * w[(size_t) i] * 0.5f;
            }
            inPos += Hs / f;
        }
    }
    a = std::move (out);
}

// Paulstretch: random-phase spectral stretch -> lush, washed-out version of the sound.
inline void opSmear (Audio& a, Rng& r, float amt)
{
    const int n = a.size();
    if (n < 4096) return;
    const double f = 1.6 + amt * r.range (1.0f, 4.0f);
    int N = a.sr >= 32000.0 ? 16384 : 8192;
    while (N > n) N >>= 1;
    const int hop = N / 2;
    const int outLen = std::min ((int) (n * f) + N, a.maxLen());

    FFT fft; fft.init (N);
    const auto win = sineWindow (N);
    std::vector<float> re ((size_t) N), im ((size_t) N);

    Audio out; out.sr = a.sr; out.resize (outLen);
    for (int c = 0; c < 2; ++c)
    {
        Rng rc (r.next());
        for (int o = 0, k = 0; o < outLen; o += hop, ++k)
        {
            const int start = (int) ((double) k * hop / f);
            if (start >= n) break;
            for (int i = 0; i < N; ++i)
            {
                const int idx = start + i;
                re[(size_t) i] = idx < n ? a.ch[c][(size_t) idx] * win[(size_t) i] : 0.0f;
                im[(size_t) i] = 0.0f;
            }
            fft.forward (re.data(), im.data());
            for (int b = 1; b < N / 2; ++b)
            {
                const float mag = std::hypot (re[(size_t) b], im[(size_t) b]);
                const float ph = rc.uni() * (float) (2.0 * kPi);
                re[(size_t) b] = mag * std::cos (ph);       im[(size_t) b] = mag * std::sin (ph);
                re[(size_t) (N - b)] = re[(size_t) b];      im[(size_t) (N - b)] = -im[(size_t) b];
            }
            re[0] = std::fabs (re[0]);                       im[0] = 0.0f;
            re[(size_t) N / 2] = std::fabs (re[(size_t) N / 2]); im[(size_t) N / 2] = 0.0f;
            fft.inverse (re.data(), im.data());
            for (int i = 0; i < N && o + i < outLen; ++i)
                out.ch[c][(size_t) (o + i)] += re[(size_t) i] * win[(size_t) i];
        }
    }
    a = std::move (out);
}

// Nearest upward zero crossing of the (mono) signal around `pos` (search +-range) -> click-free joins.
inline int snapToZero (const Audio& a, int pos, int range)
{
    const int n = a.size();
    pos = std::clamp (pos, 1, n - 2);
    int best = pos; int bestD = 1 << 30;
    const int lo = std::max (1, pos - range), hi = std::min (n - 2, pos + range);
    for (int i = lo; i <= hi; ++i)
    {
        const float m0 = a.ch[0][(size_t) i - 1] + a.ch[1][(size_t) i - 1], m1 = a.ch[0][(size_t) i] + a.ch[1][(size_t) i];
        if (m0 <= 0.0f && m1 > 0.0f && std::abs (i - pos) < bestD) { bestD = std::abs (i - pos); best = i; }
    }
    return best;
}

// Cut into slices (on zero crossings), shuffle / repeat / reverse some of them.
inline void opScramble (Audio& a, Rng& r, float amt)
{
    const int n = a.size();
    if (n < 4096) return;
    const int K = 4 + r.below ((int) (4 + amt * 20));
    const int L = n / K;
    if (L < 512) return;
    const int fade = std::min (L / 8, std::max (16, (int) (a.sr * 0.0007)));

    std::vector<int> bnd ((size_t) K + 1);
    bnd[0] = 0; bnd[(size_t) K] = n;
    for (int j = 1; j < K; ++j) bnd[(size_t) j] = snapToZero (a, j * L, L / 4);

    struct Piece { int j; bool rev; bool half; };
    std::vector<Piece> pieces;
    for (int j = 0; j < K; ++j)
    {
        Piece p { r.chance (0.2f + 0.7f * amt) ? r.below (K) : j, r.chance (amt * 0.35f), r.chance (amt * 0.25f) };
        pieces.push_back (p);
    }
    auto continuous = [&] (size_t i) {
        return i > 0 && pieces[i].j == pieces[i - 1].j + 1 && ! pieces[i].rev && ! pieces[i - 1].rev && ! pieces[i].half && ! pieces[i - 1].half;
    };

    Audio out; out.sr = a.sr; out.resize (0);
    std::vector<int> starts;
    for (size_t i = 0; i < pieces.size(); ++i)
    {
        const int b0 = bnd[(size_t) pieces[i].j], b1 = bnd[(size_t) pieces[i].j + 1];
        int len = b1 - b0;
        if (pieces[i].half) len = std::max (1, len / 2);
        starts.push_back (out.size());
        const bool fi = ! continuous (i), fo = (i + 1 < pieces.size()) ? ! continuous (i + 1) : false;
        for (int c = 0; c < 2; ++c)
            for (int t = 0; t < len; ++t)
            {
                const int srcIdx = pieces[i].rev ? (b1 - 1 - t) : (b0 + t);
                float g = 1.0f;
                if (fi && t < fade) g = std::min (g, (float) t / fade);
                if (fo && t >= len - fade) g = std::min (g, (float) (len - 1 - t) / fade);
                out.ch[c].push_back (a.ch[c][(size_t) srcIdx] * g);
            }
        if (out.size() > a.maxLen()) break;
    }
    if (out.size() > 64) a = std::move (out);
}

inline void opReverse (Audio& a, Rng& r, float)
{
    const int n = a.size();
    if (n < 512) return;
    int from = 0;
    if (r.uni() > 0.55f) from = snapToZero (a, (int) (n * r.range (0.2f, 0.55f)), (int) (a.sr * 0.05));
    const int fade = std::max (8, (int) (a.sr * 0.003));
    for (auto& c : a.ch)
    {
        std::reverse (c.begin() + from, c.end());
        for (int i = 0; i < fade && from + i < n; ++i) c[(size_t) (from + i)] *= (float) i / fade;
        for (int i = 1; i <= fade && from - i >= 0; ++i) c[(size_t) (from - i)] *= (float) i / fade;
    }
}

// STFT spectral warp: stretches/squeezes the spectrum, blurs it and (partly) randomises phases.
inline void opWarp (Audio& a, Rng& r, float amt)
{
    const int n = a.size();
    if (n < 4096) return;
    const int N = 2048, H = 512;
    const float w = std::exp (r.sign() * (0.1f + 0.9f * r.uni()) * amt * 0.42f);
    const int blur = (int) std::round (amt * r.range (2.0f, 10.0f));
    const float pr = std::pow (amt, 0.9f) * r.range (0.4f, 1.0f);

    FFT fft; fft.init (N);
    const auto win = sineWindow (N);
    std::vector<float> re ((size_t) N), im ((size_t) N), mag ((size_t) N / 2 + 1), mag2 ((size_t) N / 2 + 1), ph ((size_t) N / 2 + 1);

    for (auto& chn : a.ch)
    {
        Rng rc (r.next());
        std::vector<float> out ((size_t) n, 0.0f);
        for (int pos = -(N - H); pos < n; pos += H)
        {
            for (int i = 0; i < N; ++i)
            {
                const int idx = pos + i;
                re[(size_t) i] = (idx >= 0 && idx < n) ? chn[(size_t) idx] * win[(size_t) i] : 0.0f;
                im[(size_t) i] = 0.0f;
            }
            fft.forward (re.data(), im.data());
            for (int k = 0; k <= N / 2; ++k)
            {
                mag[(size_t) k] = std::hypot (re[(size_t) k], im[(size_t) k]);
                ph[(size_t) k]  = std::atan2 (im[(size_t) k], re[(size_t) k]);
            }
            for (int k = 0; k <= N / 2; ++k)
            {
                const float s = (float) k / w;
                const int i0 = (int) s;
                const float fr = s - (float) i0;
                const float m0 = i0 <= N / 2 ? mag[(size_t) i0] : 0.0f;
                const float m1 = i0 + 1 <= N / 2 ? mag[(size_t) (i0 + 1)] : 0.0f;
                mag2[(size_t) k] = m0 + (m1 - m0) * fr;
            }
            if (blur > 0)   // box blur of the magnitudes
            {
                std::vector<float> tmp (mag2);
                for (int k = 0; k <= N / 2; ++k)
                {
                    float acc = 0.0f; int cnt = 0;
                    for (int d = -blur; d <= blur; ++d)
                    {
                        const int kk = k + d;
                        if (kk >= 0 && kk <= N / 2) { acc += tmp[(size_t) kk]; ++cnt; }
                    }
                    mag2[(size_t) k] = acc / (float) cnt;
                }
            }
            for (int k = 0; k <= N / 2; ++k)
            {
                const float p = ph[(size_t) k] + pr * (rc.uni() * 2.0f - 1.0f) * (float) kPi;
                const bool edge = (k == 0 || k == N / 2);
                const float c = edge ? (std::cos (ph[(size_t) k]) >= 0 ? 1.0f : -1.0f) : std::cos (p);
                const float s = edge ? 0.0f : std::sin (p);
                re[(size_t) k] = mag2[(size_t) k] * c;  im[(size_t) k] = mag2[(size_t) k] * s;
                if (! edge) { re[(size_t) (N - k)] = re[(size_t) k]; im[(size_t) (N - k)] = -im[(size_t) k]; }
            }
            fft.inverse (re.data(), im.data());
            for (int i = 0; i < N; ++i)
            {
                const int idx = pos + i;
                if (idx >= 0 && idx < n) out[(size_t) idx] += re[(size_t) i] * win[(size_t) i] * 0.5f;
            }
        }
        chn = std::move (out);
    }
}

// Zavalishin TPT state-variable filter
struct Svf
{
    float ic1 = 0.0f, ic2 = 0.0f;
    inline void process (float x, float g, float k, float& lp, float& bp, float& hp)
    {
        const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;  ic2 = 2.0f * v2 - ic2;
        lp = v2; bp = v1; hp = x - k * v1 - v2;
    }
};

// Low-pass / high-pass / band-pass with a sweeping or wobbling cutoff.
inline void opFilter (Audio& a, Rng& r, float amt)
{
    const int n = a.size();
    if (n < 64) return;
    const int mode = r.below (3);          // 0 LP, 1 HP, 2 BP
    const float Q = 0.7f + amt * r.range (0.0f, 6.5f);
    const float k = 1.0f / Q;
    float f0, f1;
    if (mode == 0)       { f0 = std::exp (lerp (std::log (16000.0f), std::log (r.range (250.0f, 2500.0f)), std::pow (amt, 0.8f))); }
    else if (mode == 1)  { f0 = std::exp (lerp (std::log (30.0f), std::log (r.range (300.0f, 2500.0f)), amt)); }
    else                 { f0 = r.range (300.0f, 3500.0f); }
    const float octs = r.sign() * amt * r.range (0.5f, 2.5f);
    f1 = f0 * std::pow (2.0f, octs);
    const bool wobble = r.chance (0.35f);
    const float rate = r.range (0.3f, 5.0f), phase = r.range (0.0f, 6.28f);
    const bool pluck = ! wobble && r.chance (0.5f);
    const float mix = mode == 2 ? 0.4f + 0.6f * amt : 1.0f;
    const float nyq = (float) (a.sr * 0.45);

    for (auto& chn : a.ch)
    {
        Svf s;
        for (int i = 0; i < n; ++i)
        {
            const float t = (float) i / (float) n;
            float lc;
            if (wobble)      lc = std::log (f0) + 0.5f * octs * 0.693f * std::sin (6.2832f * rate * (float) i / (float) a.sr + phase);
            else if (pluck)  lc = std::log (f0) + (std::log (f1) - std::log (f0)) * (1.0f - std::exp (-t * 6.0f));
            else             lc = std::log (f0) + (std::log (f1) - std::log (f0)) * t;
            const float fc = std::clamp (std::exp (lc), 20.0f, nyq);
            const float g = std::tan ((float) kPi * fc / (float) a.sr);
            float lp, bp, hp;
            const float x = chn[(size_t) i];
            s.process (x, g, k, lp, bp, hp);
            const float y = mode == 0 ? lp : mode == 1 ? hp : bp * k * 1.5f;
            chn[(size_t) i] = x * (1.0f - mix) + y * mix;
        }
    }
}

// Feedback comb = tuned resonator, adds a ringing pitch.
inline void opComb (Audio& a, Rng& r, float amt)
{
    const float f0 = std::exp (lerp (std::log (90.0f), std::log (1400.0f), r.uni()));
    const float fb = 0.55f + 0.39f * amt;
    const float mix = 0.35f + 0.4f * amt;
    const int tail = std::min ((int) (1.2 * a.sr), (int) (a.sr * 0.25 + 6.9 * (a.sr / f0) / -std::log (fb)));
    extend (a, std::min (a.size() + tail, a.maxLen()));
    const int n = a.size();
    for (int c = 0; c < 2; ++c)
    {
        const double D = a.sr / (f0 * (1.0f + (c ? 0.004f : -0.004f)));
        std::vector<float> y ((size_t) n, 0.0f);
        const std::vector<float>& x = a.ch[c];
        for (int i = 0; i < n; ++i)
        {
            const double p = i - D;
            float d = 0.0f;
            if (p >= 0.0)
            {
                const int i0 = (int) p; const float fr = (float) (p - i0);
                d = y[(size_t) i0] * (1.0f - fr) + y[(size_t) std::min (i0 + 1, i)] * fr;
            }
            y[(size_t) i] = x[(size_t) i] + fb * d;
        }
        for (int i = 0; i < n; ++i)
            a.ch[c][(size_t) i] = x[(size_t) i] * (1.0f - mix) + y[(size_t) i] * mix * (1.0f - fb) * 1.6f;
    }
}

inline void opCrush (Audio& a, Rng& r, float amt)
{
    const int bits = (int) std::round (lerp (14.0f, r.range (3.0f, 6.0f), amt));
    const int hold = 1 + (int) (amt * r.range (2.0f, 14.0f));
    const float levels = std::pow (2.0f, (float) (bits - 1));
    const float wet = 0.5f + 0.5f * amt;
    for (auto& chn : a.ch)
    {
        float held = 0.0f;
        for (size_t i = 0; i < chn.size(); ++i)
        {
            if ((int) (i % (size_t) hold) == 0) held = std::round (chn[i] * levels) / levels;
            chn[i] = chn[i] * (1.0f - wet) + held * wet;
        }
    }
}

inline void opDrive (Audio& a, Rng& r, float amt)
{
    const float drive = 1.0f + amt * r.range (5.0f, 14.0f);
    const float bias = amt * r.range (0.0f, 0.35f);
    const bool fold = amt > 0.6f && r.chance (0.35f);
    const float wet = 0.4f + 0.6f * amt;
    const float norm = 1.0f / std::tanh (drive * 0.7f);
    for (auto& chn : a.ch)
        for (auto& v : chn)
        {
            const float y = fold ? std::sin (0.785f * drive * v)
                                 : (std::tanh (drive * v + bias) - std::tanh (bias)) * norm;
            v = v * (1.0f - wet) + y * wet;
        }
}

inline void opEnvelope (Audio& a, Rng& r, float amt)
{
    const int n = a.size();
    if (n < 256) return;
    const int mode = r.below (4);
    const float T = (float) (n / a.sr);
    std::vector<float> env ((size_t) n, 1.0f);
    if (mode == 0)          // soft attack
    {
        const float ta = std::max (0.01f, amt * r.range (0.03f, 0.35f));
        for (int i = 0; i < n; ++i) env[(size_t) i] = std::pow (std::min (1.0f, (float) (i / a.sr) / ta), 1.5f);
    }
    else if (mode == 1)     // pluck / shorten
    {
        const float tau = std::max (0.05f, r.range (0.12f, 0.9f) * (1.3f - amt * 0.9f));
        for (int i = 0; i < n; ++i) env[(size_t) i] = std::exp (-(float) (i / a.sr) / tau);
    }
    else if (mode == 2)     // rhythmic gate
    {
        static const int steps[4] = { 8, 12, 16, 24 };
        const int S = steps[r.below (4)];
        std::vector<char> on ((size_t) S, 1);
        for (int s = 1; s < S; ++s) on[(size_t) s] = r.chance (1.0f - amt * 0.6f);
        const float coef = std::exp (-1.0f / (float) (0.014 * a.sr));
        float z = 1.0f;
        for (int i = 0; i < n; ++i)
        {
            const int s = std::min (S - 1, (int) ((long long) i * S / n));
            const float tgt = on[(size_t) s] ? 1.0f : 1.0f - std::min (1.0f, amt * 1.1f);
            z = tgt + (z - tgt) * coef;
            env[(size_t) i] = z;
        }
    }
    else                    // swell
    {
        const float p = 1.0f + amt * 2.0f;
        for (int i = 0; i < n; ++i)
        {
            const float t = (float) i / (float) n;
            env[(size_t) i] = std::pow (t, p) * (t > 0.92f ? (1.0f - t) / 0.08f : 1.0f);
        }
    }
    (void) T;
    for (auto& chn : a.ch)
        for (int i = 0; i < n; ++i) chn[(size_t) i] *= env[(size_t) i];
}

// Small Schroeder reverb (4 damped combs + 2 allpasses per channel, stereo spread).
inline void opReverb (Audio& a, Rng& r, float amt)
{
    const float rt60 = r.range (0.5f, 0.9f + 3.0f * amt);
    const float mix = 0.18f + 0.5f * amt;
    const float damp = r.range (0.15f, 0.55f);
    const float scale = r.range (0.7f, 1.8f);
    const int tail = std::min ((int) (rt60 * a.sr * 0.8), (int) (3.5 * a.sr));
    extend (a, std::min (a.size() + tail, a.maxLen()));
    const int n = a.size();
    static const float combMs[4] = { 29.7f, 37.1f, 41.1f, 43.7f };
    static const float apMs[2] = { 5.0f, 1.7f };

    for (int c = 0; c < 2; ++c)
    {
        const float spread = c ? 1.053f : 1.0f;
        int cd[4]; float fb[4]; std::vector<float> cb[4]; float cf[4] = {}; int ci[4] = {};
        for (int j = 0; j < 4; ++j)
        {
            cd[j] = std::max (8, (int) (combMs[j] * scale * spread * 0.001f * (float) a.sr));
            fb[j] = std::pow (10.0f, -3.0f * (float) cd[j] / (rt60 * (float) a.sr));
            cb[j].assign ((size_t) cd[j], 0.0f);
        }
        int ad[2]; std::vector<float> ab[2]; int ai[2] = {};
        for (int j = 0; j < 2; ++j) { ad[j] = std::max (4, (int) (apMs[j] * spread * 0.001f * (float) a.sr)); ab[j].assign ((size_t) ad[j], 0.0f); }

        for (int i = 0; i < n; ++i)
        {
            const float x = a.ch[c][(size_t) i];
            float acc = 0.0f;
            for (int j = 0; j < 4; ++j)
            {
                const float y = cb[j][(size_t) ci[j]];
                cf[j] = y * (1.0f - damp) + cf[j] * damp;
                cb[j][(size_t) ci[j]] = x + cf[j] * fb[j];
                if (++ci[j] >= cd[j]) ci[j] = 0;
                acc += y;
            }
            float w = acc * 0.25f;
            for (int j = 0; j < 2; ++j)
            {
                const float bo = ab[j][(size_t) ai[j]];
                const float v = w + 0.5f * bo;
                ab[j][(size_t) ai[j]] = v;
                w = bo - 0.5f * v;
                if (++ai[j] >= ad[j]) ai[j] = 0;
            }
            a.ch[c][(size_t) i] = x * (1.0f - mix * 0.5f) + w * mix * 1.6f;
        }
    }
}

} // namespace detail

// ------------------------------------------------------------------------------------------
// Analysis helpers
// ------------------------------------------------------------------------------------------

// RMS over the "active" part of the sound (frames within 40 dB of the loudest frame).
inline float activeRms (const Audio& a)
{
    const int n = a.size();
    if (n == 0) return 0.0f;
    const int fr = std::max (64, (int) (a.sr * 0.02));
    std::vector<double> p;
    double pk = 0.0;
    for (int s = 0; s < n; s += fr)
    {
        const int e = std::min (n, s + fr);
        double acc = 0.0;
        for (int i = s; i < e; ++i) acc += 0.5 * ((double) a.ch[0][(size_t) i] * a.ch[0][(size_t) i] + (double) a.ch[1][(size_t) i] * a.ch[1][(size_t) i]);
        acc /= (e - s);
        p.push_back (acc);
        pk = std::max (pk, acc);
    }
    if (pk <= 0.0) return 0.0f;
    double sum = 0.0; int cnt = 0;
    for (double v : p) if (v >= pk * 1e-4) { sum += v; ++cnt; }
    return (float) std::sqrt (sum / std::max (1, cnt));
}

struct Features
{
    float band[16] = {};
    float env[32] = {};
    float logLen = 0.0f;      // log2(seconds)
    float centroid = 1000.0f; // Hz
};

inline Features analyse (const Audio& a)
{
    Features f;
    const int n = a.size();
    f.logLen = std::log2 (std::max (0.01f, (float) (n / a.sr)));
    if (n < 64) return f;

    // envelope on 32 equal segments, dB re max
    float mx = -200.0f;
    for (int s = 0; s < 32; ++s)
    {
        const int b = (int) ((long long) n * s / 32), e = (int) ((long long) n * (s + 1) / 32);
        double acc = 0.0;
        for (int i = b; i < e; ++i) acc += 0.5 * ((double) a.ch[0][(size_t) i] * a.ch[0][(size_t) i] + (double) a.ch[1][(size_t) i] * a.ch[1][(size_t) i]);
        f.env[s] = 10.0f * std::log10 ((float) (acc / std::max (1, e - b)) + 1e-10f);
        mx = std::max (mx, f.env[s]);
    }
    for (auto& v : f.env) v = std::max (-60.0f, v - mx);

    // long-term average spectrum in 16 log bands (50 Hz .. 16 kHz)
    const int N = 2048;
    FFT fft; fft.init (N);
    const auto win = detail::hannWindow (N);
    std::vector<float> re ((size_t) N), im ((size_t) N);
    std::vector<double> pw ((size_t) N / 2 + 1, 0.0);
    const int lim = std::min (n, (int) (6.0 * a.sr));
    int frames = 0;
    for (int pos = 0; pos + N <= std::max (N, lim); pos += N / 2)
    {
        for (int i = 0; i < N; ++i)
        {
            const int idx = pos + i;
            re[(size_t) i] = idx < n ? 0.5f * (a.ch[0][(size_t) idx] + a.ch[1][(size_t) idx]) * win[(size_t) i] : 0.0f;
            im[(size_t) i] = 0.0f;
        }
        fft.forward (re.data(), im.data());
        for (int k = 0; k <= N / 2; ++k) pw[(size_t) k] += (double) re[(size_t) k] * re[(size_t) k] + (double) im[(size_t) k] * im[(size_t) k];
        ++frames;
    }
    double tot = 0.0, wsum = 0.0;
    for (int k = 1; k <= N / 2; ++k) { const double hz = k * a.sr / N; tot += pw[(size_t) k]; wsum += pw[(size_t) k] * hz; }
    f.centroid = tot > 1e-12 ? (float) (wsum / tot) : 1000.0f;
    for (int b = 0; b < 16; ++b)
    {
        const double lo = 50.0 * std::pow (16000.0 / 50.0, b / 16.0), hi = 50.0 * std::pow (16000.0 / 50.0, (b + 1) / 16.0);
        double acc = 0.0; int cnt = 0;
        for (int k = 1; k <= N / 2; ++k)
        {
            const double hz = k * a.sr / N;
            if (hz >= lo && hz < hi) { acc += pw[(size_t) k]; ++cnt; }
        }
        f.band[b] = 10.0f * std::log10 ((float) (acc / std::max (1, cnt) / std::max (1, frames)) + 1e-10f);
    }
    return f;
}

// Perceptually-motivated scalar distance (0 = identical, ~1 = clearly different, 2+ = very different).
inline float distance (const Features& a, const Features& b)
{
    float bd = 0.0f, ed = 0.0f;
    for (int i = 0; i < 16; ++i) { const float d = a.band[i] - b.band[i]; bd += d * d; }
    for (int i = 0; i < 32; ++i) ed += std::fabs (a.env[i] - b.env[i]);
    bd = std::sqrt (bd / 16.0f) / 8.0f;
    ed = ed / 32.0f / 10.0f;
    const float ld = std::fabs (a.logLen - b.logLen) * 0.9f;
    return std::sqrt (bd * bd + ed * ed + ld * ld);
}

inline float hueFromCentroid (float hz)
{
    const float t = std::clamp (std::log2 (std::max (60.0f, hz) / 150.0f) / std::log2 (6000.0f / 150.0f), 0.0f, 1.0f);
    static const float hues[5] = { 0.76f, 0.60f, 0.50f, 0.15f, 0.04f };
    const float x = t * 4.0f;
    const int i = std::min (3, (int) x);
    return detail::lerp (hues[i], hues[i + 1], x - (float) i);
}

// Colour "position" (0..1) of a variation. First-generation variations get any position (so the cloud is varied);
// a variation of a variation stays close to its parent (+-0.08) so a family looks related. The editor turns the
// position into a shade of red / bordeaux. Derived from the seed only, so it survives a project reload.
inline float hueFromSeed (uint32_t seed)
{
    uint32_t x = seed * 2654435761u;
    x ^= x >> 15; x *= 2246822519u; x ^= x >> 13; x *= 3266489917u; x ^= x >> 16;
    return (float) (x >> 8) / 16777216.0f;
}

inline float childHue (float parentHue, uint32_t seed)
{
    float h = parentHue + (hueFromSeed (seed ^ 0x9e3779b9u) - 0.5f) * 0.16f;
    if (h < 0.0f) h = -h;                       // reflect at the ends instead of wrapping around
    if (h > 1.0f) h = 2.0f - h;
    return std::clamp (h, 0.0f, 1.0f);
}

// ------------------------------------------------------------------------------------------
// The transmutation itself
// ------------------------------------------------------------------------------------------
inline void finish (Audio& a, float refRms)
{
    // clean up
    for (auto& chn : a.ch)
    {
        float z = 0.0f;
        const float rc = 1.0f - (float) (2.0 * kPi * 12.0 / a.sr);      // DC blocker, ~12 Hz
        float xp = 0.0f;
        for (auto& v : chn)
        {
            if (! std::isfinite (v)) v = 0.0f;
            const float y = v - xp + rc * z;
            xp = v; z = y; v = y;
        }
        if ((int) chn.size() > a.maxLen()) chn.resize ((size_t) a.maxLen());
    }
    const int n0 = a.size();
    float pk = 0.0f;
    for (auto& chn : a.ch) for (float v : chn) pk = std::max (pk, std::fabs (v));
    if (pk <= 1e-9f) return;

    // trim silent tail (-60 dB re peak)
    int last = 0;
    for (int i = n0 - 1; i >= 0; --i)
        if (std::fabs (a.ch[0][(size_t) i]) > pk * 1e-3f || std::fabs (a.ch[1][(size_t) i]) > pk * 1e-3f) { last = i; break; }
    const int n = std::max (64, std::min (n0, last + (int) (a.sr * 0.02)));
    for (auto& chn : a.ch) chn.resize ((size_t) n);

    // fades
    const int fin = std::max (4, (int) (a.sr * 0.0015));
    const int fout = std::min (n / 2, std::max (8, (int) (a.sr * (n0 > a.maxLen() - 8 ? 0.05 : 0.025))));
    for (auto& chn : a.ch)
    {
        for (int i = 0; i < fin && i < n; ++i) chn[(size_t) i] *= (float) i / fin;
        for (int i = 0; i < fout; ++i) chn[(size_t) (n - 1 - i)] *= (float) i / fout;
    }

    // loudness match to the original, peak-safe
    const float rms = activeRms (a);
    pk = 0.0f;
    for (auto& chn : a.ch) for (float v : chn) pk = std::max (pk, std::fabs (v));
    if (rms > 1e-9f && refRms > 0.0f && pk > 0.0f)
    {
        const float g = std::min (std::clamp (refRms / rms, 0.05f, 20.0f), 1.8f / pk);
        for (auto& chn : a.ch) for (auto& v : chn) v = softClip (v * g, 0.8f);
    }
}

// ------------------------------------------------------------------------------------------
// Effect selection: how many effects a variation gets and which ones. (Indices follow the Op enum in render().)
// ------------------------------------------------------------------------------------------
namespace detail
{
constexpr int kNumOps = 12;
constexpr int kMaxEffects = 6;        // most effects chained in one variation
constexpr int kOpReverse = 4;

inline std::vector<int> pickOps (float c, Rng& rng)
{
    struct Info { float weight, minChaos; int stage; };
    static const Info table[kNumOps] = {
        { 3.0f, 0.00f, 0 },  // Pitch
        { 1.5f, 0.10f, 0 },  // Stretch
        { 1.2f, 0.35f, 0 },  // Smear
        { 1.2f, 0.25f, 0 },  // Scramble
        { 0.3f, 0.20f, 0 },  // Reverse (rare)
        { 1.5f, 0.15f, 1 },  // Warp
        { 2.5f, 0.00f, 1 },  // Filter
        { 1.2f, 0.30f, 1 },  // Comb
        { 1.0f, 0.30f, 2 },  // Crush
        { 2.0f, 0.00f, 2 },  // Drive
        { 1.5f, 0.00f, 2 },  // Env
        { 1.8f, 0.00f, 3 },  // Reverb
    };

    const float k = 2.6f + 2.8f * c * c;                 // more effects per unit of chaos at the top end, so 5-6 become reachable
    const float u1 = rng.range (0.6f, 1.0f);             // (explicit order: the compiler must not decide which random number comes first)
    const float u2 = rng.uni();
    const int nOps = std::min (kMaxEffects, 1 + (int) (c * k * u1 + u2 * 0.9f));

    std::vector<int> picks;
    bool used[kNumOps] = {};
    for (int n = 0; n < nOps; ++n)
    {
        float tot = 0.0f;
        for (int o = 0; o < kNumOps; ++o)
            if (! used[o] && table[o].minChaos <= c) tot += table[o].weight * (table[o].minChaos > 0.0f ? 0.5f + c : 1.0f);
        if (tot <= 0.0f) break;
        float x = rng.uni() * tot;
        for (int o = 0; o < kNumOps; ++o)
        {
            if (used[o] || table[o].minChaos > c) continue;
            x -= table[o].weight * (table[o].minChaos > 0.0f ? 0.5f + c : 1.0f);
            if (x <= 0.0f || o == kNumOps - 1) { used[o] = true; picks.push_back (o); break; }
        }
    }
    std::stable_sort (picks.begin(), picks.end(), [&] (int x, int y) { return table[x].stage < table[y].stage; });
    return picks;
}
} // namespace detail

// Returns the variation described by `dna`. `refRms` = activeRms() of the ORIGINAL sound.
inline Audio render (const Audio& src, const Dna& dna, float refRms)
{
    Audio a = src;
    if (a.size() < 64 || refRms <= 0.0f) return a;

    const float c = std::clamp (dna.chaos, 0.0f, 1.0f);
    Rng rng (dna.seed);

    enum Op { Pitch, Stretch, Smear, Scramble, Reverse, Warp, Filter, Comb, Crush, Drive, Env, Reverb, NumOps };
    static_assert ((int) NumOps == detail::kNumOps, "keep the effect list in sync with detail::pickOps");
    const std::vector<int> picks = detail::pickOps (c, rng);

    for (int o : picks)
    {
        const float amt = std::clamp (0.05f + 0.95f * c * rng.range (0.55f, 1.0f), 0.0f, 1.0f);
        switch (o)
        {
            case Pitch:    detail::opPitch (a, rng, amt);    break;
            case Stretch:  detail::opStretch (a, rng, amt);  break;
            case Smear:    detail::opSmear (a, rng, amt);    break;
            case Scramble: detail::opScramble (a, rng, amt); break;
            case Reverse:  detail::opReverse (a, rng, amt);  break;
            case Warp:     detail::opWarp (a, rng, amt);     break;
            case Filter:   detail::opFilter (a, rng, amt);   break;
            case Comb:     detail::opComb (a, rng, amt);     break;
            case Crush:    detail::opCrush (a, rng, amt);    break;
            case Drive:    detail::opDrive (a, rng, amt);    break;
            case Env:      detail::opEnvelope (a, rng, amt); break;
            case Reverb:   detail::opReverb (a, rng, amt);   break;
            default: break;
        }
    }
    finish (a, refRms);

    // safety net: a variation must never end up silent
    if (activeRms (a) < refRms * 0.01f)
    {
        Audio b = src;
        Rng r2 (dna.seed ^ 0xA5A5A5A5u);
        detail::opPitch (b, r2, 0.3f + 0.3f * c);
        finish (b, refRms);
        return b;
    }
    return a;
}

} // namespace poly
