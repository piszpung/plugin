// Renders the Residue window to PNGs (headless) and checks that the red / blue / white effect really is on screen.  usage: render_editor <outdir>
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include <cstdio>
#include <random>

static int fails = 0;
static void check (const char* n, bool ok) { std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", n); if (! ok) ++fails; }
static void setParam (ResidueAudioProcessor& p, const juce::String& id, float plain) { auto* prm = p.apvts.getParameter (id); prm->setValueNotifyingHost (prm->convertTo0to1 (plain)); }

static juce::Image shot (juce::AudioProcessorEditor& ed, const juce::File& f)
{
    juce::Image img = ed.createComponentSnapshot (ed.getLocalBounds(), true, 1.0f);
    f.deleteFile(); juce::FileOutputStream out (f); juce::PNGImageFormat png; png.writeImageToStream (img, out);
    std::printf ("wrote %s (%d x %d)\n", f.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
    return img;
}
static double ph = 0;
static void run (ResidueEditor& ed, ResidueAudioProcessor& p, int frames)
{
    std::mt19937 rng (3); std::uniform_real_distribution<float> U (-1.0f, 1.0f);
    juce::AudioBuffer<float> b (2, 1600); juce::MidiBuffer m;
    for (int fr = 0; fr < frames; ++fr)
    {
        for (int i = 0; i < 1600; ++i) { ph += 110.0 / 48000.0; const float v = (float) (std::sin (6.2831853 * ph) * 0.3 * (((fr / 6) % 3) == 0 ? 1.0 : 0.15)) + 0.02f * U (rng); b.setSample (0, i, v); b.setSample (1, i, v); }
        p.processBlock (b, m);
        ed.tick();
    }
}
struct Counts { long white = 0, redDom = 0, blueDom = 0, lit = 0; };
static Counts count (const juce::Image& img, juce::Rectangle<int> r)
{
    Counts c;
    for (int y = r.getY(); y < r.getBottom(); ++y)
        for (int x = r.getX(); x < r.getRight(); ++x)
        {
            const auto px = img.getPixelAt (x, y); const int R = px.getRed(), G = px.getGreen(), B = px.getBlue();
            if (R + G + B > 60) ++c.lit;
            if (R > 215 && G > 215 && B > 215) ++c.white;
            else if (R > 140 && G < 0.45 * R && B < 0.6 * R) ++c.redDom;
            else if (B > 140 && R < 0.35 * B) ++c.blueDom;
        }
    return c;
}

int main (int argc, char** argv)
{
    if (argc < 2) return 2;
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File dir (argv[1]);
    ResidueAudioProcessor proc; proc.setPlayConfigDetails (2, 2, 48000.0, 1600); proc.prepareToPlay (48000.0, 1600);
    std::unique_ptr<juce::AudioProcessorEditor> base (proc.createEditor());
    auto& ed = *dynamic_cast<ResidueEditor*> (base.get());

    run (ed, proc, 40);
    auto a = shot (ed, dir.getChildFile ("1_default.png"));
    const auto vb = ed.visualBounds();
    const auto c = count (a, vb);
    std::printf ("default: lit %ld, white %ld, red %ld, blue %ld\n", c.lit, c.white, c.redDom, c.blueDom);
    check ("picture shows red, blue and white pixels", c.white > 300 && c.redDom > 300 && c.blueDom > 300);
    check ("picture is mostly black", c.lit < (long) vb.getWidth() * vb.getHeight() / 4);

    setParam (proc, "fb", 75.0f); setParam (proc, "spread", 70.0f); setParam (proc, "wobble", 60.0f); setParam (proc, "mix", 60.0f); setParam (proc, "sync", 0.0f); setParam (proc, "time", 600.0f); setParam (proc, "style", 3.0f);
    run (ed, proc, 60);
    shot (ed, dir.getChildFile ("2_pingpong_free.png"));
    setParam (proc, "fb", 20.0f); setParam (proc, "spread", 0.0f); setParam (proc, "wobble", 0.0f); setParam (proc, "mix", 100.0f); setParam (proc, "style", 0); setParam (proc, "sync", 1.0f); setParam (proc, "note", 5.0f);
    run (ed, proc, 60);
    shot (ed, dir.getChildFile ("3_clean_short.png"));
    std::printf (fails ? "%d TEST(S) FAILED\n" : "ALL TESTS PASSED\n", fails);
    return fails ? 1 : 0;
}
