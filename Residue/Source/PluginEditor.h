#pragma once

#include <array>
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

namespace residueui
{
constexpr int kW = 900, kH = 500;

// the white-blue-red palette: red + cyan-blue add up to exactly white (the 3D-glasses look)
inline juce::Colour red() { return juce::Colour (255, 60, 50); }
inline juce::Colour blue() { return juce::Colour (0, 195, 255); }

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
};

class Pill : public juce::ToggleButton
{
public:
    explicit Pill (const juce::String& t) { setButtonText (t); setClickingTogglesState (true); }
    void paintButton (juce::Graphics&, bool hover, bool down) override;
};

class Segmented : public juce::Component
{
public:
    Segmented (juce::RangedAudioParameter* p, juce::StringArray l) : param (p), labels (std::move (l)) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent& e) override { hover = indexAt (e.x); repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
    int current() const { return juce::roundToInt (param->convertFrom0to1 (param->getValue())); }
private:
    int indexAt (int x) const { return std::clamp (x * labels.size() / std::max (1, getWidth()), 0, labels.size() - 1); }
    juce::RangedAudioParameter* param;
    juce::StringArray labels;
    int hover = -1;
};

// the picture: the repeats as a tunnel of rings receding into the dark, drawn twice (red / blue) with a parallax offset and added up
class EchoVisual : public juce::Component
{
public:
    explicit EchoVisual (ResidueAudioProcessor& p) : proc (p) {}
    void resized() override;
    void tick();
    void paint (juce::Graphics&) override;
private:
    void drawLayer (juce::Graphics&, bool isRed, float fb, float spread, float wobble, float mix, float level, float period) const;
    ResidueAudioProcessor& proc;
    juce::Image redImg, blueImg, outImg;
    float phase = 0.0f, anim = 0.0f, shownLevel = 0.0f;
    float lastFb = 0.35f, lastSpread = 0.0f, lastWob = 0.0f, lastMix = 0.3f, lastPeriod = 0.3f;
};
} // namespace residueui

class ResidueEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit ResidueEditor (ResidueAudioProcessor&);
    ~ResidueEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void tick();                       // one UI frame (the 30 Hz timer calls it; the screenshot test too)
    juce::Rectangle<int> visualBounds() const { return visual.getBounds(); }

private:
    void timerCallback() override { tick(); }
    void updateSyncView();

    ResidueAudioProcessor& proc;
    residueui::LookAndFeel laf;
    residueui::EchoVisual visual;
    residueui::Segmented styleSeg;
    residueui::Pill syncBtn { "SYNC" };

    enum { kTime, kNote, kFb, kSat, kWob, kDuck, kSpread, kLow, kHigh, kMix, kNumKnobs };
    std::array<juce::Slider, kNumKnobs> knobs;
    std::array<juce::String, kNumKnobs> names;
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::array<std::unique_ptr<SliderAtt>, kNumKnobs> att;
    std::unique_ptr<ButtonAtt> syncAtt;
    bool syncShown = true;
    int lastStyle = -1;

    juce::Rectangle<int> knobSlot (int i) const;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ResidueEditor)
};
