#include "PluginEditor.h"

#include <cmath>

namespace
{
// The layout was designed on a 1024 x 687 canvas and is scaled by K.
constexpr float K = 0.8f;
constexpr int kWidth = 820, kHeight = 550;

inline float S (float v) { return v * K; }
inline int SI (float v) { return (int) std::lround (v * K); }

const juce::Colour kTextLight { 0xfff1f2f4 };
const juce::Colour kTextDim   { 0xffa4a7ad };

juce::Font makeFont (float size, bool bold = false, float kerning = 0.0f)
{
    juce::Font f { juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain) };
    return kerning != 0.0f ? f.withExtraKerningFactor (kerning) : f;
}

// ---- small helpers for the procedural sea ----------------------------------------------
inline float hash2 (int x, int y)
{
    uint32_t h = (uint32_t) x * 374761393u + (uint32_t) y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return (float) (h & 0xffffffu) / 16777215.0f;
}
inline float smooth01 (float t) { return t * t * (3.0f - 2.0f * t); }
inline float lerpf (float a, float b, float t) { return a + (b - a) * t; }
inline float smoothstepf (float a, float b, float x)
{
    const float t = juce::jlimit (0.0f, 1.0f, (x - a) / (b - a));
    return smooth01 (t);
}
float vnoise (float x, float y)
{
    const int xi = (int) std::floor (x), yi = (int) std::floor (y);
    const float tx = smooth01 (x - (float) xi), ty = smooth01 (y - (float) yi);
    const float a = hash2 (xi, yi), b = hash2 (xi + 1, yi), c = hash2 (xi, yi + 1), d = hash2 (xi + 1, yi + 1);
    return lerpf (lerpf (a, b, tx), lerpf (c, d, tx), ty);
}
float fbm (float x, float y)
{
    float v = 0.0f, a = 0.5f;
    for (int i = 0; i < 3; ++i)
    {
        v += a * vnoise (x, y);
        x = x * 2.03f + 17.1f;
        y = y * 2.03f + 9.7f;
        a *= 0.5f;
    }
    return v / 0.875f;
}

struct V3
{
    float r, g, b;
};
inline V3 mixV (V3 a, V3 b, float t) { return { lerpf (a.r, b.r, t), lerpf (a.g, b.g, t), lerpf (a.b, b.b, t) }; }
inline V3 addV (V3 a, V3 b, float k) { return { a.r + b.r * k, a.g + b.g * k, a.b + b.b * k }; }

juce::Path chamferRect (juce::Rectangle<float> r, float c)
{
    juce::Path p;
    p.startNewSubPath (r.getX() + c, r.getY());
    p.lineTo (r.getRight() - c, r.getY());
    p.lineTo (r.getRight(), r.getY() + c);
    p.lineTo (r.getRight(), r.getBottom() - c);
    p.lineTo (r.getRight() - c, r.getBottom());
    p.lineTo (r.getX() + c, r.getBottom());
    p.lineTo (r.getX(), r.getBottom() - c);
    p.lineTo (r.getX(), r.getY() + c);
    p.closeSubPath();
    return p;
}

void drawCaption (juce::Graphics& g, const juce::String& text, float cx, float cy, float size,
                  juce::Colour col = kTextLight, float kerning = 0.14f, bool bold = false)
{
    g.setColour (col);
    g.setFont (makeFont (size * K, bold, kerning));
    g.drawText (text, juce::Rectangle<float> (S (cx) - 120.0f, S (cy) - 14.0f, 240.0f, 28.0f),
                juce::Justification::centred, false);
}

juce::String fmtStretch (double v) { return juce::String ((int) std::lround (v)) + "%"; }
juce::String fmtLpf (double v)     { return juce::String ((int) std::lround (v)) + " Hz"; }
juce::String fmtMix (double v)     { return juce::String (v, 1) + "%"; }
juce::String fmtOut (double v)     { return juce::String (v, 1) + " dB"; }
double parseNumber (const juce::String& t) { return t.retainCharacters ("0123456789.-").getDoubleValue(); }
} // namespace

