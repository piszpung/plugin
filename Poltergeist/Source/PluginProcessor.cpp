#include "PluginProcessor.h"
#include "PluginEditor.h"

juce::AudioProcessorValueTreeState::ParameterLayout PoltergeistAudioProcessor::createLayout()
{
    using P = juce::AudioParameterFloat;
    using juce::ParameterID;
    juce::AudioProcessorValueTreeState::ParameterLayout l;

    // Rate: 0.5 - 12 Hz, the middle of the knob is about 3 Hz
    juce::NormalisableRange<float> rateRange (polter::kMinRate, polter::kMaxRate, 0.01f);
    rateRange.setSkewForCentre (3.0f);
    l.add (std::make_unique<P> (ParameterID { "rate", 1 }, "Rate", rateRange, 5.0f,
        juce::AudioParameterFloatAttributes().withLabel ("Hz")
            .withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1) + " Hz"; })));

    // Depth: pitch swing in cents (100 cents = one semitone), 0 - 200
    juce::NormalisableRange<float> depthRange (0.0f, polter::kMaxDepth, 0.1f);
    depthRange.setSkewForCentre (50.0f);
    l.add (std::make_unique<P> (ParameterID { "depth", 1 }, "Depth", depthRange, 30.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ct")
            .withStringFromValueFunction ([] (float v, int) { return juce::String (juce::CharPointer_UTF8 ("\xC2\xB1")) + juce::String (juce::roundToInt (v)) + " ct"; })));
    return l;
}

PoltergeistAudioProcessor::PoltergeistAudioProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    pRate = apvts.getRawParameterValue ("rate");
    pDepth = apvts.getRawParameterValue ("depth");
    engine.prepare (48000.0);
    rate = 48000.0;
}

bool PoltergeistAudioProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    return in == out && (in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono());
}

void PoltergeistAudioProcessor::prepareToPlay (double sampleRate, int)
{
    if (sampleRate != rate) { engine.prepare (sampleRate); rate = sampleRate; }
    else engine.reset();
}

void PoltergeistAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int nch = std::min (buffer.getNumChannels(), 2);
    if (n <= 0 || nch <= 0) return;

    engine.process (buffer.getArrayOfWritePointers(), nch, n, pRate->load(), pDepth->load());

    float pk = 0.0f;
    for (int c = 0; c < nch; ++c)
    {
        const float* d = buffer.getReadPointer (c);
        for (int i = 0; i < n; ++i) pk = std::max (pk, std::fabs (d[i]));
    }
    outPeak.store (std::max (outPeak.load (std::memory_order_relaxed), pk), std::memory_order_relaxed);
    deviation.store (engine.currentDeviationCents(), std::memory_order_relaxed);
}

juce::AudioProcessorEditor* PoltergeistAudioProcessor::createEditor() { return new PoltergeistEditor (*this); }

void PoltergeistAudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
}

void PoltergeistAudioProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PoltergeistAudioProcessor(); }
