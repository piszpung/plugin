#include "PluginProcessor.h"
#include "PluginEditor.h"

using juce::ParameterID;

juce::AudioProcessorValueTreeState::ParameterLayout EmberAudioProcessor::createLayout()
{
    using F = juce::AudioParameterFloat;
    using C = juce::AudioParameterChoice;
    using B = juce::AudioParameterBool;
    using A = juce::AudioParameterFloatAttributes;
    juce::AudioProcessorValueTreeState::ParameterLayout l;

    auto dbText = [] (float v, int) { return juce::String (v, 1) + " dB"; };
    auto pctText = [] (float v, int) { return juce::String (juce::roundToInt (v)) + " %"; };
    auto sgnPct = [] (float v, int) { return (v > 0.5f ? "+" : "") + juce::String (juce::roundToInt (v)) + " %"; };
    auto hzText = [] (float v, int) { return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz"; };
    auto msText = [] (float v, int) { return v >= 100.0f ? juce::String (juce::roundToInt (v)) + " ms" : juce::String (v, 1) + " ms"; };

    l.add (std::make_unique<F> (ParameterID { "in", 1 }, "Input", juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f, A().withStringFromValueFunction (dbText)));
    l.add (std::make_unique<F> (ParameterID { "out", 1 }, "Output", juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f, A().withStringFromValueFunction (dbText)));
    l.add (std::make_unique<F> (ParameterID { "mix", 1 }, "Mix", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, A().withStringFromValueFunction (pctText)));
    l.add (std::make_unique<C> (ParameterID { "bands", 1 }, "Bands", juce::StringArray { "1", "2", "3", "4", "5", "6" }, 2));
    l.add (std::make_unique<C> (ParameterID { "os", 1 }, "Oversampling", juce::StringArray { "Off", "2x", "4x", "8x", "16x", "32x" }, 1));
    l.add (std::make_unique<C> (ParameterID { "slope", 1 }, "Crossover Slope", juce::StringArray { "6 dB/oct", "12 dB/oct", "24 dB/oct", "36 dB/oct", "48 dB/oct" }, 2));
    l.add (std::make_unique<B> (ParameterID { "ms", 1 }, "Mid/Side", false));
    l.add (std::make_unique<B> (ParameterID { "limit", 1 }, "Output Limit", false));

    const float xoDef[5] = { 120.0f, 800.0f, 3000.0f, 7000.0f, 12000.0f };
    for (int k = 0; k < 5; ++k)
    {
        juce::NormalisableRange<float> r (20.0f, 20000.0f, 1.0f); r.setSkewForCentre (1000.0f);
        l.add (std::make_unique<F> (ParameterID { "xo" + juce::String (k + 1), 1 }, "Crossover " + juce::String (k + 1), r, xoDef[k], A().withStringFromValueFunction (hzText)));
    }

    juce::StringArray styles;
    for (int i = 0; i < ember::kNumStyles; ++i) styles.add (ember::styleName (i));
    for (int b = 1; b <= ember::kMaxBands; ++b)
    {
        const auto p = "b" + juce::String (b) + "_";
        const auto nm = "Band " + juce::String (b) + " ";
        l.add (std::make_unique<B> (ParameterID { p + "on", 1 }, nm + "On", true));
        l.add (std::make_unique<B> (ParameterID { p + "solo", 1 }, nm + "Solo", false));
        l.add (std::make_unique<C> (ParameterID { p + "style", 1 }, nm + "Style", styles, 2));
        l.add (std::make_unique<F> (ParameterID { p + "drive", 1 }, nm + "Drive", juce::NormalisableRange<float> (0.0f, 60.0f, 0.1f), 12.0f, A().withStringFromValueFunction (dbText)));
        l.add (std::make_unique<F> (ParameterID { p + "level", 1 }, nm + "Level", juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f, A().withStringFromValueFunction (dbText)));
        l.add (std::make_unique<F> (ParameterID { p + "mix", 1 }, nm + "Mix", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, A().withStringFromValueFunction (pctText)));
        l.add (std::make_unique<F> (ParameterID { p + "dyn", 1 }, nm + "Dynamics", juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f, A().withStringFromValueFunction (sgnPct)));
        l.add (std::make_unique<F> (ParameterID { p + "tone", 1 }, nm + "Tone", juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f, A().withStringFromValueFunction (sgnPct)));
        l.add (std::make_unique<F> (ParameterID { p + "fb", 1 }, nm + "Feedback", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f, A().withStringFromValueFunction (pctText)));
        juce::NormalisableRange<float> fr (20.0f, 20000.0f, 1.0f); fr.setSkewForCentre (800.0f);
        l.add (std::make_unique<F> (ParameterID { p + "fbf", 1 }, nm + "Feedback Freq", fr, 800.0f, A().withStringFromValueFunction (hzText)));
        l.add (std::make_unique<F> (ParameterID { p + "pan", 1 }, nm + "Pan", juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f,
                                    A().withStringFromValueFunction ([] (float v, int) { return v < -0.5f ? juce::String (juce::roundToInt (-v)) + " % L" : v > 0.5f ? juce::String (juce::roundToInt (v)) + " % R" : juce::String ("Center"); })));
    }

    // ---- modulation sources ----
    for (int k = 1; k <= ember::kNumLfos; ++k)
    {
        const auto p = "lfo" + juce::String (k) + "_";
        juce::NormalisableRange<float> r (0.05f, 30.0f, 0.01f); r.setSkewForCentre (2.0f);
        l.add (std::make_unique<F> (ParameterID { p + "rate", 1 }, "LFO " + juce::String (k) + " Rate", r, k == 1 ? 1.0f : 0.5f, A().withStringFromValueFunction ([] (float v, int) { return juce::String (v, 2) + " Hz"; })));
        juce::StringArray sy; for (int i = 0; i < ember::kNumSyncModes; ++i) sy.add (ember::syncName (i));
        l.add (std::make_unique<C> (ParameterID { p + "sync", 1 }, "LFO " + juce::String (k) + " Sync", sy, 0));
        l.add (std::make_unique<F> (ParameterID { p + "glide", 1 }, "LFO " + juce::String (k) + " Glide", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 60.0f, A().withStringFromValueFunction (pctText)));
    }
    for (int k = 1; k <= ember::kNumEnvs; ++k)
    {
        const auto p = "env" + juce::String (k) + "_";
        juce::NormalisableRange<float> a (0.5f, 200.0f, 0.1f); a.setSkewForCentre (12.0f);
        juce::NormalisableRange<float> rl (5.0f, 2000.0f, 1.0f); rl.setSkewForCentre (150.0f);
        l.add (std::make_unique<F> (ParameterID { p + "atk", 1 }, "Envelope " + juce::String (k) + " Attack", a, 5.0f, A().withStringFromValueFunction (msText)));
        l.add (std::make_unique<F> (ParameterID { p + "rel", 1 }, "Envelope " + juce::String (k) + " Release", rl, 150.0f, A().withStringFromValueFunction (msText)));
        l.add (std::make_unique<F> (ParameterID { p + "gain", 1 }, "Envelope " + juce::String (k) + " Gain", juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f, A().withStringFromValueFunction (dbText)));
    }
    for (int k = 1; k <= ember::kNumSliders; ++k)
        l.add (std::make_unique<F> (ParameterID { "sl" + juce::String (k), 1 }, "Slider " + juce::String (k), juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f, A().withStringFromValueFunction (pctText)));

    // ---- routes: source -> target ----
    juce::StringArray srcs; for (int i = 0; i < ember::kNumSources; ++i) srcs.add (ember::sourceName (i));
    juce::StringArray tgts; for (int i = 0; i < ember::kNumTargets; ++i) { char b[48]; ember::targetName (i, b, sizeof b); tgts.add (b); }
    for (int r = 1; r <= ember::kNumRoutes; ++r)
    {
        const auto p = "m" + juce::String (r) + "_";
        l.add (std::make_unique<C> (ParameterID { p + "src", 1 }, "Mod " + juce::String (r) + " Source", srcs, 0));
        l.add (std::make_unique<C> (ParameterID { p + "tgt", 1 }, "Mod " + juce::String (r) + " Target", tgts, 0));
        l.add (std::make_unique<F> (ParameterID { p + "amt", 1 }, "Mod " + juce::String (r) + " Amount", juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 50.0f, A().withStringFromValueFunction (sgnPct)));
    }
    return l;
}

EmberAudioProcessor::EmberAudioProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    pIn = apvts.getRawParameterValue ("in");       pOut = apvts.getRawParameterValue ("out");     pMix = apvts.getRawParameterValue ("mix");
    pBands = apvts.getRawParameterValue ("bands"); pOs = apvts.getRawParameterValue ("os");       pMs = apvts.getRawParameterValue ("ms");
    pLimit = apvts.getRawParameterValue ("limit"); pSlope = apvts.getRawParameterValue ("slope");
    for (int k = 0; k < 5; ++k) pXo[(size_t) k] = apvts.getRawParameterValue ("xo" + juce::String (k + 1));
    for (int b = 0; b < ember::kMaxBands; ++b)
    {
        const auto p = "b" + juce::String (b + 1) + "_";
        auto& q = pb[(size_t) b];
        q.on = apvts.getRawParameterValue (p + "on");       q.solo = apvts.getRawParameterValue (p + "solo");   q.style = apvts.getRawParameterValue (p + "style");
        q.drive = apvts.getRawParameterValue (p + "drive"); q.level = apvts.getRawParameterValue (p + "level"); q.mix = apvts.getRawParameterValue (p + "mix");
        q.dyn = apvts.getRawParameterValue (p + "dyn");     q.tone = apvts.getRawParameterValue (p + "tone");   q.fb = apvts.getRawParameterValue (p + "fb");
        q.fbf = apvts.getRawParameterValue (p + "fbf");     q.pan = apvts.getRawParameterValue (p + "pan");
    }
    for (int k = 0; k < ember::kNumLfos; ++k)
    {
        const auto p = "lfo" + juce::String (k + 1) + "_";
        pLfo[(size_t) k] = { apvts.getRawParameterValue (p + "rate"), apvts.getRawParameterValue (p + "sync"), apvts.getRawParameterValue (p + "glide") };
    }
    for (int k = 0; k < ember::kNumEnvs; ++k)
    {
        const auto p = "env" + juce::String (k + 1) + "_";
        pEnv[(size_t) k] = { apvts.getRawParameterValue (p + "atk"), apvts.getRawParameterValue (p + "rel"), apvts.getRawParameterValue (p + "gain") };
    }
    for (int k = 0; k < ember::kNumSliders; ++k) pSlider[(size_t) k] = apvts.getRawParameterValue ("sl" + juce::String (k + 1));
    for (int r = 0; r < ember::kNumRoutes; ++r)
    {
        const auto p = "m" + juce::String (r + 1) + "_";
        pRoute[(size_t) r] = { apvts.getRawParameterValue (p + "src"), apvts.getRawParameterValue (p + "tgt"), apvts.getRawParameterValue (p + "amt") };
    }

    // the target table: where each modulation target lives and what its range is
    auto bind = [this] (int t, const juce::String& id)
    {
        if (auto* f = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter (id))) { tgt[(size_t) t].raw = apvts.getRawParameterValue (id); tgt[(size_t) t].range = f->range; }
    };
    for (int b = 0; b < ember::kMaxBands; ++b)
        for (int j = 0; j < ember::kBandTargetParams; ++j) bind (ember::bandTarget (b, j), "b" + juce::String (b + 1) + "_" + ember::bandParamId (j));
    for (int k = 0; k < 5; ++k) bind (ember::crossoverTarget (k), "xo" + juce::String (k + 1));
    bind (ember::kTargetIn, "in"); bind (ember::kTargetOut, "out"); bind (ember::kTargetMix, "mix");

    for (auto& a : anaIn) a.store (0.0f);
    for (auto& a : anaOut) a.store (0.0f);
    for (auto& m : bandMeter) m.store (0.0f);
    for (auto& a : srcValue) a.store (0.0f);
    for (auto& a : lfoPos) a.store (0.0f);
    for (auto& a : modShown) a.store (-1.0f);
    for (auto& l : lfoSteps) for (auto& s : l) s.store (0.0f);
    setLfoShape (0, ember::kShapeSine);
    setLfoShape (1, ember::kShapeRandom, 3);
    for (int i = 1; i <= kMaxOversampleStages; ++i)
        os[(size_t) i] = std::make_unique<juce::dsp::Oversampling<float>> (2, (size_t) i, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
}

