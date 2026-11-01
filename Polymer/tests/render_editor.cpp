// Renders the Polymer window to PNGs (headless).  usage: render_editor <sample.wav> <outdir>
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include <cstdio>

static void shot (juce::AudioProcessorEditor& ed, const juce::File& f)
{
    juce::Image img = ed.createComponentSnapshot (ed.getLocalBounds(), true, 1.0f);
    f.deleteFile();
    juce::FileOutputStream out (f);
    juce::PNGImageFormat png;
    png.writeImageToStream (img, out);
    std::printf ("wrote %s (%d x %d)\n", f.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
}

static void run (PolymerEditor& ed, PolymerAudioProcessor& p, float seconds)
{
    for (float t = 0.0f; t < seconds; t += 1.0f / 30.0f)
    {
        ed.advanceAnimation (1.0f / 30.0f);
        juce::Thread::sleep (8);
    }
    (void) p;
}

int main (int argc, char** argv)
{
    if (argc < 3) return 2;
    juce::ScopedJuceInitialiser_GUI init;
    PolymerAudioProcessor proc;
    proc.setPlayConfigDetails (0, 2, 48000.0, 512);
    proc.prepareToPlay (48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> base (proc.createEditor());
    auto& ed = *dynamic_cast<PolymerEditor*> (base.get());
    const juce::File dir (argv[2]);

    run (ed, proc, 0.5f);
    shot (ed, dir.getChildFile ("1_empty.png"));

    proc.loadSampleFile (juce::File (argv[1]));
    run (ed, proc, 0.9f);
    shot (ed, dir.getChildFile ("2_dropping.png"));
    run (ed, proc, 2.5f);
    shot (ed, dir.getChildFile ("3_loaded.png"));

    proc.transmute(); ed.getScene().kick();
    run (ed, proc, 0.5f);
    shot (ed, dir.getChildFile ("4_transmuting.png"));
    proc.waitUntilIdle (20000);
    run (ed, proc, 4.0f);
    shot (ed, dir.getChildFile ("5_cloud.png"));

    auto orbs = proc.getOrbs();
    proc.selectOrb (orbs[1].id, true, true);
    proc.waitUntilIdle (20000);
    run (ed, proc, 4.0f);
    shot (ed, dir.getChildFile ("6_bloom.png"));
    return 0;
}
