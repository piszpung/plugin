#include "PluginEditor.h"
#include "GhostImage.h"
#include <vector>
#include <cmath>

using juce::Colour;
using juce::Graphics;
using juce::Path;
using juce::Point;
using juce::Rectangle;

namespace ghostui
{
namespace
{
const Colour kBg     { 5, 5, 5 };
const Colour kSpirit { 246, 248, 248 };          // white light: waves, knob charge, eye glow
const Colour kBone   { 232, 230, 220 };
const Colour kInk    { 14, 14, 16 };             // captions printed on the sheet

const Point<float> kEyeL { 279.0f, 311.0f }, kEyeR { 361.0f, 311.0f };   // the two eye holes of the photo
constexpr float kHoleW = 21.0f, kHoleH = 31.0f;   // half sizes
const Rectangle<float> kScope { 240.0f, 92.0f, 160.0f, 30.0f };

juce::Font makeFont (float size, bool bold = false, float kerning = 0.0f, bool light = false)
{
    juce::Font f { juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain) };
    (void) light;
    return kerning != 0.0f ? f.withExtraKerningFactor (kerning) : f;
}

void label (Graphics& g, const juce::String& t, float cx, float cy, float size, Colour c, float kerning = 0.0f, bool bold = false)
{
    g.setColour (c);
    g.setFont (makeFont (size, bold, kerning));
    g.drawText (t, Rectangle<float> (cx - 200.0f, cy - size, 400.0f, size * 2.0f), juce::Justification::centred, false);
}

void labelL (Graphics& g, const juce::String& t, float x, float cy, float size, Colour c, float kerning = 0.0f, bool bold = false)
{
    g.setColour (c);
    g.setFont (makeFont (size, bold, kerning));
    g.drawText (t, Rectangle<float> (x, cy - size, 400.0f, size * 2.0f), juce::Justification::centredLeft, false);
}

void glowStroke (Graphics& g, const Path& p, Colour c, float width, float alpha)
{
    const float w[3] = { width * 4.5f, width * 2.4f, width };
    const float a[3] = { 0.07f, 0.17f, 1.0f };
    for (int i = 0; i < 3; ++i)
    {
        g.setColour (c.withMultipliedAlpha (a[i] * alpha));
        g.strokePath (p, juce::PathStrokeType (w[i], juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

} // namespace

//==============================================================================
void LookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s)
{
    const float size = (float) std::min (w, h);
    const Point<float> c ((float) x + (float) w * 0.5f, (float) y + (float) h * 0.5f);
    const float u = size / 76.0f, R = 12.5f * u, ring = 18.5f * u;
    const float ang = a0 + pos * (a1 - a0);
    const bool hot = s.isMouseOverOrDragging();

    Path track;
    track.addCentredArc (c.x, c.y, ring, ring, 0.0f, a0, a1, true);
    g.setColour (Colour (70, 88, 84).withAlpha (0.7f));
    g.strokePath (track, juce::PathStrokeType (2.2f * u, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    Path val;
    val.addCentredArc (c.x, c.y, ring, ring, 0.0f, a0, std::max (a0 + 0.001f, ang), true);
    glowStroke (g, val, kSpirit, 2.2f * u, hot ? 1.0f : 0.88f);

    juce::ColourGradient body (Colour (74, 78, 86), c.x - R * 0.5f, c.y - R * 0.8f, Colour (16, 17, 21), c.x + R * 0.6f, c.y + R, false);
    g.setGradientFill (body);
    g.fillEllipse (c.x - R, c.y - R, 2.0f * R, 2.0f * R);
    g.setColour (Colour (255, 255, 255).withAlpha (hot ? 0.20f : 0.11f));
    g.drawEllipse (c.x - R + 0.5f, c.y - R + 0.5f, 2.0f * R - 1.0f, 2.0f * R - 1.0f, 1.0f);

    const Point<float> d (c.x + std::sin (ang) * R * 0.62f, c.y - std::cos (ang) * R * 0.62f);
    juce::ColourGradient dg (kSpirit.withAlpha (0.55f), d.x, d.y, kSpirit.withAlpha (0.0f), d.x + 7.0f * u, d.y, true);
    g.setGradientFill (dg);
    g.fillEllipse (d.x - 5.0f * u, d.y - 5.0f * u, 10.0f * u, 10.0f * u);
    g.setColour (Colour (255, 255, 255));
    g.fillEllipse (d.x - 2.3f * u, d.y - 2.3f * u, 4.6f * u, 4.6f * u);
}

//==============================================================================
Backdrop::Backdrop()
{
    setInterceptsMouseClicks (false, false);
    setBufferedToImage (true);
    photo = juce::ImageFileFormat::loadFrom (ghostimg::kJpeg, (size_t) ghostimg::kJpegSize);
}

void Backdrop::paint (Graphics& g)
{
    const float K = (float) getWidth() / kW;
    g.addTransform (juce::AffineTransform::scale (K));
    g.fillAll (kBg);

    // the photo of the ghost
    if (photo.isValid())
    {
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (photo, 0, 0, (int) kW, (int) kH, 0, 0, photo.getWidth(), photo.getHeight());
    }

    // hide the photo's top edge (its background is a hair brighter than the plain black above)
    juce::ColourGradient top (kBg, 0.0f, 150.0f, kBg.withAlpha (0.0f), 0.0f, 160.0f, false);
    g.setGradientFill (top);
    g.fillRect (0.0f, 0.0f, kW, 160.0f);

    // the cloth sinks into the dark at the bottom edge
    juce::ColourGradient fade (kBg.withAlpha (0.0f), 0.0f, 560.0f, kBg, 0.0f, 640.0f, false);
    g.setGradientFill (fade);
    g.fillRect (0.0f, 560.0f, kW, 80.0f);

    // title
    for (float r : { 5.0f, 3.0f })
        for (int i = 0; i < 8; ++i)
        {
            const float a = (float) i * juce::MathConstants<float>::pi * 0.25f;
            labelL (g, "Poltergeist", 30.0f + std::cos (a) * r, 38.0f + std::sin (a) * r, 28.0f, kSpirit.withAlpha (0.035f), 0.06f);
        }
    labelL (g, "Poltergeist", 30.0f, 38.0f, 28.0f, kBone, 0.06f);
    labelL (g, "vibrato", 32.0f, 62.0f, 11.0f, kSpirit.withAlpha (0.65f), 0.3f);
}
} // namespace ghostui

//==============================================================================
// Animated layer
//==============================================================================
class PoltergeistEditor::Live : public juce::Component
{
public:
    static constexpr int kHist = 240;       // 4 s at 60 steps per second

    Live (PoltergeistAudioProcessor& p, std::array<juce::Slider, 2>& k) : proc (p), knobs (k)
    {
        setInterceptsMouseClicks (false, false);
        dev.fill (0.0f); lev.fill (0.0f);
    }

    void advance (float dt)
    {
        time += dt;
        const float pk = proc.takeOutputPeak();
        const float d = proc.getDeviationCents();
        level = std::max (pk, level * 0.88f);
        for (int i = 0; i < kHist - 1; ++i) { dev[(size_t) i] = dev[(size_t) i + 1]; lev[(size_t) i] = lev[(size_t) i + 1]; }
        dev[(size_t) kHist - 1] = d;
        lev[(size_t) kHist - 1] = level;
        now = d;
        glow += (std::clamp (level * 2.0f, 0.0f, 1.0f) - glow) * std::min (1.0f, dt * 10.0f);
    }

    void paint (Graphics& g) override
    {
        using namespace ghostui;
        const float K = (float) getWidth() / kW;
        g.addTransform (juce::AffineTransform::scale (K));

        paintScope (g);

        // the eyes light up with the sound
        for (auto c : { kEyeL, kEyeR })
        {
            g.saveState();
            Path h; h.addEllipse (c.x - kHoleW, c.y - kHoleH, 2 * kHoleW, 2 * kHoleH);
            g.reduceClipRegion (h);
            juce::ColourGradient eg (kSpirit.withAlpha (0.05f + 0.34f * glow), c.x, c.y, kSpirit.withAlpha (0.0f), c.x + 34.0f, c.y, true);
            g.setGradientFill (eg);
            g.fillRect (c.x - 40.0f, c.y - 44.0f, 80.0f, 88.0f);
            g.restoreState();
        }

        // captions on the sheet: the value replaces the name while a knob is touched
        const char* names[2] = { "rate", "depth" };
        const Point<float> eyes[2] = { kEyeL, kEyeR };
        for (int i = 0; i < 2; ++i)
        {
            const bool hot = knobs[(size_t) i].isMouseOverOrDragging();
            const auto t = hot ? knobs[(size_t) i].getTextFromValue (knobs[(size_t) i].getValue()) : juce::String (names[i]);
            label (g, t, eyes[i].x, eyes[i].y + 44.0f, hot ? 13.0f : 12.0f, hot ? Colour (20, 20, 20) : kInk, 0.14f, hot);
        }
    }

private:
    // The display above the ghost: a steady tone (faint) and the same tone as the vibrato bends it (bright).
    // The bright wave is frequency-modulated by the real pitch deviation, its height follows the real output level.
    void paintScope (Graphics& g)
    {
        using namespace ghostui;
        const auto r = kScope;
        const float cy = r.getCentreY();
        constexpr int M = 240;
        constexpr float cycles = 7.0f, exaggerate = 5.5f;

        // soft panel
        g.setColour (Colour (255, 255, 255).withAlpha (0.035f));
        g.fillRoundedRectangle (r.expanded (10.0f, 12.0f), 16.0f);
        g.setColour (kSpirit.withAlpha (0.10f));
        g.drawRoundedRectangle (r.expanded (10.0f, 12.0f), 16.0f, 1.0f);
        g.setColour (kSpirit.withAlpha (0.06f));
        g.fillRect (r.getX(), cy - 0.5f, r.getWidth(), 1.0f);

        float devAt[M], ampAt[M], phi[M];
        for (int j = 0; j < M; ++j)
        {
            const float pos = (float) j * (float) (kHist - 1) / (float) (M - 1);
            const int i0 = std::min (kHist - 2, (int) pos);
            const float f = pos - (float) i0;
            devAt[j] = dev[(size_t) i0] * (1.0f - f) + dev[(size_t) i0 + 1] * f;
            const float lv = lev[(size_t) i0] * (1.0f - f) + lev[(size_t) i0 + 1] * f;
            ampAt[j] = (0.17f + 0.83f * std::clamp (std::sqrt (lv * 2.4f), 0.0f, 1.0f)) * (r.getHeight() * 0.46f);
        }
        const float step = juce::MathConstants<float>::twoPi * cycles / (float) (M - 1);
        phi[M - 1] = 0.0f;
        for (int j = M - 1; j > 0; --j) phi[j - 1] = phi[j] - step * std::exp2 (devAt[j] * exaggerate / 1200.0f);

        Path dry, wet;
        for (int j = 0; j < M; ++j)
        {
            const float x = r.getX() + r.getWidth() * (float) j / (float) (M - 1);
            const float yd = cy - ampAt[j] * 0.85f * std::sin (-step * (float) (M - 1 - j));
            const float yw = cy - ampAt[j] * std::sin (phi[j]);
            if (j == 0) { dry.startNewSubPath (x, yd); wet.startNewSubPath (x, yw); }
            else        { dry.lineTo (x, yd); wet.lineTo (x, yw); }
        }
        auto fadeFill = [&] (Colour c, float a)
        {
            juce::ColourGradient gr (c.withAlpha (0.0f), r.getX(), 0.0f, c.withAlpha (0.0f), r.getRight(), 0.0f, false);
            gr.addColour (0.14, c.withAlpha (a));
            gr.addColour (0.86, c.withAlpha (a));
            g.setGradientFill (gr);
        };
        fadeFill (Colour (230, 230, 225), 0.34f);
        g.strokePath (dry, juce::PathStrokeType (1.1f));
        const float strength = 0.75f + 0.25f * glow;
        const float w[3] = { 7.0f, 3.6f, 1.7f }, a[3] = { 0.06f, 0.15f, 1.0f };
        for (int i = 0; i < 3; ++i)
        {
            fadeFill (kSpirit, a[i] * strength);
            g.strokePath (wet, juce::PathStrokeType (w[i], juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // how far the pitch is bent right now
        const juce::String t = (now >= 0 ? "+" : "") + juce::String (juce::roundToInt (now)) + " ct";
        labelL (g, t, r.getX() + 2.0f, r.getBottom() + 4.0f + 8.0f, 10.5f, kSpirit.withAlpha (0.55f), 0.1f);
    }

    PoltergeistAudioProcessor& proc;
    std::array<juce::Slider, 2>& knobs;
    std::array<float, kHist> dev, lev;
    float time = 0.0f, level = 0.0f, glow = 0.0f, now = 0.0f;
};

//==============================================================================
PoltergeistEditor::PoltergeistEditor (PoltergeistAudioProcessor& p) : juce::AudioProcessorEditor (&p), proc (p)
{
    live = std::make_unique<Live> (p, knobs);
    addAndMakeVisible (backdrop);
    addAndMakeVisible (*live);

    static const char* ids[2] = { "rate", "depth" };
    static const double defaults[2] = { 5.0, 30.0 };
    for (int i = 0; i < 2; ++i)
    {
        auto& s = knobs[(size_t) i];
        addAndMakeVisible (s);
        s.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        s.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        s.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        s.setMouseDragSensitivity (240);
        s.setLookAndFeel (&laf);
        att[(size_t) i] = std::make_unique<SliderAtt> (proc.apvts, ids[i], s);
        s.setDoubleClickReturnValue (true, defaults[i]);
    }

    setSize (576, 576);
    setResizable (false, false);
    startTimerHz (60);
}

PoltergeistEditor::~PoltergeistEditor()
{
    stopTimer();
    for (auto& k : knobs) k.setLookAndFeel (nullptr);
}

void PoltergeistEditor::resized()
{
    const float K = (float) getWidth() / ghostui::kW;
    auto R = [&] (float x, float y, float w, float h) { return juce::Rectangle<int> ((int) std::lround (x * K), (int) std::lround (y * K), (int) std::lround (w * K), (int) std::lround (h * K)); };
    backdrop.setBounds (getLocalBounds());
    live->setBounds (getLocalBounds());
    knobs[0].setBounds (R (ghostui::kEyeL.x - 38.0f, ghostui::kEyeL.y - 38.0f, 76.0f, 76.0f));
    knobs[1].setBounds (R (ghostui::kEyeR.x - 38.0f, ghostui::kEyeR.y - 38.0f, 76.0f, 76.0f));
}

void PoltergeistEditor::timerCallback() { advanceAnimation (1.0f / 60.0f); }

void PoltergeistEditor::advanceAnimation (float seconds)
{
    live->advance (seconds);
    live->repaint();
}