EmberAudioProcessor::~EmberAudioProcessor() { cancelPendingUpdate(); }

void EmberAudioProcessor::setLfoShape (int l, int shape, unsigned seed)
{
    std::array<float, ember::kSteps> st {};
    ember::fillShape (st, shape, seed);
    for (int i = 0; i < ember::kSteps; ++i) lfoSteps[(size_t) l][(size_t) i].store (st[(size_t) i], std::memory_order_relaxed);
}

bool EmberAudioProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    return in == out && (in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono());
}

void EmberAudioProcessor::switchOversampling (int idx)
{
    curOs = idx;
    if (idx > 0) { os[(size_t) idx]->reset(); latency = juce::roundToInt (os[(size_t) idx]->getLatencyInSamples()); }
    else latency = 0;
    latency = std::min (latency, ringSize - 1);
    engine.prepare (hostRate * (double) (1 << idx));
    triggerAsyncUpdate();
}

void EmberAudioProcessor::prepareToPlay (double sampleRate, int)
{
    hostRate = sampleRate;
    scratch.setSize (2, kChunk, false, true, false);
    dryRing.setSize (2, ringSize, false, true, false);
    ringPos = 0;
    for (int i = 1; i <= kMaxOversampleStages; ++i) os[(size_t) i]->initProcessing ((size_t) kChunk);
    for (auto& l : lfo) l.reset();
    for (auto& f : follower) f.reset();
    prevIn = prevOut = prevMix = -1.0f;      // "not set yet": the first chunk starts at its own value
    switchOversampling (std::clamp ((int) pOs->load(), 0, kMaxOversampleStages));
    setLatencySamples (latency);
}

