#pragma once

#include <array>
#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "PluginProcessor.h"

namespace emberui
{
constexpr int kW = 920, kH = 600;

juce::Colour bandColour (int b);
// crossover frequencies as the engine uses them (ordered, at least 12% apart)
float orderedXo (EmberAudioProcessor& p, int k);

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    // properties of a slider that change the drawing: "bipolar" (arc from the middle), "mini" (small knob), "modPos" (0..1 = where the modulation puts it now)
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos, juce::Slider::SliderStyle, juce::Slider&) override;
};

// two-state button drawn as a pill
class Pill : public juce::ToggleButton
{
public:
    Pill (const juce::String& t) { setButtonText (t); setClickingTogglesState (true); }
    void paintButton (juce::Graphics&, bool hover, bool down) override;
    juce::Colour colour { 255, 150, 60 };
};

// one-of-n selector for a choice parameter
class Segmented : public juce::Component
{
public:
    Segmented (juce::RangedAudioParameter* p, juce::StringArray l) : param (p), labels (std::move (l)) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent& e) override { hover = indexAt (e.x); repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
private:
    int indexAt (int x) const { return std::clamp (x * labels.size() / std::max (1, getWidth()), 0, labels.size() - 1); }
    juce::RangedAudioParameter* param;
    juce::StringArray labels;
    int hover = -1;
};

// style name with arrows + the transfer curve of the shaper at the current drive
class StylePanel : public juce::Component
{
public:
    explicit StylePanel (EmberAudioProcessor& p) : proc (p) {}
    void setBand (int b);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
private:
    void setStyle (int i);
    EmberAudioProcessor& proc;
    int band = 0;
    juce::RangedAudioParameter* styleP = nullptr; juce::RangedAudioParameter* driveP = nullptr; juce::RangedAudioParameter* mixP = nullptr;
};

// a pill-shaped button that shows the current choice of a choice parameter and opens a menu to change it
class ChoiceButton : public juce::Component
{
public:
    ChoiceButton (juce::RangedAudioParameter* p, std::function<void (juce::PopupMenu&, int cur)> build, juce::Colour c) : param (p), buildMenu (std::move (build)), colour (c) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
    void setEnabledLook (bool e) { dim = ! e; repaint(); }
private:
    juce::RangedAudioParameter* param;
    std::function<void (juce::PopupMenu&, int)> buildMenu;
    juce::Colour colour;
    bool hover = false, dim = false;
};

// the 16 steps of an LFO: drag to draw, right-click for a shape menu; the play-head and the live value are shown
class StepEditor : public juce::Component
{
public:
    StepEditor (EmberAudioProcessor& p, int lfo) : proc (p), index (lfo) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
private:
    void setAt (const juce::MouseEvent& e);
    EmberAudioProcessor& proc;
    int index;
};

// the modulation page: two step LFOs, two envelope followers, two sliders and the eight routes
class ModPanel : public juce::Component
{
public:
    explicit ModPanel (EmberAudioProcessor& p);
    ~ModPanel() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick();
private:
    class Route;
    EmberAudioProcessor& proc;
    LookAndFeel laf;
    std::array<std::unique_ptr<StepEditor>, 2> steps;
    std::array<std::unique_ptr<ChoiceButton>, 2> syncBtn;
    std::array<juce::Slider, 2> lfoRate, lfoGlide, sliders;
    std::array<std::array<juce::Slider, 3>, 2> env;       // attack, release, gain
    std::array<std::unique_ptr<Route>, ember::kNumRoutes> routes;
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::vector<std::unique_ptr<SliderAtt>> atts;
    friend class Route;
};

// the spectrum with the band regions, crossover handles and the per-band on/solo buttons
class Display : public juce::Component
{
public:
    Display (EmberAudioProcessor& p, std::function<void (int)> onSelect);
    void setSelected (int b) { sel = b; }
    void update();                               // new FFT frame (30 Hz)
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    static constexpr int kFft = 12, kN = 1 << kFft, kCols = 440;
    float xOf (float hz) const;
    float hzOf (float x) const;
    float yOfDb (float db) const;
    int numBands() const;
    int xoHit (float x, float y) const;           // crossover index or -1
    struct Hit { int band = -1; int what = 0; }; // what: 0 select, 1 power, 2 solo
    Hit headerHit (juce::Point<float>) const;
    void spectrumPath (const std::array<float, kCols>& v, juce::Path& line, juce::Path& fill) const;

    EmberAudioProcessor& proc;
    std::function<void (int)> select;
    juce::dsp::FFT fft { kFft };
    std::array<float, kN> window, tmpIn;
    std::array<float, 2 * kN> work;
    std::array<float, kCols> specIn, specOut;
    juce::Rectangle<float> plot;
    int sel = 0, dragXo = -1, hoverXo = -1;
};
} // namespace emberui

class EmberEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit EmberEditor (EmberAudioProcessor&);
    ~EmberEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void tick();                      // one UI frame (the 30 Hz timer calls it; also used by the screenshot test)
    void selectBand (int b);
    int getSelectedBand() const { return sel; }
    void showModulation (bool on);   // swaps the spectrum for the modulation page
    emberui::Display& displayForTest() { return display; }      // lets the tests drive the mouse handling
    emberui::StylePanel& styleForTest() { return stylePanel; }

private:
    void timerCallback() override { tick(); }
    class BandTitle;

    EmberAudioProcessor& proc;
    emberui::LookAndFeel laf;
    int sel = 0;

    emberui::Display display;
    emberui::StylePanel stylePanel;
    std::unique_ptr<emberui::ModPanel> modPanel;
    std::unique_ptr<BandTitle> title;
    std::unique_ptr<emberui::Segmented> bandsSeg, osSeg, slopeSeg;
    emberui::Pill bandOn { "ON" }, bandSolo { "SOLO" }, msBtn { "M/S" }, limitBtn { "LIMIT" }, modBtn { "MOD" };

    enum { kDrive, kLevel, kMix, kDyn, kTone, kFb, kFbf, kPan, kNumBandKnobs };
    std::array<juce::Slider, kNumBandKnobs> knobs;
    std::array<juce::Slider, 3> gKnobs;            // input, output, mix

    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::array<std::unique_ptr<SliderAtt>, kNumBandKnobs> knobAtt;
    std::array<std::unique_ptr<SliderAtt>, 3> gAtt;
    std::unique_ptr<ButtonAtt> onAtt, soloAtt, msAtt, limitAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EmberEditor)
};
