#include "PluginEditor.h"

using juce::Colour;
using juce::Graphics;
using juce::Path;
using juce::Point;
using juce::Rectangle;

namespace emberui
{
namespace
{
const Colour kBg      { 12, 11, 13 };
const Colour kCard    { 22, 20, 24 };
const Colour kCardHi  { 30, 28, 33 };
const Colour kEdge    { 255, 255, 255 };
const Colour kText    { 238, 232, 226 };
const Colour kDim     { 140, 133, 128 };
const Colour kAmber   { 255, 158, 64 };
const Colour kRed     { 255, 84, 52 };

juce::Font font (float size, bool bold = false) { return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain)); }

void text (Graphics& g, const juce::String& t, Rectangle<float> r, float size, Colour c, juce::Justification j = juce::Justification::centred, bool bold = false, float kern = 0.0f)
{
    g.setColour (c);
    auto f = font (size, bold);
    if (kern != 0.0f) f = f.withExtraKerningFactor (kern);
    g.setFont (f);
    g.drawText (t, r, j, false);
}

void card (Graphics& g, Rectangle<float> r)
{
    juce::ColourGradient gr (kCardHi, r.getX(), r.getY(), kCard, r.getX(), r.getBottom(), false);
    g.setGradientFill (gr);
    g.fillRoundedRectangle (r, 14.0f);
    g.setColour (kEdge.withAlpha (0.06f));
    g.drawRoundedRectangle (r.reduced (0.5f), 14.0f, 1.0f);
}

void glowArc (Graphics& g, const Path& p, Colour c, float w, float a)
{
    const float ws[3] = { w * 4.2f, w * 2.2f, w }, as[3] = { 0.07f, 0.16f, 1.0f };
    for (int i = 0; i < 3; ++i)
    {
        g.setColour (c.withMultipliedAlpha (as[i] * a));
        g.strokePath (p, juce::PathStrokeType (ws[i], juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

juce::RangedAudioParameter* par (EmberAudioProcessor& p, const juce::String& id) { return p.apvts.getParameter (id); }
void setPlain (juce::RangedAudioParameter* p, float v) { if (p) p->setValueNotifyingHost (p->convertTo0to1 (v)); }
float plainOf (juce::RangedAudioParameter* p) { return p ? p->convertFrom0to1 (p->getValue()) : 0.0f; }
juce::String bid (int band, const char* n) { return "b" + juce::String (band + 1) + "_" + n; }

const char* const kStyleNote[ember::kNumStyles] = {
    "a hint of asymmetry  -  barely there", "gentle  -  mostly odd harmonics", "asymmetric  -  even harmonics", "hot  -  asymmetric",
    "soft knee  -  light HF loss", "classic odd harmonics  -  HF roll-off", "soft  -  dark top end", "hard  -  HF roll-off",
    "gentle rational curve", "smooth rational curve", "cubic limiter", "three-piece soft clip", "brick wall",
    "bass saturates first, highs stay clean", "transformer  -  asymmetric, warm", "transformer  -  hard knee",
    "tight, clean preamp with presence", "two gain stages  -  scooped mids", "mid-hump crunch", "three stages  -  singing lead", "hard asymmetric fuzz",
    "wraps back past full scale", "smooth sine fold", "positive half only", "doubles the frequency",
    "Drive = bits and sample rate", "heavy clip + octave down", "all-pass smear into a soft clip" };
} // namespace

Colour bandColour (int b)
{
    static const Colour c[6] = { Colour (255, 106, 61), Colour (255, 166, 61), Colour (242, 212, 92), Colour (239, 93, 138), Colour (167, 107, 239), Colour (70, 194, 201) };
    return c[std::clamp (b, 0, 5)];
}

float orderedXo (EmberAudioProcessor& p, int k)
{
    float f = 20.0f;
    for (int i = 0; i <= k; ++i)
    {
        auto* prm = par (p, "xo" + juce::String (i + 1));
        const float mod = p.getModulatedNormalised (ember::crossoverTarget (i));
        float t = std::clamp (mod >= 0.0f ? prm->convertFrom0to1 (mod) : plainOf (prm), 20.0f, 20000.0f);
        if (i > 0) t = std::max (t, f * 1.12f);
        f = t;
    }
    return f;
}

//==============================================================================
void LookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s)
{
    const bool mini = (bool) s.getProperties().getWithDefault ("mini", false);
    const float cx = (float) x + (float) w * 0.5f, cy = (float) y + (mini ? 21.0f : 38.0f);
    const float R = mini ? 12.5f : 22.0f, ring = mini ? 18.0f : 30.0f, lw = mini ? 2.3f : 3.0f;
    const float ang = a0 + pos * (a1 - a0);
    const bool hot = s.isMouseOverOrDragging();
    const bool bip = (bool) s.getProperties().getWithDefault ("bipolar", false);
    const float modPos = (float) (double) s.getProperties().getWithDefault ("modPos", -1.0);
    const Colour col = s.findColour (juce::Slider::rotarySliderFillColourId);

    Path track; track.addCentredArc (cx, cy, ring, ring, 0.0f, a0, a1, true);
    g.setColour (Colour (62, 58, 62).withAlpha (0.9f));
    g.strokePath (track, juce::PathStrokeType (lw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // with modulation the arc shows where the parameter really is now; the knob itself stays where the user put it
    const float shown = modPos >= 0.0f ? a0 + modPos * (a1 - a0) : ang;
    const float from = bip ? 0.5f * (a0 + a1) : a0;
    const float lo = std::min (from, shown), hi = std::max (from, shown);
    if (hi - lo > 0.004f)
    {
        Path val; val.addCentredArc (cx, cy, ring, ring, 0.0f, lo, hi, true);
        glowArc (g, val, col, lw, hot ? 1.0f : 0.9f);
    }
    if (modPos >= 0.0f)
    {
        const float lo2 = std::min (ang, shown), hi2 = std::max (ang, shown);
        if (hi2 - lo2 > 0.004f) { Path m; m.addCentredArc (cx, cy, ring + lw + 1.5f, ring + lw + 1.5f, 0.0f, lo2, hi2, true); g.setColour (Colour (255, 255, 255).withAlpha (0.55f)); g.strokePath (m, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)); }
        const float mx = cx + std::sin (shown) * (ring + lw + 1.5f), my = cy - std::cos (shown) * (ring + lw + 1.5f);
        g.setColour (Colour (255, 255, 255)); g.fillEllipse (mx - 2.4f, my - 2.4f, 4.8f, 4.8f);
    }

    juce::ColourGradient body (Colour (72, 68, 74), cx - R * 0.5f, cy - R * 0.9f, Colour (18, 16, 20), cx + R * 0.5f, cy + R, false);
    g.setGradientFill (body);
    g.fillEllipse (cx - R, cy - R, 2 * R, 2 * R);
    g.setColour (kEdge.withAlpha (hot ? 0.22f : 0.11f));
    g.drawEllipse (cx - R + 0.5f, cy - R + 0.5f, 2 * R - 1.0f, 2 * R - 1.0f, 1.0f);
    // pointer
    const float sn = std::sin (ang), cs = -std::cos (ang);
    g.setColour (Colour (255, 250, 244));
    g.drawLine (cx + sn * R * 0.30f, cy + cs * R * 0.30f, cx + sn * R * 0.86f, cy + cs * R * 0.86f, mini ? 1.8f : 2.4f);
    if (! mini)
    {
        juce::ColourGradient dot (col.withAlpha (0.5f), cx + sn * R * 0.86f, cy + cs * R * 0.86f, col.withAlpha (0.0f), cx + sn * R * 0.86f + 7.0f, cy + cs * R * 0.86f, true);
        g.setGradientFill (dot);
        g.fillEllipse (cx + sn * R * 0.86f - 7.0f, cy + cs * R * 0.86f - 7.0f, 14.0f, 14.0f);
    }

    // caption: the name, or the value while the knob is touched
    const auto cap = hot ? s.getTextFromValue (s.getValue()) : s.getName();
    text (g, cap, Rectangle<float> ((float) x - 4.0f, (float) (y + h) - (mini ? 17.0f : 22.0f), (float) w + 8.0f, mini ? 14.0f : 18.0f), mini ? (hot ? 10.0f : 9.5f) : (hot ? 12.5f : 11.0f), hot ? kText : kDim, juce::Justification::centred, hot, hot ? 0.0f : 0.06f);
}

// thin bipolar bar used for the amount of a modulation route
void LookAndFeel::drawLinearSlider (Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle, juce::Slider& s)
{
    const Colour col = s.findColour (juce::Slider::trackColourId);
    const float cy = (float) y + (float) h * 0.5f, x0 = (float) x + 2.0f, x1 = (float) (x + w) - 2.0f, mid = 0.5f * (x0 + x1);
    g.setColour (Colour (10, 9, 11)); g.fillRoundedRectangle (x0 - 2.0f, cy - 4.0f, x1 - x0 + 4.0f, 8.0f, 4.0f);
    g.setColour (kEdge.withAlpha (0.10f)); g.fillRect (mid - 0.5f, cy - 5.0f, 1.0f, 10.0f);
    const float lo = std::min (mid, pos), hi = std::max (mid, pos);
    if (hi - lo > 0.5f) { g.setColour (col.withAlpha (s.isMouseOverOrDragging() ? 1.0f : 0.85f)); g.fillRoundedRectangle (lo, cy - 3.0f, hi - lo, 6.0f, 3.0f); }
    g.setColour (Colour (255, 250, 244)); g.fillEllipse (pos - 4.0f, cy - 4.0f, 8.0f, 8.0f);
}

void Pill::paintButton (Graphics& g, bool hover, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    const bool on = getToggleState();
    g.setColour (on ? colour.withAlpha (0.92f) : Colour (40, 37, 42).brighter (hover ? 0.15f : 0.0f));
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour (on ? colour.brighter (0.4f).withAlpha (0.6f) : kEdge.withAlpha (0.10f));
    g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);
    emberui::text (g, getButtonText(), r.translated (0.0f, down ? 0.5f : 0.0f), 10.5f, on ? Colour (24, 14, 10) : kDim, juce::Justification::centred, true, 0.1f);
}

void Segmented::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (Colour (10, 9, 11));
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour (kEdge.withAlpha (0.08f));
    g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);
    const int n = labels.size();
    const int cur = param ? juce::roundToInt (plainOf (param)) : 0;
    const float cw = r.getWidth() / (float) std::max (1, n);
    for (int i = 0; i < n; ++i)
    {
        auto c = Rectangle<float> (r.getX() + cw * (float) i, r.getY(), cw, r.getHeight()).reduced (2.0f);
        if (i == cur)
        {
            juce::ColourGradient gr (kAmber, c.getX(), c.getY(), kRed, c.getRight(), c.getBottom(), false);
            g.setGradientFill (gr);
            g.fillRoundedRectangle (c, c.getHeight() * 0.5f);
        }
        else if (i == hover) { g.setColour (kEdge.withAlpha (0.07f)); g.fillRoundedRectangle (c, c.getHeight() * 0.5f); }
        text (g, labels[i], c, 11.0f, i == cur ? Colour (28, 14, 8) : kDim, juce::Justification::centred, i == cur);
    }
}

