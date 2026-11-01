#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout PolymerAudioProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    p.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { "chaos", 1 }, "Chaos", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 40.0f,
        AudioParameterFloatAttributes().withLabel ("%")));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "gate", 1 }, "Gate", false));
    return { p.begin(), p.end() };
}

PolymerAudioProcessor::PolymerAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    pChaos = apvts.getRawParameterValue ("chaos");
    pGate  = apvts.getRawParameterValue ("gate");
    formats.registerBasicFormats();
    worker.startThread (juce::Thread::Priority::low);
}

PolymerAudioProcessor::~PolymerAudioProcessor()
{
    worker.stopThread (8000);
    purgeGraveyard (true);
}

bool PolymerAudioProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo() && l.getMainInputChannelSet().isDisabled();
}

void PolymerAudioProcessor::prepareToPlay (double sr, int)
{
    hostRate = sr;
    sampler.prepare (sr);
}

//==============================================================================
void PolymerAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    const int n = buffer.getNumSamples();
    if (n <= 0 || buffer.getNumChannels() < 2) { midi.clear(); return; }
    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);

    if (const poly::Sound* a = auditionReq.exchange (nullptr))
    {
        sampler.stopAudition();
        sampler.noteOn (60, 0.9f, a, true);
        lastTriggerId.store (a->id);
        triggerCount.fetch_add (1);
    }

    const bool cut = pGate->load() > 0.5f;
    int pos = 0;
    for (const auto meta : midi)
    {
        const int t = juce::jlimit (0, n, meta.samplePosition);
        if (t > pos) { sampler.render (L + pos, R + pos, t - pos); pos = t; }
        const auto m = meta.getMessage();
        if (m.isNoteOn())
        {
            if (const poly::Sound* s = active.load())
            {
                sampler.noteOn (m.getNoteNumber(), m.getFloatVelocity(), s, false);
                lastTriggerId.store (s->id);
                triggerCount.fetch_add (1);
            }
        }
        else if (m.isNoteOff())                         sampler.noteOff (m.getNoteNumber(), cut);
        else if (m.isAllNotesOff() || m.isAllSoundOff()) sampler.allOff();
        else if (m.isPitchWheel())                      sampler.setPitchBend ((float) (m.getPitchWheelValue() - 8192) / 8192.0f * 2.0f);
    }
    if (pos < n) sampler.render (L + pos, R + pos, n - pos);

    for (int i = 0; i < n; ++i) { L[i] = poly::softClip (L[i] * 0.8f); R[i] = poly::softClip (R[i] * 0.8f); }
    midi.clear();
}

//==============================================================================
// Tree management
//==============================================================================
std::shared_ptr<poly::Sound> PolymerAudioProcessor::findLocked (int id) const
{
    for (auto& s : sounds) if (s->id == id) return s;
    return nullptr;
}

void PolymerAudioProcessor::retireLocked (const std::shared_ptr<poly::Sound>& s)
{
    s->retired.store (true);
    const poly::Sound* p = s.get();
    const poly::Sound* expected = p;
    auditionReq.compare_exchange_strong (expected, nullptr);
    graveyard.emplace_back (s, juce::Time::getMillisecondCounter());   // stays alive a few seconds: voices fade out
}

void PolymerAudioProcessor::purgeGraveyard (bool all)
{
    std::vector<std::shared_ptr<poly::Sound>> dead;
    {
        const juce::ScopedLock sl (lock);
        const auto now = juce::Time::getMillisecondCounter();
        for (size_t i = 0; i < graveyard.size();)
            if (all || now - graveyard[i].second > 4000u) { dead.push_back (std::move (graveyard[i].first)); graveyard.erase (graveyard.begin() + (long) i); }
            else ++i;
    }
    dead.clear();   // freed here, outside the lock and never on the audio thread
}

std::shared_ptr<poly::Sound> PolymerAudioProcessor::makeSound (int id, int parent, int gen, poly::Dna dna, poly::Audio&& a, float parentHue) const
{
    auto s = std::make_shared<poly::Sound>();
    s->id = id; s->parent = parent; s->generation = gen; s->dna = dna;
    s->audio = std::move (a);
    s->seconds = (float) (s->audio.size() / s->audio.sr);
    s->peaks = poly::makePeaks (s->audio);
    s->hue = id <= 0 ? 0.5f : (parent <= 0 ? poly::hueFromSeed (dna.seed) : poly::childHue (parentHue, dna.seed));
    return s;
}

