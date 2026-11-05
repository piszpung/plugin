// Plain C++ tests of the Ember modulation sources.  g++ -O2 -std=c++17 test_mod.cpp -o test_mod
#include <string>
#include "../Source/Modulation.h"
#include <cstdio>
#include <vector>
using namespace ember;
static int fails = 0;
#define CHECK(c, ...) do { const bool ok_ = (c); std::printf ("%s  ", ok_ ? "PASS" : "FAIL"); std::printf (__VA_ARGS__); std::printf ("\n"); if (! ok_) ++fails; } while (0)

int main()
{
    // free LFO: frequency
    {
        Lfo l; l.reset(); LfoSettings s; s.rateHz = 4.0f; s.glide = 1.0f; fillShape (s.steps, kShapeSine);
        const double dt = 0.0027; int crossings = 0; float prev = 0; double t = 0;
        for (int i = 0; i < 3700; ++i, t += dt) { const float v = l.run (s, dt, -1.0, 120.0); if (prev < 0 && v >= 0) ++crossings; prev = v; }
        CHECK (std::abs (crossings / t - 4.0) < 0.3, "free LFO at 4 Hz measures %.2f Hz", crossings / t);
    }
    // tempo-locked: one bar at 120 bpm = 2 s per cycle, driven by the host position; hard steps visit exactly the 16 step values
    {
        Lfo l; l.reset(); LfoSettings s; s.sync = 3; s.glide = 0.0f; fillShape (s.steps, kShapeSawUp);
        bool ok = true;
        for (int i = 0; i < 16; ++i) { const double ppq = (i + 0.5) / 16.0 * 4.0 + 8.0; const float v = l.run (s, 0.01, ppq, 120.0); ok = ok && std::abs (v - s.steps[(size_t) i]) < 1e-6f; }
        CHECK (ok, "tempo-locked LFO follows the host position and plays the 16 steps");
        Lfo f; f.reset(); LfoSettings q = s; q.sync = 1;     // no transport: runs on the tempo
        double t = 0; float prev = -2; int wraps = 0;
        for (int i = 0; i < 4000; ++i, t += 0.001) { const float v = f.run (q, 0.001, -1.0, 120.0); if (v < prev - 0.5f) ++wraps; prev = v; }
        CHECK (wraps == 8 || wraps == 7 || wraps == 9, "1 beat at 120 bpm = 0.5 s per cycle: %d cycles in 4 s", wraps);
    }
    // glide smooths the steps
    {
        Lfo a, b; a.reset(); b.reset(); LfoSettings s; s.rateHz = 1.0f; fillShape (s.steps, kShapeSquare);
        float maxJumpHard = 0, maxJumpSoft = 0, pa = 0, pb = 0; LfoSettings soft = s; soft.glide = 0.8f;
        for (int i = 0; i < 4000; ++i)
        {
            const float va = a.run (s, 0.001, -1.0, 120.0), vb = b.run (soft, 0.001, -1.0, 120.0);
            if (i) { maxJumpHard = std::max (maxJumpHard, std::abs (va - pa)); maxJumpSoft = std::max (maxJumpSoft, std::abs (vb - pb)); }
            pa = va; pb = vb;
        }
        CHECK (maxJumpHard > 1.5f && maxJumpSoft < 0.3f, "glide turns jumps (%.2f) into ramps (%.3f)", maxJumpHard, maxJumpSoft);
    }
    // follower
    {
        Follower f; f.reset();
        float quiet = 0, loud = 0;
        for (int i = 0; i < 400; ++i) quiet = f.run (0.003f, 0.0027, 5.0f, 150.0f, 0.0f);      // -50 dB
        for (int i = 0; i < 400; ++i) loud = f.run (0.5f, 0.0027, 5.0f, 150.0f, 0.0f);          // -6 dB
        float released = loud; for (int i = 0; i < 800; ++i) released = f.run (0.0f, 0.0027, 5.0f, 150.0f, 0.0f);
        float boosted = 0; { Follower g; g.reset(); for (int i = 0; i < 400; ++i) boosted = g.run (0.1f, 0.0027, 5.0f, 150.0f, 12.0f); }
        float plain = 0; { Follower g; g.reset(); for (int i = 0; i < 400; ++i) plain = g.run (0.1f, 0.0027, 5.0f, 150.0f, 0.0f); }
        CHECK (quiet < 0.1f && loud > 0.85f && released < 0.1f && boosted > plain + 0.2f, "follower: quiet %.2f, loud %.2f, after release %.2f, +12 dB gain %.2f vs %.2f", quiet, loud, released, boosted, plain);
    }
    // the target table
    {
        char b[48]; targetName (bandTarget (2, 6), b, sizeof b);
        char c[48]; targetName (crossoverTarget (3), c, sizeof c);
        char d[48]; targetName (kTargetMix, d, sizeof d);
        CHECK (std::string (b) == "Band 3  Fb Freq" && std::string (c) == "Crossover 4" && std::string (d) == "Mix" && kNumTargets == 57, "target names: '%s', '%s', '%s' (%d targets)", b, c, d, kNumTargets);
    }
    std::printf (fails ? "\n%d TEST(S) FAILED\n" : "\nALL TESTS PASSED\n", fails);
    return fails ? 1 : 0;
}
