// Offline tests of the Polymer variation engine (no JUCE).
// usage: test_transmuter <input.f32 stereo raw> <sampleRate> [outdir]
#include "../Source/Transmuter.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

using namespace poly;
static int failures = 0;
static void check (const char* name, bool ok, const std::string& extra = "")
{
    std::printf ("%s  %s  %s\n", ok ? "PASS" : "FAIL", name, extra.c_str());
    if (! ok) ++failures;
}

static Audio readRaw (const char* path, double sr)
{
    std::ifstream f (path, std::ios::binary | std::ios::ate);
    const size_t bytes = (size_t) f.tellg(); f.seekg (0);
    std::vector<float> d (bytes / 4);
    f.read ((char*) d.data(), (std::streamsize) (d.size() * 4));
    Audio a; a.sr = sr;
    const int n = (int) (d.size() / 2);
    a.resize (std::min (n, a.maxLen()));
    for (int i = 0; i < a.size(); ++i) { a.ch[0][(size_t) i] = d[(size_t) i * 2]; a.ch[1][(size_t) i] = d[(size_t) i * 2 + 1]; }
    return a;
}

static void writeRaw (const std::string& path, const Audio& a)
{
    std::ofstream f (path, std::ios::binary);
    for (int i = 0; i < a.size(); ++i) { f.write ((const char*) &a.ch[0][(size_t) i], 4); f.write ((const char*) &a.ch[1][(size_t) i], 4); }
}

static float peak (const Audio& a) { float p = 0; for (auto& c : a.ch) for (float v : c) p = std::max (p, std::fabs (v)); return p; }
static bool finite (const Audio& a) { for (auto& c : a.ch) for (float v : c) if (! std::isfinite (v)) return false; return true; }

