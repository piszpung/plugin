#include "PluginEditor.h"

using namespace residueui;

namespace
{
juce::Font font (float size, bool bold = false) { return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain)); }
constexpr float kPi = 3.14159265f;

// text with the 3D-glasses fringe: red shifted left, blue shifted right, white on top
void fringeText (juce::Graphics& g, const juce::String& t, juce::Rectangle<int> r, juce::Justification j, juce::Font f, float offs, float alpha = 1.0f)
{
    g.setFont (f);
    g.setColour (red().withAlpha (alpha));  g.drawText (t, r.translated (-(int) std::round (offs), 0), j, false);
    g.setColour (blue().withAlpha (alpha)); g.drawText (t, r.translated ((int) std::round (offs), 0), j, false);
    g.setColour (juce::Colours::white.withAlpha (alpha)); g.drawText (t, r, j, false);
}
}

// ---------------------------------------------------------------------------------------------------------------------
void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
{
    auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (5.0f);
    const float r = std::min (b.getWidth(), b.getHeight()) * 0.5f, cx = b.getCentreX(), cy = b.getCentreY();
    const float ang = start + pos * (end - start);

    juce::Path track; track.addCentredArc (cx, cy, r, r, 0.0f, start, end, true);
    g.setColour (juce::Colour (0xff1c1c24)); g.strokePath (track, juce::PathStrokeType (2.6f));
    juce::Path val; val.addCentredArc (cx, cy, r, r, 0.0f, start, std::max (ang, start + 0.001f), true);
    const bool hot = s.isMouseOverOrDragging();
    const float o = hot ? 2.2f : 1.6f;
    g.setColour (red().withAlpha (0.95f));  g.strokePath (val, juce::PathStrokeType (2.8f), juce::AffineTransform::translation (-o, -0.5f));
    g.setColour (blue().withAlpha (0.95f)); g.strokePath (val, juce::PathStrokeType (2.8f), juce::AffineTransform::translation (o, 0.5f));
    g.setColour (juce::Colours::white);     g.strokePath (val, juce::PathStrokeType (1.5f));
    const float px = cx + r * std::sin (ang), py = cy - r * std::cos (ang);
    g.fillEllipse (px - 2.6f, py - 2.6f, 5.2f, 5.2f);

    g.setColour (juce::Colours::white.withAlpha (0.92f)); g.setFont (font (12.0f));
    g.drawText (s.getTextFromValue (s.getValue()), juce::Rectangle<float> (cx - r, cy - 8.0f, 2.0f * r, 16.0f), juce::Justification::centred, false);
}

void Pill::paintButton (juce::Graphics& g, bool hover, bool)
{
    auto b = getLocalBounds().toFloat().reduced (1.5f);
    const bool on = getToggleState();
    const float rad = b.getHeight() * 0.5f;
    if (on) { g.setColour (red().withAlpha (0.9f)); g.drawRoundedRectangle (b.translated (-1.5f, 0.0f), rad, 1.4f); g.setColour (blue().withAlpha (0.9f)); g.drawRoundedRectangle (b.translated (1.5f, 0.0f), rad, 1.4f); }
    g.setColour (on ? juce::Colours::white : juce::Colour (hover ? 0xff7c7c8c : 0xff4a4a56)); g.drawRoundedRectangle (b, rad, 1.2f);
    g.setFont (font (11.5f, true)); g.drawText (getButtonText(), getLocalBounds(), juce::Justification::centred, false);
}

void Segmented::paint (juce::Graphics& g)
{
    const int n = labels.size(), cur = current();
    const float cw = (float) getWidth() / (float) n;
    for (int i = 0; i < n; ++i)
    {
        auto r = juce::Rectangle<int> ((int) std::round (cw * (float) i), 0, (int) std::round (cw), getHeight());
        if (i == cur) { fringeText (g, labels[i], r, juce::Justification::centred, font (12.5f, true), 1.6f); }
        else { g.setColour (juce::Colour (i == hover ? 0xffbcbccc : 0xff62626f)); g.setFont (font (12.5f, true)); g.drawText (labels[i], r, juce::Justification::centred, false); }
    }
    const auto cr = juce::Rectangle<float> (cw * (float) cur + cw * 0.28f, (float) getHeight() - 3.0f, cw * 0.44f, 1.6f);
    g.setColour (red());   g.fillRect (cr.translated (-2.0f, 0.0f));
    g.setColour (blue());  g.fillRect (cr.translated (2.0f, 0.0f));
    g.setColour (juce::Colours::white); g.fillRect (cr);
}
void Segmented::mouseDown (const juce::MouseEvent& e)
{
    const int i = indexAt (e.x);
    param->beginChangeGesture(); param->setValueNotifyingHost (param->convertTo0to1 ((float) i)); param->endChangeGesture();
    repaint();
}

