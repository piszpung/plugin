// Renders the Ember window to PNGs (headless).  usage: render_editor <outdir>
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include <cstdio>
#include <random>

static void shot (juce::AudioProcessorEditor& ed, const juce::File& f)
{
    juce::Image img = ed.createComponentSnapshot (ed.getLocalBounds(), true, 1.0f);
    f.deleteFile();
    juce::FileOutputStream out (f);
    juce::PNGImageFormat png;
    png.writeImageToStream (img, out);
    std::printf ("wrote %s (%d x %d)\n", f.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
}
static void setParam (EmberAudioProcessor& p, const juce::String& id, float plain) { auto* prm = p.apvts.getParameter (id); prm->setValueNotifyingHost (prm->convertTo0to1 (plain)); }

// a bass + saw chord + hats-like noise: something with a spectrum
static double ph[6] = {};
static std::mt19937 rng (11);
static void run (EmberEditor& ed, EmberAudioProcessor& p, int frames)
{
    std::uniform_real_distribution<float> U (-1.0f, 1.0f);
    juce::AudioBuffer<float> b (2, 1024); juce::MidiBuffer m;
    const double f[6] = { 55.0, 110.0, 164.8, 220.0, 329.6, 440.0 };
    for (int fr = 0; fr < frames; ++fr)
    {
        for (int i = 0; i < 1024; ++i)
        {
            float v = 0.0f;
            for (int k = 0; k < 6; ++k) { ph[k] += f[k] / 48000.0; ph[k] -= std::floor (ph[k]); v += (float) (2.0 * ph[k] - 1.0) * (k == 0 ? 0.35f : 0.12f); }
            v += 0.05f * U (rng);
            b.setSample (0, i, v * 0.55f); b.setSample (1, i, v * 0.55f);
        }
        p.processBlock (b, m);
        if (fr % 2 == 0) ed.tick();
    }
}

static int fails = 0;
static void check (const char* n, bool ok) { std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", n); if (! ok) ++fails; }

// synthesises a mouse event for a component (what the host would deliver)
static juce::MouseEvent ev (juce::Component& c, juce::Point<float> pos, juce::Point<float> downPos, bool dragged)
{
    auto src = juce::Desktop::getInstance().getMainMouseSource();
    return juce::MouseEvent (src, pos, juce::ModifierKeys::leftButtonModifier, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, juce::Time::getCurrentTime(), downPos, juce::Time::getCurrentTime(), 1, dragged);
}

int main (int argc, char** argv)
{
    if (argc < 2) return 2;
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File dir (argv[1]);
    {
        EmberAudioProcessor proc;
        proc.setPlayConfigDetails (2, 2, 48000.0, 1024);
        proc.prepareToPlay (48000.0, 1024);
        std::unique_ptr<juce::AudioProcessorEditor> base (proc.createEditor());
        auto& ed = *dynamic_cast<EmberEditor*> (base.get());

        run (ed, proc, 60);
        shot (ed, dir.getChildFile ("1_default.png"));

        setParam (proc, "bands", 3); setParam (proc, "xo1", 90.0f); setParam (proc, "xo2", 500.0f); setParam (proc, "xo3", 2800.0f);
        setParam (proc, "b1_style", 3); setParam (proc, "b1_drive", 18.0f);
        setParam (proc, "b2_style", 2); setParam (proc, "b2_drive", 22.0f); setParam (proc, "b2_dyn", -40.0f);
        setParam (proc, "b3_style", 12); setParam (proc, "b3_drive", 14.0f); setParam (proc, "b3_tone", 35.0f);
        setParam (proc, "b4_on", 0.0f);
        ed.selectBand (1);
        run (ed, proc, 90);
        shot (ed, dir.getChildFile ("2_four_bands.png"));

        setParam (proc, "bands", 5); ed.selectBand (5);
        setParam (proc, "b6_style", 20); setParam (proc, "b6_drive", 30.0f); setParam (proc, "b6_fb", 55.0f); setParam (proc, "b6_pan", 40.0f);
        setParam (proc, "slope", 4); setParam (proc, "os", 3);
        setParam (proc, "m1_src", 1); setParam (proc, "m1_tgt", 1); setParam (proc, "m1_amt", 35.0f);
        setParam (proc, "m2_src", 3); setParam (proc, "m2_tgt", 8); setParam (proc, "m2_amt", -60.0f);
        setParam (proc, "m3_src", 2); setParam (proc, "m3_tgt", 50); setParam (proc, "m3_amt", 40.0f);
        setParam (proc, "lfo1_rate", 4.0f); setParam (proc, "sl1", 35.0f);
        ed.selectBand (0);
        run (ed, proc, 90);
        shot (ed, dir.getChildFile ("3_six_bands.png"));

        ed.showModulation (true);
        run (ed, proc, 40);
        shot (ed, dir.getChildFile ("4_modulation.png"));
        ed.showModulation (false);
    }
    // mouse handling of the display and the style panel
    {
        EmberAudioProcessor proc;
        proc.setPlayConfigDetails (2, 2, 48000.0, 512);
        proc.prepareToPlay (48000.0, 512);
        std::unique_ptr<juce::AudioProcessorEditor> base (proc.createEditor());
        auto& ed = *dynamic_cast<EmberEditor*> (base.get());
        auto& d = ed.displayForTest();
        auto val = [&] (const char* id) { return proc.apvts.getRawParameterValue (id)->load(); };
        // default 3 bands, crossovers 120 Hz and 800 Hz. plot: x from 44 to 866 (log 20 Hz..20 kHz), top 10
        const auto xOf = [&] (float hz) { return 44.0f + 822.0f * std::log (hz / 20.0f) / std::log (1000.0f); };
        const auto yMid = 150.0f;
        d.mouseMove (ev (d, { xOf (800.0f), yMid }, { xOf (800.0f), yMid }, false));
        d.mouseDown (ev (d, { xOf (800.0f), yMid }, { xOf (800.0f), yMid }, false));
        d.mouseDrag (ev (d, { xOf (1500.0f), yMid }, { xOf (800.0f), yMid }, true));
        d.mouseUp (ev (d, { xOf (1500.0f), yMid }, { xOf (800.0f), yMid }, true));
        check ("dragging the 800 Hz crossover to ~1500 Hz changes xo2", std::fabs (val ("xo2") - 1500.0f) < 60.0f);
        d.mouseDown (ev (d, { xOf (130.0f), yMid }, { xOf (130.0f), yMid }, false));
        d.mouseDrag (ev (d, { xOf (4000.0f), yMid }, { xOf (130.0f), yMid }, true));
        d.mouseUp (ev (d, { xOf (4000.0f), yMid }, { xOf (130.0f), yMid }, true));
        check ("a crossover cannot be dragged past its neighbour", val ("xo1") < val ("xo2") && val ("xo1") > 100.0f);
        d.mouseDoubleClick (ev (d, { xOf (val ("xo2")), yMid }, { xOf (val ("xo2")), yMid }, false));
        check ("double-click resets a crossover", std::fabs (val ("xo2") - 800.0f) < 2.0f);
        // header: band 3 region is x > xo2; power dot at x0 + 31, y = 10 + 11
        const float x3 = xOf (val ("xo2"));
        d.mouseDown (ev (d, { x3 + 31.0f, 21.0f }, { x3 + 31.0f, 21.0f }, false));
        check ("clicking the power dot of band 3 turns it off and selects it", val ("b3_on") < 0.5f && ed.getSelectedBand() == 2);
        d.mouseDown (ev (d, { x3 + 31.0f, 21.0f }, { x3 + 31.0f, 21.0f }, false));
        check ("...and on again", val ("b3_on") > 0.5f);
        d.mouseDown (ev (d, { x3 + 52.0f, 21.0f }, { x3 + 52.0f, 21.0f }, false));
        check ("the S button solos band 3", val ("b3_solo") > 0.5f);
        d.mouseDown (ev (d, { x3 + 52.0f, 21.0f }, { x3 + 52.0f, 21.0f }, false));
        d.mouseDown (ev (d, { 100.0f, 150.0f }, { 100.0f, 150.0f }, false));
        check ("clicking inside band 1 selects it", ed.getSelectedBand() == 0);
        auto& sp = ed.styleForTest();
        sp.mouseDown (ev (sp, { 200.0f, 50.0f }, { 200.0f, 50.0f }, false));
        check ("style arrow right goes to the next style (Warm Tube -> Hot Tube)", val ("b1_style") == 3.0f);
        setParam (proc, "b1_style", 0.0f);
        sp.mouseDown (ev (sp, { 10.0f, 50.0f }, { 10.0f, 50.0f }, false));
        check ("style arrows wrap around (0 -> 27)", val ("b1_style") == 27.0f);
    }

    // lifecycle: what a host does when an effect is added and removed again, many times
    for (int i = 0; i < 30; ++i)
    {
        auto* pr = new EmberAudioProcessor();
        pr->setPlayConfigDetails (2, 2, 48000.0, 512);
        pr->prepareToPlay (48000.0, 512);
        auto* e = pr->createEditor();
        e->createComponentSnapshot (e->getLocalBounds(), true, 1.0f);
        if (i % 2) { juce::AudioBuffer<float> b (2, 512); juce::MidiBuffer m; b.clear(); pr->processBlock (b, m); }
        delete e;
        pr->releaseResources();
        delete pr;
    }
    std::printf ("lifecycle: 30 create/destroy cycles OK\n");
    std::printf (fails ? "%d UI TEST(S) FAILED\n" : "UI tests passed\n", fails);
    return fails ? 1 : 0;
}
