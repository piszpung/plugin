// Renders the plugin window to a PNG (headless): render_editor out.png [stretch] [seconds]
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include <cstdio>
#include <cstdlib>

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    ChronosphereAudioProcessor proc;
    proc.setPlayConfigDetails (2, 2, 48000.0, 512);
    proc.prepareToPlay (48000.0, 512);

    if (argc > 2)
    {
        auto* p = proc.apvts.getParameter ("stretch");
        p->setValueNotifyingHost (p->convertTo0to1 ((float) std::atof (argv[2])));
    }
    proc.apvts.getParameter ("key")->setValueNotifyingHost (proc.apvts.getParameter ("key")->convertTo0to1 (3.0f));
    proc.apvts.getParameter ("scale")->setValueNotifyingHost (proc.apvts.getParameter ("scale")->convertTo0to1 (1.0f));

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    auto* editor = dynamic_cast<ChronosphereEditor*> (ed.get());
    editor->advanceAnimation (argc > 3 ? (float) std::atof (argv[3]) : 3.0f);

    juce::Image img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
    juce::File f (argv[1]);
    f.deleteFile();
    juce::FileOutputStream out (f);
    juce::PNGImageFormat png;
    png.writeImageToStream (img, out);
    std::printf ("wrote %s (%d x %d)\n", argv[1], img.getWidth(), img.getHeight());
    return 0;
}