// ---------------------------------------------------------------------------------------------------------------------
void EchoVisual::resized()
{
    const auto fmt = juce::Image::ARGB;
    redImg = juce::Image (fmt, std::max (1, getWidth()), std::max (1, getHeight()), true, juce::SoftwareImageType());
    blueImg = juce::Image (fmt, std::max (1, getWidth()), std::max (1, getHeight()), true, juce::SoftwareImageType());
    outImg = juce::Image (fmt, std::max (1, getWidth()), std::max (1, getHeight()), true, juce::SoftwareImageType());
}

void EchoVisual::drawLayer (juce::Graphics& g, bool isRed, float fb, float spread, float wobble, float mix, float level, float period) const
{
    (void) period;
    const float w = (float) getWidth(), h = (float) getHeight();
    constexpr int kRings = 16;
    const float zPh = phase;
    const float base = 0.46f * h, stretch = 1.75f;
    const juce::Colour near = isRed ? red() : blue();
    const juce::Colour far = isRed ? juce::Colour (200, 20, 70) : juce::Colour (30, 90, 255);
    const float bFb = std::max (fb, 0.3f);

    for (int k = kRings - 1; k >= 0; --k)
    {
        const float z = (float) k + zPh;
        const float s = 1.0f / (1.0f + 0.42f * z);
        float b = std::pow (bFb, z * 0.85f) * std::min (1.0f, z * 5.0f) * std::clamp ((float) kRings - 1.5f - z, 0.0f, 1.0f);
        b = std::min (1.0f, b * (1.15f + 0.45f * std::sqrt (std::clamp (level, 0.0f, 1.0f))));
        if (b < 0.015f) continue;
        const float R = base * s;
        const float side = spread * 0.30f * w * std::cos (kPi * z) * s;
        const float cx = w * 0.5f + side, cy = h * 0.5f - (1.0f - s) * 0.07f * h;
        const float off = 2.5f + 12.0f * s * (0.3f + 0.7f * mix);
        const float ox = isRed ? -off : off, oy = isRed ? -0.35f * off : 0.35f * off;
        const float th = 2.2f + 9.0f * s;
        const auto col = far.interpolatedWith (near, std::clamp (1.0f - z / 9.0f, 0.0f, 1.0f));

        juce::Path ring;
        constexpr int kSeg = 96;
        for (int i = 0; i <= kSeg; ++i)
        {
            const float t = 2.0f * kPi * (float) i / (float) kSeg;
            const float rr = R * (1.0f + 0.06f * wobble * std::sin (4.0f * t + 2.2f * anim + 0.7f * z) + 0.03f * wobble * std::sin (7.0f * t - 1.3f * anim));
            const float px = cx + ox + rr * stretch * std::cos (t), py = cy + oy + rr * std::sin (t);
            if (i == 0) ring.startNewSubPath (px, py); else ring.lineTo (px, py);
        }
        ring.closeSubPath();
        g.setColour (col.withMultipliedBrightness (b * 0.28f)); g.strokePath (ring, juce::PathStrokeType (th * 3.4f));
        g.setColour (col.withMultipliedBrightness (b));          g.strokePath (ring, juce::PathStrokeType (th));
    }

    // the source: a bright core at the front, a pulse with the input level
    const float rc = 8.0f + 26.0f * std::sqrt (std::clamp (level, 0.0f, 1.0f));
    const float off = 2.5f + 12.0f * (0.3f + 0.7f * mix);
    const float cx = w * 0.5f + spread * 0.30f * w + (isRed ? -off : off), cy = h * 0.5f + (isRed ? -0.35f * off : 0.35f * off);
    g.setColour (near.withMultipliedBrightness (0.9f)); g.fillEllipse (cx - rc, cy - rc, 2.0f * rc, 2.0f * rc);
}

