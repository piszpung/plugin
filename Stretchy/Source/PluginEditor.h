#pragma once

#include <atomic>
#include <memory>
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

namespace chrono_ui
{

//==============================================================================
// Procedurally generated, animated, pixelated sea at sunrise (drawn in a circular window).
class PixelSea : public juce::Component, private juce::Timer
{
public:
    explicit PixelSea (std::atomic<float>* stretchPercentParam);
    ~PixelSea() override { stopTimer(); }

    void paint (juce::Graphics&) override;
    void advance (float seconds);          // moves the animation forward and re-renders the pixels
    void startAnimation() { startTimerHz (30); }

private:
    static constexpr int kCells = 56;

    void timerCallback() override { advance (1.0f / 30.0f); repaint(); }
    void render();

    juce::Image image { juce::Image::ARGB, kCells, kCells, true };
    float time = 0.0f;
    std::atomic<float>* stretch;
};

//==============================================================================
class ChronoLookAndFeel : public juce::LookAndFeel_V4
{
public:
    ChronoLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;
    void drawLabel (juce::Graphics&, juce::Label&) override;
    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
};

} // namespace chrono_ui

//==============================================================================
class ChronosphereEditor : public juce::AudioProcessorEditor
{
public:
    explicit ChronosphereEditor (ChronosphereAudioProcessor&);
    ~ChronosphereEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void advanceAnimation (float seconds) { sea.advance (seconds); }   // used by the screenshot test

private:
    void renderBackground();

    ChronosphereAudioProcessor& proc;
    juce::Image background;

    chrono_ui::ChronoLookAndFeel laf;
    chrono_ui::PixelSea sea;

    juce::Slider stretchKnob, lpfKnob, mixKnob, outKnob;
    juce::ComboBox keyBox, scaleBox;
    juce::ToggleButton resetButton { "Reset on play" }, safeButton { "Safe clip" };

    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAtt  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<SliderAtt> attStretch, attLpf, attMix, attOut;
    std::unique_ptr<ComboAtt> attKey, attScale;
    std::unique_ptr<ButtonAtt> attReset, attSafe;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChronosphereEditor)
};
