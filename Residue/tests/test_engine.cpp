// Pure engine tests: timing, feedback, styles, ping-pong, stability, transparency.
#include "../Source/EchoEngine.h"
#include <cstdio>
#include <random>
#include <vector>
using namespace residue;
static int failures = 0;
#define CHECK(c, ...) do { const bool ok_ = (c); std::printf ("%s  ", ok_ ? "PASS" : "FAIL"); std::printf (__VA_ARGS__); std::printf ("\n"); if (! ok_) ++failures; } while (0)

static Params clean (int T)
{
    Params p; p.style = kDigital; p.timeSamples = (float) T; p.fb = 0.0f; p.sat = 0; p.wobble = 0; p.duck = 0; p.spread = 0; p.mix = 1.0f; p.lowHz = 20; p.highHz = 20000;
    return p;
}
static void run (Engine& e, std::vector<float>& l, std::vector<float>& r)
{
    for (size_t s = 0; s < l.size(); s += 64) { const int n = (int) std::min<size_t> (64, l.size() - s); e.process (l.data() + s, r.data() + s, n); }
}
static size_t peakAt (const std::vector<float>& v, size_t from, size_t to) { size_t b = from; for (size_t i = from; i < to && i < v.size(); ++i) if (std::fabs (v[i]) > std::fabs (v[b])) b = i; return b; }

