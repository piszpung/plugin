#include "PluginProcessor.h"
#include "PluginEditor.h"

juce::AudioProcessorValueTreeState::ParameterLayout SporeAudioProcessor::createLayout()
{
    using P = juce::AudioParameterFloat;
    using juce::ParameterID;
    juce::AudioProcessorValueTreeState::ParameterLayout l;

    auto text = [] (auto fn) { return [fn] (float v, int) { return juce::String (fn (v)); }; };
    auto unit = juce::NormalisableRange<float> (0.0f, 1.0f, 0.0001f);

    l.add (std::make_unique<P> (ParameterID { "density", 1 }, "Density", unit, 0.6f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (text ([] (float v) { return juce::String (juce::roundToInt (spore::grainCount (v))) + " grains"; }))));
    l.add (std::make_unique<P> (ParameterID { "size", 1 }, "Size", unit, 0.8f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (text ([] (float v) { return juce::String (juce::roundToInt (spore::grainMs (v))) + " ms"; }))));
    l.add (std::make_unique<P> (ParameterID { "blur", 1 }, "Blur", unit, 0.3f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (text ([] (float v) { return juce::String (juce::CharPointer_UTF8 ("\xC2\xB1")) + juce::String (juce::roundToInt (spore::blurCents (v))) + " ct"; }))));
    l.add (std::make_unique<P> (ParameterID { "decay", 1 }, "Decay", unit, 0.2f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (text ([] (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; }))));
    l.add (std::make_unique<juce::AudioParameterBool> (ParameterID { "freeze", 1 }, "Germinate (freeze)", false));
    return l;
}

SporeAudioProcessor::SporeAudioProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    pDensity = apvts.getRawParameterValue ("density");
    pSize    = apvts.getRawParameterValue ("size");
    pBlur    = apvts.getRawParameterValue ("blur");
    pDecay   = apvts.getRawParameterValue ("decay");
    pFreeze  = apvts.getRawParameterValue ("freeze");
    engine.prepare (48000.0);      // so that getGrain etc. are valid before the host calls prepareToPlay
    rate = 48000.0;
}

bool SporeAudioProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    return l.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
        && l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void SporeAudioProcessor::prepareToPlay (double sampleRate, int)
{
    if (sampleRate != rate)
    {
        engine.prepare (sampleRate);
        rate = sampleRate;
    }
    else engine.reset();
}

void SporeAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (buffer.getNumChannels() < 2 || n <= 0) return;

    spore::Params p;
    p.density = pDensity->load();
    p.size    = pSize->load();
    p.blur    = pBlur->load();
    p.decay   = pDecay->load();
    p.freeze  = pFreeze->load() > 0.5f;

    float* l = buffer.getWritePointer (0);
    float* r = buffer.getWritePointer (1);

    float pkIn = 0.0f;
    for (int i = 0; i < n; ++i) pkIn = std::max (pkIn, std::max (std::fabs (l[i]), std::fabs (r[i])));

    engine.process (l, r, l, r, n, p);

    float pkOut = 0.0f;
    for (int i = 0; i < n; ++i) pkOut = std::max (pkOut, std::max (std::fabs (l[i]), std::fabs (r[i])));

    inPeak.store (std::max (inPeak.load (std::memory_order_relaxed), pkIn), std::memory_order_relaxed);
    outPeak.store (std::max (outPeak.load (std::memory_order_relaxed), pkOut), std::memory_order_relaxed);

    engine.snapshot (viz);
    for (int i = 0; i < kGrains; ++i)
    {
        vizPan[(size_t) i].store (viz[i].pan, std::memory_order_relaxed);
        vizDepth[(size_t) i].store (viz[i].depth, std::memory_order_relaxed);
        vizEnv[(size_t) i].store (viz[i].env, std::memory_order_relaxed);
        vizOn[(size_t) i].store (viz[i].active, std::memory_order_relaxed);
    }
}

juce::AudioProcessorEditor* SporeAudioProcessor::createEditor() { return new SporeEditor (*this); }

void SporeAudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
}

void SporeAudioProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SporeAudioProcessor(); }