void EchoVisual::tick()
{
    if (getWidth() < 8 || getHeight() < 8) return;
    constexpr float dt = 1.0f / 30.0f;
    auto prm = [this] (const char* id) { return proc.apvts.getRawParameterValue (id)->load(); };
    const float fb = prm ("fb") * 0.01f, spread = prm ("spread") * 0.01f, wob = prm ("wobble") * 0.01f, mix = prm ("mix") * 0.01f;
    const float period = std::clamp (proc.getDelaySeconds(), 0.12f, 2.0f);
    const float lvl = std::max (proc.getOutputLevel(), proc.getInputLevel());
    shownLevel += (lvl - shownLevel) * 0.35f;
    lastFb = fb; lastSpread = spread; lastWob = wob; lastMix = mix; lastPeriod = period;

    phase += dt / period; phase -= std::floor (phase);
    anim += dt;

    redImg.clear (redImg.getBounds(), juce::Colours::black);
    blueImg.clear (blueImg.getBounds(), juce::Colours::black);
    { juce::Graphics g (redImg);  g.setImageResamplingQuality (juce::Graphics::highResamplingQuality); drawLayer (g, true, fb, spread, wob, mix, shownLevel, period); }
    { juce::Graphics g (blueImg); drawLayer (g, false, fb, spread, wob, mix, shownLevel, period); }

    // additive blend: red + blue = white
    juce::Image::BitmapData a (redImg, juce::Image::BitmapData::readOnly), b (blueImg, juce::Image::BitmapData::readOnly), o (outImg, juce::Image::BitmapData::writeOnly);
    for (int y = 0; y < o.height; ++y)
        for (int x = 0; x < o.width; ++x)
        {
            const auto pa = *(const juce::PixelARGB*) a.getPixelPointer (x, y);
            const auto pb = *(const juce::PixelARGB*) b.getPixelPointer (x, y);
            juce::PixelARGB r;
            r.setARGB (255, (uint8_t) std::min (255, pa.getRed() + pb.getRed()), (uint8_t) std::min (255, pa.getGreen() + pb.getGreen()), (uint8_t) std::min (255, pa.getBlue() + pb.getBlue()));
            *(juce::PixelARGB*) o.getPixelPointer (x, y) = r;
        }
    repaint();
}

