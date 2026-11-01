// Headless test of PolymerAudioProcessor: loading, transmute, selection, MIDI playback, state round trip.
// usage: proc_test <sample.wav>
#include "../Source/PluginProcessor.h"
#include <cmath>
#include <cstdio>

static int failures = 0;
static void check (const char* name, bool ok, const std::string& extra = "")
{
    std::printf ("%s  %s  %s\n", ok ? "PASS" : "FAIL", name, extra.c_str());
    if (! ok) ++failures;
}

static double energy (const juce::AudioBuffer<float>& b)
{
    double e = 0.0;
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = 0; i < b.getNumSamples(); ++i) e += (double) b.getSample (c, i) * b.getSample (c, i);
    return e;
}

// renders `blocks` blocks, optionally with one note-on in the first block; returns total energy and whether all finite
static double play (PolymerAudioProcessor& p, int blocks, int note, int noteOffBlock, bool& finite, float* peakOut = nullptr)
{
    double e = 0.0; float pk = 0.0f; finite = true;
    for (int b = 0; b < blocks; ++b)
    {
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        if (b == 0 && note >= 0) midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), 10);
        if (b == noteOffBlock && note >= 0) midi.addEvent (juce::MidiMessage::noteOff (1, note), 5);
        p.processBlock (buf, midi);
        e += energy (buf);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 512; ++i) { const float v = buf.getSample (c, i); if (! std::isfinite (v)) finite = false; pk = std::max (pk, std::fabs (v)); }
    }
    if (peakOut) *peakOut = pk;
    return e;
}