void Segmented::mouseDown (const juce::MouseEvent& e)
{
    if (! param) return;
    param->beginChangeGesture();
    param->setValueNotifyingHost (param->convertTo0to1 ((float) indexAt (e.x)));
    param->endChangeGesture();
    repaint();
}

//==============================================================================
void StylePanel::setBand (int b)
{
    band = b;
    styleP = par (proc, bid (b, "style")); driveP = par (proc, bid (b, "drive")); mixP = par (proc, bid (b, "mix"));
}

void StylePanel::setStyle (int i)
{
    const int n = ember::kNumStyles;
    i = ((i % n) + n) % n;
    styleP->beginChangeGesture(); styleP->setValueNotifyingHost (styleP->convertTo0to1 ((float) i)); styleP->endChangeGesture();
    repaint();
}

void StylePanel::mouseDown (const juce::MouseEvent& e)
{
    if (! styleP || e.y < 28 || e.y > 78) return;
    const int cur = juce::roundToInt (plainOf (styleP));
    if (e.x < 36) setStyle (cur - 1);
    else if (e.x > getWidth() - 36) setStyle (cur + 1);
    else
    {
        juce::PopupMenu m;
        for (int gI = 0; gI < ember::kNumGroups; ++gI)
        {
            juce::PopupMenu sub;
            for (int i = ember::groupStart (gI); i < ember::groupStart (gI + 1); ++i) sub.addItem (i + 1, ember::styleName (i), true, i == cur);
            m.addSubMenu (ember::groupName (gI), sub, true, nullptr, ember::groupOf (cur) == gI);
        }
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMinimumWidth (getWidth() - 40), [this] (int r) { if (r > 0) setStyle (r - 1); });
    }
}

