#include "PluginEditor.h"

using juce::Colour;
using juce::Graphics;
using juce::Path;
using juce::Point;
using juce::Rectangle;

namespace sporeui
{
namespace
{
const Colour kNeon   { 62, 255, 80 };
const Colour kNeonLt { 150, 255, 170 };
const Colour kDim    { 104, 140, 112 };          // captions / knob labels
const Colour kRed    { 255, 60, 60 };

const Rectangle<float> kPanel { 12.0f, 10.0f, 536.0f, 598.0f };
const Point<float> kDisc { 280.0f, 236.0f };
constexpr float kDiscR = 118.0f;
constexpr float kKnobY = 420.0f;
constexpr float kKnobX[4] = { 92.5f, 217.5f, 342.5f, 467.5f };
constexpr const char* kKnobName[4] = { "DENSITY", "SIZE", "BLUR", "DECAY" };
const Rectangle<float> kButton { 200.0f, 548.0f, 160.0f, 40.0f };
const Rectangle<float> kMeterL { 44.0f, 236.0f, 92.0f, 8.0f };
const Rectangle<float> kMeterR { 424.0f, 236.0f, 92.0f, 8.0f };

juce::Font makeFont (float size, bool bold = false, float kerning = 0.0f)
{
    juce::Font f { juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain) };
    return kerning != 0.0f ? f.withExtraKerningFactor (kerning) : f;
}

void text (Graphics& g, const juce::String& t, float cx, float cy, float size, Colour c, float kerning = 0.0f, bool bold = false)
{
    g.setColour (c);
    g.setFont (makeFont (size, bold, kerning));
    g.drawText (t, Rectangle<float> (cx - 200.0f, cy - size, 400.0f, size * 2.0f), juce::Justification::centred, false);
}

void glowStroke (Graphics& g, const Path& p, Colour c, float width, float alpha)
{
    const float w[4] = { width * 5.0f, width * 3.0f, width * 1.8f, width };
    const float a[4] = { 0.05f, 0.09f, 0.2f, 1.0f };
    for (int i = 0; i < 4; ++i)
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
    const float u = size / 90.0f, R = 31.0f * u, ring = 40.0f * u;
    const float ang = a0 + pos * (a1 - a0);
    const bool hot = s.isMouseOverOrDragging();

    Path track;
    track.addCentredArc (c.x, c.y, ring, ring, 0.0f, a0, a1, true);
    g.setColour (Colour (40, 62, 46));
    g.strokePath (track, juce::PathStrokeType (4.0f * u, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    Path val;
    val.addCentredArc (c.x, c.y, ring, ring, 0.0f, a0, std::max (a0 + 0.001f, ang), true);
    glowStroke (g, val, kNeon, 4.0f * u, hot ? 1.0f : 0.85f);

    // red marker at the maximum end of the track
    const Point<float> e (c.x + std::sin (a1) * ring, c.y - std::cos (a1) * ring);
    g.setColour (kRed);
    g.fillEllipse (e.x - 2.2f * u, e.y - 2.2f * u, 4.4f * u, 4.4f * u);

    // body
    juce::ColourGradient rim (Colour (96, 110, 100), c.x - R * 0.5f, c.y - R * 0.8f, Colour (24, 30, 26), c.x + R * 0.6f, c.y + R, false);
    g.setGradientFill (rim);
    g.fillEllipse (c.x - R, c.y - R, 2.0f * R, 2.0f * R);
    const float r2 = R - 2.5f * u;
    juce::ColourGradient face (Colour (44, 50, 46), c.x - r2 * 0.4f, c.y - r2 * 0.6f, Colour (16, 19, 17), c.x + r2 * 0.5f, c.y + r2 * 0.8f, false);
    g.setGradientFill (face);
    g.fillEllipse (c.x - r2, c.y - r2, 2.0f * r2, 2.0f * r2);

    // pointer dot
    const Point<float> d (c.x + std::sin (ang) * R * 0.66f, c.y - std::cos (ang) * R * 0.66f);
    juce::ColourGradient dg (kNeon.withAlpha (0.55f), d.x, d.y, kNeon.withAlpha (0.0f), d.x + 8.0f * u, d.y, true);
    g.setGradientFill (dg);
    g.fillEllipse (d.x - 8.0f * u, d.y - 8.0f * u, 16.0f * u, 16.0f * u);
    g.setColour (kNeonLt);
    g.fillEllipse (d.x - 2.8f * u, d.y - 2.8f * u, 5.6f * u, 5.6f * u);
}

//==============================================================================
void Backdrop::paint (Graphics& g)
{
    const float K = (float) getWidth() / kW;
    g.fillAll (Colour (2, 3, 2));
    g.addTransform (juce::AffineTransform::scale (K));

    // a darker plate slightly behind the panel (as in the concept)
    g.setColour (Colour (11, 14, 12));
    g.fillRoundedRectangle (kPanel.translated (8.0f, 8.0f), 16.0f);

    juce::ColourGradient pg (Colour (26, 31, 28), 0.0f, kPanel.getY(), Colour (14, 17, 15), 0.0f, kPanel.getBottom(), false);
    g.setGradientFill (pg);
    g.fillRoundedRectangle (kPanel, 16.0f);
    g.setColour (Colour (72, 96, 80));
    g.drawRoundedRectangle (kPanel, 16.0f, 1.6f);
    g.setColour (Colour (255, 255, 255).withAlpha (0.04f));
    g.drawRoundedRectangle (kPanel.reduced (2.0f), 14.0f, 1.0f);

    // title with a soft glow
    for (float r : { 6.0f, 3.5f, 2.0f })
        for (int i = 0; i < 8; ++i)
        {
            const float a = (float) i * juce::MathConstants<float>::pi * 0.25f;
            sporeui::text (g, "SPORE", 280.0f + std::cos (a) * r, 64.0f + std::sin (a) * r, 50.0f, kNeon.withAlpha (0.045f), 0.14f);
        }
    sporeui::text (g, "SPORE", 280.0f, 64.0f, 50.0f, kNeon, 0.14f);
    g.setColour (kDim.withAlpha (0.8f));
    g.setFont (makeFont (11.0f, false, 0.12f));
    g.drawText ("AUDIO", Rectangle<float> (60.0f, 54.0f, 100.0f, 20.0f), juce::Justification::centredRight, false);
    g.drawText ("AUDIO PROCESSOR", Rectangle<float> (398.0f, 54.0f, 150.0f, 20.0f), juce::Justification::centredLeft, false);

    // bezel of the display
    const auto c = kDisc;
    const float Ro = kDiscR + 16.0f;
    juce::ColourGradient bz (Colour (92, 124, 100), c.x - Ro * 0.7f, c.y - Ro * 0.8f, Colour (28, 38, 31), c.x + Ro * 0.7f, c.y + Ro * 0.8f, false);
    g.setGradientFill (bz);
    g.fillEllipse (c.x - Ro, c.y - Ro, 2.0f * Ro, 2.0f * Ro);
    g.setColour (Colour (6, 8, 7));
    g.fillEllipse (c.x - kDiscR - 7.0f, c.y - kDiscR - 7.0f, 2.0f * (kDiscR + 7.0f), 2.0f * (kDiscR + 7.0f));
    g.setColour (Colour (60, 84, 66));
    g.drawEllipse (c.x - kDiscR - 7.0f, c.y - kDiscR - 7.0f, 2.0f * (kDiscR + 7.0f), 2.0f * (kDiscR + 7.0f), 1.0f);

    // meter tracks
    for (const auto& r : { kMeterL, kMeterR })
    {
        g.setColour (Colour (36, 44, 39));
        g.fillRoundedRectangle (r, 3.0f);
    }
    sporeui::text (g, "IN", kMeterL.getCentreX(), kMeterL.getBottom() + 14.0f, 9.0f, kDim.withAlpha (0.7f), 0.12f);
    sporeui::text (g, "OUT", kMeterR.getCentreX(), kMeterR.getBottom() + 14.0f, 9.0f, kDim.withAlpha (0.7f), 0.12f);
}

//==============================================================================
void GerminateButton::paintButton (Graphics& g, bool over, bool down)
{
    const float K = (float) getWidth() / kButton.getWidth();
    g.addTransform (juce::AffineTransform::scale (K));
    const auto r = Rectangle<float> (0.0f, 0.0f, kButton.getWidth(), kButton.getHeight()).reduced (4.0f);
    const bool on = getToggleState();
    const float cr = r.getHeight() * 0.5f;

    if (on)
    {
        const float p = 0.5f + 0.5f * std::sin (pulse);
        for (int i = 4; i >= 1; --i)
        {
            g.setColour (kNeon.withAlpha ((0.05f + 0.04f * p) * (float) (5 - i)));
            g.fillRoundedRectangle (r.expanded ((float) i * 1.6f), cr + (float) i * 1.6f);
        }
        juce::ColourGradient gr (Colour (170, 255, 190), 0.0f, r.getY(), Colour (48, 226, 78), 0.0f, r.getBottom(), false);
        g.setGradientFill (gr);
        g.fillRoundedRectangle (r, cr);
        sporeui::text (g, "GERMINATE", r.getCentreX(), r.getCentreY(), 13.0f, Colour (8, 28, 12), 0.1f, true);
    }
    else
    {
        g.setColour (Colour (20, 27, 22));
        g.fillRoundedRectangle (r, cr);
        Path p; p.addRoundedRectangle (r, cr);
        glowStroke (g, p, kNeon, 1.2f, (over ? 0.95f : 0.6f) * (down ? 0.7f : 1.0f));
        sporeui::text (g, "GERMINATE", r.getCentreX(), r.getCentreY(), 13.0f, kNeon.withAlpha (over ? 1.0f : 0.8f), 0.1f, true);
    }
}
} // namespace sporeui

//==============================================================================
// Animated layer
//==============================================================================
class SporeEditor::Live : public juce::Component
{
public:
    Live (SporeAudioProcessor& p, std::array<juce::Slider, 4>& k) : proc (p), knobs (k) { setInterceptsMouseClicks (false, false); }

    void advance (float dt)
    {
        time += dt;
        const float pi = proc.takeInputPeak(), po = proc.takeOutputPeak();
        auto follow = [dt] (float& lvl, float& hold, float v)
        {
            lvl = std::max (v, lvl * std::exp (-dt * 7.0f));
            hold = std::max (v, hold * std::exp (-dt * 0.9f));
        };
        follow (lvIn, hdIn, pi);
        follow (lvOut, hdOut, po);
        activity += (std::clamp (lvOut * 6.0f, 0.0f, 1.0f) - activity) * std::min (1.0f, dt * 6.0f);
        frozenAmt += ((proc.isFrozen() ? 1.0f : 0.0f) - frozenAmt) * std::min (1.0f, dt * 5.0f);
    }

    void paint (Graphics& g) override
    {
        using namespace sporeui;
        const float K = (float) getWidth() / kW;
        g.addTransform (juce::AffineTransform::scale (K));

        // ---- display
        const auto c = kDisc;
        Path disc; disc.addEllipse (c.x - kDiscR, c.y - kDiscR, 2.0f * kDiscR, 2.0f * kDiscR);
        g.saveState();
        g.reduceClipRegion (disc);
        juce::ColourGradient bg (Colour (22, 30, 25), c.x, c.y - 30.0f, Colour (8, 11, 9), c.x, c.y + kDiscR, true);
        g.setGradientFill (bg);
        g.fillPath (disc);

        for (int i = 0; i < SporeAudioProcessor::kGrains; ++i)
        {
            const auto v = proc.getGrain (i);
            if (! v.active || v.env < 0.02f) continue;
            // stable scatter per grain slot, so the picture does not shimmer
            uint32_t h = (uint32_t) (i + 1) * 2654435761u; h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
            const float jx = ((float) (h & 255u) / 255.0f - 0.5f) * 0.95f, jy = ((float) ((h >> 8) & 255u) / 255.0f - 0.5f) * 0.95f;
            float px = (v.pan - 0.5f) * 1.3f + jx, py = (v.depth - 0.2f) * 1.2f + jy * 0.8f;
            const float rr = std::sqrt (px * px + py * py);
            if (rr > 0.9f) { px *= 0.9f / rr; py *= 0.9f / rr; }
            const float x = c.x + px * kDiscR, y = c.y + py * kDiscR;
            const float e = std::sqrt (std::clamp (v.env, 0.0f, 1.0f));
            const float vis = (0.25f + 0.75f * activity) * e;
            const float size = 2.2f + 2.0f * e;
            juce::ColourGradient gl (kNeon.withAlpha (0.35f * vis), x, y, kNeon.withAlpha (0.0f), x + size * 3.2f, y, true);
            g.setGradientFill (gl);
            g.fillEllipse (x - size * 3.2f, y - size * 3.2f, size * 6.4f, size * 6.4f);
            g.setColour (Colour (84, 255, 108).withAlpha (0.25f + 0.75f * vis));
            g.fillEllipse (x - size, y - size, 2.0f * size, 2.0f * size);
        }
        // frozen: a thin ring of light just inside the glass
        if (frozenAmt > 0.01f)
        {
            Path rim; rim.addEllipse (c.x - kDiscR + 2.0f, c.y - kDiscR + 2.0f, 2.0f * kDiscR - 4.0f, 2.0f * kDiscR - 4.0f);
            g.setColour (kNeon.withAlpha (frozenAmt * (0.22f + 0.1f * std::sin (time * 3.0f))));
            g.strokePath (rim, juce::PathStrokeType (2.0f));
        }
        g.restoreState();

        // ---- meters
        meter (g, kMeterL, lvIn, hdIn);
        meter (g, kMeterR, lvOut, hdOut);

        // ---- knob labels (the value replaces the name while a knob is touched)
        for (int i = 0; i < 4; ++i)
        {
            const bool hot = knobs[(size_t) i].isMouseOverOrDragging();
            const auto t = hot ? knobs[(size_t) i].getTextFromValue (knobs[(size_t) i].getValue()) : juce::String (kKnobName[i]);
            sporeui::text (g, t, kKnobX[i], kKnobY + 66.0f, 12.0f, hot ? kNeonLt : kDim, 0.1f);
        }
    }

private:
    static float toPos (float v)
    {
        if (v <= 1e-6f) return 0.0f;
        return std::clamp ((20.0f * std::log10 (v) + 54.0f) / 54.0f, 0.0f, 1.0f);
    }

    void meter (Graphics& g, Rectangle<float> r, float level, float hold)
    {
        using namespace sporeui;
        const float lp = toPos (level), hp = toPos (hold);
        auto fill = r.withWidth (r.getWidth() * lp);
        if (fill.getWidth() > 0.5f)
        {
            g.setColour (kNeon.withAlpha (0.18f));
            g.fillRoundedRectangle (fill.expanded (0.0f, 2.0f), 4.0f);
            juce::ColourGradient gr (Colour (40, 200, 66), r.getX(), 0.0f, kNeon, r.getRight(), 0.0f, false);
            g.setGradientFill (gr);
            g.fillRoundedRectangle (fill, 3.0f);
        }
        const float x = r.getX() + r.getWidth() * hp;
        Path caret;
        caret.startNewSubPath (x - 4.0f, r.getY() - 1.0f);
        caret.lineTo (x, r.getY() - 10.0f);
        caret.lineTo (x + 4.0f, r.getY() - 1.0f);
        g.setColour (kRed.withAlpha (hold > 1e-4f ? 0.95f : 0.35f));
        g.strokePath (caret, juce::PathStrokeType (1.4f));
    }

    SporeAudioProcessor& proc;
    std::array<juce::Slider, 4>& knobs;
    float time = 0.0f, lvIn = 0.0f, hdIn = 0.0f, lvOut = 0.0f, hdOut = 0.0f, activity = 0.0f, frozenAmt = 0.0f;
};

//==============================================================================
SporeEditor::SporeEditor (SporeAudioProcessor& p) : juce::AudioProcessorEditor (&p), proc (p)
{
    live = std::make_unique<Live> (p, knobs);
    addAndMakeVisible (backdrop);
    addAndMakeVisible (*live);

    static const char* ids[4] = { "density", "size", "blur", "decay" };
    static const double defaults[4] = { 0.5, 0.5, 0.35, 0.25 };
    for (int i = 0; i < 4; ++i)
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
    addAndMakeVisible (button);
    attFreeze = std::make_unique<ButtonAtt> (proc.apvts, "freeze", button);

    setSize (504, 558);
    setResizable (false, false);
    startTimerHz (30);
}

SporeEditor::~SporeEditor()
{
    stopTimer();
    for (auto& k : knobs) k.setLookAndFeel (nullptr);
}

void SporeEditor::resized()
{
    const float K = (float) getWidth() / sporeui::kW;
    auto R = [&] (float x, float y, float w, float h) { return juce::Rectangle<int> ((int) std::lround (x * K), (int) std::lround (y * K), (int) std::lround (w * K), (int) std::lround (h * K)); };
    backdrop.setBounds (getLocalBounds());
    live->setBounds (getLocalBounds());
    const float cx[4] = { 92.5f, 217.5f, 342.5f, 467.5f };
    for (int i = 0; i < 4; ++i) knobs[(size_t) i].setBounds (R (cx[i] - 45.0f, 420.0f - 45.0f, 90.0f, 90.0f));
    button.setBounds (R (200.0f, 548.0f, 160.0f, 40.0f));
}

void SporeEditor::timerCallback()
{
    static juce::uint32 last = 0;
    const auto now = juce::Time::getMillisecondCounter();
    const float dt = last == 0 ? 1.0f / 30.0f : std::clamp ((float) (now - last) * 0.001f, 0.005f, 0.1f);
    last = now;
    advanceAnimation (dt);
}

void SporeEditor::advanceAnimation (float seconds)
{
    live->advance (seconds);
    button.pulse += seconds * 4.0f;
    if (button.getToggleState()) button.repaint();
    live->repaint();
}