namespace
{
bool encodeFlac16 (const poly::Audio& src, juce::MemoryBlock& block)
{
    juce::AudioBuffer<float> buf (2, src.size());
    for (int c = 0; c < 2; ++c) std::copy (src.ch[c].begin(), src.ch[c].end(), buf.getWritePointer (c));
    block.reset();
    juce::FlacAudioFormat flac;
    std::unique_ptr<juce::OutputStream> out = std::make_unique<juce::MemoryOutputStream> (block, false);
    auto w = flac.createWriterFor (out, juce::AudioFormatWriterOptions().withSampleRate (src.sr).withNumChannels (2)
                                         .withBitsPerSample (16).withQualityOptionIndex (5));
    if (! w) return false;
    const bool ok = w->writeFromAudioSampleBuffer (buf, 0, src.size());
    w.reset();
    return ok && block.getSize() > 0;
}

bool decodeFlac (const juce::MemoryBlock& block, poly::Audio& a)
{
    juce::FlacAudioFormat flac;
    std::unique_ptr<juce::AudioFormatReader> reader (flac.createReaderFor (new juce::MemoryInputStream (block, false), true));
    if (reader == nullptr || reader->lengthInSamples < 64) return false;
    const int n = (int) reader->lengthInSamples;
    juce::AudioBuffer<float> buf (2, n);
    reader->read (&buf, 0, n, 0, true, true);
    a.sr = reader->sampleRate;
    a.resize (n);
    std::copy (buf.getReadPointer (0), buf.getReadPointer (0) + n, a.ch[0].begin());
    std::copy (buf.getReadPointer (1), buf.getReadPointer (1) + n, a.ch[1].begin());
    return true;
}
} // namespace

void PolymerAudioProcessor::setSource (poly::Audio&& a, bool wasTrimmed, const juce::MemoryBlock* encoded)
{
    // The source always goes through the exact same 16-bit FLAC encode -> decode path that a saved project uses,
    // so a restored project re-creates bit-identical variations.
    juce::MemoryBlock block;
    if (encoded != nullptr) block = *encoded;
    else for (auto& c : a.ch) for (auto& v : c) v = std::clamp (v, -1.0f, 1.0f);
    if (encoded == nullptr && ! encodeFlac16 (a, block)) block.reset();
    poly::Audio dec;
    if (block.getSize() > 0 && decodeFlac (block, dec)) a = std::move (dec);
    else block.reset();
    const float rms = poly::activeRms (a);

    auto src = makeSound (0, -1, 0, {}, std::move (a));
    {
        const juce::ScopedLock sl (lock);
        ++epoch;
        for (auto& s : sounds) retireLocked (s);
        sounds.clear();
        sounds.push_back (src);
        sourceFlac = std::move (block);
        selected = 0; nextId = 1; wantSelected = -1;
        refRms = rms;
        pending.store (0);
        active.store (src.get());
    }
    {
        const juce::ScopedLock sl (queueLock);
        jobs.clear();
    }
    trimmed.store (wasTrimmed);
    sampleVersion.fetch_add (1);
}

int PolymerAudioProcessor::findPrunableLocked (int protectedParent) const
{
    std::vector<int> childCount ((size_t) nextId + 1, 0);
    for (auto& s : sounds) if (s->id > 0 && s->parent >= 0 && s->parent <= nextId) ++childCount[(size_t) s->parent];
    std::vector<char> prot ((size_t) nextId + 1, 0);
    prot[0] = 1;
    if (protectedParent >= 0 && protectedParent <= nextId) prot[(size_t) protectedParent] = 1;
    for (int id = selected; id > 0;)       // selected sound and all its ancestors
    {
        if (id <= nextId) prot[(size_t) id] = 1;
        auto s = findLocked (id);
        id = s ? s->parent : 0;
    }
    for (auto& s : sounds)
        if (s->id > 0 && ! prot[(size_t) s->id] && childCount[(size_t) s->id] == 0) return s->id;
    return -1;
}

bool PolymerAudioProcessor::transmute()
{
    Job job;
    {
        const juce::ScopedLock sl (lock);
        if (sounds.empty() || pending.load() > 0) return false;
        const int parent = selected;
        for (;;)   // dissolve the oldest leaves when the cloud is full
        {
            const int total = (int) sounds.size() - 1 + kBatch;
            if (total <= kMaxOrbs) break;
            const int victim = findPrunableLocked (parent);
            if (victim < 0) break;
            for (size_t i = 0; i < sounds.size(); ++i)
                if (sounds[i]->id == victim) { retireLocked (sounds[i]); sounds.erase (sounds.begin() + (long) i); break; }
        }
        job.epoch = epoch;
        const float chaos = pChaos->load() * 0.01f;
        for (int i = 0; i < kBatch; ++i) job.specs.push_back ({ nextId++, parent, 0u, chaos });
        pending.store (kBatch);
    }
    {
        const juce::ScopedLock sl (queueLock);
        jobs.push_back (std::move (job));
    }
    worker.notify();
    return true;
}