void StylePanel::paint (Graphics& g)
{
    if (! styleP) return;
    const auto col = bandColour (band);
    const int st = juce::roundToInt (plainOf (styleP));
    const float w = (float) getWidth();

    text (g, "STYLE", { 18.0f, 14.0f, 80.0f, 14.0f }, 10.0f, kDim, juce::Justification::centredLeft, true, 0.2f);
    text (g, juce::String (ember::groupName (ember::groupOf (st))).toUpperCase(), { w - 118.0f, 14.0f, 100.0f, 14.0f }, 9.5f, col.withAlpha (0.9f), juce::Justification::centredRight, true, 0.18f);
    // arrows + name
    auto arrow = [&] (float cx, int dir)
    {
        Path p; p.addTriangle (cx + 5.0f * (float) dir, 53.0f - 7.0f, cx + 5.0f * (float) dir, 53.0f + 7.0f, cx - 5.0f * (float) dir, 53.0f);
        g.setColour (kText.withAlpha (0.55f)); g.fillPath (p);
    };
    arrow (20.0f, 1); arrow (w - 20.0f, -1);
    g.setColour (Colour (10, 9, 11)); g.fillRoundedRectangle (Rectangle<float> (36.0f, 36.0f, w - 72.0f, 34.0f), 17.0f);
    g.setColour (kEdge.withAlpha (0.07f)); g.drawRoundedRectangle (Rectangle<float> (36.5f, 36.5f, w - 73.0f, 33.0f), 17.0f, 1.0f);
    text (g, ember::styleName (st), { 36.0f, 36.0f, w - 72.0f, 34.0f }, st == 8 || st == 13 || st == 14 || st == 15 || st == 17 || st == 18 ? 13.0f : 15.0f, kText, juce::Justification::centred, true);

    // transfer curve
    const Rectangle<float> box (18.0f, 86.0f, w - 36.0f, 118.0f);
    g.setColour (Colour (10, 9, 11)); g.fillRoundedRectangle (box, 10.0f);
    g.setColour (kEdge.withAlpha (0.07f)); g.drawRoundedRectangle (box.reduced (0.5f), 10.0f, 1.0f);
    const auto in = box.reduced (10.0f);
    const auto X = [&] (float v) { return in.getCentreX() + v * in.getWidth() * 0.5f; };
    const auto Y = [&] (float v) { return in.getCentreY() - v * in.getHeight() * 0.5f / 1.25f; };
    g.setColour (kEdge.withAlpha (0.07f));
    g.drawLine (in.getX(), in.getCentreY(), in.getRight(), in.getCentreY()); g.drawLine (in.getCentreX(), in.getY(), in.getCentreX(), in.getBottom());
    g.setColour (kEdge.withAlpha (0.20f));
    { Path d; d.startNewSubPath (X (-1.0f), Y (-1.0f)); d.lineTo (X (1.0f), Y (1.0f)); const float dl[2] = { 3.0f, 4.0f }; juce::PathStrokeType (1.0f).createDashedStroke (d, d, dl, 2); g.fillPath (d); }
    const float gain = std::pow (10.0f, plainOf (driveP) * 0.05f), mix = plainOf (mixP) * 0.01f;
    Path c;
    g.saveState(); g.reduceClipRegion (box.reduced (4.0f).toNearestInt());
    for (int i = 0; i <= 200; ++i)
    {
        const float x = -1.0f + 2.0f * (float) i / 200.0f;
        const float y = std::clamp ((1.0f - mix) * x + mix * ember::shape (st, x * gain), -1.25f, 1.25f);
        if (i == 0) c.startNewSubPath (X (x), Y (y)); else c.lineTo (X (x), Y (y));
    }
    glowArc (g, c, col, 2.0f, 1.0f);
    g.restoreState();
    text (g, "in", { box.getRight() - 28.0f, box.getCentreY() + 2.0f, 22.0f, 12.0f }, 9.0f, kDim, juce::Justification::centredRight);
    text (g, "out", { box.getCentreX() + 4.0f, box.getY() + 3.0f, 26.0f, 12.0f }, 9.0f, kDim, juce::Justification::centredLeft);
    text (g, kStyleNote[std::clamp (st, 0, ember::kNumStyles - 1)], { 12.0f, box.getBottom() + 8.0f, w - 24.0f, 28.0f }, 10.5f, kDim, juce::Justification::centred);
}

//==============================================================================
Display::Display (EmberAudioProcessor& p, std::function<void (int)> onSel) : proc (p), select (std::move (onSel))
{
    for (int i = 0; i < kN; ++i) window[(size_t) i] = 0.5f - 0.5f * std::cos (6.283185307f * (float) i / (float) (kN - 1));
    specIn.fill (-140.0f); specOut.fill (-140.0f); tmpIn.fill (0.0f); work.fill (0.0f);
}

void Display::resized() { plot = getLocalBounds().toFloat().withTrimmedLeft (44.0f).withTrimmedRight (14.0f).withTrimmedTop (10.0f).withTrimmedBottom (24.0f); }
float Display::xOf (float hz) const { return plot.getX() + plot.getWidth() * std::log (std::max (hz, 20.0f) / 20.0f) / std::log (1000.0f); }
float Display::hzOf (float x) const { return 20.0f * std::exp (std::log (1000.0f) * std::clamp ((x - plot.getX()) / plot.getWidth(), 0.0f, 1.0f)); }
float Display::yOfDb (float db) const { const float top = plot.getY() + 24.0f; return top + (plot.getBottom() - top) * std::clamp ((-6.0f - db) / 90.0f, 0.0f, 1.0f); }
int Display::numBands() const { return std::clamp (juce::roundToInt (plainOf (par (proc, "bands"))) + 1, 1, 6); }

void Display::update()
{
    const double fs = proc.getSampleRateHz();
    for (int pass = 0; pass < 2; ++pass)
    {
        proc.copyAnalyzer (pass == 1, tmpIn.data(), kN);
        std::fill (work.begin(), work.end(), 0.0f);
        for (int i = 0; i < kN; ++i) work[(size_t) i] = tmpIn[(size_t) i] * window[(size_t) i];
        fft.performFrequencyOnlyForwardTransform (work.data());
        auto& spec = pass ? specOut : specIn;
        const float halfStep = std::pow (1000.0f, 0.5f / (float) (kCols - 1));
        for (int j = 0; j < kCols; ++j)
        {
            const float f = 20.0f * std::pow (1000.0f, (float) j / (float) (kCols - 1));
            const float b0 = f / halfStep * (float) kN / (float) fs, b1 = f * halfStep * (float) kN / (float) fs;
            float mag;
            if (b1 - b0 < 1.0f)
            {
                const float b = 0.5f * (b0 + b1); const int i0 = std::clamp ((int) b, 0, kN / 2 - 1); const float fr = b - (float) i0;
                mag = work[(size_t) i0] * (1.0f - fr) + work[(size_t) i0 + 1] * fr;
            }
            else
            {
                mag = 0.0f;
                for (int i = std::max (1, (int) b0); i <= std::min (kN / 2 - 1, (int) b1); ++i) mag = std::max (mag, work[(size_t) i]);
            }
            const float db = 20.0f * std::log10 (4.0f * mag / (float) kN + 1.0e-9f) + 3.0f * std::log2 (f / 1000.0f);
            spec[(size_t) j] = db > spec[(size_t) j] ? db : spec[(size_t) j] * 0.82f + db * 0.18f;
        }
    }
}

void Display::spectrumPath (const std::array<float, kCols>& v, Path& line, Path& fill) const
{
    for (int j = 0; j < kCols; ++j)
    {
        const float x = plot.getX() + plot.getWidth() * (float) j / (float) (kCols - 1), y = yOfDb (v[(size_t) j]);
        if (j == 0) line.startNewSubPath (x, y); else line.lineTo (x, y);
    }
    fill = line;
    fill.lineTo (plot.getRight(), plot.getBottom()); fill.lineTo (plot.getX(), plot.getBottom()); fill.closeSubPath();
}

