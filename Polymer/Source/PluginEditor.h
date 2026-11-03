#pragma once

#include <map>
#include <memory>
#include <set>
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

namespace polyui
{

constexpr float kW = 560.0f, kH = 668.0f;        // design canvas (everything is laid out in these units)

//==============================================================================
class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
};

//==============================================================================
// Static scene: black background, neon title, captions, divider lines. Buffered by JUCE, drawn once.
class Backdrop : public juce::Component
{
public:
    Backdrop() { setInterceptsMouseClicks (false, false); setBufferedToImage (true); }
    void paint (juce::Graphics&) override;
};

//==============================================================================
class TransmuteButton : public juce::Button
{
public:
    TransmuteButton() : juce::Button ("Transmute") { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paintButton (juce::Graphics&, bool over, bool down) override;
    bool busy = false, ready = false;
    float spin = 0.0f;
};

// Small round button: drops all variations, keeps the sample.
class ResetButton : public juce::Button
{
public:
    ResetButton() : juce::Button ("Reset") { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paintButton (juce::Graphics&, bool over, bool down) override;
    bool active = false;
};

//==============================================================================
// The animated middle: vial, liquid, cloud of bubbles, threads, drop zone, grab box.
class Scene : public juce::Component
{
public:
    explicit Scene (PolymerAudioProcessor&);

    void advance (float dt);               // physics + animation + sync with the processor
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override { grabArmed = false; }
    void mouseExit (const juce::MouseEvent&) override { hoverId = -2; }

    void loadFile (const juce::File&);
    void browse();
    void setDragHover (bool b) { dragHover = b; }
    void kick() { kickTimer = 1.0f; }      // Transmute pressed (visual feedback even before results arrive)
    void dissolve();                       // Reset pressed: the cloud evaporates
    bool isBrewing() const { return pending > 0 || kickTimer > 0.0f; }

private:
    struct Orb
    {
        int id = 0, parent = 0, gen = 1;
        float hue = 0.6f, seconds = 1.0f;
        float x = 280, y = 328, vx = 0, vy = 0;
        float born = 0.0f, pulse = 0.0f, alpha = 1.0f, phase = 0.0f;
        bool dying = false, evap = false;
        float radius() const { return 7.8f - 0.9f * (float) std::min (gen - 1, 3); }
    };
    struct Fly { float delay, t, dur, size; juce::Point<float> p0, c1, c2, p3; bool landed = false; };
    struct Spark { juce::Point<float> p, v; float life, maxLife, size; juce::Colour c; bool smoke = false; };
    struct Bubble { float x, y, vy, r, wob; };
    struct Ring { float x, y, t; juce::Colour c; };      // expanding wave from a bubble that has just been played

    void sync();
    void startDrop();
    void spawnOrb (const OrbView&, bool instant);
    void step (float dt);
    void updateBlob (float dt);
    int  orbAt (juce::Point<float> p) const;
    bool onVial (juce::Point<float> p) const;
    bool onDropZone (juce::Point<float> p) const;
    bool onGrab (juce::Point<float> p) const;
    void burst (juce::Point<float> p, juce::Colour c, int n, float speed);
    void showExportMenu();

    void drawDropZone (juce::Graphics&);
    void drawGrab (juce::Graphics&);
    void drawFrame (juce::Graphics&);
    void drawCloud (juce::Graphics&);
    void drawThreads (juce::Graphics&);
    void drawOrbs (juce::Graphics&);
    void drawVial (juce::Graphics&);
    void drawFlyers (juce::Graphics&);
    void drawStatus (juce::Graphics&);
    void drawMotes (juce::Graphics&);
    void drawRings (juce::Graphics&);
    juce::Path vialPath() const;
    float K() const { return (float) getWidth() / kW; }

    PolymerAudioProcessor& proc;
    std::vector<Orb> orbs;
    std::set<int> known;
    std::vector<OrbView> queue;
    std::vector<Fly> flyers;
    std::vector<Spark> sparks;
    std::vector<Bubble> bubbles;
    std::vector<Ring> rings;
    std::map<int, std::vector<float>> peakCache;

    float time = 0.0f, revealTimer = 0.0f, kickTimer = 0.0f;
    float liquid = 0.0f, liquidTarget = 0.0f, slosh = 0.0f, sloshV = 0.0f, potionRed = 0.0f;
    float brew = 0.0f, cloudAlpha = 0.0f, vialPulse = 0.0f, dropGlow = 0.0f;
    float errorTimer = 0.0f, noteTimer = 0.0f, level = 0.0f;
    int lastVersion = -1, selectedId = 0, pending = 0, hoverId = -2;
    uint32_t lastTrigger = 0;
    bool hasSource = false, dragHover = false, firstSync = true, hoverDrop = false, hoverGrab = false, grabArmed = false;
    juce::String fileName;

    static constexpr int kBlobN = 72;
    float blobR[kBlobN] = {};
    juce::Point<float> blobC { 280.0f, 190.0f };
    bool blobInit = false;
    juce::Random rnd { 12345 };
    std::unique_ptr<juce::FileChooser> chooser;
};

} // namespace polyui

//==============================================================================
class PolymerEditor : public juce::AudioProcessorEditor,
                      public juce::FileDragAndDropTarget,
                      private juce::Timer
{
public:
    explicit PolymerEditor (PolymerAudioProcessor&);
    ~PolymerEditor() override;

    void paint (juce::Graphics&) override {}
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { scene.setDragHover (true); }
    void fileDragExit (const juce::StringArray&) override { scene.setDragHover (false); }
    void filesDropped (const juce::StringArray&, int, int) override;

    void advanceAnimation (float seconds);       // used by the screenshot test
    polyui::Scene& getScene() { return scene; }
    void pressReset();                            // same as clicking the reset button

private:
    void timerCallback() override;

    PolymerAudioProcessor& proc;
    polyui::LookAndFeel laf;
    polyui::Backdrop backdrop;
    polyui::Scene scene;
    polyui::TransmuteButton button;
    polyui::ResetButton resetButton;
    juce::Slider chaosKnob;
    juce::ToggleButton gateToggle { "NOTE-OFF CUT" };

    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<SliderAtt> attChaos;
    std::unique_ptr<ButtonAtt> attGate;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PolymerEditor)
};