void EmberAudioProcessor::readParameters (ember::GlobalParams& g, const float* off) const
{
    // the plain value of a parameter with the modulation of its target added in the 0..1 domain
    auto P = [&] (int t, const std::atomic<float>* raw)
    {
        float v = raw->load (std::memory_order_relaxed);
        if (t > 0 && off[t] != 0.0f)
        {
            const auto& r = tgt[(size_t) t].range;
            v = r.convertFrom0to1 (std::clamp (r.convertTo0to1 (v) + off[t], 0.0f, 1.0f));
        }
        return v;
    };
    g.numBands = std::clamp ((int) pBands->load() + 1, 1, ember::kMaxBands);
    g.slope = std::clamp ((int) pSlope->load(), 0, 4);
    for (int k = 0; k < 5; ++k) g.xoHz[(size_t) k] = P (ember::crossoverTarget (k), pXo[(size_t) k]);
    g.midSide = pMs->load() > 0.5f;
    for (int b = 0; b < ember::kMaxBands; ++b)
    {
        const auto& q = pb[(size_t) b];
        auto& d = g.band[(size_t) b];
        d.on = q.on->load() > 0.5f;       d.solo = q.solo->load() > 0.5f;
        d.style = (int) q.style->load();
        d.driveDb = P (ember::bandTarget (b, 0), q.drive);   d.levelDb = P (ember::bandTarget (b, 1), q.level);
        d.mix = P (ember::bandTarget (b, 2), q.mix) * 0.01f; d.dyn = P (ember::bandTarget (b, 3), q.dyn) * 0.01f;
        d.tone = P (ember::bandTarget (b, 4), q.tone) * 0.01f; d.fb = P (ember::bandTarget (b, 5), q.fb) * 0.01f;
        d.fbHz = P (ember::bandTarget (b, 6), q.fbf);        d.pan = P (ember::bandTarget (b, 7), q.pan) * 0.01f;
    }
}