void PolymerAudioProcessor::selectOrb (int id, bool audition, bool bloom)
{
    bool doBloom = false;
    {
        const juce::ScopedLock sl (lock);
        auto s = findLocked (id);
        if (! s) return;
        selected = id;
        active.store (s.get());
        if (audition) auditionReq.store (s.get());
        if (bloom && id > 0 && pending.load() == 0)
        {
            doBloom = true;
            for (auto& o : sounds) if (o->parent == id) { doBloom = false; break; }
        }
    }
    if (doBloom) transmute();
}

bool PolymerAudioProcessor::hasSource() const { const juce::ScopedLock sl (lock); return ! sounds.empty(); }
int  PolymerAudioProcessor::getSelected() const { const juce::ScopedLock sl (lock); return selected; }

std::vector<OrbView> PolymerAudioProcessor::getOrbs() const
{
    std::vector<OrbView> out;
    const juce::ScopedLock sl (lock);
    for (auto& s : sounds)
        if (s->id > 0) out.push_back ({ s->id, s->parent, s->generation, s->hue, s->seconds });
    return out;
}

std::vector<float> PolymerAudioProcessor::getPeaks (int id) const
{
    const juce::ScopedLock sl (lock);
    auto s = findLocked (id);
    return s ? s->peaks : std::vector<float>();
}

float PolymerAudioProcessor::getSourceSeconds() const
{
    const juce::ScopedLock sl (lock);
    return sounds.empty() ? 0.0f : sounds[0]->seconds;
}

namespace
{
juce::File defaultExportDir() { return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Polymer exports"); }
juce::File settingsFile() { return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Polymer").getChildFile ("settings.xml"); }
juce::String readLastExportDir()
{
    if (auto xml = juce::XmlDocument::parse (settingsFile())) return xml->getStringAttribute ("exportDir");
    return {};
}
void writeLastExportDir (const juce::String& path)
{
    juce::XmlElement xml ("PolymerSettings");
    xml.setAttribute ("exportDir", path);
    const auto f = settingsFile();
    f.getParentDirectory().createDirectory();
    xml.writeTo (f);
}
} // namespace

juce::File PolymerAudioProcessor::getExportDir() const
{
    juce::String p;
    { const juce::ScopedLock sl (lock); p = exportDirPath; }
    if (p.isEmpty()) p = readLastExportDir();
    return p.isNotEmpty() ? juce::File (p) : defaultExportDir();
}

void PolymerAudioProcessor::setExportDir (const juce::File& d)
{
    { const juce::ScopedLock sl (lock); exportDirPath = d.getFullPathName(); }
    writeLastExportDir (d.getFullPathName());
    updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
}

juce::String PolymerAudioProcessor::getExportDirName() const
{
    const auto d = getExportDir();
    return d.getFileName().isNotEmpty() ? d.getFileName() : d.getFullPathName();
}

void PolymerAudioProcessor::resetCloud()
{
    {
        const juce::ScopedLock sl (lock);
        if (sounds.empty()) return;
        ++epoch;                                         // renders still running are discarded
        for (size_t i = 1; i < sounds.size(); ++i) retireLocked (sounds[i]);
        sounds.resize (1);
        selected = 0; wantSelected = -1;                 // nextId keeps counting, so ids are never reused
        pending.store (0);
        active.store (sounds[0].get());
    }
    const juce::ScopedLock sl (queueLock);
    jobs.clear();
}

juce::File PolymerAudioProcessor::exportSound (int id, const juce::String& baseName) const
{
    std::shared_ptr<poly::Sound> s;
    {
        const juce::ScopedLock sl (lock);
        s = findLocked (id);
    }
    if (s == nullptr || s->audio.size() < 2) return {};

    // Exports are never deleted or overwritten: a sampler that was given one of these files refers to it by path.
    auto dir = getExportDir();
    if (! dir.createDirectory().wasOk())
    {
        dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Polymer exports");
        if (! dir.createDirectory().wasOk()) return {};
    }

    juce::String name = juce::File::createLegalFileName (baseName.isNotEmpty() ? baseName : juce::String ("Polymer"));
    name += id == 0 ? juce::String ("_original") : "_poly" + juce::String (id);
    name += "_" + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S");
    auto file = dir.getChildFile (name + ".wav");
    for (int n = 2; file.existsAsFile(); ++n) file = dir.getChildFile (name + "_" + juce::String (n) + ".wav");

    std::unique_ptr<juce::OutputStream> out = std::make_unique<juce::FileOutputStream> (file);
    if (! static_cast<juce::FileOutputStream*> (out.get())->openedOk()) return {};
    juce::WavAudioFormat wav;
    auto w = wav.createWriterFor (out, juce::AudioFormatWriterOptions().withSampleRate (s->audio.sr).withNumChannels (2).withBitsPerSample (24));
    if (! w) return {};
    juce::AudioBuffer<float> buf (2, s->audio.size());
    for (int ch = 0; ch < 2; ++ch) std::copy (s->audio.ch[ch].begin(), s->audio.ch[ch].end(), buf.getWritePointer (ch));
    const bool ok = w->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
    w.reset();
    return ok && file.existsAsFile() ? file : juce::File();
}

bool PolymerAudioProcessor::waitUntilIdle (int timeoutMs) const
{
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    while (juce::Time::getMillisecondCounter() < end)
    {
        if (pending.load() == 0) return true;
        juce::Thread::sleep (10);
    }
    return pending.load() == 0;
}

//==============================================================================
// Loading samples
//==============================================================================
bool PolymerAudioProcessor::loadSampleFile (const juce::File& file)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr || reader->sampleRate < 8000.0 || reader->lengthInSamples < 64) return false;