int main (int argc, char** argv)
{
    if (argc < 3) { std::puts ("usage"); return 2; }
    const double sr = std::atof (argv[2]);
    const std::string outdir = argc > 3 ? argv[3] : "";
    Audio src = readRaw (argv[1], sr);
    const float ref = activeRms (src);
    const Features fSrc = analyse (src);
    std::printf ("source: %.2f s, activeRms %.4f, centroid %.0f Hz\n", src.size() / sr, ref, fSrc.centroid);

    // --- all outputs sane over a grid of seeds and chaos values -------------------------------
    bool allFinite = true, allPeak = true, allLen = true, allAlive = true;
    double maxMs = 0.0; float maxPk = 0.0f;
    const float chaosGrid[] = { 0.0f, 0.1f, 0.25f, 0.4f, 0.6f, 0.8f, 1.0f };
    std::vector<std::vector<float>> dist (7);
    int nOK = 0;
    for (size_t ci = 0; ci < 7; ++ci)
        for (uint32_t seed = 1; seed <= 40; ++seed)
        {
            const auto t0 = std::chrono::steady_clock::now();
            Audio v = render (src, { seed * 7919u + 13u, chaosGrid[ci] }, ref);
            const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
            maxMs = std::max (maxMs, ms);
            allFinite &= finite (v);
            maxPk = std::max (maxPk, peak (v));
            allPeak &= peak (v) <= 1.0f;
            allLen &= v.size() >= 64 && v.size() <= v.maxLen();
            allAlive &= activeRms (v) > ref * 0.05f;
            dist[ci].push_back (distance (fSrc, analyse (v)));
            ++nOK;
        }
    check ("all finite", allFinite);
    check ("peak <= 1.0", allPeak, "max peak " + std::to_string (maxPk));
    check ("length within limits", allLen);
    check ("never silent (rms >= 5% of source)", allAlive);
    std::printf ("slowest render: %.0f ms (%d renders)\n", maxMs, nOK);
    check ("render time < 3 s", maxMs < 3000.0);

    // --- chaos -> distance from the original ----------------------------------------------------
    std::vector<float> meanD;
    for (size_t ci = 0; ci < 7; ++ci)
    {
        float m = 0; for (float d : dist[ci]) m += d; m /= (float) dist[ci].size();
        std::vector<float> s = dist[ci]; std::sort (s.begin(), s.end());
        std::printf ("chaos %.2f: mean dist %.3f  (p10 %.3f  median %.3f  p90 %.3f)\n", chaosGrid[ci], m, s[4], s[20], s[36]);
        meanD.push_back (m);
    }
    bool mono = true;
    for (size_t i = 1; i < meanD.size(); ++i) mono &= meanD[i] > meanD[i - 1];
    check ("mean distance grows with chaos (strictly)", mono);
    check ("chaos 0 stays close to the original (mean < 0.6)", meanD[0] < 0.6f);
    check ("chaos 1 is far from the original (mean > 1.5x of chaos 0.25)", meanD[6] > 1.5f * meanD[2]);

    // --- determinism ------------------------------------------------------------------------------
    {
        Audio x = render (src, { 424242u, 0.7f }, ref), y = render (src, { 424242u, 0.7f }, ref);
        check ("deterministic (same seed -> identical audio)", x.size() == y.size() && x.ch[0] == y.ch[0] && x.ch[1] == y.ch[1]);
        Audio z = render (src, { 424243u, 0.7f }, ref);
        check ("different seed -> different audio", distance (analyse (x), analyse (z)) > 0.05f || x.size() != z.size());
    }

    // --- siblings differ from each other ----------------------------------------------------------
    for (float c : { 0.15f, 0.5f, 0.9f })
    {
        std::vector<Features> fs;
        for (uint32_t s = 0; s < 6; ++s) fs.push_back (analyse (render (src, { 1000u + s * 31u, c }, ref)));
        float minD = 1e9f, sumD = 0; int cnt = 0;
        for (size_t i = 0; i < fs.size(); ++i) for (size_t j = i + 1; j < fs.size(); ++j) { float d = distance (fs[i], fs[j]); minD = std::min (minD, d); sumD += d; ++cnt; }
        std::printf ("chaos %.2f: sibling distance min %.3f mean %.3f\n", c, minD, sumD / cnt);
    }

    // --- edge cases ---------------------------------------------------------------------------------
    {
        Audio sil; sil.sr = sr; sil.resize (20000);
        Audio v = render (sil, { 5u, 0.8f }, 0.0f);
        check ("silent input does not crash", v.size() == 20000 && finite (v));
        Audio tiny; tiny.sr = sr; tiny.resize (40); tiny.ch[0][3] = 0.5f; tiny.ch[1][3] = 0.5f;
        Audio t2 = render (tiny, { 5u, 1.0f }, 0.1f);
        check ("tiny input does not crash", finite (t2));
        Audio imp; imp.sr = sr; imp.resize ((int) sr); imp.ch[0][100] = 1.0f; imp.ch[1][100] = 1.0f;
        bool ok = true;
        for (uint32_t s = 0; s < 30; ++s) { Audio q = render (imp, { s, 1.0f }, activeRms (imp)); ok &= finite (q) && peak (q) <= 1.0f && q.size() >= 64; }
        check ("impulse input, chaos 1, 30 seeds", ok);
        Audio longA; longA.sr = sr; longA.resize (longA.maxLen());
        Rng rr (3); for (int i = 0; i < longA.size(); ++i) { float v2 = 0.3f * std::sin (i * 0.05f) * (1.0f - (float) i / longA.size()) + 0.02f * (rr.uni() - 0.5f); longA.ch[0][(size_t) i] = v2; longA.ch[1][(size_t) i] = v2; }
        double worst = 0; bool lok = true;
        for (uint32_t s = 0; s < 25; ++s)
        {
            auto t0 = std::chrono::steady_clock::now();
            Audio q = render (longA, { s * 17u, 1.0f }, activeRms (longA));
            worst = std::max (worst, std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count());
            lok &= finite (q) && q.size() <= q.maxLen() && peak (q) <= 1.0f;
        }
        std::printf ("8 s source, chaos 1: slowest %.0f ms\n", worst);
        check ("max-length source stays within limits", lok);
        // second generation (variation of a variation)
        Audio g1 = render (src, { 77u, 0.8f }, ref);
        Audio g2 = render (g1, { 78u, 0.8f }, ref), g3 = render (g2, { 79u, 0.8f }, ref);
        check ("3 generations stay finite and loudness-stable", finite (g3) && std::fabs (20 * std::log10 (activeRms (g3) / ref)) < 6.0f,
               "rms vs ref " + std::to_string (20 * std::log10 (activeRms (g3) / ref)) + " dB");
    }

    // --- dump some variations for listening / spectrograms ---------------------------------------------
    if (! outdir.empty())
    {
        writeRaw (outdir + "/orig.raw", src);
        int k = 0;
        for (float c : { 0.1f, 0.35f, 0.65f, 1.0f })
            for (uint32_t s = 0; s < 4; ++s)
            {
                Audio v = render (src, { 9000u + s * 101u, c }, ref);
                char nm[128]; std::snprintf (nm, sizeof nm, "/v_c%02d_s%u.raw", (int) (c * 100), s);
                writeRaw (outdir + nm, v); ++k;
            }
    }
    std::printf ("%s (%d failures)\n", failures ? "SOME TESTS FAILED" : "ALL TESTS PASSED", failures);
    return failures ? 1 : 0;
}
