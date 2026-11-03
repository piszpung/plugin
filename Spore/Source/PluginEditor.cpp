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
// palette: warm night forest
const Colour kAmber   { 255, 190, 112 };      // accent: value arcs, spores, button
const Colour kAmberLt { 255, 222, 166 };
const Colour kCream   { 238, 227, 204 };
const Colour kDim     { 160, 142, 120 };      // labels
const Colour kInk     { 22, 16, 13 };

constexpr float kKnobY = 456.0f;
constexpr float kKnobX[4] = { 92.5f, 217.5f, 342.5f, 467.5f };
constexpr const char* kKnobName[4] = { "density", "size", "blur", "decay" };
const Rectangle<float> kButton { 200.0f, 550.0f, 160.0f, 44.0f };
const Point<float> kBase { 280.0f, 352.0f };       // where the big mushroom stands

juce::Font makeFont (float size, bool bold = false, float kerning = 0.0f)
{
    juce::Font f { juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain) };
    return kerning != 0.0f ? f.withExtraKerningFactor (kerning) : f;
}

void label (Graphics& g, const juce::String& t, float cx, float cy, float size, Colour c, float kerning = 0.0f, bool bold = false)
{
    g.setColour (c);
    g.setFont (makeFont (size, bold, kerning));
    g.drawText (t, Rectangle<float> (cx - 200.0f, cy - size, 400.0f, size * 2.0f), juce::Justification::centred, false);
}

