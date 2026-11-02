#pragma once

#include <array>
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

namespace sporeui
{
constexpr float kW = 560.0f, kH = 620.0f;       // design canvas; the window is this scaled by 0.9

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
};

// static picture: background, panel, title, captions (buffered, drawn once)
class Backdrop : public juce::Component
{
public:
    Backdrop() { setInterceptsMouseClicks (false, false); setBufferedToImage (true); }
    void paint (juce::Graphics&) override;
};

// "GERMINATE" = freeze
class GerminateButton : public juce::Button
{
public:
    GerminateButton() : juce::Button ("Germinate") { setClickingTogglesState (true); setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paintButton (juce::Graphics&, bool over, bool down) override;
    float pulse = 0.0f;
};
} // namespace sporeui

class SporeEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SporeEditor (SporeAudioProcessor&);
    ~SporeEditor() override;

    void paint (juce::Graphics&) override {}
    void resized() override;
    void advanceAnimation (float seconds);       // also used by the screenshot test

private:
    class Live;                                   // animated layer: grain display, meters, knob labels
    void timerCallback() override;

    SporeAudioProcessor& proc;
    sporeui::LookAndFeel laf;
    sporeui::Backdrop backdrop;
    std::unique_ptr<Live> live;
    std::array<juce::Slider, 4> knobs;
    sporeui::GerminateButton button;

    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::array<std::unique_ptr<SliderAtt>, 4> att;
    std::unique_ptr<ButtonAtt> attFreeze;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SporeEditor)
};