    const juce::int64 maxN = (juce::int64) (poly::kMaxSeconds * reader->sampleRate);
    const bool wasTrimmed = reader->lengthInSamples > maxN;
    const int n = (int) std::min<juce::int64> (reader->lengthInSamples, maxN);

    juce::AudioBuffer<float> buf (2, n);
    buf.clear();
    reader->read (&buf, 0, n, 0, true, true);
    if (reader->numChannels == 1) buf.copyFrom (1, 0, buf, 0, 0, n);

    poly::Audio a;
    a.sr = reader->sampleRate;
    a.resize (n);
    std::copy (buf.getReadPointer (0), buf.getReadPointer (0) + n, a.ch[0].begin());
    std::copy (buf.getReadPointer (1), buf.getReadPointer (1) + n, a.ch[1].begin());
    if (poly::activeRms (a) < 1e-5f) return false;     // silence

    setSource (std::move (a), wasTrimmed);
    selectOrb (0, true, false);
    return true;
}

//==============================================================================
// Worker thread
//==============================================================================
void PolymerAudioProcessor::workerLoop (juce::Thread& t)
{
    while (! t.threadShouldExit())
    {
        Job job; bool have = false;
        {
            const juce::ScopedLock sl (queueLock);
            if (! jobs.empty()) { job = std::move (jobs.front()); jobs.pop_front(); have = true; }
        }
        purgeGraveyard (false);
        if (! have) { t.wait (250); continue; }
        processJob (job, t);
    }
}

bool PolymerAudioProcessor::publish (std::shared_ptr<poly::Sound> s, int ep)
{
    const juce::ScopedLock sl (lock);
    if (ep != epoch) return false;
    const int id = s->id;
    sounds.push_back (std::move (s));
    pending.fetch_sub (1);
    if (id == wantSelected)
    {
        selected = id;
        active.store (sounds.back().get());
        wantSelected = -1;
    }
    return true;
}

