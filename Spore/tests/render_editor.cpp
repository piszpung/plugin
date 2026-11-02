// Renders the Spore window to PNGs (headless).  usage: render_editor <outdir>
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

static std::mt19937 rng (11);
static double phase = 0.0;

// plays `seconds` of a pad-like signal through the plug-in while the editor animates
static void run (SporeEditor& ed, SporeAudioProcessor& p, float seconds, bool silent = false)
{
    juce::AudioBuffer<float> b (2, 512);
    juce::MidiBuffer m;
    std::normal_distribution<float> nd (0.0f, 1.0f);
    const int frames = (int) (seconds * 30.0f);
    for (int f = 0; f < frames; ++f)
    {
        for (int blk = 0; blk < 3; ++blk)
        {
            for (int i = 0; i < 512; ++i)
            {
                const float v = silent ? 0.0f : 0.22f * ((float) std::sin (phase) + 0.5f * (float) std::sin (phase * 2.01) + 0.3f * (float) std::sin (phase * 3.02)) + 0.02f * nd (rng);
                phase += 2.0 * 3.14159265358979 * 196.0 / 48000.0;
                b.setSample (0, i, v); b.setSample (1, i, v);
            }
            p.processBlock (b, m);
        }
        ed.advanceAnimation (1.0f / 30.0f);
        juce::Thread::sleep (6);
    }
}

int main (int argc, char** argv)
{
    if (argc < 2) return 2;
    juce::ScopedJuceInitialiser_GUI init;
    SporeAudioProcessor proc;
    proc.setPlayConfigDetails (2, 2, 48000.0, 512);
    proc.prepareToPlay (48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> base (proc.createEditor());
    auto& ed = *dynamic_cast<SporeEditor*> (base.get());
    const juce::File dir (argv[1]);

    run (ed, proc, 0.3f, true);
    shot (ed, dir.getChildFile ("1_idle.png"));
    run (ed, proc, 3.0f);
    shot (ed, dir.getChildFile ("2_playing.png"));
    proc.apvts.getParameter ("freeze")->setValueNotifyingHost (1.0f);
    proc.apvts.getParameter ("density")->setValueNotifyingHost (0.9f);
    run (ed, proc, 2.0f, true);
    shot (ed, dir.getChildFile ("3_frozen.png"));
    return 0;
}