void Display::paint (Graphics& g)
{
    card (g, getLocalBounds().toFloat());
    g.setColour (Colour (9, 8, 10));
    g.fillRoundedRectangle (plot.expanded (6.0f, 6.0f).withBottom (plot.getBottom() + 2.0f), 8.0f);

    const int N = numBands();
    bool anySolo = false;
    for (int b = 0; b < N; ++b) anySolo = anySolo || plainOf (par (proc, bid (b, "solo"))) > 0.5f;

    // grid
    for (float db = -18.0f; db >= -90.0f; db -= 12.0f)
    {
        const float y = yOfDb (db);
        g.setColour (kEdge.withAlpha (0.045f)); g.drawLine (plot.getX(), y, plot.getRight(), y);
        text (g, juce::String ((int) db), { 4.0f, y - 7.0f, 34.0f, 14.0f }, 9.5f, kDim.withAlpha (0.7f), juce::Justification::centredRight);
    }
    for (float f : { 30.0f, 40.0f, 50.0f, 60.0f, 70.0f, 80.0f, 90.0f, 100.0f, 200.0f, 300.0f, 400.0f, 500.0f, 600.0f, 700.0f, 800.0f, 900.0f, 1000.0f, 2000.0f, 3000.0f, 4000.0f, 5000.0f, 6000.0f, 7000.0f, 8000.0f, 9000.0f, 10000.0f })
    {
        const bool major = (f == 100.0f || f == 1000.0f || f == 10000.0f);
        g.setColour (kEdge.withAlpha (major ? 0.08f : 0.03f));
        g.drawLine (xOf (f), plot.getY() + 24.0f, xOf (f), plot.getBottom());
    }
    for (auto f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
        text (g, f >= 1000.0f ? juce::String ((int) (f / 1000.0f)) + "k" : juce::String ((int) f), { xOf (f) - 20.0f, plot.getBottom() + 5.0f, 40.0f, 14.0f }, 9.5f, kDim.withAlpha (0.8f));

    // spectra
    Path inLine, inFill, outLine, outFill;
    spectrumPath (specIn, inLine, inFill);
    spectrumPath (specOut, outLine, outFill);

    auto regionX = [&] (int b, float& x0, float& x1)
    {
        x0 = b == 0 ? plot.getX() : xOf (orderedXo (proc, b - 1));
        x1 = b == N - 1 ? plot.getRight() : xOf (orderedXo (proc, b));
    };
    g.saveState();
    g.reduceClipRegion (plot.toNearestInt());
    g.setColour (Colour (255, 255, 255).withAlpha (0.20f));
    g.strokePath (inLine, juce::PathStrokeType (1.0f));
    for (int b = 0; b < N; ++b)
    {
        float x0, x1; regionX (b, x0, x1);
        const bool audible = plainOf (par (proc, bid (b, "on"))) > 0.5f && (! anySolo || plainOf (par (proc, bid (b, "solo"))) > 0.5f);
        const auto col = bandColour (b);
        const float k = audible ? 1.0f : 0.3f;
        g.saveState();
        g.reduceClipRegion (Rectangle<float> (x0, plot.getY(), x1 - x0, plot.getHeight()).toNearestInt());
        g.setColour (col.withAlpha ((b == sel ? 0.12f : 0.055f) * k)); g.fillRect (plot);
        juce::ColourGradient gr (col.withAlpha (0.62f * k), 0.0f, plot.getY() + 24.0f, col.withAlpha (0.03f), 0.0f, plot.getBottom(), false);
        g.setGradientFill (gr); g.fillPath (outFill);
        glowArc (g, outLine, col.brighter (0.25f), 1.3f, k);
        g.restoreState();
    }
    g.restoreState();

    // header strips with the band buttons
    for (int b = 0; b < N; ++b)
    {
        float x0, x1; regionX (b, x0, x1);
        const auto col = bandColour (b);
        const bool on = plainOf (par (proc, bid (b, "on"))) > 0.5f, solo = plainOf (par (proc, bid (b, "solo"))) > 0.5f;
        const auto hdr = Rectangle<float> (x0 + 1.0f, plot.getY() + 1.0f, x1 - x0 - 2.0f, 21.0f);
        g.setColour (col.withAlpha (b == sel ? 0.34f : 0.16f)); g.fillRoundedRectangle (hdr, 6.0f);
        text (g, juce::String (b + 1), { hdr.getX() + 4.0f, hdr.getY(), 18.0f, hdr.getHeight() }, 12.0f, b == sel ? kText : kText.withAlpha (0.8f), juce::Justification::centred, true);
        if (hdr.getWidth() > 50.0f)
        {
            const auto pw = Rectangle<float> (hdr.getX() + 26.0f, hdr.getCentreY() - 5.0f, 10.0f, 10.0f);
            g.setColour (on ? col.brighter (0.3f) : Colour (60, 56, 60)); g.fillEllipse (pw);
            if (on) { g.setColour (col.withAlpha (0.35f)); g.fillEllipse (pw.expanded (3.0f)); }
        }
        if (hdr.getWidth() > 76.0f)
        {
            const auto sr = Rectangle<float> (hdr.getX() + 44.0f, hdr.getCentreY() - 7.0f, 16.0f, 14.0f);
            g.setColour (solo ? col.brighter (0.2f) : Colour (0, 0, 0).withAlpha (0.35f)); g.fillRoundedRectangle (sr, 4.0f);
            text (g, "S", sr, 9.5f, solo ? Colour (24, 14, 10) : kDim, juce::Justification::centred, true);
        }
    }
    // crossover handles
    for (int k = 0; k < N - 1; ++k)
    {
        const float x = xOf (orderedXo (proc, k));
        const bool hot = (k == hoverXo || k == dragXo);
        g.setColour (kEdge.withAlpha (hot ? 0.85f : 0.40f));
        g.drawLine (x, plot.getY() + 24.0f, x, plot.getBottom(), hot ? 1.6f : 1.0f);
        const auto grip = Rectangle<float> (x - 3.5f, plot.getY() + 4.0f, 7.0f, 15.0f);
        g.setColour (hot ? kText : Colour (210, 204, 198)); g.fillRoundedRectangle (grip, 3.5f);
        g.setColour (Colour (20, 18, 22)); g.drawLine (x, grip.getY() + 4.0f, x, grip.getBottom() - 4.0f, 1.0f);
        if (hot)
        {
            const float f = orderedXo (proc, k);
            text (g, f >= 1000.0f ? juce::String (f / 1000.0f, 2) + " kHz" : juce::String (juce::roundToInt (f)) + " Hz", { x - 40.0f, plot.getY() + 26.0f, 80.0f, 14.0f }, 10.5f, kText);
        }
    }
}

int Display::xoHit (float x, float y) const
{
    if (y < plot.getY() - 4.0f || y > plot.getBottom()) return -1;
    int best = -1; float bd = 8.0f;
    for (int k = 0; k < numBands() - 1; ++k)
    {
        const float d = std::fabs (x - xOf (orderedXo (proc, k)));
        if (d < bd) { bd = d; best = k; }
    }
    return best;
}

Display::Hit Display::headerHit (Point<float> p) const
{
    Hit h;
    const int N = numBands();
    for (int b = 0; b < N; ++b)
    {
        const float x0 = b == 0 ? plot.getX() : xOf (orderedXo (proc, b - 1)), x1 = b == N - 1 ? plot.getRight() : xOf (orderedXo (proc, b));
        if (p.x < x0 || p.x >= x1) continue;
        h.band = b;
        if (p.y < plot.getY() + 23.0f)
        {
            const float w = x1 - x0;
            if (w > 50.0f && std::fabs (p.x - (x0 + 31.0f)) < 8.0f) h.what = 1;
            else if (w > 76.0f && p.x > x0 + 43.0f && p.x < x0 + 62.0f) h.what = 2;
        }
        return h;
    }
    return h;
}

void Display::mouseMove (const juce::MouseEvent& e)
{
    const int k = xoHit (e.position.x, e.position.y);
    if (k != hoverXo) { hoverXo = k; setMouseCursor (k >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor); }
}

void Display::mouseDown (const juce::MouseEvent& e)
{
    dragXo = xoHit (e.position.x, e.position.y);
    if (dragXo >= 0) { if (auto* p = par (proc, "xo" + juce::String (dragXo + 1))) p->beginChangeGesture(); return; }
    const auto h = headerHit (e.position);
    if (h.band < 0) return;
    if (h.what > 0)
    {
        if (auto* p = par (proc, bid (h.band, h.what == 1 ? "on" : "solo"))) { p->beginChangeGesture(); p->setValueNotifyingHost (p->getValue() > 0.5f ? 0.0f : 1.0f); p->endChangeGesture(); }
    }
    select (h.band);
}

void Display::mouseDrag (const juce::MouseEvent& e)
{
    if (dragXo < 0) return;
    const float lo = dragXo > 0 ? orderedXo (proc, dragXo - 1) * 1.15f : 20.0f;
    const float hi = dragXo < numBands() - 2 ? orderedXo (proc, dragXo + 1) / 1.15f : 20000.0f;
    setPlain (par (proc, "xo" + juce::String (dragXo + 1)), std::clamp (hzOf (e.position.x), lo, std::max (lo, hi)));
}

void Display::mouseUp (const juce::MouseEvent&)
{
    if (dragXo >= 0) if (auto* p = par (proc, "xo" + juce::String (dragXo + 1))) p->endChangeGesture();
    dragXo = -1;
}

void Display::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int k = xoHit (e.position.x, e.position.y);
    if (k < 0) return;
    if (auto* p = par (proc, "xo" + juce::String (k + 1))) { p->beginChangeGesture(); p->setValueNotifyingHost (p->getDefaultValue()); p->endChangeGesture(); }
}