void glowStroke (Graphics& g, const Path& p, Colour c, float width, float alpha)
{
    const float w[3] = { width * 4.0f, width * 2.2f, width };
    const float a[3] = { 0.07f, 0.16f, 1.0f };
    for (int i = 0; i < 3; ++i)
    {
        g.setColour (c.withMultipliedAlpha (a[i] * alpha));
        g.strokePath (p, juce::PathStrokeType (w[i], juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

Path capPath()          // local coordinates: base of the stem at (0,0), up is negative
{
    Path c;
    c.startNewSubPath (-140.0f, -146.0f);
    c.cubicTo (-140.0f, -226.0f, -76.0f, -262.0f, 0.0f, -262.0f);
    c.cubicTo (76.0f, -262.0f, 140.0f, -226.0f, 140.0f, -146.0f);
    c.cubicTo (104.0f, -131.0f, -104.0f, -131.0f, -140.0f, -146.0f);
    c.closeSubPath();
    return c;
}

// one mushroom; `tint` shifts the cap colour so the small ones differ from the big one
void drawMushroom (Graphics& g, Point<float> base, float s, Colour capTop, Colour capBot, bool spots)
{
    g.saveState();
    g.addTransform (juce::AffineTransform::scale (s).translated (base.x, base.y));

    // stem
    Path stem;
    stem.startNewSubPath (-23.0f, -148.0f);
    stem.cubicTo (-22.0f, -92.0f, -39.0f, -42.0f, -37.0f, -2.0f);
    stem.quadraticTo (0.0f, 8.0f, 37.0f, -2.0f);
    stem.cubicTo (39.0f, -42.0f, 22.0f, -92.0f, 23.0f, -148.0f);
    stem.closeSubPath();
    juce::ColourGradient sg (Colour (176, 156, 128), -38.0f, 0.0f, Colour (160, 140, 112), 38.0f, 0.0f, false);
    sg.addColour (0.35, Colour (244, 233, 210));
    sg.addColour (0.62, Colour (226, 211, 184));
    g.setGradientFill (sg);
    g.fillPath (stem);
    // soft shade just under the cap
    juce::ColourGradient sh (Colour (60, 34, 24).withAlpha (0.55f), 0.0f, -140.0f, Colour (60, 34, 24).withAlpha (0.0f), 0.0f, -100.0f, false);
    g.saveState(); g.reduceClipRegion (stem); g.setGradientFill (sh); g.fillRect (-60.0f, -150.0f, 120.0f, 60.0f); g.restoreState();

    // underside of the cap with gills
    Path under;
    under.startNewSubPath (-138.0f, -147.0f);
    under.cubicTo (-104.0f, -118.0f, 104.0f, -118.0f, 138.0f, -147.0f);
    under.cubicTo (104.0f, -131.0f, -104.0f, -131.0f, -138.0f, -147.0f);
    under.closeSubPath();
    g.setColour (Colour (84, 46, 34));
    g.fillPath (under);
    g.setColour (Colour (236, 188, 150).withAlpha (0.38f));
    for (int i = -10; i <= 10; ++i)
    {
        const float x = (float) i * 12.8f;
        const float y = -139.0f + 17.0f * (1.0f - (x / 138.0f) * (x / 138.0f)) * 0.55f;
        g.drawLine (x * 0.2f, -131.0f, x, y, 0.9f);
    }

    // cap
    const Path cap = capPath();
    juce::ColourGradient cg (capTop, 0.0f, -262.0f, capBot, 0.0f, -130.0f, false);
    g.setGradientFill (cg);
    g.fillPath (cap);
    g.saveState();
    g.reduceClipRegion (cap);
    juce::ColourGradient hi (Colour (255, 255, 255).withAlpha (0.22f), -60.0f, -228.0f, Colour (255, 255, 255).withAlpha (0.0f), -60.0f + 110.0f, -228.0f + 40.0f, true);
    g.setGradientFill (hi);
    g.fillEllipse (-170.0f, -270.0f, 220.0f, 110.0f);
    juce::ColourGradient rim (Colour (30, 10, 8).withAlpha (0.0f), 0.0f, -190.0f, Colour (30, 10, 8).withAlpha (0.40f), 0.0f, -130.0f, false);
    g.setGradientFill (rim);
    g.fillRect (-150.0f, -200.0f, 300.0f, 80.0f);
    if (spots)
    {
        static const float sp[][3] = { { -84, -190, 13 }, { -30, -226, 10 }, { 38, -212, 15 }, { 96, -176, 11 }, { -112, -156, 7 }, { 14, -170, 8 }, { 70, -246, 7 }, { -58, -150, 6 } };
        g.setColour (Colour (250, 240, 220).withAlpha (0.88f));
        for (const auto& q : sp) g.fillEllipse (q[0] - q[2], q[1] - q[2] * 0.72f, q[2] * 2.0f, q[2] * 1.44f);
    }
    g.restoreState();
    g.restoreState();
}
} // namespace

//==============================================================================
void LookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s)
{
    const float size = (float) std::min (w, h);
    const Point<float> c ((float) x + (float) w * 0.5f, (float) y + (float) h * 0.5f);
    const float u = size / 90.0f, R = 27.0f * u, ring = 36.0f * u;
    const float ang = a0 + pos * (a1 - a0);
    const bool hot = s.isMouseOverOrDragging();

    Path track;
    track.addCentredArc (c.x, c.y, ring, ring, 0.0f, a0, a1, true);
    g.setColour (Colour (64, 50, 42));
    g.strokePath (track, juce::PathStrokeType (3.0f * u, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    Path val;
    val.addCentredArc (c.x, c.y, ring, ring, 0.0f, a0, std::max (a0 + 0.001f, ang), true);
    glowStroke (g, val, kAmber, 3.0f * u, hot ? 1.0f : 0.9f);

    // body: a smooth stone-like button
    juce::ColourGradient body (Colour (70, 56, 48), c.x - R * 0.5f, c.y - R * 0.8f, Colour (28, 21, 18), c.x + R * 0.6f, c.y + R, false);
    g.setGradientFill (body);
    g.fillEllipse (c.x - R, c.y - R, 2.0f * R, 2.0f * R);
    g.setColour (Colour (255, 255, 255).withAlpha (hot ? 0.16f : 0.09f));
    g.drawEllipse (c.x - R + 0.5f, c.y - R + 0.5f, 2.0f * R - 1.0f, 2.0f * R - 1.0f, 1.0f);

    // pointer: a little spore
    const Point<float> d (c.x + std::sin (ang) * R * 0.66f, c.y - std::cos (ang) * R * 0.66f);
    juce::ColourGradient dg (kAmber.withAlpha (0.5f), d.x, d.y, kAmber.withAlpha (0.0f), d.x + 8.0f * u, d.y, true);
    g.setGradientFill (dg);
    g.fillEllipse (d.x - 8.0f * u, d.y - 8.0f * u, 16.0f * u, 16.0f * u);
    g.setColour (kAmberLt);
    g.fillEllipse (d.x - 2.6f * u, d.y - 2.6f * u, 5.2f * u, 5.2f * u);
}

//==============================================================================
void Backdrop::paint (Graphics& g)
{
    const float K = (float) getWidth() / kW;
    g.addTransform (juce::AffineTransform::scale (K));

    // night: warm near-black, lighter behind the mushroom
    juce::ColourGradient bg (Colour (34, 26, 22), 280.0f, 190.0f, Colour (12, 9, 8), 280.0f, 640.0f, true);
    bg.addColour (0.55, Colour (22, 17, 15));
    g.setGradientFill (bg);
    g.fillRect (0.0f, 0.0f, kW, kH);

    // ground: a soft mound of moss
    juce::ColourGradient gr (Colour (52, 42, 32), kBase.x, kBase.y - 8.0f, Colour (52, 42, 32).withAlpha (0.0f), kBase.x + 250.0f, kBase.y + 14.0f, true);
    g.setGradientFill (gr);
    g.fillEllipse (kBase.x - 250.0f, kBase.y - 20.0f, 500.0f, 50.0f);
    g.setColour (Colour (92, 110, 64).withAlpha (0.55f));
    for (int i = 0; i < 46; ++i)
    {
        uint32_t h = (uint32_t) (i + 3) * 2654435761u; h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
        const float t = ((float) (h & 1023u) / 1023.0f - 0.5f) * 2.0f;
        const float x = kBase.x + t * 215.0f, y = kBase.y + 2.0f + ((float) ((h >> 10) & 255u) / 255.0f) * 12.0f * (1.0f - 0.5f * std::fabs (t));
        const float r = 1.0f + (float) ((h >> 20) & 3u) * 0.5f;
        g.fillEllipse (x - r, y - r * 0.7f, 2.0f * r, 1.4f * r);
    }

    // the small ones
    drawMushroom (g, { 138.0f, kBase.y + 4.0f }, 0.36f, Colour (232, 178, 108), Colour (168, 98, 62), false);
    drawMushroom (g, { 432.0f, kBase.y + 6.0f }, 0.27f, Colour (214, 120, 96), Colour (140, 62, 56), true);
    drawMushroom (g, { 92.0f, kBase.y + 8.0f }, 0.17f, Colour (240, 214, 170), Colour (190, 150, 112), false);

    // the big one
    drawMushroom (g, kBase, 1.0f, Colour (250, 176, 118), Colour (178, 66, 54), true);

    // title
    label (g, "spore", 280.0f, 30.0f, 17.0f, kCream.withAlpha (0.9f), 0.5f);
    g.setColour (kAmber.withAlpha (0.5f));
    g.fillEllipse (280.0f - 1.5f, 44.0f, 3.0f, 3.0f);
}

//==============================================================================
void GerminateButton::paintButton (Graphics& g, bool over, bool down)
{
    const float K = (float) getWidth() / kButton.getWidth();
    g.addTransform (juce::AffineTransform::scale (K));
    const auto r = Rectangle<float> (0.0f, 0.0f, kButton.getWidth(), kButton.getHeight()).reduced (6.0f);
    const bool on = getToggleState();
    const float cr = r.getHeight() * 0.5f;

    if (on)
    {
        const float p = 0.5f + 0.5f * std::sin (pulse);
        for (int i = 4; i >= 1; --i)
        {
            g.setColour (kAmber.withAlpha ((0.04f + 0.03f * p) * (float) (5 - i)));
            g.fillRoundedRectangle (r.expanded ((float) i * 2.0f), cr + (float) i * 2.0f);
        }
        juce::ColourGradient gr (kAmberLt, 0.0f, r.getY(), Colour (240, 150, 84), 0.0f, r.getBottom(), false);
        g.setGradientFill (gr);
        g.fillRoundedRectangle (r, cr);
        label (g, "germinate", r.getCentreX(), r.getCentreY(), 13.0f, kInk, 0.18f, true);
    }
    else
    {
        g.setColour (Colour (255, 255, 255).withAlpha (over ? 0.07f : 0.03f));
        g.fillRoundedRectangle (r, cr);
        g.setColour (kAmber.withAlpha ((over ? 0.95f : 0.6f) * (down ? 0.7f : 1.0f)));
        g.drawRoundedRectangle (r, cr, 1.3f);
        label (g, "germinate", r.getCentreX(), r.getCentreY(), 13.0f, kAmber.withAlpha (over ? 1.0f : 0.85f), 0.18f, true);
    }
}
} // namespace sporeui

//==============================================================================
// Animated layer: the cap glows with the sound, grains drift down as spores
//==============================================================================
class SporeEditor::Live : public juce::Component
{
public:
    Live (SporeAudioProcessor& p, std::array<juce::Slider, 4>& k) : proc (p), knobs (k) { setInterceptsMouseClicks (false, false); }

    void advance (float dt)
    {
        time += dt;
        const float po = proc.takeOutputPeak();
        proc.takeInputPeak();
        lvOut = std::max (po, lvOut * std::exp (-dt * 5.0f));
        activity += (std::clamp (lvOut * 6.0f, 0.0f, 1.0f) - activity) * std::min (1.0f, dt * 6.0f);
        frozenAmt += ((proc.isFrozen() ? 1.0f : 0.0f) - frozenAmt) * std::min (1.0f, dt * 4.0f);
        drift += dt * (1.0f - 0.92f * frozenAmt);          // spores almost stop when frozen
    }

    void paint (Graphics& g) override
    {
        using namespace sporeui;
        const float K = (float) getWidth() / kW;
        g.addTransform (juce::AffineTransform::scale (K));

        // warm light on the cap: follows the loudness, stays on when frozen
        {
            const float a = 0.02f + 0.08f * activity + 0.10f * frozenAmt * (0.8f + 0.2f * std::sin (time * 2.0f));
            juce::ColourGradient gl (kAmber.withAlpha (a), kBase.x, kBase.y - 190.0f, kAmber.withAlpha (0.0f), kBase.x + 210.0f, kBase.y - 190.0f, true);
            g.setGradientFill (gl);
            g.fillEllipse (kBase.x - 210.0f, kBase.y - 330.0f, 420.0f, 280.0f);
        }

        // spores
        for (int i = 0; i < SporeAudioProcessor::kGrains; ++i)
        {
            const auto v = proc.getGrain (i);
            if (! v.active || v.env < 0.02f) continue;
            uint32_t h = (uint32_t) (i + 1) * 2654435761u; h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
            const float r1 = (float) (h & 255u) / 255.0f, r2 = (float) ((h >> 8) & 255u) / 255.0f, r3 = (float) ((h >> 16) & 255u) / 255.0f;
            // x follows the grain's pan, y how far back in time it reads: older material has fallen further
            const float x = kBase.x + ((v.pan - 0.5f) * 1.6f + (r1 - 0.5f) * 0.9f) * 235.0f + 9.0f * std::sin (drift * (0.5f + r3) + r2 * 6.0f);
            const float fall = std::clamp (v.depth * 1.6f + r2 * 0.55f, 0.0f, 1.0f);
            const float y = (kBase.y - 128.0f) + fall * 150.0f + 5.0f * std::sin (drift * (0.7f + r1) + r3 * 6.0f);
            const float e = std::sqrt (std::clamp (v.env, 0.0f, 1.0f));
            const float vis = (0.45f + 0.55f * activity) * e;
            const float size = 1.8f + 2.4f * e * (0.6f + 0.6f * r3);
            juce::ColourGradient gl (kAmber.withAlpha (0.30f * vis), x, y, kAmber.withAlpha (0.0f), x + size * 3.4f, y, true);
            g.setGradientFill (gl);
            g.fillEllipse (x - size * 3.4f, y - size * 3.4f, size * 6.8f, size * 6.8f);
            g.setColour (kAmberLt.withAlpha (0.2f + 0.8f * vis));
            g.fillEllipse (x - size, y - size, 2.0f * size, 2.0f * size);
        }

        // knob labels: the value replaces the name while a knob is touched
        for (int i = 0; i < 4; ++i)
        {
            const bool hot = knobs[(size_t) i].isMouseOverOrDragging();
            const auto t = hot ? knobs[(size_t) i].getTextFromValue (knobs[(size_t) i].getValue()) : juce::String (kKnobName[i]);
            label (g, t, kKnobX[i], kKnobY + 60.0f, 12.0f, hot ? kAmberLt : kDim, 0.14f);
        }
    }

private:
    SporeAudioProcessor& proc;
    std::array<juce::Slider, 4>& knobs;
    float time = 0.0f, lvOut = 0.0f, activity = 0.0f, frozenAmt = 0.0f, drift = 0.0f;
};

//==============================================================================
SporeEditor::SporeEditor (SporeAudioProcessor& p) : juce::AudioProcessorEditor (&p), proc (p)
{
    live = std::make_unique<Live> (p, knobs);
    addAndMakeVisible (backdrop);
    addAndMakeVisible (*live);

    static const char* ids[4] = { "density", "size", "blur", "decay" };
    static const double defaults[4] = { 0.6, 0.8, 0.3, 0.2 };
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
    for (int i = 0; i < 4; ++i) knobs[(size_t) i].setBounds (R (cx[i] - 45.0f, 456.0f - 45.0f, 90.0f, 90.0f));
    button.setBounds (R (200.0f, 550.0f, 160.0f, 44.0f));
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
    button.pulse += seconds * 3.0f;
    if (button.getToggleState()) button.repaint();
    live->repaint();
}