//==============================================================================
// PixelSea
//==============================================================================
namespace chrono_ui
{
PixelSea::PixelSea (std::atomic<float>* stretchPercentParam) : stretch (stretchPercentParam)
{
    setInterceptsMouseClicks (false, false);
    render();
}

void PixelSea::advance (float seconds)
{
    // the larger the stretch, the slower the waves move
    const float pct = stretch != nullptr ? juce::jmax (100.0f, stretch->load()) : 500.0f;
    const float speed = 1.0f / (1.0f + 0.6f * std::log (pct / 100.0f));
    time += seconds * speed;
    render();
}

void PixelSea::render()
{
    juce::Image::BitmapData bd (image, juce::Image::BitmapData::writeOnly);

    const float n = (float) kCells;
    const float horizon = 0.47f * n;
    const float sunX = 0.5f * n, sunY = horizon - 1.0f;
    const V3 warm { 255.0f, 228.0f, 176.0f };

    for (int y = 0; y < kCells; ++y)
    {
        for (int x = 0; x < kCells; ++x)
        {
            const float u = ((float) x + 0.5f) / n;
            V3 c;

            if ((float) y < horizon)
            {
                const float k = ((float) y + 0.5f) / horizon;           // 0 top ... 1 horizon
                const float kk = std::pow (k, 1.4f);
                c = { lerpf (30.0f, 138.0f, kk), lerpf (44.0f, 150.0f, kk), lerpf (70.0f, 172.0f, kk) };

                const float n1 = fbm (u * 5.5f + time * 0.03f, k * 2.6f + 1.7f);
                const float n2 = fbm (u * 11.0f - time * 0.02f, k * 5.0f + 7.3f);
                const float dens = smoothstepf (0.46f, 0.74f, n1) * (0.25f + 0.75f * std::sin (k * 1.5707963f));
                const V3 cloud = mixV ({ 58.0f, 76.0f, 104.0f }, { 176.0f, 190.0f, 208.0f }, n2);
                c = mixV (c, cloud, dens * 0.9f);

                const float dx = ((float) x + 0.5f - sunX) / (n * 0.17f);
                const float dy = ((float) y + 0.5f - sunY) / (n * 0.20f);
                const float d2 = dx * dx + dy * dy;
                const float glow = std::exp (-d2 * 2.2f);
                const float core = std::exp (-d2 * 38.0f);
                c = addV (c, warm, glow * 0.55f * (1.0f - dens * 0.35f));
                c = mixV (c, { 255.0f, 250.0f, 236.0f }, core);
            }
            else
            {
                const float yy = ((float) y + 0.5f - horizon) / (n - horizon);   // 0 horizon ... 1 bottom
                const float depth = std::pow (yy, 0.65f);
                V3 base { lerpf (96.0f, 22.0f, depth), lerpf (116.0f, 36.0f, depth), lerpf (142.0f, 60.0f, depth) };

                const float wx = u * (10.0f + 46.0f * yy);
                const float wy = std::pow (yy, 0.55f) * 20.0f - time * 0.9f;
                const float w = fbm (wx, wy);
                const float shade = 1.0f + (w - 0.5f) * 0.55f;
                base = { base.r * shade, base.g * shade, base.b * shade };

                const float dxs = (float) x + 0.5f - sunX;
                const float width = 1.2f + 6.5f * yy;
                const float streak = std::exp (-(dxs * dxs) / (width * width));
                const float sparkle = smoothstepf (0.50f, 0.78f, w);
                const float refl = streak * (0.22f + 0.95f * sparkle) * (1.0f - 0.55f * yy);
                base = addV (base, warm, refl * 0.8f);

                const float haze = std::exp (-yy * 9.0f) * 0.55f;
                c = mixV (base, { 168.0f, 182.0f, 200.0f }, haze);
            }

            // posterise for the pixel-art look
            auto q = [] (float v) { return (juce::uint8) juce::jlimit (0.0f, 255.0f, std::floor (v / 10.0f + 0.5f) * 10.0f); };
            bd.setPixelColour (x, y, juce::Colour (q (c.r), q (c.g), q (c.b)));
        }
    }
}

void PixelSea::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const float cx = b.getCentreX(), cy = b.getCentreY();
    const float ringR = b.getWidth() * 0.5f - 19.0f * (b.getWidth() / 304.0f);
    const float imgR = ringR - 3.0f;