//==============================================================================
void ChoiceButton::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (Colour (10, 9, 11).brighter (hover ? 0.12f : 0.0f)); g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour (kEdge.withAlpha (hover ? 0.16f : 0.08f)); g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);
    const auto label = param ? param->getCurrentValueAsText() : juce::String();
    const bool none = label == "None";
    text (g, label, r.withTrimmedLeft (8.0f).withTrimmedRight (16.0f), 10.5f, dim ? kDim.withAlpha (0.5f) : none ? kDim : colour.brighter (0.35f), juce::Justification::centredLeft, ! none);
    Path t; const float cx = r.getRight() - 10.0f, cy = r.getCentreY();
    t.addTriangle (cx - 3.5f, cy - 2.0f, cx + 3.5f, cy - 2.0f, cx, cy + 2.5f);
    g.setColour (kDim); g.fillPath (t);
}

void ChoiceButton::mouseDown (const juce::MouseEvent&)
{
    if (! param) return;
    const int cur = juce::roundToInt (plainOf (param));
    juce::PopupMenu m;
    buildMenu (m, cur);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMinimumWidth (getWidth()), [this] (int r)
    {
        if (r <= 0 || param == nullptr) return;
        param->beginChangeGesture(); param->setValueNotifyingHost (param->convertTo0to1 ((float) (r - 1))); param->endChangeGesture();
        repaint();
    });
}

//==============================================================================
void StepEditor::paint (Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setColour (Colour (10, 9, 11)); g.fillRoundedRectangle (r, 8.0f);
    g.setColour (kEdge.withAlpha (0.07f)); g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
    const float cy = r.getCentreY(), half = r.getHeight() * 0.5f - 5.0f;
    g.setColour (kEdge.withAlpha (0.08f)); g.drawLine (r.getX() + 4.0f, cy, r.getRight() - 4.0f, cy);
    const float cw = (r.getWidth() - 8.0f) / (float) ember::kSteps;
    const int cur = std::clamp ((int) (proc.getLfoPosition (index) * (float) ember::kSteps), 0, ember::kSteps - 1);
    const Colour c = index == 0 ? Colour (255, 166, 61) : Colour (70, 194, 201);
    for (int i = 0; i < ember::kSteps; ++i)
    {
        const float v = proc.getLfoStep (index, i);
        const float x = r.getX() + 4.0f + cw * (float) i + 1.5f, w = cw - 3.0f;
        const float y0 = cy, y1 = cy - v * half;
        const auto bar = Rectangle<float> (x, std::min (y0, y1), w, std::max (1.5f, std::fabs (y1 - y0)));
        if (i == cur) { g.setColour (c.withAlpha (0.18f)); g.fillRoundedRectangle (Rectangle<float> (x - 1.0f, r.getY() + 3.0f, w + 2.0f, r.getHeight() - 6.0f), 3.0f); }
        g.setColour (i == cur ? c.brighter (0.4f) : c.withAlpha (0.75f)); g.fillRoundedRectangle (bar, 2.0f);
    }
    text (g, (proc.getSourceValue (ember::kSrcLfo1 + index) >= 0 ? "+" : "") + juce::String (proc.getSourceValue (ember::kSrcLfo1 + index), 2), { r.getRight() - 46.0f, r.getY() + 1.0f, 44.0f, 12.0f }, 9.0f, kDim, juce::Justification::centredRight);
}

void StepEditor::setAt (const juce::MouseEvent& e)
{
    const int i = std::clamp ((int) ((e.position.x - 4.0f) / ((float) getWidth() - 8.0f) * (float) ember::kSteps), 0, ember::kSteps - 1);
    const float half = (float) getHeight() * 0.5f - 5.0f;
    float v = (((float) getHeight() * 0.5f) - e.position.y) / half;
    if (std::fabs (v) < 0.06f) v = 0.0f;                    // a little magnet at zero
    proc.setLfoStep (index, i, std::clamp (v, -1.0f, 1.0f));
    repaint();
}

void StepEditor::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        juce::PopupMenu m;
        m.addSectionHeader ("Shape");
        for (int i = 0; i < ember::kNumShapes; ++i) m.addItem (i + 1, ember::shapeName (i));
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this] (int r)
        {
            if (r > 0) { proc.setLfoShape (index, r - 1, (unsigned) juce::Time::getMillisecondCounter()); repaint(); }
        });
        return;
    }
    setAt (e);
}

void StepEditor::mouseDrag (const juce::MouseEvent& e) { if (! e.mods.isPopupMenu()) setAt (e); }

void StepEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int i = std::clamp ((int) ((e.position.x - 4.0f) / ((float) getWidth() - 8.0f) * (float) ember::kSteps), 0, ember::kSteps - 1);
    proc.setLfoStep (index, i, 0.0f);
    repaint();
}

//==============================================================================
// one route: source, target, amount
class ModPanel::Route : public juce::Component
{
public:
    Route (ModPanel& owner, int r) : mp (owner), idx (r)
    {
        auto& p = owner.proc;
        const auto id = "m" + juce::String (r + 1) + "_";
        src = std::make_unique<ChoiceButton> (par (p, id + "src"), [] (juce::PopupMenu& m, int cur)
        {
            for (int i = 0; i < ember::kNumSources; ++i) m.addItem (i + 1, ember::sourceName (i), true, i == cur);
        }, kAmber);
        tgt = std::make_unique<ChoiceButton> (par (p, id + "tgt"), [] (juce::PopupMenu& m, int cur)
        {
            char b[48];
            m.addItem (1, "None", true, cur == 0);
            for (int bnd = 0; bnd < 6; ++bnd)
            {
                juce::PopupMenu sub;
                for (int j = 0; j < ember::kBandTargetParams; ++j) { const int t = ember::bandTarget (bnd, j); sub.addItem (t + 1, ember::bandParamName (j), true, t == cur); }
                m.addSubMenu ("Band " + juce::String (bnd + 1), sub, true, nullptr, cur >= ember::bandTarget (bnd, 0) && cur <= ember::bandTarget (bnd, 7));
            }
            juce::PopupMenu xo;
            for (int k = 0; k < 5; ++k) { const int t = ember::crossoverTarget (k); ember::targetName (t, b, sizeof b); xo.addItem (t + 1, b, true, t == cur); }
            m.addSubMenu ("Crossovers", xo, true, nullptr, cur >= ember::crossoverTarget (0) && cur < ember::kTargetIn);
            juce::PopupMenu gl;
            for (int t = ember::kTargetIn; t <= ember::kTargetMix; ++t) { ember::targetName (t, b, sizeof b); gl.addItem (t + 1, b, true, t == cur); }
            m.addSubMenu ("Global", gl, true, nullptr, cur >= ember::kTargetIn);
        }, kAmber);
        addAndMakeVisible (*src); addAndMakeVisible (*tgt); addAndMakeVisible (amt);
        amt.setSliderStyle (juce::Slider::LinearHorizontal);
        amt.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        amt.setLookAndFeel (&owner.laf);
        amt.setColour (juce::Slider::trackColourId, kAmber);
        amt.setDoubleClickReturnValue (true, 50.0);
        att = std::make_unique<SliderAtt> (p.apvts, id + "amt", amt);
        amt.onValueChange = [this] { repaint(); };
    }
    ~Route() override { amt.setLookAndFeel (nullptr); }