static inline float softLimit (float y)
{
    const float a = std::fabs (y);
    if (a <= 0.9f) return y;
    const float s = 0.9f + 0.1f * ember::fastTanh ((a - 0.9f) * 10.0f);
    return y < 0.0f ? -s : s;
}

void EmberAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int total = buffer.getNumSamples();
    const int nch = std::min (buffer.getNumChannels(), 2);
    if (total <= 0 || nch <= 0) return;

    const int osIdx = std::clamp ((int) pOs->load(), 0, kMaxOversampleStages);
    if (osIdx != curOs) switchOversampling (osIdx);

    // transport (for the tempo-synced LFOs)
    double bpm = 120.0, ppq0 = -1.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = std::clamp (*b, 20.0, 400.0);
            if (pos->getIsPlaying()) if (auto q = pos->getPpqPosition()) ppq0 = *q;
        }

    const bool limit = pLimit->load() > 0.5f;
    float off[ember::kNumTargets];

    for (int start = 0; start < total; start += kChunk)
    {
        const int n = std::min (kChunk, total - start);
        const double dt = (double) n / hostRate;
        float* sc[2] = { scratch.getWritePointer (0), scratch.getWritePointer (1) };

        // ---- modulation: sources -> offsets of the targets ----
        float sv[ember::kNumSources] = {};
        for (int k = 0; k < ember::kNumLfos; ++k)
        {
            ember::LfoSettings ls;
            ls.rateHz = pLfo[(size_t) k].rate->load(); ls.sync = (int) pLfo[(size_t) k].sync->load(); ls.glide = pLfo[(size_t) k].glide->load() * 0.01f;
            for (int i = 0; i < ember::kSteps; ++i) ls.steps[(size_t) i] = lfoSteps[(size_t) k][(size_t) i].load (std::memory_order_relaxed);
            const double ppq = ppq0 >= 0.0 ? ppq0 + (double) start / hostRate * bpm / 60.0 : -1.0;
            sv[ember::kSrcLfo1 + k] = lfo[(size_t) k].run (ls, dt, ppq, bpm);
            lfoPos[(size_t) k].store (lfo[(size_t) k].position(), std::memory_order_relaxed);
        }
        for (int k = 0; k < ember::kNumSliders; ++k) sv[ember::kSrcSlider1 + k] = pSlider[(size_t) k]->load() * 0.01f;

        // the input (with the input gain, ramped over the chunk) and its peak for the envelope followers
        const float inRaw = pIn->load();
        float peak = 0.0f;
        for (int c = 0; c < 2; ++c)
        {
            const float* src = buffer.getReadPointer (std::min (c, nch - 1)) + start;
            for (int i = 0; i < n; ++i) { const float v = src[i]; sc[c][i] = std::isfinite (v) ? v : 0.0f; if (c == 0 || nch > 1) peak = std::max (peak, std::fabs (sc[c][i])); }
        }
        for (int k = 0; k < ember::kNumEnvs; ++k)
            sv[ember::kSrcEnv1 + k] = follower[(size_t) k].run (peak * std::pow (10.0f, inRaw * 0.05f), dt, pEnv[(size_t) k].atk->load(), pEnv[(size_t) k].rel->load(), pEnv[(size_t) k].gain->load());
        for (int s = 1; s < ember::kNumSources; ++s) srcValue[(size_t) s].store (sv[s], std::memory_order_relaxed);

        std::fill (off, off + ember::kNumTargets, 0.0f);
        bool touched[ember::kNumTargets] = {};
        for (int r = 0; r < ember::kNumRoutes; ++r)
        {
            const int s = (int) pRoute[(size_t) r].src->load(), t = (int) pRoute[(size_t) r].tgt->load();
            const float amt = pRoute[(size_t) r].amt->load() * 0.01f;
            if (s <= 0 || s >= ember::kNumSources || t <= 0 || t >= ember::kNumTargets || tgt[(size_t) t].raw == nullptr) continue;
            off[t] += sv[s] * amt; touched[t] = true;
        }
        for (int t = 1; t < ember::kNumTargets; ++t)
            modShown[(size_t) t].store (touched[t] ? std::clamp (tgt[(size_t) t].range.convertTo0to1 (tgt[(size_t) t].raw->load()) + off[t], 0.0f, 1.0f) : -1.0f, std::memory_order_relaxed);

        readParameters (gp, off);
        auto G = [&] (int t, const std::atomic<float>* raw) { float v = raw->load(); if (off[t] != 0.0f) { const auto& r = tgt[(size_t) t].range; v = r.convertFrom0to1 (std::clamp (r.convertTo0to1 (v) + off[t], 0.0f, 1.0f)); } return v; };
        const float inG = std::pow (10.0f, G (ember::kTargetIn, pIn) * 0.05f), outG = std::pow (10.0f, G (ember::kTargetOut, pOut) * 0.05f);
        const float mix = std::clamp (G (ember::kTargetMix, pMix) * 0.01f, 0.0f, 1.0f);
        const float inFrom = prevIn < 0.0f ? inG : prevIn, outFrom = prevOut < 0.0f ? outG : prevOut, mixFrom = prevMix < 0.0f ? mix : prevMix;

        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i) sc[c][i] *= inFrom + (inG - inFrom) * (float) (i + 1) / (float) n;
        if (nch == 1) std::fill (sc[1], sc[1] + n, 0.0f);

        // dry copy for the global mix (aligned with the oversampler latency) and the input analyzer
        {
            int ip = anaInPos.load (std::memory_order_relaxed);
            for (int i = 0; i < n; ++i)
            {
                dryRing.setSample (0, ringPos, sc[0][i]);
                dryRing.setSample (1, ringPos, sc[1][i]);
                anaIn[(size_t) ip].store (nch > 1 ? 0.5f * (sc[0][i] + sc[1][i]) : sc[0][i], std::memory_order_relaxed);
                ip = (ip + 1) % kAnalyzerSize;
                ringPos = (ringPos + 1) % ringSize;
            }
            anaInPos.store (ip, std::memory_order_release);
        }

        if (osIdx == 0)
        {
            engine.process (sc, nch, n, gp);
        }
        else
        {
            auto& o = *os[(size_t) osIdx];
            juce::dsp::AudioBlock<const float> in (const_cast<const float* const*> (sc), 2, (size_t) n);
            auto up = o.processSamplesUp (in);
            float* upPtrs[2] = { up.getChannelPointer (0), up.getChannelPointer (1) };
            engine.process (upPtrs, nch, (int) up.getNumSamples(), gp);
            juce::dsp::AudioBlock<float> out (sc, 2, (size_t) n);
            o.processSamplesDown (out);
        }

        // output gain, global dry/wet, optional safety limiter, output analyzer
        int ip = anaOutPos.load (std::memory_order_relaxed);
        const int base = ringPos - n;                                   // ring index of this chunk's first dry sample
        for (int c = 0; c < nch; ++c)
        {
            float* dst = buffer.getWritePointer (c) + start;
            for (int i = 0; i < n; ++i)
            {
                const float k = (float) (i + 1) / (float) n;
                const int rp = ((base + i - latency) % ringSize + ringSize) % ringSize;
                const float dry = dryRing.getSample (c, rp);
                const float og = outFrom + (outG - outFrom) * k, mx = mixFrom + (mix - mixFrom) * k;
                float y = sc[c][i] * og;
                y = dry * (1.0f - mx) + y * mx;
                dst[i] = limit ? softLimit (y) : y;
            }
        }
        for (int i = 0; i < n; ++i)
        {
            const float* d0 = buffer.getReadPointer (0) + start;
            const float* d1 = buffer.getReadPointer (nch - 1) + start;
            anaOut[(size_t) ip].store (0.5f * (d0[i] + d1[i]), std::memory_order_relaxed);
            ip = (ip + 1) % kAnalyzerSize;
        }
        anaOutPos.store (ip, std::memory_order_release);
        prevIn = inG; prevOut = outG; prevMix = mix;
    }
    for (int b = 0; b < ember::kMaxBands; ++b) bandMeter[(size_t) b].store (engine.bandLevel (b), std::memory_order_relaxed);
}