int main (int argc, char** argv)
{
    if (argc < 2) { std::puts ("usage: proc_test sample.wav"); return 2; }
    juce::ScopedJuceInitialiser_GUI init;
    PolymerAudioProcessor proc;
    proc.setPlayConfigDetails (0, 2, 48000.0, 512);
    proc.prepareToPlay (48000.0, 512);
    bool fin = true;

    check ("starts empty", ! proc.hasSource() && proc.getOrbs().empty());
    check ("no sound without a sample", play (proc, 10, 60, -1, fin) == 0.0);
    check ("transmute without sample is refused", ! proc.transmute());
    check ("missing file is rejected", ! proc.loadSampleFile (juce::File ("/nonexistent/file.wav")));

    check ("load sample", proc.loadSampleFile (juce::File (argv[1])));
    check ("has source, no bubbles yet", proc.hasSource() && proc.getOrbs().empty() && proc.getSelected() == 0);
    {
        const double e = play (proc, 40, -1, -1, fin);   // audition of the freshly loaded sample
        check ("loading auditions the sample", e > 1e-3 && fin, "energy " + std::to_string (e));
    }
    play (proc, 300, -1, -1, fin);                   // let the audition tail ring out
    float pk = 0.0f;
    const double eOrig = play (proc, 220, 60, -1, fin, &pk);
    check ("MIDI note plays the sample", eOrig > 1e-3 && fin && pk <= 1.0f, "peak " + std::to_string (pk));
    play (proc, 300, -1, -1, fin);
    const double eUp = play (proc, 220, 72, -1, fin);
    check ("note +12 plays (shorter, so less energy than C5)", eUp > 1e-4 && eUp < eOrig, "up " + std::to_string (eUp) + " orig " + std::to_string (eOrig));

    // transmute
    check ("transmute accepted", proc.transmute());
    check ("second transmute while busy is refused", ! proc.transmute());
    check ("variations finish", proc.waitUntilIdle (20000));
    auto orbs = proc.getOrbs();
    check ("6 bubbles created", orbs.size() == (size_t) PolymerAudioProcessor::kBatch, std::to_string (orbs.size()));
    bool ok = true; std::set<int> ids;
    for (auto& o : orbs) { ok &= o.parent == 0 && o.generation == 1 && o.hue >= 0.0f && o.hue <= 1.0f && o.seconds > 0.0f; ids.insert (o.id); }
    check ("bubbles are children of the original with unique ids", ok && ids.size() == orbs.size());
    check ("peaks available", proc.getPeaks (orbs[0].id).size() == 160);

    // select a bubble -> MIDI now plays that sound
    const auto trigBefore = proc.getTriggerCount();
    proc.selectOrb (orbs[2].id, true, false);
    const double eVar = play (proc, 60, -1, -1, fin);
    check ("selecting a bubble auditions it", eVar > 1e-3 && fin && proc.getTriggerCount() > trigBefore && proc.getLastTriggerId() == orbs[2].id);
    check ("selection stored", proc.getSelected() == orbs[2].id);
    const double eVarNote = play (proc, 60, 60, -1, fin);
    check ("MIDI plays the selected bubble", eVarNote > 1e-3 && fin);

    // bloom: clicking a bubble with no children makes its variations
    proc.selectOrb (orbs[0].id, false, true);
    check ("bloom started", proc.getPending() > 0);
    check ("bloom finishes", proc.waitUntilIdle (20000));
    auto orbs2 = proc.getOrbs();
    int gen2 = 0; for (auto& o : orbs2) if (o.generation == 2 && o.parent == orbs[0].id) ++gen2;
    check ("bloom adds 6 children of that bubble", gen2 == 6 && orbs2.size() == 12);
    proc.selectOrb (orbs[0].id, false, true);
    check ("second click on same bubble does not bloom again", proc.getPending() == 0);

    // gate / release
    {
        proc.selectOrb (0, false, false);
        auto* gate = proc.apvts.getParameter ("gate");
        gate->setValueNotifyingHost (0.0f);
        const double eFull = play (proc, 80, 48, 3, fin);      // note off at block 3, gate off -> keeps sounding
        gate->setValueNotifyingHost (1.0f);
        const double eCut = play (proc, 80, 48, 3, fin);       // gate on -> cut
        check ("gate on cuts the sound at note-off", eCut < eFull * 0.5, std::to_string (eCut) + " vs " + std::to_string (eFull));
        gate->setValueNotifyingHost (0.0f);
    }

    // chaos parameter reaches the engine
    {
        proc.waitUntilIdle (20000);
        auto* ch = proc.apvts.getParameter ("chaos");
        ch->setValueNotifyingHost (1.0f);
        proc.selectOrb (0, false, false);
        check ("transmute at chaos 100", proc.transmute() && proc.waitUntilIdle (20000));
        ch->setValueNotifyingHost (ch->convertTo0to1 (40.0f));
    }

    // export (drag out as .wav)
    {
        const auto orbs = proc.getOrbs();
        const auto f0 = proc.exportSound (0, "my sample");
        const auto f1 = proc.exportSound (orbs.empty() ? 0 : orbs[0].id, "my sample");
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatReader> r0 (f0.existsAsFile() ? wav.createReaderFor (f0.createInputStream().release(), true) : nullptr);
        std::unique_ptr<juce::AudioFormatReader> r1 (f1.existsAsFile() ? wav.createReaderFor (f1.createInputStream().release(), true) : nullptr);
        check ("export original as wav", r0 != nullptr && r0->numChannels == 2 && r0->bitsPerSample == 24 && r0->lengthInSamples > 1000, f0.getFileName().toStdString());
        check ("export variation as wav (other file)", r1 != nullptr && r1->lengthInSamples > 100 && f1 != f0, f1.getFileName().toStdString());
        const auto f2 = proc.exportSound (0, "my sample");
        check ("exporting again never overwrites an earlier export", f2.existsAsFile() && f2 != f0 && f0.existsAsFile());
        f0.deleteFile(); f1.deleteFile(); f2.deleteFile();
        check ("export unknown id fails cleanly", ! proc.exportSound (99999, "x").existsAsFile());
    }

    // cloud limit
    for (int i = 0; i < 12; ++i) { proc.selectOrb (0, false, false); proc.transmute(); proc.waitUntilIdle (20000); }
    check ("cloud never exceeds the limit", (int) proc.getOrbs().size() <= PolymerAudioProcessor::kMaxOrbs, std::to_string (proc.getOrbs().size()));

    // state round trip
    {
        proc.selectOrb (0, false, false);
        proc.waitUntilIdle (20000);
        const auto before = proc.getOrbs();
        const int sel = before.back().id;
        proc.selectOrb (sel, false, false);
        juce::MemoryBlock state;
        proc.getStateInformation (state);
        std::printf ("state size: %.1f KB for %zu bubbles\n", state.getSize() / 1024.0, before.size());

        PolymerAudioProcessor p2;
        p2.setPlayConfigDetails (0, 2, 48000.0, 512);
        p2.prepareToPlay (48000.0, 512);
        p2.setStateInformation (state.getData(), (int) state.getSize());
        check ("restored project has the source", p2.hasSource());
        check ("restored project re-renders all bubbles", p2.waitUntilIdle (60000));
        const auto after = p2.getOrbs();
        bool same = after.size() == before.size();
        for (size_t i = 0; same && i < after.size(); ++i) same &= after[i].id == before[i].id && after[i].parent == before[i].parent
                                                                 && std::fabs (after[i].seconds - before[i].seconds) < 1e-5f
                                                                 && std::fabs (after[i].hue - before[i].hue) < 1e-4f;
        if (! same)
        {
            std::printf ("DEBUG before %zu after %zu\n", before.size(), after.size());
            for (size_t i = 0; i < std::max (before.size(), after.size()); ++i)
                std::printf ("  [%zu] before id %d par %d sec %.5f hue %.4f | after id %d par %d sec %.5f hue %.4f\n", i,
                    i < before.size() ? before[i].id : -9, i < before.size() ? before[i].parent : -9, i < before.size() ? before[i].seconds : 0.f, i < before.size() ? before[i].hue : 0.f,
                    i < after.size() ? after[i].id : -9, i < after.size() ? after[i].parent : -9, i < after.size() ? after[i].seconds : 0.f, i < after.size() ? after[i].hue : 0.f);
        }
        check ("restored bubbles are identical (ids, tree, length, colour)", same);
        auto pa = proc.getPeaks (sel), pb = p2.getPeaks (sel);
        bool peaksSame = pa.size() == pb.size();
        for (size_t i = 0; peaksSame && i < pa.size(); ++i) peaksSame &= std::fabs (pa[i] - pb[i]) < 1e-5f;
        check ("restored waveform of a variation is identical", peaksSame);
        check ("restored selection", p2.getSelected() == sel);
        check ("restored chaos parameter", std::fabs (p2.apvts.getRawParameterValue ("chaos")->load() - 40.0f) < 0.2f);

        // garbage state must not crash
        const char junk[] = "this is not a state";
        p2.setStateInformation (junk, (int) sizeof junk);
        check ("garbage state is ignored", p2.hasSource());
    }

    // new sample replaces everything
    {
        const int v = proc.getSampleVersion();
        check ("loading a new sample", proc.loadSampleFile (juce::File (argv[1])));
        check ("cloud cleared and version bumped", proc.getOrbs().empty() && proc.getSampleVersion() == v + 1 && proc.getSelected() == 0);
        check ("transmute works again", proc.transmute() && proc.waitUntilIdle (20000) && proc.getOrbs().size() == 6);
    }

    // reset, hue closeness, export folder
    {
        proc.selectOrb (0, false, false);
        check ("transmute before reset", proc.transmute() && proc.waitUntilIdle (20000));
        auto first = proc.getOrbs();
        check ("first-generation colours are varied", [&] { float lo = 1, hi = 0; for (auto& o : first) { lo = std::min (lo, o.hue); hi = std::max (hi, o.hue); } return first.size() >= 6 && hi - lo > 0.15f; }());
        proc.selectOrb (first[0].id, false, false);
        check ("transmute from a variation", proc.transmute() && proc.waitUntilIdle (20000));
        bool close = true, anyChild = false;
        for (auto& o : proc.getOrbs())
            if (o.parent == first[0].id) { anyChild = true; if (std::fabs (o.hue - first[0].hue) > 0.081f) close = false; }
        check ("variations of a variation keep their parent's colour (+-0.08)", anyChild && close);

        const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("polymer_export_test");
        dir.deleteRecursively();
        proc.setExportDir (dir);
        check ("export dir set", proc.getExportDir() == dir);
        const auto ex = proc.exportSound (first[0].id, "my sample");
        check ("export goes to the chosen folder", ex.existsAsFile() && ex.getParentDirectory() == dir);
        juce::MemoryBlock st; proc.getStateInformation (st);
        proc.setExportDir (juce::File());
        proc.setStateInformation (st.getData(), (int) st.getSize());
        check ("export folder restored from project state", proc.getExportDir() == dir);
        proc.waitUntilIdle (60000);

        const int ver = proc.getSampleVersion();
        proc.resetCloud();
        check ("reset removes all variations", proc.getOrbs().empty());
        check ("reset keeps the sample", proc.hasSource() && proc.getSelected() == 0 && proc.getSampleVersion() == ver);
        check ("reset: nothing pending", proc.getPending() == 0);
        check ("reset: peaks of the original still there", ! proc.getPeaks (0).empty());
        check ("transmute works after reset", proc.transmute() && proc.waitUntilIdle (20000) && proc.getOrbs().size() == 6);
        proc.resetCloud(); proc.resetCloud();
        check ("double reset is harmless", proc.getOrbs().empty());
        juce::MemoryBlock st2; proc.getStateInformation (st2);
        check ("exported file still exists after reset", ex.existsAsFile());
        dir.deleteRecursively();
    }

    // MIDI stress: many notes, pitch bend, all-notes-off, no NaN
    {
        bool allFinite = true;
        for (int b = 0; b < 200; ++b)
        {
            juce::AudioBuffer<float> buf (2, 256);
            juce::MidiBuffer midi;
            for (int k = 0; k < 4; ++k) midi.addEvent (juce::MidiMessage::noteOn (1, 30 + (b * 7 + k * 11) % 60, (juce::uint8) 127), k * 40);
            if (b % 17 == 0) midi.addEvent (juce::MidiMessage::pitchWheel (1, 16383), 100);
            if (b % 50 == 49) midi.addEvent (juce::MidiMessage::allNotesOff (1), 50);
            proc.processBlock (buf, midi);
            for (int c = 0; c < 2; ++c) for (int i = 0; i < 256; ++i) { const float v = buf.getSample (c, i); if (! std::isfinite (v) || std::fabs (v) > 1.0f) allFinite = false; }
        }
        check ("MIDI stress: finite and within +-1", allFinite);
    }

    std::printf ("%s (%d failures)\n", failures ? "SOME TESTS FAILED" : "ALL TESTS PASSED", failures);
    return failures ? 1 : 0;
}