    void resized() override
    {
        src->setBounds (18, 1, 92, 21); tgt->setBounds (120, 1, 150, 21); amt.setBounds (280, 1, 100, 21);
    }
    void paint (Graphics& g) override
    {
        const bool active = mp.proc.apvts.getRawParameterValue ("m" + juce::String (idx + 1) + "_src")->load() > 0.5f && mp.proc.apvts.getRawParameterValue ("m" + juce::String (idx + 1) + "_tgt")->load() > 0.5f;
        text (g, juce::String (idx + 1), { 0.0f, 0.0f, 14.0f, 23.0f }, 10.0f, active ? kAmber : kDim.withAlpha (0.6f), juce::Justification::centred, true);
        const float v = (float) amt.getValue();
        text (g, (v > 0.5f ? "+" : "") + juce::String (juce::roundToInt (v)) + "%", { 384.0f, 0.0f, 48.0f, 23.0f }, 10.5f, active ? kText : kDim, juce::Justification::centredLeft, active);
    }
private:
    ModPanel& mp;
    int idx;
    std::unique_ptr<ChoiceButton> src, tgt;
    juce::Slider amt;
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<SliderAtt> att;
};

ModPanel::ModPanel (EmberAudioProcessor& p) : proc (p)
{
    auto setup = [this] (juce::Slider& s, const char* name, juce::Colour c, bool bip = false)
    {
        addAndMakeVisible (s);
        s.setName (name);
        s.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        s.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        s.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        s.setMouseDragSensitivity (200);
        s.setLookAndFeel (&laf);
        s.setColour (juce::Slider::rotarySliderFillColourId, c);
        s.getProperties().set ("mini", true);
        if (bip) s.getProperties().set ("bipolar", true);
    };
    auto attach = [this, &p] (const juce::String& id, juce::Slider& s)
    {
        atts.push_back (std::make_unique<SliderAtt> (p.apvts, id, s));
        s.setDoubleClickReturnValue (true, par (p, id)->convertFrom0to1 (par (p, id)->getDefaultValue()));
    };
    for (int k = 0; k < 2; ++k)
    {
        const Colour c = k == 0 ? Colour (255, 166, 61) : Colour (70, 194, 201);
        const auto id = "lfo" + juce::String (k + 1) + "_";
        steps[(size_t) k] = std::make_unique<StepEditor> (p, k);
        addAndMakeVisible (*steps[(size_t) k]);
        syncBtn[(size_t) k] = std::make_unique<ChoiceButton> (par (p, id + "sync"), [] (juce::PopupMenu& m, int cur)
        {
            for (int i = 0; i < ember::kNumSyncModes; ++i) m.addItem (i + 1, ember::syncName (i), true, i == cur);
        }, c);
        addAndMakeVisible (*syncBtn[(size_t) k]);
        setup (lfoRate[(size_t) k], "Rate", c); attach (id + "rate", lfoRate[(size_t) k]);
        setup (lfoGlide[(size_t) k], "Glide", c); attach (id + "glide", lfoGlide[(size_t) k]);
        const auto eid = "env" + juce::String (k + 1) + "_";
        static const char* en[3] = { "Attack", "Release", "Gain" }; static const char* ei[3] = { "atk", "rel", "gain" };
        for (int j = 0; j < 3; ++j) { setup (env[(size_t) k][(size_t) j], en[j], Colour (239, 93, 138), j == 2); attach (eid + ei[j], env[(size_t) k][(size_t) j]); }
        setup (sliders[(size_t) k], k == 0 ? "Slider 1" : "Slider 2", Colour (242, 212, 92)); attach ("sl" + juce::String (k + 1), sliders[(size_t) k]);
    }
    for (int r = 0; r < ember::kNumRoutes; ++r) { routes[(size_t) r] = std::make_unique<Route> (*this, r); addAndMakeVisible (*routes[(size_t) r]); }
}

ModPanel::~ModPanel()
{
    for (auto& s : lfoRate) s.setLookAndFeel (nullptr);
    for (auto& s : lfoGlide) s.setLookAndFeel (nullptr);
    for (auto& s : sliders) s.setLookAndFeel (nullptr);
    for (auto& e : env) for (auto& s : e) s.setLookAndFeel (nullptr);
}

void ModPanel::resized()
{
    for (int k = 0; k < 2; ++k)
    {
        const int y0 = 8 + k * 80;
        steps[(size_t) k]->setBounds (16, y0 + 18, 252, 58);
        syncBtn[(size_t) k]->setBounds (282, y0 + 16, 92, 20);
        lfoRate[(size_t) k].setBounds (272, y0 + 36, 58, 44);
        lfoGlide[(size_t) k].setBounds (334, y0 + 36, 58, 44);
        const int ye = 170 + k * 46;
        for (int j = 0; j < 3; ++j) env[(size_t) k][(size_t) j].setBounds (74 + j * 62, ye, 58, 44);
        sliders[(size_t) k].setBounds (500 + k * 66, 222, 58, 46);
    }
    for (int r = 0; r < ember::kNumRoutes; ++r) routes[(size_t) r]->setBounds (436, 30 + r * 23, 436, 23);
}

void ModPanel::tick()
{
    // the target the LFO 1 sync button sits next to is only meaningful while it is not synced: dim the rate knob
    for (int k = 0; k < 2; ++k)
    {
        const bool free = plainOf (par (proc, "lfo" + juce::String (k + 1) + "_sync")) < 0.5f;
        lfoRate[(size_t) k].setEnabled (free);
        lfoRate[(size_t) k].setAlpha (free ? 1.0f : 0.35f);
        syncBtn[(size_t) k]->repaint();
    }
    repaint();
}