    // image in a circular window
    {
        juce::Graphics::ScopedSaveState state (g);
        juce::Path circle;
        circle.addEllipse (cx - imgR, cy - imgR, imgR * 2.0f, imgR * 2.0f);
        g.reduceClipRegion (circle);
        g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
        g.drawImage (image, juce::Rectangle<float> (cx - imgR, cy - imgR, imgR * 2.0f, imgR * 2.0f));

        // soft vignette
        juce::ColourGradient vg (juce::Colours::transparentBlack, cx, cy,
                                 juce::Colour (0xff000000).withAlpha (0.72f), cx + imgR, cy, true);
        vg.addColour (0.62, juce::Colours::transparentBlack);
        g.setGradientFill (vg);
        g.fillEllipse (cx - imgR, cy - imgR, imgR * 2.0f, imgR * 2.0f);
    }

    // glowing ring
    for (int i = 6; i >= 1; --i)
    {
        g.setColour (juce::Colours::white.withAlpha (0.045f));
        const float r = ringR + (float) i * 1.6f;
        g.drawEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f, 2.0f);
    }
    g.setColour (juce::Colour (0xff040405));
    g.drawEllipse (cx - ringR - 3.5f, cy - ringR - 3.5f, (ringR + 3.5f) * 2.0f, (ringR + 3.5f) * 2.0f, 5.0f);
    g.setColour (juce::Colour (0xffeceef1));
    g.drawEllipse (cx - ringR, cy - ringR, ringR * 2.0f, ringR * 2.0f, 2.4f);
    g.setColour (juce::Colours::white.withAlpha (0.18f));
    g.drawEllipse (cx - ringR + 3.0f, cy - ringR + 3.0f, (ringR - 3.0f) * 2.0f, (ringR - 3.0f) * 2.0f, 1.0f);
}

//==============================================================================
// Look and feel
//==============================================================================
ChronoLookAndFeel::ChronoLookAndFeel()
{
    setColour (juce::ComboBox::textColourId, kTextLight);
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff111214));
    setColour (juce::PopupMenu::textColourId, kTextLight);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xff3a3c42));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff0e0f11));
    setColour (juce::TextEditor::textColourId, kTextLight);
    setColour (juce::TextEditor::highlightColourId, juce::Colour (0xff4a4c52));
    setColour (juce::Label::textWhenEditingColourId, kTextLight);
}

void ChronoLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                                          float startAngle, float endAngle, juce::Slider&)
{
    const float cx = (float) x + (float) width * 0.5f, cy = (float) y + (float) height * 0.5f;
    const float r = (float) juce::jmin (width, height) * 0.5f * 0.70f;
    const float angle = startAngle + pos * (endAngle - startAngle);

    // tick dots around the knob
    const int ticks = 11;
    for (int i = 0; i < ticks; ++i)
    {
        const float f = (float) i / (float) (ticks - 1);
        const float a = startAngle + f * (endAngle - startAngle);
        const float rr = r * 1.34f;
        const bool lit = f <= pos + 0.001f;
        g.setColour (lit ? juce::Colour (0xffc6c9cf) : juce::Colour (0xff505257));
        g.fillEllipse (cx + std::sin (a) * rr - 1.5f, cy - std::cos (a) * rr - 1.5f, 3.0f, 3.0f);
    }

    // shadow + body
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillEllipse (cx - r - 4.0f, cy - r - 1.0f, r * 2.0f + 8.0f, r * 2.0f + 8.0f);
    g.setColour (juce::Colour (0xff09090a));
    g.fillEllipse (cx - r - 2.5f, cy - r - 2.5f, r * 2.0f + 5.0f, r * 2.0f + 5.0f);

    juce::ColourGradient body (juce::Colour (0xff62656b), cx - r * 0.35f, cy - r * 0.45f,
                               juce::Colour (0xff131416), cx + r * 0.7f, cy + r * 0.8f, true);
    body.addColour (0.55, juce::Colour (0xff2b2d31));
    g.setGradientFill (body);
    g.fillEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f);

    // brushed concentric rings
    for (int i = 1; i <= 5; ++i)
    {
        const float rr = r * (0.18f + 0.16f * (float) i);
        g.setColour (juce::Colours::white.withAlpha (i % 2 ? 0.035f : 0.02f));
        g.drawEllipse (cx - rr, cy - rr, rr * 2.0f, rr * 2.0f, 1.0f);
    }
    g.setColour (juce::Colour (0xff8d9097).withAlpha (0.55f));
    g.drawEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f, 1.3f);

    // pointer with glow
    const float x1 = cx + std::sin (angle) * r * 0.22f, y1 = cy - std::cos (angle) * r * 0.22f;
    const float x2 = cx + std::sin (angle) * r * 0.88f, y2 = cy - std::cos (angle) * r * 0.88f;
    g.setColour (juce::Colours::white.withAlpha (0.16f));
    g.drawLine (x1, y1, x2, y2, 7.0f);
    g.setColour (juce::Colours::white.withAlpha (0.95f));
    juce::Path line;
    line.startNewSubPath (x1, y1);
    line.lineTo (x2, y2);
    g.strokePath (line, juce::PathStrokeType (2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void ChronoLookAndFeel::drawLabel (juce::Graphics& g, juce::Label& label)
{
    if (dynamic_cast<juce::Slider*> (label.getParentComponent()) != nullptr)
    {
        const auto b = label.getLocalBounds().toFloat().reduced (1.5f);
        g.setColour (juce::Colour (0xff0d0e10));
        g.fillRoundedRectangle (b, 8.0f);
        g.setColour (juce::Colour (0xff52545a));
        g.drawRoundedRectangle (b, 8.0f, 1.5f);
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.drawRoundedRectangle (b.reduced (3.0f), 6.0f, 1.0f);
        if (! label.isBeingEdited())
        {
            g.setColour (kTextLight);
            g.setFont (makeFont (18.0f * K, true));
            g.drawText (label.getText(), label.getLocalBounds(), juce::Justification::centred, false);
        }
    }
    else
    {
        LookAndFeel_V4::drawLabel (g, label);
    }
}

juce::Font ChronoLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return makeFont (20.0f * K, true);
}

void ChronoLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox&)
{
    const auto b = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (1.5f);
    g.setColour (juce::Colour (0xff25262a));
    g.fillRoundedRectangle (b, 9.0f);
    g.setColour (juce::Colour (0xff5b5d63));
    g.drawRoundedRectangle (b, 9.0f, 1.6f);
    const auto inner = b.reduced (4.0f);
    juce::ColourGradient fill (juce::Colour (0xff191a1d), 0.0f, inner.getY(), juce::Colour (0xff0c0d0e), 0.0f, inner.getBottom(), false);
    g.setGradientFill (fill);
    g.fillRoundedRectangle (inner, 6.0f);

    // up/down arrows
    const float ax = (float) width - 20.0f, ay = (float) height * 0.5f;
    juce::Path up, down;
    up.addTriangle (ax - 4.5f, ay - 2.0f, ax + 4.5f, ay - 2.0f, ax, ay - 7.0f);
    down.addTriangle (ax - 4.5f, ay + 2.0f, ax + 4.5f, ay + 2.0f, ax, ay + 7.0f);
    g.setColour (juce::Colour (0xffb4b7bd));
    g.fillPath (up);
    g.fillPath (down);
}

void ChronoLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool)
{
    const bool boxRight = (bool) button.getProperties().getWithDefault ("boxRight", false);
    const float boxSize = 22.0f * K + 4.0f;
    const auto area = button.getLocalBounds().toFloat();
    const float by = area.getCentreY() - boxSize * 0.5f;
    const float bx = boxRight ? area.getRight() - boxSize - 2.0f : area.getX() + 2.0f;
    const juce::Rectangle<float> box (bx, by, boxSize, boxSize);

    g.setColour (juce::Colour (0xff0d0e10));
    g.fillRoundedRectangle (box, 4.0f);
    g.setColour (highlighted ? juce::Colour (0xffe0e2e6) : juce::Colour (0xffb7bac0));
    g.drawRoundedRectangle (box, 4.0f, 1.6f);

    if (button.getToggleState())
    {
        juce::Path tick;
        tick.startNewSubPath (box.getX() + box.getWidth() * 0.22f, box.getCentreY() + 0.5f);
        tick.lineTo (box.getX() + box.getWidth() * 0.43f, box.getBottom() - box.getHeight() * 0.25f);
        tick.lineTo (box.getRight() - box.getWidth() * 0.2f, box.getY() + box.getHeight() * 0.27f);
        g.setColour (juce::Colours::white);
        g.strokePath (tick, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    g.setColour (kTextLight.withAlpha (highlighted ? 1.0f : 0.9f));
    g.setFont (makeFont (21.0f * K, false, 0.02f));
    auto textArea = area;
    if (boxRight) textArea.setRight (bx - 10.0f); else textArea.setLeft (bx + boxSize + 10.0f);
    g.drawText (button.getButtonText(), textArea, boxRight ? juce::Justification::centredRight : juce::Justification::centredLeft, false);
}

} // namespace chrono_ui

//==============================================================================
// Editor
//==============================================================================
ChronosphereEditor::ChronosphereEditor (ChronosphereAudioProcessor& p)
    : juce::AudioProcessorEditor (&p),
      proc (p),
      sea (p.apvts.getRawParameterValue ("stretch"))
{
    setLookAndFeel (&laf);

    auto setupKnob = [this] (juce::Slider& s, bool valueBox)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        s.setTextBoxStyle (valueBox ? juce::Slider::TextBoxBelow : juce::Slider::NoTextBox, false, SI (104.0f), SI (38.0f));
        s.setMouseDragSensitivity (220);
        addAndMakeVisible (s);
    };
    setupKnob (stretchKnob, true);
    setupKnob (lpfKnob, true);
    setupKnob (mixKnob, false);
    setupKnob (outKnob, false);

    attStretch = std::make_unique<SliderAtt> (p.apvts, "stretch", stretchKnob);
    attLpf     = std::make_unique<SliderAtt> (p.apvts, "lpf", lpfKnob);
    attMix     = std::make_unique<SliderAtt> (p.apvts, "mix", mixKnob);
    attOut     = std::make_unique<SliderAtt> (p.apvts, "output", outKnob);

    // value formatting (set after the attachments, which install their own)
    auto fmt = [] (juce::Slider& s, juce::String (*f) (double))
    {
        s.textFromValueFunction = [f] (double v) { return f (v); };
        s.valueFromTextFunction = [] (const juce::String& t) { return parseNumber (t); };
        s.updateText();
    };
    fmt (stretchKnob, fmtStretch);
    fmt (lpfKnob, fmtLpf);
    fmt (mixKnob, fmtMix);
    fmt (outKnob, fmtOut);

    mixKnob.onValueChange = [this] { repaint(); };
    outKnob.onValueChange = [this] { repaint(); };

    for (auto* box : { &keyBox, &scaleBox })
    {
        box->setJustificationType (juce::Justification::centred);
        addAndMakeVisible (*box);
    }
    if (auto* kp = dynamic_cast<juce::AudioParameterChoice*> (p.apvts.getParameter ("key")))
        keyBox.addItemList (kp->choices, 1);
    if (auto* sp = dynamic_cast<juce::AudioParameterChoice*> (p.apvts.getParameter ("scale")))
        scaleBox.addItemList (sp->choices, 1);
    attKey   = std::make_unique<ComboAtt> (p.apvts, "key", keyBox);
    attScale = std::make_unique<ComboAtt> (p.apvts, "scale", scaleBox);

    safeButton.getProperties().set ("boxRight", true);
    addAndMakeVisible (resetButton);
    addAndMakeVisible (safeButton);
    attReset = std::make_unique<ButtonAtt> (p.apvts, "autoreset", resetButton);
    attSafe  = std::make_unique<ButtonAtt> (p.apvts, "safe", safeButton);

    addAndMakeVisible (sea);
    sea.startAnimation();

    setSize (kWidth, kHeight);
    setResizable (false, false);
}

ChronosphereEditor::~ChronosphereEditor()
{
    setLookAndFeel (nullptr);
}