void PolymerAudioProcessor::processJob (const Job& job, juce::Thread& t)
{
    float ref;
    { const juce::ScopedLock sl (lock); if (job.epoch != epoch) return; ref = refRms; }

    poly::Rng seedRng ((uint64_t) juce::Time::currentTimeMillis() * 2654435761ull ^ (uint64_t) juce::Time::getHighResolutionTicks());
    std::vector<poly::Features> siblings;
    std::shared_ptr<poly::Sound> lastParent;
    poly::Features parentF;

    for (const auto& spec : job.specs)
    {
        if (t.threadShouldExit()) return;
        std::shared_ptr<poly::Sound> parent;
        { const juce::ScopedLock sl (lock); if (job.epoch != epoch) return; parent = findLocked (spec.parentId); }
        if (! parent)       // parent was dissolved meanwhile
        {
            const juce::ScopedLock sl (lock);
            if (job.epoch == epoch) pending.fetch_sub (1);
            continue;
        }

        poly::Dna dna { spec.seed, spec.chaos };
        poly::Audio rendered;
        if (job.restore)
        {
            rendered = poly::render (parent->audio, dna, ref);
        }
        else
        {
            if (parent != lastParent) { parentF = poly::analyse (parent->audio); lastParent = parent; }
            const float dmin = 0.12f + 0.35f * spec.chaos;
            float bestScore = -1.0f;
            for (int attempt = 0; attempt < 4; ++attempt)
            {
                const uint32_t seed = (uint32_t) (seedRng.next() >> 20) | 1u;
                poly::Audio cand = poly::render (parent->audio, { seed, spec.chaos }, ref);
                const poly::Features f = poly::analyse (cand);
                float minD = poly::distance (parentF, f) / 0.6f;
                for (auto& sf : siblings) minD = std::min (minD, poly::distance (sf, f));
                if (minD > bestScore) { bestScore = minD; dna.seed = seed; rendered = std::move (cand); }
                if (minD >= dmin) break;
            }
            siblings.push_back (poly::analyse (rendered));
        }
        auto snd = makeSound (spec.id, spec.parentId, parent->generation + 1, dna, std::move (rendered), parent->hue);
        publish (std::move (snd), job.epoch);
    }
}

//==============================================================================
// State
//==============================================================================
juce::AudioProcessorEditor* PolymerAudioProcessor::createEditor() { return new PolymerEditor (*this); }

void PolymerAudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto root = std::make_unique<juce::XmlElement> ("PolymerState");
    if (auto params = apvts.copyState().createXml()) root->addChildElement (params.release());

    const juce::ScopedLock sl (lock);
    if (exportDirPath.isNotEmpty()) root->setAttribute ("exportDir", exportDirPath);
    if (! sounds.empty())
    {
        auto* cloud = root->createNewChildElement ("Cloud");
        const auto& src = sounds[0]->audio;
        cloud->setAttribute ("rate", src.sr);
        cloud->setAttribute ("selected", selected);

        juce::MemoryBlock block = sourceFlac;
        if (block.getSize() == 0) encodeFlac16 (src, block);
        cloud->setAttribute ("flac", block.toBase64Encoding());
        for (auto& s : sounds)
            if (s->id > 0)
            {
                auto* n = cloud->createNewChildElement ("Node");
                n->setAttribute ("id", s->id);
                n->setAttribute ("parent", s->parent);
                n->setAttribute ("seed", juce::String ((juce::int64) s->dna.seed));
                n->setAttribute ("chaos", (double) s->dna.chaos);
            }
    }
    copyXmlToBinary (*root, dest);
}

void PolymerAudioProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName ("PolymerState")) return;

    if (auto* params = xml->getChildByName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*params));

    { const juce::ScopedLock sl (lock); exportDirPath = xml->getStringAttribute ("exportDir"); }

    auto* cloud = xml->getChildByName ("Cloud");
    if (cloud == nullptr) return;

    juce::MemoryBlock block;
    if (! block.fromBase64Encoding (cloud->getStringAttribute ("flac"))) return;
    poly::Audio a;
    if (! decodeFlac (block, a)) return;
    setSource (std::move (a), false, &block);

    Job job;
    int maxId = 0;
    {
        const juce::ScopedLock sl (lock);
        job.epoch = epoch;
        job.restore = true;
        for (auto* n2 : cloud->getChildWithTagNameIterator ("Node"))
        {
            Spec sp { n2->getIntAttribute ("id"), n2->getIntAttribute ("parent"),
                      (uint32_t) n2->getStringAttribute ("seed").getLargeIntValue(), (float) n2->getDoubleAttribute ("chaos") };
            if (sp.id <= 0) continue;
            job.specs.push_back (sp);
            maxId = std::max (maxId, sp.id);
        }
        std::sort (job.specs.begin(), job.specs.end(), [] (const Spec& x, const Spec& y) { return x.id < y.id; });
        nextId = maxId + 1;
        wantSelected = cloud->getIntAttribute ("selected", 0);
        if (wantSelected == 0) wantSelected = -1;
        pending.store ((int) job.specs.size());
    }
    if (! job.specs.empty())
    {
        const juce::ScopedLock sl (queueLock);
        jobs.push_back (std::move (job));
        worker.notify();
    }
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PolymerAudioProcessor(); }