void ModPanel::paint (Graphics& g)
{
    card (g, getLocalBounds().toFloat());
    g.setColour (kEdge.withAlpha (0.07f)); g.fillRect (424.0f, 14.0f, 1.0f, (float) getHeight() - 28.0f);
    text (g, "ROUTES", { 436.0f, 8.0f, 100.0f, 14.0f }, 9.5f, kDim, juce::Justification::centredLeft, true, 0.2f);
    text (g, "source", { 454.0f, 18.0f, 60.0f, 12.0f }, 8.5f, kDim.withAlpha (0.6f), juce::Justification::centredLeft);
    text (g, "target", { 556.0f, 18.0f, 60.0f, 12.0f }, 8.5f, kDim.withAlpha (0.6f), juce::Justification::centredLeft);
    text (g, "amount", { 716.0f, 18.0f, 60.0f, 12.0f }, 8.5f, kDim.withAlpha (0.6f), juce::Justification::centredLeft);
    for (int k = 0; k < 2; ++k)
    {
        const Colour c = k == 0 ? Colour (255, 166, 61) : Colour (70, 194, 201);
        const int y0 = 8 + k * 80;
        g.setColour (c); g.fillEllipse (16.0f, (float) y0 + 4.0f, 7.0f, 7.0f);
        text (g, "LFO " + juce::String (k + 1), { 28.0f, (float) y0 - 1.0f, 70.0f, 16.0f }, 11.0f, kText, juce::Justification::centredLeft, true, 0.1f);
        const int ye = 170 + k * 46;
        g.setColour (Colour (239, 93, 138)); g.fillEllipse (16.0f, (float) ye + 17.0f, 7.0f, 7.0f);
        text (g, "ENV " + juce::String (k + 1), { 28.0f, (float) ye + 11.0f, 50.0f, 18.0f }, 11.0f, kText, juce::Justification::centredLeft, true, 0.1f);
        // live level of the follower
        const float v = proc.getSourceValue (ember::kSrcEnv1 + k);
        const Rectangle<float> bar (276.0f, (float) ye + 16.0f, 118.0f, 8.0f);
        g.setColour (Colour (10, 9, 11)); g.fillRoundedRectangle (bar, 4.0f);
        g.setColour (Colour (239, 93, 138)); g.fillRoundedRectangle (bar.withWidth (std::max (3.0f, bar.getWidth() * v)), 4.0f);
        text (g, "input level", { 276.0f, (float) ye + 2.0f, 118.0f, 12.0f }, 8.5f, kDim.withAlpha (0.7f), juce::Justification::centredLeft);
    }
    text (g, "MANUAL", { 436.0f, 240.0f, 60.0f, 12.0f }, 9.5f, kDim, juce::Justification::centredLeft, true, 0.2f);
    text (g, "sliders are sources too", { 700.0f, 240.0f, 190.0f, 16.0f }, 10.0f, kDim.withAlpha (0.7f), juce::Justification::centredLeft);
}
} // namespace emberui

//==============================================================================
class EmberEditor::BandTitle : public juce::Component
{
public:
    BandTitle (EmberEditor& e) : ed (e) { setInterceptsMouseClicks (false, false); }
    void paint (Graphics& g) override
    {
        using namespace emberui;
        const int b = ed.sel;
        const int N = juce::roundToInt (plainOf (par (ed.proc, "bands"))) + 1;
        const float lo = b == 0 ? 20.0f : orderedXo (ed.proc, b - 1), hi = b == N - 1 ? 20000.0f : orderedXo (ed.proc, b);
        auto fmt = [] (float f) { return f >= 1000.0f ? juce::String (f / 1000.0f, f >= 10000.0f ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (f)) + " Hz"; };
        g.setColour (bandColour (b)); g.fillEllipse (2.0f, 7.0f, 9.0f, 9.0f);
        text (g, "BAND " + juce::String (b + 1), { 18.0f, 0.0f, 70.0f, 22.0f }, 12.0f, kText, juce::Justification::centredLeft, true, 0.1f);
        text (g, fmt (lo) + "  -  " + fmt (hi), { 88.0f, 0.0f, 200.0f, 22.0f }, 11.5f, kDim, juce::Justification::centredLeft);
    }
private:
    EmberEditor& ed;
};

namespace
{
juce::Path flame()
{
    juce::Path p;
    p.startNewSubPath (12.0f, 1.0f);
    p.cubicTo (13.5f, 6.0f, 20.0f, 9.0f, 20.0f, 16.0f);
    p.cubicTo (20.0f, 21.0f, 16.5f, 24.5f, 12.0f, 24.5f);
    p.cubicTo (7.5f, 24.5f, 4.0f, 21.0f, 4.0f, 16.0f);
    p.cubicTo (4.0f, 12.5f, 6.0f, 10.5f, 7.5f, 8.5f);
    p.cubicTo (8.0f, 11.0f, 9.0f, 12.0f, 10.0f, 12.5f);
    p.cubicTo (10.0f, 8.0f, 10.5f, 4.5f, 12.0f, 1.0f);
    p.closeSubPath();
    return p;
}
}

EmberEditor::EmberEditor (EmberAudioProcessor& p)
    : juce::AudioProcessorEditor (&p), proc (p),
      display (p, [this] (int b) { selectBand (b); }), stylePanel (p)
{
    using namespace emberui;
    title = std::make_unique<BandTitle> (*this);
    bandsSeg = std::make_unique<Segmented> (par (p, "bands"), juce::StringArray { "1", "2", "3", "4", "5", "6" });
    osSeg = std::make_unique<Segmented> (par (p, "os"), juce::StringArray { "Off", "2x", "4x", "8x", "16x", "32x" });
    slopeSeg = std::make_unique<Segmented> (par (p, "slope"), juce::StringArray { "6", "12", "24", "36", "48" });
    modPanel = std::make_unique<ModPanel> (p);

    addAndMakeVisible (display); addAndMakeVisible (*modPanel); addAndMakeVisible (stylePanel); addAndMakeVisible (*title);
    addAndMakeVisible (*bandsSeg); addAndMakeVisible (*osSeg); addAndMakeVisible (*slopeSeg);
    addAndMakeVisible (bandOn); addAndMakeVisible (bandSolo); addAndMakeVisible (msBtn); addAndMakeVisible (limitBtn); addAndMakeVisible (modBtn);
    msBtn.colour = kAmber; limitBtn.colour = kAmber; modBtn.colour = Colour (239, 93, 138);
    modPanel->setVisible (false);
    modBtn.onClick = [this] { showModulation (modBtn.getToggleState()); };
    msAtt = std::make_unique<ButtonAtt> (p.apvts, "ms", msBtn);
    limitAtt = std::make_unique<ButtonAtt> (p.apvts, "limit", limitBtn);

    static const char* names[kNumBandKnobs] = { "Drive", "Level", "Mix", "Dynamics", "Tone", "Feedback", "Fb Freq", "Pan" };
    for (int i = 0; i < kNumBandKnobs; ++i)
    {
        auto& s = knobs[(size_t) i];
        addAndMakeVisible (s);
        s.setName (names[i]);
        s.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        s.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        s.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        s.setMouseDragSensitivity (220);
        s.setLookAndFeel (&laf);
        if (i == kLevel || i == kDyn || i == kTone || i == kPan) s.getProperties().set ("bipolar", true);
    }
    static const char* gnames[3] = { "Input", "Output", "Mix" };
    static const char* gids[3] = { "in", "out", "mix" };
    for (int i = 0; i < 3; ++i)
    {
        auto& s = gKnobs[(size_t) i];
        addAndMakeVisible (s);
        s.setName (gnames[i]);
        s.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        s.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        s.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        s.setMouseDragSensitivity (220);
        s.setLookAndFeel (&laf);
        s.setColour (juce::Slider::rotarySliderFillColourId, kAmber);
        if (i < 2) s.getProperties().set ("bipolar", true);
        gAtt[(size_t) i] = std::make_unique<SliderAtt> (p.apvts, gids[i], s);
        s.setDoubleClickReturnValue (true, par (p, gids[i])->convertFrom0to1 (par (p, gids[i])->getDefaultValue()));
    }

    selectBand (0);
    setSize (kW, kH);
    setResizable (false, false);
    startTimerHz (30);
}

