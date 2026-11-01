#include "../Source/PluginProcessor.h"
#include <cstdio>
int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    PolymerAudioProcessor proc;
    proc.setPlayConfigDetails (0, 2, 48000.0, 512);
    proc.prepareToPlay (48000.0, 512);
    for (int i = 1; i < argc; ++i)
        std::printf ("%s -> %d\n", argv[i], (int) proc.loadSampleFile (juce::File (argv[i])));
    std::printf ("done\n");
}
