// Renders the Poltergeist window to PNGs (headless).  usage: render_editor <outdir>
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

static double phase = 0.0;
// plays `seconds` of a pad-like signal through the plug-in (60 animation steps per second, 800 samples each)
static void run (PoltergeistEditor& ed, PoltergeistAudioProcessor& p, float seconds, float amp)
{
    juce::AudioBuffer<float> b (2, 800); juce::MidiBuffer m;
    const int frames = (int) (seconds * 60.0f);
    for (int f = 0; f < frames; ++f)
    {
        for (int i = 0; i < 800; ++i)
        {
            const float v = amp * ((float) std::sin (phase) + 0.4f * (float) std::sin (phase * 2.0));
            phase += 2.0 * 3.14159265358979 * 220.0 / 48000.0;
            b.setSample (0, i, v); b.setSample (1, i, v);
        }
        p.processBlock (b, m);
        ed.advanceAnimation (1.0f / 60.0f);
    }
}

int main (int argc, char** argv)
{
    if (argc < 2) return 2;
    juce::ScopedJuceInitialiser_GUI init;
    PoltergeistAudioProcessor proc;
    proc.setPlayConfigDetails (2, 2, 48000.0, 800);
    proc.prepareToPlay (48000.0, 800);
    std::unique_ptr<juce::AudioProcessorEditor> base (proc.createEditor());
    auto& ed = *dynamic_cast<PoltergeistEditor*> (base.get());
    const juce::File dir (argv[1]);

    run (ed, proc, 0.5f, 0.0f);
    shot (ed, dir.getChildFile ("1_silent.png"));
    run (ed, proc, 5.0f, 0.35f);
    shot (ed, dir.getChildFile ("2_default.png"));
    auto set = [&] (const char* id, float plain) { auto* p = proc.apvts.getParameter (id); p->setValueNotifyingHost (p->convertTo0to1 (plain)); };
    set ("depth", 160.0f); set ("rate", 3.0f);
    run (ed, proc, 6.0f, 0.35f);
    shot (ed, dir.getChildFile ("3_deep.png"));
    base.reset();

    // lifecycle: what a host does when an effect is added and removed again, many times
    for (int i = 0; i < 40; ++i)
    {
        auto* pr = new PoltergeistAudioProcessor();
        pr->setPlayConfigDetails (2, 2, 48000.0, 800);
        pr->prepareToPlay (48000.0, 800);
        auto* e = pr->createEditor();
        e->setSize (576, 576);
        e->createComponentSnapshot (e->getLocalBounds(), true, 1.0f);
        if (i % 2) { juce::AudioBuffer<float> b (2, 800); juce::MidiBuffer m; b.clear(); pr->processBlock (b, m); }
        delete e;                 // the host closes the editor first ...
        pr->releaseResources();
        delete pr;                // ... then removes the plug-in
    }
    std::printf ("lifecycle: 40 create/destroy cycles OK\n");
    return 0;
}