void ChronosphereEditor::resized()
{
    sea.setBounds (SI (512.0f - 190.0f), SI (328.0f - 190.0f), SI (380.0f), SI (380.0f));

    stretchKnob.setBounds (SI (237.0f - 65.0f), SI (62.0f), SI (130.0f), SI (166.0f));
    lpfKnob.setBounds (SI (213.0f - 65.0f), SI (290.0f), SI (130.0f), SI (166.0f));
    mixKnob.setBounds (SI (512.0f - 50.0f), SI (545.0f - 50.0f), SI (100.0f), SI (100.0f));
    outKnob.setBounds (SI (815.0f - 42.0f), SI (478.0f - 42.0f), SI (84.0f), SI (84.0f));

    keyBox.setBounds (SI (815.0f - 80.0f), SI (147.0f - 26.0f), SI (160.0f), SI (52.0f));
    scaleBox.setBounds (SI (815.0f - 80.0f), SI (360.0f - 26.0f), SI (160.0f), SI (52.0f));

    resetButton.setBounds (SI (92.0f), SI (622.0f - 20.0f), SI (210.0f), SI (40.0f));
    safeButton.setBounds (SI (800.0f), SI (622.0f - 20.0f), SI (150.0f), SI (40.0f));

    renderBackground();
}

void ChronosphereEditor::renderBackground()
{
    background = juce::Image (juce::Image::ARGB, getWidth(), getHeight(), true);
    juce::Graphics g (background);
    const auto full = getLocalBounds().toFloat();

    // base: very dark metal
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff17181b), 0.0f, 0.0f,
                                             juce::Colour (0xff08080a), 0.0f, full.getBottom(), false));
    g.fillAll();
    g.setGradientFill (juce::ColourGradient (juce::Colour (0x18ffffff), full.getCentreX(), S (330.0f),
                                             juce::Colours::transparentBlack, 0.0f, 0.0f, true));
    g.fillAll();

    // brushed texture
    juce::Random rnd (1234);
    for (int y = 0; y < getHeight(); ++y)
    {
        const float a = rnd.nextFloat() * 0.05f;
        g.setColour ((y % 2 ? juce::Colours::white : juce::Colours::black).withAlpha (a));
        g.fillRect (0, y, getWidth(), 1);
    }

    // chamfered frame lines
    g.setColour (juce::Colour (0xff2e3034));
    g.strokePath (chamferRect (full.reduced (7.0f), 36.0f), juce::PathStrokeType (2.0f));
    g.setColour (juce::Colour (0xff3c3e43).withAlpha (0.7f));
    g.strokePath (chamferRect (full.reduced (20.0f), 48.0f), juce::PathStrokeType (1.2f));

    // bottom bar
    {
        juce::Path bar;
        bar.startNewSubPath (S (120.0f), S (640.0f));
        bar.lineTo (S (205.0f), S (584.0f));
        bar.lineTo (S (1024.0f - 205.0f), S (584.0f));
        bar.lineTo (S (1024.0f - 120.0f), S (640.0f));
        g.setColour (juce::Colour (0xff3a3c41));
        g.strokePath (bar, juce::PathStrokeType (1.6f));
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.drawLine (S (205.0f), S (586.0f), S (1024.0f - 205.0f), S (586.0f), 1.0f);
    }

    // decorative corner lines (left / right)
    for (int side = 0; side < 2; ++side)
    {
        auto X = [side] (float x) { return S (side == 0 ? x : 1024.0f - x); };
        juce::Path d;
        d.startNewSubPath (X (60.0f), S (170.0f));
        d.lineTo (X (104.0f), S (214.0f));
        d.lineTo (X (104.0f), S (300.0f));
        d.lineTo (X (60.0f), S (344.0f));
        g.setColour (juce::Colour (0xff35373b));
        g.strokePath (d, juce::PathStrokeType (1.3f));
        juce::Path e;
        e.startNewSubPath (X (60.0f), S (470.0f));
        e.lineTo (X (110.0f), S (520.0f));
        e.lineTo (X (150.0f), S (520.0f));
        g.strokePath (e, juce::PathStrokeType (1.3f));
    }

    // arcs on both sides of the sea window
    {
        const float cx = S (512.0f), cy = S (328.0f);
        for (int side = 0; side < 2; ++side)
        {
            const float centre = side == 0 ? juce::MathConstants<float>::pi * 1.5f : juce::MathConstants<float>::pi * 0.5f;
            const float span = 0.66f;
            juce::Path arc;
            arc.addCentredArc (cx, cy, S (214.0f), S (214.0f), 0.0f, centre - span, centre + span, true);
            g.setColour (juce::Colour (0xff6b6e74));
            g.strokePath (arc, juce::PathStrokeType (1.5f));

            // radial ticks on the inside of the arc
            for (int i = -14; i <= 14; ++i)
            {
                const float a = centre + (float) i * (span / 14.0f);
                const float r0 = S (206.0f), r1 = S (i % 7 == 0 ? 194.0f : 199.0f);
                g.setColour (juce::Colour (0xff4b4d52).withAlpha (i % 7 == 0 ? 0.9f : 0.55f));
                g.drawLine (cx + std::sin (a) * r0, cy - std::cos (a) * r0,
                            cx + std::sin (a) * r1, cy - std::cos (a) * r1, 1.0f);
            }
            // end dots
            for (float sgn : { -1.0f, 1.0f })
            {
                const float a = centre + sgn * span;
                g.setColour (juce::Colour (0xff7e8187));
                g.drawEllipse (cx + std::sin (a) * S (214.0f) - 3.5f, cy - std::cos (a) * S (214.0f) - 3.5f, 7.0f, 7.0f, 1.2f);
            }
            // middle dot (at 9 o'clock / 3 o'clock)
            const float mx = cx + (side == 0 ? -S (214.0f) : S (214.0f));
            g.setColour (juce::Colour (0xff7e8187));
            g.drawEllipse (mx - 3.5f, cy - 3.5f, 7.0f, 7.0f, 1.2f);
        }
    }

    // planet logo
    {
        const float cx = S (512.0f), cy = S (55.0f);
        juce::ColourGradient halo (juce::Colours::white.withAlpha (0.28f), cx, cy, juce::Colours::transparentBlack, cx + S (62.0f), cy, true);
        g.setGradientFill (halo);
        g.fillEllipse (cx - S (62.0f), cy - S (40.0f), S (124.0f), S (80.0f));

        const float rot = -0.2f, rx = S (50.0f), ry = S (14.0f);
        juce::Path back, front;
        back.addCentredArc (cx, cy, rx, ry, rot, juce::MathConstants<float>::pi, juce::MathConstants<float>::twoPi, true);
        front.addCentredArc (cx, cy, rx, ry, rot, 0.0f, juce::MathConstants<float>::pi, true);
        g.setColour (juce::Colour (0xffe9ebee));
        g.strokePath (back, juce::PathStrokeType (S (5.0f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        const float pr = S (17.0f);
        juce::ColourGradient planet (juce::Colours::white, cx - pr * 0.3f, cy - pr * 0.4f, juce::Colour (0xffb9bcc2), cx + pr, cy + pr, true);
        g.setGradientFill (planet);
        g.fillEllipse (cx - pr, cy - pr, pr * 2.0f, pr * 2.0f);
        g.setColour (juce::Colour (0xfff4f5f7));
        g.strokePath (front, juce::PathStrokeType (S (5.0f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // static captions
    g.setColour (kTextLight);
    g.setFont (makeFont (21.0f * K, false, 0.52f));
    g.drawText ("CHRONOSPHERE", juce::Rectangle<float> (0.0f, S (113.0f) - 16.0f, (float) getWidth(), 32.0f), juce::Justification::centred, false);

    drawCaption (g, "STRETCH", 237.0f, 55.0f, 17.0f);
    drawCaption (g, "LPF", 215.0f, 290.0f - 8.0f, 17.0f);
    drawCaption (g, "KEY", 815.0f, 102.0f, 17.0f);
    drawCaption (g, "SCALE", 818.0f, 315.0f, 17.0f);
}

void ChronosphereEditor::paint (juce::Graphics& g)
{
    g.drawImageAt (background, 0, 0);

    // live readouts of the two knobs without a value box
    drawCaption (g, "MIX  " + fmtMix (mixKnob.getValue()), 512.0f, 618.0f, 16.0f, kTextLight, 0.1f);
    drawCaption (g, "OUTPUT", 815.0f, 548.0f, 15.0f, kTextLight, 0.12f);
    drawCaption (g, fmtOut (outKnob.getValue()), 815.0f, 572.0f, 14.0f, kTextDim, 0.04f);
}
