#include "PluginProcessor.h"
#include "PluginEditor.h"

juce::AudioProcessorValueTreeState::ParameterLayout ResidueAudioProcessor::createLayout()
{
    using F = juce::AudioParameterFloat;
    using C = juce::AudioParameterChoice;
    using B = juce::AudioParameterBool;
    using A = juce::AudioParameterFloatAttributes;
    juce::AudioProcessorValueTreeState::ParameterLayout l;

    auto pct = [] (float v, int) { return juce::String (juce::roundToInt (v)) + " %"; };
    auto hz = [] (float v, int) { return v >= 1000.0f ? juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz"; };
    auto ms = [] (float v, int) { return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " s" : juce::String (juce::roundToInt (v)) + " ms"; };

    juce::StringArray styles, notes;
    for (int i = 0; i < residue::kNumStyles; ++i) styles.add (residue::styleName (i));
    for (int i = 0; i < residue::kNumNotes; ++i) notes.add (residue::noteName (i));

    juce::NormalisableRange<float> tr (10.0f, 2000.0f, 1.0f); tr.setSkewForCentre (350.0f);
    juce::NormalisableRange<float> lr (20.0f, 2000.0f, 1.0f); lr.setSkewForCentre (200.0f);
    juce::NormalisableRange<float> hr (1000.0f, 20000.0f, 10.0f); hr.setSkewForCentre (6000.0f);

    l.add (std::make_unique<C> (juce::ParameterID { "style", 1 }, "Style", styles, residue::kTape));
    l.add (std::make_unique<B> (juce::ParameterID { "sync", 1 }, "Sync", true));
    l.add (std::make_unique<C> (juce::ParameterID { "note", 1 }, "Note", notes, 7));
    l.add (std::make_unique<F> (juce::ParameterID { "time", 1 }, "Time", tr, 350.0f, A().withStringFromValueFunction (ms)));
    l.add (std::make_unique<F> (juce::ParameterID { "fb", 1 }, "Feedback", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 35.0f, A().withStringFromValueFunction (pct)));
    l.add (std::make_unique<F> (juce::ParameterID { "sat", 1 }, "Saturation", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 15.0f, A().withStringFromValueFunction (pct)));
    l.add (std::make_unique<F> (juce::ParameterID { "wobble", 1 }, "Wobble", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 15.0f, A().withStringFromValueFunction (pct)));
    l.add (std::make_unique<F> (juce::ParameterID { "duck", 1 }, "Ducking", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f, A().withStringFromValueFunction (pct)));
    l.add (std::make_unique<F> (juce::ParameterID { "spread", 1 }, "Ping-Pong", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f, A().withStringFromValueFunction (pct)));
    l.add (std::make_unique<F> (juce::ParameterID { "low", 1 }, "Low Cut", lr, 80.0f, A().withStringFromValueFunction (hz)));
    l.add (std::make_unique<F> (juce::ParameterID { "high", 1 }, "High Cut", hr, 12000.0f, A().withStringFromValueFunction (hz)));
    l.add (std::make_unique<F> (juce::ParameterID { "mix", 1 }, "Mix", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 30.0f, A().withStringFromValueFunction (pct)));
    return l;
}

ResidueAudioProcessor::ResidueAudioProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true).withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    auto get = [this] (const char* id) { return apvts.getRawParameterValue (id); };
    pTime = get ("time"); pSync = get ("sync"); pNote = get ("note"); pFb = get ("fb"); pSat = get ("sat"); pWob = get ("wobble");
    pDuck = get ("duck"); pSpread = get ("spread"); pLow = get ("low"); pHigh = get ("high"); pMix = get ("mix"); pStyle = get ("style");
}

bool ResidueAudioProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto o = l.getMainOutputChannelSet();
    return (o == juce::AudioChannelSet::mono() || o == juce::AudioChannelSet::stereo()) && o == l.getMainInputChannelSet();
}

void ResidueAudioProcessor::prepareToPlay (double sr, int samplesPerBlock)
{
    hostRate = sr;
    engine.prepare (sr, kMaxDelaySeconds);
    scratch.setSize (2, std::max (samplesPerBlock, kChunk), false, true, false);
}

void ResidueAudioProcessor::processBlock (juce::AudioBuffer<float>& buf, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buf.getNumSamples(), nch = buf.getNumChannels();
    if (nch < 1 || n < 1) return;
    if (scratch.getNumSamples() < n) scratch.setSize (2, n, false, false, true);   // only if the host exceeds its announced block size

    double bpm = 120.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm()) if (*b > 20.0 && *b < 999.0) bpm = *b;

    float* L = buf.getWritePointer (0);
    float* R = nch > 1 ? buf.getWritePointer (1) : scratch.getWritePointer (1);
    if (nch < 2) std::copy (L, L + n, R);

    residue::Params p;
    p.fb = *pFb * 0.01f; p.sat = *pSat * 0.01f; p.wobble = *pWob * 0.01f; p.duck = *pDuck * 0.01f; p.spread = *pSpread * 0.01f;
    p.mix = *pMix * 0.01f; p.lowHz = *pLow; p.highHz = *pHigh; p.style = (int) *pStyle;
    const double sec = *pSync > 0.5f ? residue::noteBeats ((int) *pNote) * 60.0 / bpm : (double) *pTime * 0.001;
    const double maxSec = (double) (engine.maxDelaySamples() - 8) / hostRate;
    const double clamped = std::clamp (sec, 0.003, maxSec);
    p.timeSamples = (float) (clamped * hostRate);
    delaySec.store ((float) clamped, std::memory_order_relaxed);
    engine.setParams (p);

    float inPk = 0.0f, outPk = 0.0f;
    for (int s0 = 0; s0 < n; s0 += kChunk)
    {
        const int len = std::min (kChunk, n - s0);
        engine.process (L + s0, R + s0, len);
        inPk = std::max (inPk, engine.lastInputPeak()); outPk = std::max (outPk, engine.lastOutputPeak());
    }
    if (nch > 2) for (int c = 2; c < nch; ++c) buf.clear (c, 0, n);

    const float ci = inLevel.load (std::memory_order_relaxed), co = outLevel.load (std::memory_order_relaxed);
    inLevel.store (std::max (inPk, ci * 0.85f), std::memory_order_relaxed);
    outLevel.store (std::max (outPk, co * 0.85f), std::memory_order_relaxed);
}

void ResidueAudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
}
void ResidueAudioProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType())) apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* ResidueAudioProcessor::createEditor() { return new ResidueEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ResidueAudioProcessor(); }
