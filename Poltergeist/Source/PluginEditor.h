#pragma once

#include <array>
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

namespace ghostui
{
constexpr float kW = 640.0f, kH = 640.0f;        // design canvas; the window is this scaled by 0.9

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
};

// the static picture: black room, the sheet-ghost with its two eye holes, the title (buffered, drawn once)
class Backdrop : public juce::Component
{
public:
    Backdrop() { setInterceptsMouseClicks (false, false); setBufferedToImage (true); }
    void paint (juce::Graphics&) override;
};
} // namespace ghostui

class PoltergeistEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit PoltergeistEditor (PoltergeistAudioProcessor&);
    ~PoltergeistEditor() override;

    void paint (juce::Graphics&) override {}
    void resized() override;
    void advanceAnimation (float seconds);       // one history step per call (the timer calls it 60 times a second); also used by the screenshot test

private:
    class Live;                                   // animated layer: the distortion display, glow in the eyes, knob captions
    void timerCallback() override;

    PoltergeistAudioProcessor& proc;
    ghostui::LookAndFeel laf;
    ghostui::Backdrop backdrop;
    std::unique_ptr<Live> live;
    std::array<juce::Slider, 2> knobs;       // 0 = Rate, 1 = Depth

    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::array<std::unique_ptr<SliderAtt>, 2> att;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PoltergeistEditor)
};