void EchoVisual::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
    if (outImg.isValid()) g.drawImageAt (outImg, 0, 0);
    // fade the edges into the black window
    const float w = (float) getWidth(), h = (float) getHeight();
    juce::ColourGradient v (juce::Colours::transparentBlack, w * 0.5f, h * 0.5f, juce::Colours::black, w * 0.5f + w * 0.62f, h * 0.5f, true);
    v.addColour (0.55, juce::Colours::transparentBlack);
    g.setGradientFill (v); g.fillRect (getLocalBounds());
    g.setGradientFill (juce::ColourGradient (juce::Colours::black, 0.0f, 0.0f, juce::Colours::transparentBlack, 0.0f, 26.0f, false)); g.fillRect (0, 0, getWidth(), 26);
    g.setGradientFill (juce::ColourGradient (juce::Colours::black, 0.0f, h, juce::Colours::transparentBlack, 0.0f, h - 26.0f, false)); g.fillRect (0, getHeight() - 26, getWidth(), 26);
    {   // the echo time, in the corner
        const float sec = proc.getDelaySeconds();
        const juce::String t = sec >= 1.0f ? juce::String (sec, 2) + " s" : juce::String (juce::roundToInt (sec * 1000.0f)) + " ms";
        fringeText (g, t, juce::Rectangle<int> (28, getHeight() - 38, 180, 28), juce::Justification::centredLeft, font (20.0f, true).withExtraKerningFactor (0.08f), 1.6f, 0.9f);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
ResidueEditor::ResidueEditor (ResidueAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p), visual (p),
      styleSeg (p.apvts.getParameter ("style"), [] { juce::StringArray s; for (int i = 0; i < residue::kNumStyles; ++i) s.add (residue::styleName (i)); return s; }())
{
    setLookAndFeel (&laf);
    setSize (kW, kH);
    setOpaque (true);

    addAndMakeVisible (visual);
    addAndMakeVisible (styleSeg);
    addAndMakeVisible (syncBtn);
    syncAtt = std::make_unique<ButtonAtt> (proc.apvts, "sync", syncBtn);

    static const char* ids[kNumKnobs] = { "time", "note", "fb", "sat", "wobble", "duck", "spread", "low", "high", "mix" };
    static const char* nm[kNumKnobs] = { "TIME", "NOTE", "FEEDBACK", "SATURATION", "WOBBLE", "DUCKING", "PING-PONG", "LOW CUT", "HIGH CUT", "MIX" };
    for (int i = 0; i < kNumKnobs; ++i)
    {
        auto& s = knobs[(size_t) i];
        names[(size_t) i] = nm[i];
        s.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        s.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        s.setRotaryParameters (kPi * 1.2f, kPi * 2.8f, true);
        s.setMouseDragSensitivity (180);
        addAndMakeVisible (s);
        att[(size_t) i] = std::make_unique<SliderAtt> (proc.apvts, ids[i], s);
        if (auto* prm = proc.apvts.getParameter (ids[i]); prm != nullptr && i != kNote)
            s.setDoubleClickReturnValue (true, prm->convertFrom0to1 (prm->getDefaultValue()));
    }
    updateSyncView();
    startTimerHz (30);
}

ResidueEditor::~ResidueEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

juce::Rectangle<int> ResidueEditor::knobSlot (int i) const
{
    // the TIME and NOTE knobs share the first slot; the rest follow
    const int slot = i == kNote ? 0 : i > kNote ? i - 1 : i;
    const int n = kNumKnobs - 1, left = 34, total = kW - 2 * left, cw = total / n;
    return juce::Rectangle<int> (left + slot * cw, 352, cw, 100);
}

void ResidueEditor::resized()
{
    styleSeg.setBounds (kW - 24 - 330, 18, 330, 26);
    syncBtn.setBounds (kW - 24 - 330 - 14 - 64, 20, 64, 22);
    visual.setBounds (0, 50, kW, 290);
    for (int i = 0; i < kNumKnobs; ++i)
    {
        auto r = knobSlot (i);
        knobs[(size_t) i].setBounds (r.withSizeKeepingCentre (70, 70).withY (r.getY()));
    }
}

void ResidueEditor::updateSyncView()
{
    const bool sync = proc.apvts.getRawParameterValue ("sync")->load() > 0.5f;
    syncShown = sync;
    knobs[kTime].setVisible (! sync);
    knobs[kNote].setVisible (sync);
}

void ResidueEditor::tick()
{
    if ((proc.apvts.getRawParameterValue ("sync")->load() > 0.5f) != syncShown) updateSyncView();
    if (styleSeg.current() != lastStyle) { lastStyle = styleSeg.current(); styleSeg.repaint(); }
    visual.tick();
    repaint (0, 0, kW, 50);
}

void ResidueEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
    fringeText (g, "RESIDUE", juce::Rectangle<int> (28, 10, 300, 36), juce::Justification::centredLeft, font (30.0f, true).withExtraKerningFactor (0.22f), 2.2f);
    g.setColour (juce::Colour (0xff3a3a46)); g.fillRect (28, 342, kW - 56, 1);

    for (int i = 0; i < kNumKnobs; ++i)
    {
        if (i == kTime && syncShown) continue;
        if (i == kNote && ! syncShown) continue;
        auto r = knobSlot (i);
        g.setColour (juce::Colour (0xffa4a4b4)); g.setFont (font (11.0f, true));
        g.drawText (names[(size_t) i], r.getX(), r.getY() + 76, r.getWidth(), 16, juce::Justification::centred, false);
    }
    g.setColour (juce::Colour (0xff4a4a56)); g.setFont (font (10.5f));
    g.drawText ("Double-click a knob to reset it.", 28, kH - 30, 400, 16, juce::Justification::centredLeft, false);
}