EmberEditor::~EmberEditor()
{
    stopTimer();
    for (auto& k : knobs) k.setLookAndFeel (nullptr);
    for (auto& k : gKnobs) k.setLookAndFeel (nullptr);
}

void EmberEditor::selectBand (int b)
{
    using namespace emberui;
    const int N = juce::roundToInt (plainOf (par (proc, "bands"))) + 1;
    sel = std::clamp (b, 0, N - 1);
    display.setSelected (sel);
    stylePanel.setBand (sel);
    static const char* ids[kNumBandKnobs] = { "drive", "level", "mix", "dyn", "tone", "fb", "fbf", "pan" };
    for (int i = 0; i < kNumBandKnobs; ++i)
    {
        knobAtt[(size_t) i].reset();
        const auto id = bid (sel, ids[i]);
        knobAtt[(size_t) i] = std::make_unique<SliderAtt> (proc.apvts, id, knobs[(size_t) i]);
        knobs[(size_t) i].setDoubleClickReturnValue (true, par (proc, id)->convertFrom0to1 (par (proc, id)->getDefaultValue()));
        knobs[(size_t) i].setColour (juce::Slider::rotarySliderFillColourId, bandColour (sel));
    }
    onAtt.reset(); soloAtt.reset();
    onAtt = std::make_unique<ButtonAtt> (proc.apvts, bid (sel, "on"), bandOn);
    soloAtt = std::make_unique<ButtonAtt> (proc.apvts, bid (sel, "solo"), bandSolo);
    bandOn.colour = bandColour (sel); bandSolo.colour = bandColour (sel);
    repaint();
}

void EmberEditor::showModulation (bool on)
{
    display.setVisible (! on);
    modPanel->setVisible (on);
    modBtn.setToggleState (on, juce::dontSendNotification);
}

void EmberEditor::tick()
{
    // the number of bands can change from the host/automation: keep the selection valid
    const int N = juce::roundToInt (emberui::plainOf (emberui::par (proc, "bands"))) + 1;
    if (sel >= N) selectBand (N - 1);
    if (display.isVisible()) { display.update(); display.repaint(); }
    if (modPanel->isVisible()) modPanel->tick();
    stylePanel.repaint();
    title->repaint();
    bandsSeg->repaint(); osSeg->repaint(); slopeSeg->repaint();
    // knobs show where the modulation moves them
    for (int i = 0; i < kNumBandKnobs; ++i)
    {
        const float m = proc.getModulatedNormalised (ember::bandTarget (sel, i));
        knobs[(size_t) i].getProperties().set ("modPos", (double) m);
        knobs[(size_t) i].repaint();
    }
    static const int gt[3] = { ember::kTargetIn, ember::kTargetOut, ember::kTargetMix };
    for (int i = 0; i < 3; ++i) { gKnobs[(size_t) i].getProperties().set ("modPos", (double) proc.getModulatedNormalised (gt[i])); gKnobs[(size_t) i].repaint(); }
}

void EmberEditor::resized()
{
    display.setBounds (20, 56, 880, 270);
    modPanel->setBounds (20, 56, 880, 270);
    stylePanel.setBounds (20, 340, 210, 248);
    title->setBounds (256, 350, 300, 22);
    bandOn.setBounds (574, 351, 56, 22); bandSolo.setBounds (636, 351, 64, 22);
    bandsSeg->setBounds (714, 14, 186, 26);
    slopeSeg->setBounds (470, 14, 150, 26);
    modBtn.setBounds (336, 16, 56, 22);
    osSeg->setBounds (730, 556, 160, 24);
    msBtn.setBounds (822, 466, 66, 22); limitBtn.setBounds (822, 494, 66, 22);

    for (int i = 0; i < kNumBandKnobs; ++i)
    {
        const float cell = 470.0f / 4.0f;
        const int row = i / 4, col = i % 4;
        knobs[(size_t) i].setBounds ((int) (240.0f + cell * ((float) col + 0.5f) - 48.0f), 380 + row * 102, 96, 94);
    }
    gKnobs[0].setBounds (720 + 45 - 48, 372, 96, 90);
    gKnobs[1].setBounds (720 + 135 - 48, 372, 96, 90);
    gKnobs[2].setBounds (720 + 45 - 48, 454, 96, 90);
}

void EmberEditor::paint (Graphics& g)
{
    using namespace emberui;
    juce::ColourGradient bg (Colour (20, 17, 20), 460.0f, 0.0f, kBg, 460.0f, (float) kH, false);
    g.setGradientFill (bg); g.fillAll();
    juce::ColourGradient glow (kRed.withAlpha (0.10f), 80.0f, -20.0f, kRed.withAlpha (0.0f), 480.0f, 160.0f, true);
    g.setGradientFill (glow); g.fillRect (0, 0, kW, kH);

    // header
    {
        g.saveState();
        g.addTransform (juce::AffineTransform::translation (24.0f, 12.0f));
        juce::ColourGradient fg (kAmber, 12.0f, 2.0f, kRed, 12.0f, 25.0f, false);
        g.setGradientFill (fg); g.fillPath (flame());
        Path inner; inner.addEllipse (9.0f, 14.0f, 6.0f, 8.0f);
        g.setColour (Colour (255, 226, 170).withAlpha (0.9f)); g.fillPath (inner);
        g.restoreState();
        text (g, "Ember", { 56.0f, 10.0f, 150.0f, 30.0f }, 24.0f, kText, juce::Justification::centredLeft, true, 0.02f);
        text (g, "multiband saturator", { 150.0f, 14.0f, 180.0f, 26.0f }, 11.0f, kDim, juce::Justification::centredLeft, false, 0.1f);
        text (g, "BANDS", { 646.0f, 14.0f, 62.0f, 26.0f }, 10.0f, kDim, juce::Justification::centredRight, true, 0.2f);
        text (g, "SLOPE", { 398.0f, 14.0f, 66.0f, 26.0f }, 10.0f, kDim, juce::Justification::centredRight, true, 0.2f);
        text (g, "dB/oct", { 470.0f, 38.0f, 150.0f, 12.0f }, 8.5f, kDim.withAlpha (0.6f), juce::Justification::centred);
    }
    card (g, Rectangle<float> (20.0f, 340.0f, 210.0f, 248.0f));
    card (g, Rectangle<float> (240.0f, 340.0f, 470.0f, 248.0f));
    card (g, Rectangle<float> (720.0f, 340.0f, 180.0f, 248.0f));
    text (g, "GLOBAL", { 736.0f, 352.0f, 100.0f, 14.0f }, 10.0f, kDim, juce::Justification::centredLeft, true, 0.2f);
    text (g, "OVERSAMPLING", { 736.0f, 540.0f, 140.0f, 14.0f }, 9.5f, kDim, juce::Justification::centredLeft, true, 0.15f);
    const int lat = proc.getOversampleLatency();
    if (lat > 0) text (g, juce::String (lat) + " smp", { 800.0f, 540.0f, 90.0f, 14.0f }, 9.5f, kDim.withAlpha (0.7f), juce::Justification::centredRight);
}
