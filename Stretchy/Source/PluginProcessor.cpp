#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout ChronosphereAudioProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;

    // Stretch: 100 % ... 3000 %, logarithmic-ish (centre of the knob = ~548 %)
    NormalisableRange<float> stretchRange (100.0f, 3000.0f, 1.0f);
    stretchRange.setSkewForCentre (548.0f);
    p.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { "stretch", 1 }, "Stretch", stretchRange, 500.0f,
        AudioParameterFloatAttributes().withLabel ("%")));

    // LPF: 100 Hz ... 20 kHz
    NormalisableRange<float> lpfRange (100.0f, 20000.0f, 1.0f);
    lpfRange.setSkewForCentre (1414.0f);
    p.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { "lpf", 1 }, "LPF", lpfRange, 1000.0f,
        AudioParameterFloatAttributes().withLabel ("Hz")));

    p.push_back (std::make_unique<AudioParameterChoice> (
        ParameterID { "key", 1 }, "Key",
        StringArray { "None", "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 0));

    p.push_back (std::make_unique<AudioParameterChoice> (
        ParameterID { "scale", 1 }, "Scale",
        StringArray { "Major", "Minor", "Dorian", "Phrygian", "Lydian", "Mixolydian", "Locrian",
                      "Harmonic Minor", "Pentatonic Major", "Pentatonic Minor", "Blues" }, 0));

    p.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { "mix", 1 }, "Mix", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
        AudioParameterFloatAttributes().withLabel ("%")));

    p.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { "output", 1 }, "Output", NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("dB")));

    p.push_back (std::make_unique<AudioParameterBool> (
        ParameterID { "autoreset", 1 }, "Reset on play", true));

    p.push_back (std::make_unique<AudioParameterBool> (
        ParameterID { "safe", 1 }, "Safe clip", true));

    return { p.begin(), p.end() };
}

//==============================================================================
ChronosphereAudioProcessor::ChronosphereAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    pStretch   = apvts.getRawParameterValue ("stretch");
    pLpf       = apvts.getRawParameterValue ("lpf");
    pKey       = apvts.getRawParameterValue ("key");
    pScale     = apvts.getRawParameterValue ("scale");
    pMix       = apvts.getRawParameterValue ("mix");
    pOutput    = apvts.getRawParameterValue ("output");
    pAutoReset = apvts.getRawParameterValue ("autoreset");
    pSafe      = apvts.getRawParameterValue ("safe");
}

bool ChronosphereAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet()  == juce::AudioChannelSet::stereo()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void ChronosphereAudioProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    if (sampleRate != currentSampleRate)
    {
        currentSampleRate = sampleRate;
        engine.prepare (sampleRate);
    }
    else
    {
        engine.reset();
    }

    dryL.assign ((size_t) kChunk, 0.0f);
    dryR.assign ((size_t) kChunk, 0.0f);
    wetL.assign ((size_t) kChunk, 0.0f);
    wetR.assign ((size_t) kChunk, 0.0f);

    lastMixGain = pMix->load() * 0.01f;
    lastOutGain = juce::Decibels::decibelsToGain (pOutput->load());
    lastGate = 1.0f;
    wasPlaying = true;
    resetWhenSilent = false;
    lastTimeInSamples = -1;
}

//==============================================================================
void ChronosphereAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    if (buffer.getNumChannels() < 2 || currentSampleRate <= 0.0 || numSamples <= 0)
        return;

    const bool autoReset = pAutoReset->load() > 0.5f;

    // --- transport: the pad only sounds while the host plays --------------------------------
    // (hosts that give no position information are treated as "playing")
    bool playing = true;
    juce::int64 t = -1;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            playing = pos->getIsPlaying();
            if (auto ts = pos->getTimeInSamples()) t = *ts;
        }

    if (playing)
    {
        const bool started = ! wasPlaying;
        const bool jumpedBack = t >= 0 && lastTimeInSamples >= 0 && t < lastTimeInSamples;
        if (autoReset && (started || jumpedBack))
            engine.reset();
        resetWhenSilent = false;
    }
    else if (wasPlaying)
    {
        resetWhenSilent = autoReset;     // restart the pad on the next play
    }
    wasPlaying = playing;
    lastTimeInSamples = (t >= 0 && playing) ? t + numSamples : -1;

    const float gate = playing ? 1.0f : 0.0f;
    const bool runEngine = playing || lastGate > 0.0f;

    // --- parameters ---------------------------------------------------------------------------
    engine.setStretch (pStretch->load() * 0.01f);
    engine.setLowpass (pLpf->load());
    engine.setScale ((int) pKey->load() - 1, (int) pScale->load());

    const float mixGain = pMix->load() * 0.01f;
    const float outGain = juce::Decibels::decibelsToGain (pOutput->load());
    const bool safe = pSafe->load() > 0.5f;

    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);

    for (int offset = 0; offset < numSamples; offset += kChunk)
    {
        const int n = juce::jmin (kChunk, numSamples - offset);

        std::copy (L + offset, L + offset + n, dryL.begin());
        std::copy (R + offset, R + offset + n, dryR.begin());

        if (runEngine)
        {
            engine.process (dryL.data(), dryR.data(), wetL.data(), wetR.data(), n);
        }
        else
        {
            std::fill (wetL.begin(), wetL.begin() + n, 0.0f);
            std::fill (wetR.begin(), wetR.begin() + n, 0.0f);
        }

        // linear ramps for mix / output gain / transport gate across the whole host block (no zipper noise)
        for (int i = 0; i < n; ++i)
        {
            const float a = (float) (offset + i + 1) / (float) numSamples;
            const float mg = lastMixGain + (mixGain - lastMixGain) * a;
            const float og = lastOutGain + (outGain - lastOutGain) * a;
            const float gg = lastGate + (gate - lastGate) * a;

            float l = (dryL[(size_t) i] * (1.0f - mg) + wetL[(size_t) i] * mg * gg) * og;
            float r = (dryR[(size_t) i] * (1.0f - mg) + wetR[(size_t) i] * mg * gg) * og;
            if (safe) { l = slowmo::softClip (l); r = slowmo::softClip (r); }
            L[offset + i] = l;
            R[offset + i] = r;
        }
    }

    lastMixGain = mixGain;
    lastOutGain = outGain;
    lastGate = gate;

    if (! playing && resetWhenSilent)    // fade-out finished: forget the old material
    {
        engine.reset();
        resetWhenSilent = false;
    }
}

//==============================================================================
juce::AudioProcessorEditor* ChronosphereAudioProcessor::createEditor()
{
    return new ChronosphereEditor (*this);
}

void ChronosphereAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void ChronosphereAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ChronosphereAudioProcessor();
}