void EmberAudioProcessor::copyAnalyzer (bool output, float* dst, int n) const
{
    n = std::min (n, kAnalyzerSize);
    const auto& a = output ? anaOut : anaIn;
    const int pos = (output ? anaOutPos : anaInPos).load (std::memory_order_acquire);
    for (int i = 0; i < n; ++i) dst[i] = a[(size_t) (((pos - n + i) % kAnalyzerSize + kAnalyzerSize) % kAnalyzerSize)].load (std::memory_order_relaxed);
}

juce::AudioProcessorEditor* EmberAudioProcessor::createEditor() { return new EmberEditor (*this); }

void EmberAudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto st = apvts.copyState();
    for (int l = 0; l < ember::kNumLfos; ++l)
    {
        juce::StringArray v;
        for (int i = 0; i < ember::kSteps; ++i) v.add (juce::String (lfoSteps[(size_t) l][(size_t) i].load(), 4));
        st.setProperty ("lfo" + juce::String (l + 1) + "steps", v.joinIntoString (","), nullptr);
    }
    if (auto xml = st.createXml()) copyXmlToBinary (*xml, dest);
}

void EmberAudioProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            const auto st = juce::ValueTree::fromXml (*xml);
            apvts.replaceState (st);
            for (int l = 0; l < ember::kNumLfos; ++l)
            {
                const auto v = juce::StringArray::fromTokens (st.getProperty ("lfo" + juce::String (l + 1) + "steps").toString(), ",", "");
                if (v.size() == ember::kSteps)
                    for (int i = 0; i < ember::kSteps; ++i) setLfoStep (l, i, v[i].getFloatValue());
            }
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new EmberAudioProcessor(); }
