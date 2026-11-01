#include "PluginEditor.h"

using juce::Colour;
using juce::Graphics;
using juce::Path;
using juce::Point;
using juce::Rectangle;

namespace polyui
{
namespace
{
juce::Font makeFont (float size, bool bold = false, float kerning = 0.0f)
{
    juce::Font f { juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain) };
    return kerning != 0.0f ? f.withExtraKerningFactor (kerning) : f;
}

void text (Graphics& g, const juce::String& t, float cx, float cy, float size, Colour c, float kerning = 0.0f, bool bold = false)
{
    g.setColour (c);
    g.setFont (makeFont (size, bold, kerning));
    g.drawText (t, Rectangle<float> (cx - 320.0f, cy - size, 640.0f, size * 2.0f), juce::Justification::centred, false);
}

void textL (Graphics& g, const juce::String& t, float x, float cy, float size, Colour c, float kerning = 0.0f, bool bold = false)
{
    g.setColour (c);
    g.setFont (makeFont (size, bold, kerning));
    g.drawText (t, Rectangle<float> (x, cy - size, 640.0f, size * 2.0f), juce::Justification::centredLeft, false);
}

float easeOutBack (float x)
{
    x = std::clamp (x, 0.0f, 1.0f);
    const float c1 = 1.70158f, c3 = c1 + 1.0f;
    return 1.0f + c3 * std::pow (x - 1.0f, 3.0f) + c1 * std::pow (x - 1.0f, 2.0f);
}

// variations come in shades of red / bordeaux (the stored "hue" only picks the shade)
Colour orbColour (float hue, float alpha = 1.0f)
{
    const Colour bordeaux (128, 8, 34), crimson (232, 22, 52), bright (255, 84, 92);
    const float t = std::clamp (hue, 0.0f, 1.0f);
    const Colour c = t < 0.5f ? bordeaux.interpolatedWith (crimson, t * 2.0f) : crimson.interpolatedWith (bright, (t - 0.5f) * 2.0f);
    return c.withAlpha (alpha);
}

const Colour kCloud { 54, 255, 120 };      // neon green: cloud, potion, frame
const Colour kGreen { 40, 235, 92 };
const Colour kLime  { 206, 244, 40 };      // flask outline, Transmute
const Colour kBlue  { 70, 110, 255 };      // grab box

// soft glow = a few strokes of decreasing width / increasing opacity
void glowStroke (Graphics& g, const Path& p, Colour c, float width, float alpha)
{
    const float w[4] = { width * 6.0f, width * 3.4f, width * 1.9f, width };
    const float a[4] = { 0.05f, 0.09f, 0.18f, 1.0f };
    for (int i = 0; i < 4; ++i)
    {
        g.setColour (c.withMultipliedAlpha (a[i] * alpha));
        g.strokePath (p, juce::PathStrokeType (w[i], juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

Point<float> bezier (Point<float> p0, Point<float> c1, Point<float> c2, Point<float> p3, float t)
{
    const float u = 1.0f - t;
    return p0 * (u * u * u) + c1 * (3.0f * u * u * t) + c2 * (3.0f * u * t * t) + p3 * (t * t * t);
}

const Point<float> kNeck { 288.0f, 347.0f };
const Point<float> kCloudCentre { 291.0f, 218.0f };
const Rectangle<float> kDropZone { 46.0f, 545.0f, 163.0f, 69.0f };
const Rectangle<float> kGrab { 46.0f, 440.0f, 163.0f, 48.0f };
const Rectangle<float> kFrame { 103.0f, 38.0f, 371.0f, 396.0f };
const juce::AffineTransform kVialMove = juce::AffineTransform::translation (-512.0f, -404.0f).scaled (0.85f).translated (288.0f, 418.0f);   // the vial is drawn in local coords (centre x 512)
} // namespace

//==============================================================================
// Look and feel
//==============================================================================
void LookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s)
{
    const float size = (float) std::min (w, h);
    const Point<float> c ((float) x + (float) w * 0.5f, (float) y + (float) h * 0.5f);
    const float u = size / 80.0f, R = 31.0f * u;
    const float ang = a0 + pos * (a1 - a0);
    const bool hot = s.isMouseOverOrDragging();

    Path track;
    track.addCentredArc (c.x, c.y, R + 5.5f * u, R + 5.5f * u, 0.0f, a0, a1, true);
    g.setColour (Colour (66, 12, 18));
    g.strokePath (track, juce::PathStrokeType (3.0f * u, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    Path val;
    val.addCentredArc (c.x, c.y, R + 5.5f * u, R + 5.5f * u, 0.0f, a0, std::max (a0 + 0.001f, ang), true);
    glowStroke (g, val, Colour (236, 54, 66), 3.0f * u, hot ? 1.0f : 0.85f);

    juce::ColourGradient body (Colour (176, 30, 44), c.x - R * 0.35f, c.y - R * 0.55f, Colour (52, 4, 11), c.x + R * 0.6f, c.y + R * 0.8f, true);
    g.setGradientFill (body);
    g.fillEllipse (c.x - R, c.y - R, 2.0f * R, 2.0f * R);
    g.setColour (Colour (20, 2, 6).withAlpha (0.9f));
    g.drawEllipse (c.x - R, c.y - R, 2.0f * R, 2.0f * R, 1.6f * u);
    g.setColour (juce::Colours::white.withAlpha (0.14f));
    g.fillEllipse (c.x - R * 0.62f, c.y - R * 0.9f, R * 1.1f, R * 0.5f);

    const Point<float> dir (std::sin (ang), -std::cos (ang));
    juce::Line<float> ptr (c + dir * (R * 0.22f), c + dir * (R * 0.84f));
    g.setColour (Colour (255, 218, 218));
    g.drawLine (ptr, 2.6f * u);
}

void LookAndFeel::drawToggleButton (Graphics& g, juce::ToggleButton& b, bool hot, bool)
{
    const float h = (float) b.getHeight(), u = h / 34.0f;
    const bool on = b.getToggleState();
    const Point<float> c (14.0f * u, h * 0.5f);
    if (on)
    {
        juce::ColourGradient glow (Colour (255, 120, 80).withAlpha (0.5f), c.x, c.y, Colour (255, 120, 80).withAlpha (0.0f), c.x + 13.0f * u, c.y, true);
        g.setGradientFill (glow);
        g.fillEllipse (c.x - 13.0f * u, c.y - 13.0f * u, 26.0f * u, 26.0f * u);
    }
    g.setColour (on ? Colour (255, 140, 96) : Colour (62, 44, 46));
    g.fillEllipse (c.x - 5.0f * u, c.y - 5.0f * u, 10.0f * u, 10.0f * u);
    g.setColour (Colour (150, 150, 160).withAlpha (hot ? 0.9f : 0.5f));
    g.drawEllipse (c.x - 6.5f * u, c.y - 6.5f * u, 13.0f * u, 13.0f * u, 1.2f * u);
    g.setColour (Colour (200, 204, 212).withAlpha (on ? 0.95f : (hot ? 0.75f : 0.55f)));
    g.setFont (makeFont (11.5f * u, false, 0.1f));
    g.drawText (b.getButtonText(), Rectangle<float> (28.0f * u, 0.0f, (float) b.getWidth() - 28.0f * u, h), juce::Justification::centredLeft, false);
}

//==============================================================================
// Backdrop
//==============================================================================
Backdrop::Backdrop()
{
    setInterceptsMouseClicks (false, false);
    setBufferedToImage (true);
    grain = juce::Image (juce::Image::ARGB, 128, 128, true);
    juce::Random r (7);
    for (int y = 0; y < 128; ++y)
        for (int x = 0; x < 128; ++x)
            grain.setPixelAt (x, y, juce::Colours::white.withAlpha (r.nextFloat() * 0.045f));
}

void Backdrop::paint (Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const float K = b.getWidth() / kW;
    g.fillAll (Colour (0, 0, 0));
    g.addTransform (juce::AffineTransform::scale (K));

    // neon title, top left
    for (float r : { 5.0f, 3.2f, 1.8f })
        for (int i = 0; i < 8; ++i)
        {
            const float a = (float) i * juce::MathConstants<float>::pi * 0.25f;
            textL (g, "Polymer", 22.0f + std::cos (a) * r, 18.0f + std::sin (a) * r, 21.0f, kGreen.withAlpha (0.05f), 0.08f);
        }
    textL (g, "Polymer", 22.0f, 18.0f, 21.0f, Colour (150, 255, 180), 0.08f);

    text (g, "drop sample", 127.5f, 530.0f, 13.0f, Colour (240, 244, 250), 0.06f);
    text (g, "transmute", 279.0f, 634.0f, 13.0f, kLime, 0.06f);
    text (g, "chaos", 397.0f, 634.0f, 13.0f, Colour (236, 54, 66), 0.06f);
}

//==============================================================================
// Transmute button
//==============================================================================
void TransmuteButton::paintButton (Graphics& g, bool over, bool down)
{
    g.addTransform (juce::AffineTransform::scale ((float) getWidth() / 70.0f));
    const Point<float> c (35.0f, 35.0f);
    const float R = down ? 29.0f : 31.0f;
    const float a = ready ? 1.0f : 0.45f;

    if (over || busy)
    {
        juce::ColourGradient gl (kLime.withAlpha (0.28f * a), c.x, c.y, kCloud.withAlpha (0.0f), c.x + 35.0f, c.y, true);
        g.setGradientFill (gl);
        g.fillEllipse (0.0f, 0.0f, 70.0f, 70.0f);
    }
    juce::ColourGradient body (Colour (22, 26, 4).withAlpha (a), c.x, c.y - 14.0f, Colour (4, 6, 0).withAlpha (a), c.x, c.y + 32.0f, false);
    g.setGradientFill (body);
    g.fillEllipse (c.x - R, c.y - R, 2.0f * R, 2.0f * R);
    g.setColour ((over || down ? kLime.brighter (0.4f) : kLime.darker (0.15f)).withMultipliedAlpha (a));
    g.drawEllipse (c.x - R, c.y - R, 2.0f * R, 2.0f * R, 1.7f);

    // icon: little triangle (vial) with a dot above it
    Path tri;
    tri.addTriangle (35.0f, 28.0f, 24.5f, 47.0f, 45.5f, 47.0f);
    g.setColour (kLime.withMultipliedAlpha (a));
    g.strokePath (tri, juce::PathStrokeType (1.7f, juce::PathStrokeType::curved));
    g.fillEllipse (32.4f, 18.4f, 5.2f, 5.2f);
    glowStroke (g, [&] { Path r; r.addEllipse (c.x - R, c.y - R, 2.0f * R, 2.0f * R); return r; }(), kLime, 1.2f, (over ? 0.9f : 0.55f) * a);

    if (busy)
    {
        Path arc;
        arc.addCentredArc (c.x, c.y, R + 3.0f, R + 3.0f, 0.0f, spin, spin + 1.9f, true);
        glowStroke (g, arc, kLime, 1.7f, 1.0f);
    }
}

//==============================================================================
// Scene
//==============================================================================
Scene::Scene (PolymerAudioProcessor& p) : proc (p)
{
    for (int i = 0; i < kBlobN; ++i) blobR[i] = 40.0f;
}

Path Scene::vialPath() const
{
    Path p;
    p.startNewSubPath (503.0f, 320.0f);
    p.lineTo (503.0f, 348.0f);
    p.lineTo (461.0f, 392.0f);
    p.lineTo (461.0f, 398.0f);
    p.lineTo (467.0f, 404.0f);
    p.lineTo (557.0f, 404.0f);
    p.lineTo (563.0f, 398.0f);
    p.lineTo (563.0f, 392.0f);
    p.lineTo (521.0f, 348.0f);
    p.lineTo (521.0f, 320.0f);
    p.closeSubPath();
    return p.createPathWithRoundedCorners (6.0f);
}

bool Scene::onVial (Point<float> p) const { return Rectangle<float> (232.0f, 338.0f, 112.0f, 90.0f).contains (p); }
bool Scene::onGrab (Point<float> p) const { return kGrab.contains (p); }
bool Scene::onDropZone (Point<float> p) const { return kDropZone.contains (p); }

int Scene::orbAt (Point<float> p) const
{
    int best = -1; float bestD = 1e9f;
    for (const auto& o : orbs)
    {
        if (o.dying || o.born < 0.25f) continue;
        const float d = p.getDistanceFrom ({ o.x, o.y });
        if (d < o.radius() + 8.0f && d < bestD) { bestD = d; best = o.id; }
    }
    return best;
}

void Scene::burst (Point<float> p, Colour c, int n, float speed)
{
    for (int i = 0; i < n; ++i)
    {
        const float a = rnd.nextFloat() * juce::MathConstants<float>::twoPi, s = speed * (0.3f + rnd.nextFloat());
        const float life = 0.4f + rnd.nextFloat() * 0.5f;
        sparks.push_back ({ p, { std::cos (a) * s, std::sin (a) * s }, life, life, 1.2f + rnd.nextFloat() * 1.8f, c });
    }
}

//------------------------------------------------------------------------------
void Scene::loadFile (const juce::File& f)
{
    if (proc.loadSampleFile (f)) { fileName = f.getFileNameWithoutExtension(); errorTimer = 0.0f; }
    else                         { errorTimer = 3.5f; }
}

void Scene::browse()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose a sample", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
    juce::Component::SafePointer<Scene> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (safe != nullptr && f.existsAsFile()) safe->loadFile (f);
                          });
}

void Scene::mouseDown (const juce::MouseEvent& e)
{
    const Point<float> p = e.position / K();
    const int id = orbAt (p);
    if (id > 0)
    {
        proc.selectOrb (id, true, false);     // listen only; new variations come from Transmute
        for (auto& o : orbs) if (o.id == id) o.pulse = 1.0f;
    }
    else if (onVial (p))
    {
        if (hasSource) { proc.selectOrb (0, true, false); vialPulse = 1.0f; }
        else browse();
    }
    else if (onDropZone (p))
        browse();
    else if (onGrab (p) && hasSource)
        grabArmed = true;
}

void Scene::mouseDrag (const juce::MouseEvent& e)
{
    if (! grabArmed || e.getDistanceFromDragStart() < 6) return;
    grabArmed = false;
    const auto file = proc.exportSound (selectedId, fileName);
    if (file.existsAsFile())
        juce::DragAndDropContainer::performExternalDragDropOfFiles (juce::StringArray (file.getFullPathName()), false, this);
}

void Scene::mouseMove (const juce::MouseEvent& e)
{
    const Point<float> p = e.position / K();
    hoverId = orbAt (p);
    hoverDrop = onDropZone (p);
    hoverGrab = hasSource && onGrab (p);
    const bool clickable = hoverId > 0 || onVial (p) || hoverDrop;
    setMouseCursor (hoverGrab ? juce::MouseCursor::DraggingHandCursor : clickable ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

//------------------------------------------------------------------------------
void Scene::startDrop()
{
    flyers.clear();
    for (int i = 0; i < 44; ++i)
    {
        Fly f;
        f.delay = (float) i * 0.02f + rnd.nextFloat() * 0.05f;
        f.t = 0.0f;
        f.dur = 0.8f + rnd.nextFloat() * 0.3f;
        f.size = 1.6f + rnd.nextFloat() * 1.8f;
        f.p0 = { 56.0f + rnd.nextFloat() * 145.0f, 560.0f + rnd.nextFloat() * 40.0f };
        f.c1 = { 215.0f + rnd.nextFloat() * 30.0f, 600.0f - rnd.nextFloat() * 30.0f };
        f.c2 = { 281.0f + rnd.nextFloat() * 14.0f, 270.0f + rnd.nextFloat() * 40.0f };
        f.p3 = { 285.0f + rnd.nextFloat() * 6.0f, 358.0f };
        flyers.push_back (f);
    }
    liquid = 0.0f;
    liquidTarget = 0.0f;
    dropGlow = 1.0f;
    noteTimer = proc.sourceWasTrimmed() ? 5.0f : 0.0f;
}

void Scene::spawnOrb (const OrbView& v, bool instant)
{
    Orb o;
    o.id = v.id; o.parent = v.parent; o.gen = v.generation; o.hue = v.hue; o.seconds = v.seconds;
    o.phase = rnd.nextFloat() * 6.28f;
    if (instant)
    {
        o.x = kCloudCentre.x + (rnd.nextFloat() - 0.5f) * 220.0f;
        o.y = kCloudCentre.y + (rnd.nextFloat() - 0.5f) * 120.0f;
        o.born = 1.0f;
    }
    else
    {
        o.x = kNeck.x + (rnd.nextFloat() - 0.5f) * 8.0f;
        o.y = kNeck.y;
        o.vx = (rnd.nextFloat() - 0.5f) * 80.0f;
        o.vy = -200.0f - rnd.nextFloat() * 60.0f;
        burst (kNeck, orbColour (o.hue), 9, 70.0f);
        vialPulse = std::max (vialPulse, 0.6f);
    }
    orbs.push_back (o);
}

void Scene::sync()
{
    const int v = proc.getSampleVersion();
    hasSource = proc.hasSource();
    if (v != lastVersion)
    {
        const bool instant = firstSync && hasSource;
        lastVersion = v;
        orbs.clear(); known.clear(); queue.clear(); peakCache.clear();
        cloudAlpha = instant ? cloudAlpha : 0.0f;
        blobInit = false;
        if (hasSource)
        {
            if (instant) { liquid = liquidTarget = 0.62f; flyers.clear(); }
            else startDrop();
        }
        else { liquid = liquidTarget = 0.0f; }
    }
    const bool wasFirst = firstSync;
    firstSync = false;

    selectedId = proc.getSelected();
    pending = proc.getPending();

    const auto views = proc.getOrbs();
    std::set<int> alive;
    for (const auto& vw : views) alive.insert (vw.id);
    for (auto& o : orbs) if (! alive.count (o.id)) o.dying = true;
    for (const auto& vw : views)
        if (! known.count (vw.id))
        {
            known.insert (vw.id);
            if (wasFirst) spawnOrb (vw, true);
            else queue.push_back (vw);
        }
    if (wasFirst && ! orbs.empty())
        for (int i = 0; i < 240; ++i) step (1.0f / 60.0f);

    const uint32_t tc = proc.getTriggerCount();
    if (tc != lastTrigger)
    {
        lastTrigger = tc;
        const int id = proc.getLastTriggerId();
        if (id == 0) vialPulse = 1.0f;
        for (auto& o : orbs) if (o.id == id) o.pulse = 1.0f;
    }
}

//------------------------------------------------------------------------------
void Scene::step (float dt)
{
    time += dt;

    // --- flyers (the sample being poured into the vial)
    for (auto& f : flyers)
    {
        if (f.landed) continue;
        if (f.delay > 0.0f) { f.delay -= dt; continue; }
        f.t += dt / f.dur;
        if (f.t >= 1.0f)
        {
            f.landed = true;
            liquidTarget = std::min (0.62f, liquidTarget + 0.62f / 44.0f);
            sloshV += 14.0f * (rnd.nextFloat() - 0.4f);
            burst ({ f.p3.x, 383.0f }, Colour (150, 255, 180), 2, 40.0f);
        }
    }
    if (! flyers.empty() && std::all_of (flyers.begin(), flyers.end(), [] (const Fly& f) { return f.landed; }) && liquid > liquidTarget - 0.01f)
        flyers.clear();

    liquid += (liquidTarget - liquid) * std::min (1.0f, dt * 5.0f);
    sloshV += (-70.0f * slosh - 3.5f * sloshV) * dt;
    slosh += sloshV * dt;

    const float brewTarget = isBrewing() ? 1.0f : 0.0f;
    brew += (brewTarget - brew) * std::min (1.0f, dt * 4.0f);

    // --- bubbles inside the liquid
    if (hasSource && liquid > 0.1f)
    {
        const float rate = 2.5f + 45.0f * brew;
        if (rnd.nextFloat() < rate * dt)
            bubbles.push_back ({ 482.0f + rnd.nextFloat() * 60.0f, 400.0f, -(18.0f + rnd.nextFloat() * 26.0f) * (1.0f + brew), 1.2f + rnd.nextFloat() * 2.2f, rnd.nextFloat() * 6.28f });
    }
    const float surfaceY = 404.0f - liquid * 66.0f;
    for (auto& b : bubbles) { b.y += b.vy * dt; b.x += std::sin (time * 3.0f + b.wob) * 6.0f * dt; }
    bubbles.erase (std::remove_if (bubbles.begin(), bubbles.end(), [&] (const Bubble& b) { return b.y < surfaceY + 2.0f; }), bubbles.end());

    // --- sparks
    for (auto& s : sparks) { s.p += s.v * dt; s.v *= std::exp (-2.2f * dt); s.life -= dt; }
    sparks.erase (std::remove_if (sparks.begin(), sparks.end(), [] (const Spark& s) { return s.life <= 0.0f; }), sparks.end());

    // --- reveal queue: orbs leave the vial one after another
    if (! queue.empty())
    {
        revealTimer -= dt;
        if (revealTimer <= 0.0f)
        {
            spawnOrb (queue.front(), false);
            queue.erase (queue.begin());
            revealTimer = 0.14f;
        }
    }

    // --- orb physics
    const Point<float> C = kCloudCentre;
    const float rx = 136.0f, ry = 92.0f;
    const Point<float> anchor0 { 290.0f, 272.0f };
    for (auto& o : orbs)
    {
        o.born = std::min(1.0f, o.born + dt * 1.8f);
        o.pulse = std::max (0.0f, o.pulse - dt * 2.6f);
        if (o.dying) { o.alpha -= dt * 2.5f; continue; }

        Point<float> a ((C.x - o.x) * 2.0f, (C.y - o.y) * 2.0f);
        Point<float> pa = anchor0; float L = 0.0f, k = 1.6f;
        if (o.parent > 0)
            for (const auto& q : orbs) if (q.id == o.parent && ! q.dying) { pa = { q.x, q.y }; L = 46.0f; k = 12.0f; break; }
        {
            Point<float> d (pa.x - o.x, pa.y - o.y);
            const float dist = std::max (0.01f, std::sqrt (d.x * d.x + d.y * d.y));
            const float f = k * (dist - L);
            a += d * (f / dist);
        }
        for (const auto& q : orbs)
        {
            if (&q == &o || q.dying) continue;
            Point<float> d (o.x - q.x, o.y - q.y);
            const float dist = std::max (0.01f, std::sqrt (d.x * d.x + d.y * d.y));
            const float minD = o.radius() + q.radius() + 12.0f;
            if (dist < minD) a += d * (900.0f * (minD - dist) / minD / dist);
        }
        const float qx = (o.x - C.x) / rx, qy = (o.y - C.y) / ry;
        const float qq = std::sqrt (qx * qx + qy * qy);
        if (qq > 1.0f) a += Point<float> (C.x - o.x, C.y - o.y) * (28.0f * (qq - 1.0f));
        a.x += 14.0f * std::sin (time * 0.6f + o.phase);
        a.y += 14.0f * std::cos (time * 0.5f + o.phase * 1.3f);

        o.vx += a.x * dt; o.vy += a.y * dt;
        const float damp = std::exp (-2.3f * dt);
        o.vx *= damp; o.vy *= damp;
        o.x += o.vx * dt; o.y += o.vy * dt;
    }
    orbs.erase (std::remove_if (orbs.begin(), orbs.end(), [] (const Orb& o) { return o.dying && o.alpha <= 0.0f; }), orbs.end());

    // --- misc timers
    kickTimer = std::max (0.0f, kickTimer - dt);
    vialPulse = std::max (0.0f, vialPulse - dt * 2.0f);
    dropGlow = std::max (0.0f, dropGlow - dt * 0.7f);
    errorTimer = std::max (0.0f, errorTimer - dt);
    noteTimer = std::max (0.0f, noteTimer - dt);
    updateBlob (dt);
}

void Scene::updateBlob (float dt)
{
    int n = 0; Point<float> c;
    for (const auto& o : orbs) if (! o.dying && o.born > 0.3f) { c += Point<float> (o.x, o.y); ++n; }
    const float target = n > 0 ? 1.0f : 0.0f;
    cloudAlpha += (target - cloudAlpha) * std::min (1.0f, dt * (n > 0 ? 3.0f : 2.0f));
    if (n == 0) return;
    c = c / (float) n;
    blobC = blobInit ? blobC + (c - blobC) * std::min (1.0f, dt * 6.0f) : c;

    float tgt[kBlobN];
    for (int i = 0; i < kBlobN; ++i)
    {
        const float th = (float) i * juce::MathConstants<float>::twoPi / (float) kBlobN;
        const Point<float> u (std::cos (th), std::sin (th));
        float h = 0.0f;
        for (const auto& o : orbs)
            if (! o.dying && o.born > 0.3f)
                h = std::max (h, (o.x - blobC.x) * u.x + (o.y - blobC.y) * u.y + o.radius());
        tgt[i] = h + 20.0f;
    }
    for (int pass = 0; pass < 3; ++pass)
    {
        float tmp[kBlobN];
        for (int i = 0; i < kBlobN; ++i)
        {
            float s = 0.0f;
            for (int d = -2; d <= 2; ++d) s += tgt[(i + d + kBlobN) % kBlobN];
            tmp[i] = s / 5.0f;
        }
        std::copy (tmp, tmp + kBlobN, tgt);
    }
    for (int i = 0; i < kBlobN; ++i)
        blobR[i] = blobInit ? blobR[i] + (tgt[i] - blobR[i]) * std::min (1.0f, dt * 6.0f) : tgt[i];
    blobInit = true;
}

void Scene::advance (float dt)
{
    sync();
    float left = dt;
    while (left > 1e-6f)
    {
        const float h = std::min (left, 1.0f / 60.0f);
        step (h);
        left -= h;
    }
    if (dt <= 0.0f) step (0.0f);
}

//------------------------------------------------------------------------------
// Drawing
//------------------------------------------------------------------------------
void Scene::paint (Graphics& g)
{
    g.addTransform (juce::AffineTransform::scale (K()));
    drawDropZone (g);
    drawFrame (g);
    drawFlyers (g);
    drawStatus (g);

    // chaos value under the label
    const float chaos = proc.apvts.getRawParameterValue ("chaos")->load();
    text (g, juce::String (juce::roundToInt (chaos)) + "%", 397.0f, 652.0f, 11.0f, Colour (236, 54, 66).withAlpha (0.7f), 0.05f);
    drawGrab (g);
}

void Scene::drawStatus (Graphics& g)
{
    juce::String msg; Colour c = Colour (170, 255, 195).withAlpha (0.6f);
    if (errorTimer > 0.0f)         { msg = "Couldn't read that file"; c = Colour (235, 110, 110).withAlpha (0.85f); }
    else if (! hasSource)          msg = "Drop a sample to begin";
    else if (noteTimer > 0.0f)     msg = "Long sample - using the first 8 seconds";
    else if (pending > 0)          msg = "Transmuting...";
    else if (orbs.empty())         msg = "Press Transmute";
    else                           msg = "Click a bubble to hear it - Transmute grows it";
    text (g, msg, 300.0f, 18.0f, 12.0f, c, 0.05f);
}

void Scene::drawDropZone (Graphics& g)
{
    const auto r = kDropZone;
    const bool hot = dragHover || hoverDrop;
    g.setColour (Colour (255, 255, 255).withAlpha (hot ? 0.07f : 0.03f));
    g.fillRoundedRectangle (r, 6.0f);
    if (dragHover)
    {
        Path pr; pr.addRoundedRectangle (r, 6.0f);
        glowStroke (g, pr, Colour (96, 212, 128), 1.4f, 1.0f);
    }
    else
    {
        Path pr; pr.addRoundedRectangle (r, 6.0f);
        glowStroke (g, pr, Colour (245, 248, 252), 1.3f, hot ? 1.0f : 0.8f);
        if (dropGlow > 0.0f)
        {
            Path pr; pr.addRoundedRectangle (r, 6.0f);
            glowStroke (g, pr, kLime, 1.4f, dropGlow);
        }
    }

    const auto inner = r.reduced (8.0f, 12.0f);
    const float cy = inner.getCentreY();
    auto bars = [&] (const std::vector<float>& pk, Colour col, float scaleTo)
    {
        if (pk.empty()) return;
        float mx = 1e-6f; for (float v : pk) mx = std::max (mx, v);
        const float bw = inner.getWidth() / (float) pk.size();
        g.setColour (col);
        for (size_t i = 0; i < pk.size(); ++i)
        {
            const float h = std::max (0.7f, pk[i] / mx * inner.getHeight() * 0.5f * scaleTo);
            g.fillRect ((float) inner.getX() + (float) i * bw, cy - h, bw * 0.72f, 2.0f * h);
        }
    };

    if (! hasSource)
    {
        std::vector<float> ph (150);
        for (size_t i = 0; i < ph.size(); ++i)
        {
            const float t = (float) i / (float) ph.size();
            ph[i] = std::exp (-t * 3.0f) * (0.3f + 0.7f * std::fabs (std::sin ((float) i * 1.7f) * std::sin ((float) i * 0.37f)));
        }
        bars (ph, Colour (120, 140, 150).withAlpha (hot ? 0.5f : 0.35f), 1.0f);
        text (g, dragHover ? "DROP TO LOAD" : "DRAG & DROP", r.getCentreX(), cy - 2.0f, 11.0f, Colour (235, 240, 246).withAlpha (0.85f), 0.1f);
        text (g, "or click to browse", r.getCentreX(), cy + 13.0f, 9.5f, Colour (200, 206, 216).withAlpha (0.45f), 0.03f);
    }
    else
    {
        auto peaksFor = [&] (int id) -> const std::vector<float>& {
            auto it = peakCache.find (id);
            if (it == peakCache.end()) it = peakCache.emplace (id, proc.getPeaks (id)).first;
            return it->second;
        };
        bars (peaksFor (0), Colour (205, 238, 215).withAlpha (selectedId == 0 ? 0.85f : 0.4f), 1.0f);
        if (selectedId != 0)
        {
            float hue = 0.6f;
            for (const auto& o : orbs) if (o.id == selectedId) hue = o.hue;
            bars (peaksFor (selectedId), orbColour (hue, 0.85f), 0.9f);
        }
        const juce::String cap = (fileName.isNotEmpty() ? fileName + "  -  " : juce::String()) + juce::String (proc.getSourceSeconds(), 1) + " s"
                                 + (hot ? "  (replace)" : "");
        g.setColour (Colour (200, 206, 216).withAlpha (0.55f));
        g.setFont (makeFont (9.5f, false, 0.02f));
        g.drawText (cap, Rectangle<float> (r.getX() + 8.0f, r.getBottom() - 15.0f, r.getWidth() - 16.0f, 13.0f), juce::Justification::centredLeft, true);
    }
}

void Scene::drawGrab (Graphics& g)
{
    const auto r = kGrab;
    const float a = hasSource ? 1.0f : 0.35f;
    const bool hot = hoverGrab || grabArmed;
    g.setColour (kBlue.withAlpha (hot ? 0.16f : 0.06f));
    g.fillRoundedRectangle (r, 6.0f);
    Path pr; pr.addRoundedRectangle (r, 6.0f);
    glowStroke (g, pr, kBlue, 1.5f, (hot ? 1.0f : 0.8f) * a);
    text (g, "grab sample", r.getCentreX(), r.getCentreY() - (hot ? 5.0f : 0.0f), 13.0f, Colour (120, 150, 255).withAlpha (a), 0.06f);
    if (hot)
        text (g, selectedId == 0 ? "drag the original out as .wav" : "drag this sound out as .wav", r.getCentreX(), r.getCentreY() + 10.0f, 9.5f, Colour (170, 190, 255).withAlpha (0.8f), 0.02f);
}

void Scene::drawFrame (Graphics& g)
{
    Path frame; frame.addRoundedRectangle (kFrame, 56.0f);

    g.saveState();
    g.reduceClipRegion (frame);
    juce::ColourGradient ig (Colour (4, 22, 11), 291.0f, 250.0f, Colour (0, 0, 0), 120.0f, 60.0f, true);
    g.setGradientFill (ig);
    g.fillPath (frame);
    drawCloud (g);
    drawOrbs (g);
    drawVial (g);
    // sparks
    for (const auto& s : sparks)
    {
        const float a = std::clamp (s.life / s.maxLife, 0.0f, 1.0f);
        g.setColour (s.c.withAlpha (a));
        g.fillEllipse (s.p.x - s.size * 0.5f, s.p.y - s.size * 0.5f, s.size, s.size);
    }
    g.restoreState();

    glowStroke (g, frame, kGreen, 1.8f, 0.85f + 0.15f * brew * std::sin (time * 6.0f));
}

void Scene::drawCloud (Graphics& g)
{
    const float streamAmt = std::max (cloudAlpha, brew * 0.85f);
    if (streamAmt < 0.02f) return;

    Point<float> bot (kNeck.x, kNeck.y - 74.0f * brew);
    if (cloudAlpha > 0.02f) bot = { blobC.x, blobC.y + blobR[kBlobN / 4] };

    for (int k = 0; k < 2; ++k)
    {
        Path s;
        s.startNewSubPath (kNeck);
        const float fk = (float) k;
        s.cubicTo ({ kNeck.x + (14.0f + 4.0f * fk) * std::sin (time * 1.7f + fk * 2.1f), kNeck.y + (bot.y - kNeck.y) * 0.33f },
                   { kNeck.x - (16.0f + 3.0f * fk) * std::sin (time * 1.3f + 1.0f + fk), kNeck.y + (bot.y - kNeck.y) * 0.68f },
                   { bot.x + 6.0f * std::sin (time * 0.9f + fk), bot.y });
        glowStroke (g, s, kCloud, k == 0 ? 1.5f : 1.0f, streamAmt * (k == 0 ? 0.85f : 0.45f));
    }

    if (cloudAlpha < 0.02f) return;
    for (int k = 0; k < 2; ++k)
    {
        Point<float> pts[kBlobN];
        for (int i = 0; i < kBlobN; ++i)
        {
            const float th = (float) i * juce::MathConstants<float>::twoPi / (float) kBlobN;
            const float wob = 1.0f + 0.05f * std::sin (3.0f * th + 0.9f * time + (float) k * 1.7f)
                                   + 0.035f * std::sin (5.0f * th - 1.3f * time + (float) k)
                                   + 0.02f * std::sin (8.0f * th + 2.0f * time);
            pts[i] = { blobC.x + std::cos (th) * blobR[i] * wob, blobC.y + std::sin (th) * blobR[i] * wob };
        }
        Path p;
        auto mid = [&] (int a, int b) { return (pts[a % kBlobN] + pts[b % kBlobN]) * 0.5f; };
        p.startNewSubPath (mid (kBlobN - 1, 0));
        for (int i = 0; i < kBlobN; ++i) p.quadraticTo (pts[i], mid (i, i + 1));
        p.closeSubPath();
        if (k == 0)
        {
            g.setColour (kCloud.withAlpha (0.05f * cloudAlpha));
            g.fillPath (p);
        }
        glowStroke (g, p, kCloud, k == 0 ? 1.5f : 1.0f, cloudAlpha * (k == 0 ? 0.9f : 0.45f));
    }
}

void Scene::drawOrbs (Graphics& g)
{
    // threads: every bubble hangs on a thread from the bubble it was made from (or from the potion for first generation)
    for (const auto& o : orbs)
    {
        if (o.dying && o.alpha <= 0.0f) continue;
        Point<float> from = kNeck;
        float fromAlpha = 0.55f;
        if (o.parent > 0)
        {
            bool found = false;
            for (const auto& q : orbs)
                if (q.id == o.parent && ! q.dying) { from = { q.x, q.y }; found = true; break; }
            if (! found) continue;
            fromAlpha = 0.7f;
        }
        const Point<float> to (o.x, o.y);
        const Point<float> d = to - from;
        const float len = std::max (1.0f, std::sqrt (d.x * d.x + d.y * d.y));
        const Point<float> nrm (-d.y / len, d.x / len);
        const float sway = std::min (14.0f, 4.0f + len * 0.08f);
        const float al = std::min (o.alpha, std::min (o.born, 1.0f)) * fromAlpha;
        for (int k = 0; k < 2; ++k)
        {
            const float sg = k == 0 ? 1.0f : -1.0f;
            const float w1 = std::sin (time * 1.3f + o.phase + (float) k * 2.0f), w2 = std::sin (time * 1.0f + o.phase * 1.7f + (float) k);
            Path t;
            t.startNewSubPath (from);
            t.cubicTo (from + d * 0.33f + nrm * (sway * sg * w1), from + d * 0.68f + nrm * (sway * -sg * w2 * 0.8f), to);
            const Colour col = o.parent > 0 ? orbColour (o.hue) : kGreen.interpolatedWith (orbColour (o.hue), 0.45f);
            glowStroke (g, t, col, k == 0 ? 0.9f : 0.6f, al * (k == 0 ? 1.0f : 0.5f));
        }
    }
    for (const auto& o : orbs)
    {
        const float appear = o.born < 1.0f ? easeOutBack (o.born) : 1.0f;
        const bool hov = o.id == hoverId;
        const float s = appear * (hov ? 1.14f : 1.0f) * (1.0f + o.pulse * 0.45f);
        const float r = o.radius() * s;
        const float al = o.alpha * std::min (1.0f, o.born * 3.0f);
        const Colour col = orbColour (o.hue);

        juce::ColourGradient glow (col.withAlpha (0.5f * al * (1.0f + o.pulse)), o.x, o.y, col.withAlpha (0.0f), o.x + r * 3.6f, o.y, true);
        g.setGradientFill (glow);
        g.fillEllipse (o.x - r * 3.6f, o.y - r * 3.6f, r * 7.2f, r * 7.2f);

        juce::ColourGradient core (col.brighter (0.7f).withAlpha (al), o.x - r * 0.3f, o.y - r * 0.35f, col.darker (0.15f).withAlpha (al), o.x + r * 0.5f, o.y + r * 0.6f, true);
        g.setGradientFill (core);
        g.fillEllipse (o.x - r, o.y - r, 2.0f * r, 2.0f * r);

        if (o.id == selectedId)
        {
            g.setColour (juce::Colours::white.withAlpha (al * (0.72f + 0.2f * std::sin (time * 4.0f))));
            g.drawEllipse (o.x - r - 5.0f, o.y - r - 5.0f, 2.0f * (r + 5.0f), 2.0f * (r + 5.0f), 1.4f);
        }
        else if (hov)
        {
            g.setColour (juce::Colours::white.withAlpha (al * 0.35f));
            g.drawEllipse (o.x - r - 4.0f, o.y - r - 4.0f, 2.0f * (r + 4.0f), 2.0f * (r + 4.0f), 1.0f);
        }
    }
}

void Scene::drawVial (Graphics& g)
{
    g.saveState();
    g.addTransform (kVialMove);
    const Path vp = vialPath();
    const float surfaceY = 404.0f - liquid * 66.0f;

    // halo when the original is the selected sound
    if (hasSource && selectedId == 0)
    {
        juce::ColourGradient halo (kLime.withAlpha (0.08f + 0.14f * vialPulse), 512.0f, 372.0f, kLime.withAlpha (0.0f), 512.0f, 292.0f, true);
        g.setGradientFill (halo);
        g.fillEllipse (402.0f, 292.0f, 220.0f, 160.0f);
    }

    // glass
    g.setColour (Colour (255, 255, 255).withAlpha (0.035f));
    g.fillPath (vp);

    if (hasSource && liquid > 0.01f)
    {
        g.saveState();
        g.reduceClipRegion (vp);
        Path wave;
        wave.startNewSubPath (452.0f, 420.0f);
        for (float x = 452.0f; x <= 572.0f; x += 4.0f)
            wave.lineTo (x, surfaceY + 1.8f * std::sin (x * 0.09f + time * 2.2f) * (0.45f + brew) + slosh * (x - 512.0f) * 0.07f);
        wave.lineTo (572.0f, 420.0f);
        wave.closeSubPath();
        const float mixT = brew * (0.5f + 0.5f * std::sin (time * 5.0f));
        const Colour top = Colour (96, 255, 150).interpolatedWith (Colour (210, 255, 70), mixT * 0.8f);
        const Colour bot = Colour (10, 128, 58).interpolatedWith (Colour (70, 150, 20), mixT * 0.7f);
        juce::ColourGradient lg (top.withAlpha (0.88f), 512.0f, surfaceY, bot.withAlpha (0.92f), 512.0f, 404.0f, false);
        g.setGradientFill (lg);
        g.fillPath (wave);
        // surface highlight
        Path line;
        for (float x = 452.0f; x <= 572.0f; x += 4.0f)
        {
            const float y = surfaceY + 1.8f * std::sin (x * 0.09f + time * 2.2f) * (0.45f + brew) + slosh * (x - 512.0f) * 0.07f;
            if (x == 452.0f) line.startNewSubPath (x, y); else line.lineTo (x, y);
        }
        g.setColour (Colour (200, 255, 220).withAlpha (0.8f));
        g.strokePath (line, juce::PathStrokeType (1.3f));
        for (const auto& b : bubbles)
        {
            g.setColour (Colour (190, 255, 215).withAlpha (0.55f));
            g.drawEllipse (b.x - b.r, b.y - b.r, 2.0f * b.r, 2.0f * b.r, 1.0f);
        }
        g.restoreState();
    }

    // outline
    const bool sel = hasSource && selectedId == 0;
    const float a = (hasSource ? 0.78f : 0.55f) + (sel ? 0.2f : 0.0f) + 0.2f * brew * std::sin (time * 7.0f) + 0.2f * vialPulse;
    glowStroke (g, vp, kLime, 1.7f, std::clamp (a, 0.0f, 1.0f));
    g.setColour (kLime.withAlpha (0.55f));
    g.drawLine (499.0f, 320.0f, 525.0f, 320.0f, 1.6f);
    g.restoreState();
}

void Scene::drawFlyers (Graphics& g)
{
    for (const auto& f : flyers)
    {
        if (f.delay > 0.0f || f.landed) continue;
        for (int k = 0; k < 6; ++k)
        {
            const float t = std::clamp (f.t - (float) k * 0.03f, 0.0f, 1.0f);
            const float e = t * t * (3.0f - 2.0f * t);
            const Point<float> p = bezier (f.p0, f.c1, f.c2, f.p3, e);
            const float al = (1.0f - (float) k / 6.0f) * 0.9f;
            const float sz = f.size * (1.0f - (float) k * 0.1f);
            juce::ColourGradient gl (Colour (120, 255, 160).withAlpha (0.35f * al), p.x, p.y, Colour (120, 255, 160).withAlpha (0.0f), p.x + sz * 4.0f, p.y, true);
            g.setGradientFill (gl);
            g.fillEllipse (p.x - sz * 4.0f, p.y - sz * 4.0f, sz * 8.0f, sz * 8.0f);
            g.setColour (Colour (225, 255, 235).withAlpha (al));
            g.fillEllipse (p.x - sz * 0.5f, p.y - sz * 0.5f, sz, sz);
        }
    }
}

} // namespace polyui

//==============================================================================
// Editor
//==============================================================================
PolymerEditor::PolymerEditor (PolymerAudioProcessor& p) : juce::AudioProcessorEditor (&p), proc (p), scene (p)
{
    addAndMakeVisible (backdrop);
    addAndMakeVisible (scene);
    addAndMakeVisible (button);
    addAndMakeVisible (chaosKnob);
    addAndMakeVisible (gateToggle);

    button.onClick = [this]
    {
        if (! proc.hasSource()) { scene.browse(); return; }
        if (proc.transmute()) scene.kick();
    };

    chaosKnob.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    chaosKnob.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    chaosKnob.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    chaosKnob.setMouseDragSensitivity (240);
    chaosKnob.setLookAndFeel (&laf);
    attChaos = std::make_unique<SliderAtt> (proc.apvts, "chaos", chaosKnob);
    chaosKnob.setDoubleClickReturnValue (true, 40.0);

    gateToggle.setLookAndFeel (&laf);
    gateToggle.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    attGate = std::make_unique<ButtonAtt> (proc.apvts, "gate", gateToggle);

    setSize (504, 601);
    setResizable (false, false);
    scene.advance (0.0f);
    startTimerHz (30);
}

PolymerEditor::~PolymerEditor()
{
    stopTimer();
    chaosKnob.setLookAndFeel (nullptr);
    gateToggle.setLookAndFeel (nullptr);
}

void PolymerEditor::resized()
{
    const float K = (float) getWidth() / polyui::kW;
    auto R = [&] (float x, float y, float w, float h) { return juce::Rectangle<int> ((int) std::lround (x * K), (int) std::lround (y * K), (int) std::lround (w * K), (int) std::lround (h * K)); };
    backdrop.setBounds (getLocalBounds());
    scene.setBounds (getLocalBounds());
    button.setBounds (R (230.0f, 524.0f, 98.0f, 98.0f));
    chaosKnob.setBounds (R (341.0f, 517.0f, 112.0f, 112.0f));
    gateToggle.setBounds (R (428.0f, 452.0f, 130.0f, 34.0f));
}

bool PolymerEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
    {
        const auto ext = juce::File (f).getFileExtension().toLowerCase();
        if (ext == ".wav" || ext == ".aif" || ext == ".aiff" || ext == ".flac" || ext == ".mp3" || ext == ".ogg") return true;
    }
    return false;
}

void PolymerEditor::filesDropped (const juce::StringArray& files, int, int)
{
    scene.setDragHover (false);
    for (auto& f : files)
        if (isInterestedInFileDrag (juce::StringArray (f))) { scene.loadFile (juce::File (f)); return; }
}

void PolymerEditor::timerCallback()
{
    static juce::uint32 last = 0;
    const auto now = juce::Time::getMillisecondCounter();
    const float dt = last == 0 ? 1.0f / 30.0f : std::clamp ((float) (now - last) * 0.001f, 0.005f, 0.1f);
    last = now;
    advanceAnimation (dt);
}

void PolymerEditor::advanceAnimation (float seconds)
{
    scene.advance (seconds);
    button.ready = proc.hasSource();
    button.busy = scene.isBrewing();
    button.spin += seconds * 5.0f;
    button.repaint();
    scene.repaint();
}