int main()
{
    const double fs = 48000;
    {   // timing: the echo comes exactly `T` samples later
        Engine e; e.prepare (fs, 4.0); e.setParams (clean (4800)); e.reset();
        std::vector<float> l (30000, 0.0f), r (30000, 0.0f); l[100] = 1.0f; r[100] = 1.0f; run (e, l, r);
        const size_t pk = peakAt (l, 200, 20000);
        CHECK (pk == 4900 && std::fabs (l[pk]) > 0.95f, "echo time exact (peak at %zu, expected 4900, level %.3f)", pk, std::fabs (l[pk]));
    }
    {   // feedback: each repeat is `fb` times the previous one (digital)
        Engine e; e.prepare (fs, 4.0); auto p = clean (4800); p.fb = 0.5f; e.setParams (p); e.reset();
        std::vector<float> l (40000, 0.0f), r (40000, 0.0f); l[100] = 0.3f; r[100] = 0.3f; run (e, l, r);
        const float a = std::fabs (l[4900]), b = std::fabs (l[9700]), c = std::fabs (l[14500]);
        CHECK (std::fabs (b / a - 0.5f) < 0.03f && std::fabs (c / b - 0.5f) < 0.03f, "feedback 50%%: ratios %.3f, %.3f", b / a, c / b);
    }
    {   // fb 0: exactly one echo
        Engine e; e.prepare (fs, 4.0); e.setParams (clean (4800)); e.reset();
        std::vector<float> l (30000, 0.0f), r (30000, 0.0f); l[100] = 0.5f; r[100] = 0.5f; run (e, l, r);
        float later = 0; for (size_t i = 5200; i < l.size(); ++i) later = std::max (later, std::fabs (l[i]));
        CHECK (later < 0.01f, "no feedback: no second echo (%.5f)", later);
    }
    {   // mix 0 is bit-exact
        Engine e; e.prepare (fs, 4.0); auto p = clean (3000); p.mix = 0.0f; p.fb = 0.8f; p.style = kTape; p.sat = 0.7f; e.setParams (p); e.reset();
        std::mt19937 g (3); std::uniform_real_distribution<float> U (-0.5f, 0.5f);
        std::vector<float> l (20000), r (20000); for (auto& v : l) v = U (g); r = l; const auto ref = l; run (e, l, r);
        bool same = true; for (size_t i = 0; i < l.size(); ++i) if (l[i] != ref[i] || r[i] != ref[i]) same = false;
        CHECK (same, "mix 0%% leaves the signal bit-identical");
    }
    {   // tape / analog / lo-fi: every repeat gets darker
        auto hf = [&] (int style)
        {
            Engine e; e.prepare (fs, 4.0); auto p = clean (4800); p.style = style; p.fb = 0.5f; e.setParams (p); e.reset();
            std::vector<float> l (60000, 0.0f), r (60000, 0.0f);
            for (size_t i = 0; i < 2400; ++i) { l[i] = r[i] = 0.3f * std::sin (6.2831853f * 9000.0f * (float) i / (float) fs); }
            run (e, l, r);
            auto rms = [&] (size_t a, size_t b) { double s = 0; for (size_t i = a; i < b; ++i) s += (double) l[i] * l[i]; return std::sqrt (s / (double) (b - a)); };
            return rms (4800, 7200) / (0.3 / std::sqrt (2.0));
        };
        const double d = hf (kDigital), t = hf (kTape), a = hf (kAnalog);
        CHECK (d > 0.9 && t < 0.8 && a < t, "9 kHz after one echo: digital %.2f, tape %.2f, analog %.2f (darker each)", d, t, a);
    }
    {   // ping-pong: first echo on L, second on R
        Engine e; e.prepare (fs, 4.0); auto p = clean (4800); p.fb = 0.8f; p.spread = 1.0f; e.setParams (p); e.reset();
        std::vector<float> l (30000, 0.0f), r (30000, 0.0f); l[100] = 1.0f; r[100] = 1.0f; run (e, l, r);
        CHECK (std::fabs (l[4900]) > 0.4f && std::fabs (r[4900]) < 0.02f && std::fabs (r[9700]) > 0.25f && std::fabs (l[9700]) < 0.02f,
               "ping-pong: L %.2f / R %.2f at 1st, L %.2f / R %.2f at 2nd", l[4900], r[4900], l[9700], r[9700]);
    }
    {   // stability: 100% feedback with loud noise for 20 s stays bounded and finite, all styles, max saturation
        for (int style = 0; style < kNumStyles; ++style)
        {
            Engine e; e.prepare (fs, 4.0); auto p = clean (6000); p.style = style; p.fb = 1.0f; p.sat = 1.0f; p.wobble = 1.0f; p.spread = 0.5f; p.mix = 0.5f; e.setParams (p); e.reset();
            std::mt19937 g (5); std::uniform_real_distribution<float> U (-1.0f, 1.0f);
            float peak = 0; bool fin = true;
            for (int b = 0; b < 15000; ++b)
            {
                float l[64], r[64]; for (int i = 0; i < 64; ++i) { l[i] = (b < 2000 ? 0.9f : 0.0f) * U (g); r[i] = l[i]; }
                e.process (l, r, 64);
                for (int i = 0; i < 64; ++i) { peak = std::max (peak, std::max (std::fabs (l[i]), std::fabs (r[i]))); if (! std::isfinite (l[i]) || ! std::isfinite (r[i])) fin = false; }
            }
            CHECK (fin && peak < 3.0f, "style %s, fb 100%%, 20 s: finite, peak %.2f", styleName (style), peak);
        }
    }
    {   // changing the time glides (no click): the biggest sample jump while the time is swept stays small
        Engine e; e.prepare (fs, 4.0); auto p = clean (4800); p.style = kTape; e.setParams (p); e.reset();
        double ph = 0; float last = 0, maxJump = 0;
        for (int b = 0; b < 3000; ++b)
        {
            if (b == 1000) { p.timeSamples = 24000; e.setParams (p); }
            float l[64], r[64]; for (int i = 0; i < 64; ++i) { ph += 220.0 / fs; l[i] = r[i] = 0.4f * std::sin (6.2831853 * ph); }
            e.process (l, r, 64);
            if (b > 600) for (int i = 0; i < 64; ++i) { maxJump = std::max (maxJump, std::fabs (l[i] - last)); last = l[i]; }
        }
        CHECK (maxJump < 0.35f, "time change is smooth (max sample step %.3f)", maxJump);
    }
    {   // ducking: the echo is pushed down while the input plays and comes back later
        auto echoRms = [&] (float duck, int T, size_t playLen, size_t a, size_t b)
        {
            Engine e; e.prepare (fs, 4.0); auto p = clean (T); p.mix = 1.0f; p.duck = duck; e.setParams (p); e.reset();
            std::vector<float> l (60000), r (60000);
            for (size_t i = 0; i < l.size(); ++i) l[i] = r[i] = (i < playLen ? 0.5f : 0.0f) * std::sin (6.2831853f * 300.0f * (float) i / (float) fs);
            run (e, l, r);
            double s = 0; for (size_t i = a; i < b; ++i) s += (double) l[i] * l[i]; return std::sqrt (s / (double) (b - a));
        };
        const double during = echoRms (1.0f, 4800, 12000, 6000, 11000) / echoRms (0.0f, 4800, 12000, 6000, 11000);
        const double later = echoRms (1.0f, 30000, 6000, 31000, 35000) / echoRms (0.0f, 30000, 6000, 31000, 35000);
        CHECK (during < 0.1 && later > 0.6, "ducking: echo while playing %.3f of normal, 0.5 s after the input stopped %.3f", during, later);
    }
    std::printf (failures ? "%d TEST(S) FAILED\n" : "ALL TESTS PASSED\n", failures);
    return failures ? 1 : 0;
}
