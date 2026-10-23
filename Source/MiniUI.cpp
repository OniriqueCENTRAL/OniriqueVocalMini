#include "MiniUI.h"
#include "OniriqueKnob.h"

using namespace onirique;

namespace
{
constexpr float kPi = juce::MathConstants<float>::pi;
constexpr float kStart = -2.36f, kEnd = 2.36f;   // course des potards (radians, 0 en haut) : 270°

juce::Point<float> onCircle (juce::Point<float> c, float r, float a) { return { c.x + std::sin (a) * r, c.y - std::cos (a) * r }; }

// valeur en dB, signe compris, une décimale
juce::String dbText (float db) { db = std::round (db * 10.0f) / 10.0f + 0.0f; return (db > 0.0f ? "+" : "") + juce::String (db, 1) + " dB"; }
} // namespace

namespace minidraw
{
// halo doux (dégradé radial) centré sur c
void softGlow (juce::Graphics& g, juce::Point<float> c, float r, juce::Colour col, float alpha)
{
    if (alpha <= 0.003f || r < 1.0f) return;
    juce::ColourGradient grad (col.withAlpha (juce::jmin (1.0f, alpha)), c.x, c.y, col.withAlpha (0.0f), c.x + r, c.y, true);
    grad.addColour (0.45, col.withAlpha (juce::jmin (1.0f, alpha) * 0.42f));
    g.setGradientFill (grad);
    g.fillEllipse (juce::Rectangle<float> (2.0f * r, 2.0f * r).withCentre (c));
}

// le même halo, rayon limité pour s'éteindre avant le bord de la zone (sinon le bord du composant le coupe net, en carré)
void softGlowIn (juce::Graphics& g, juce::Rectangle<float> area, juce::Point<float> c, float r, juce::Colour col, float alpha)
{
    const float room = std::min ({ c.x - area.getX(), area.getRight() - c.x, c.y - area.getY(), area.getBottom() - c.y });
    softGlow (g, c, std::min (r, room), col, alpha);
}

// panneau de verre sombre (rangées, panneaux détaillés)
void glassPanel (juce::Graphics& g, juce::Rectangle<float> r, float radius, float u, float strength)
{
    g.setColour (ui::deep.withAlpha (0.94f));
    g.fillRoundedRectangle (r, radius);
    juce::ColourGradient sheen (ui::lilac.withAlpha (0.045f * strength), r.getX(), r.getY(), ui::lilac.withAlpha (0.0f), r.getX(), r.getY() + r.getHeight() * 0.6f, false);
    g.setGradientFill (sheen);
    g.fillRoundedRectangle (r, radius);
    g.setColour (ui::line);
    g.drawRoundedRectangle (r.reduced (0.5f), radius, std::max (1.0f, u));
}
} // namespace minidraw

using namespace minidraw;

//==============================================================================
MiniKnob::MiniKnob (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramId, const juce::String& t, Size s, bool bip,
                    const juce::String& tooltip)
    : title (t), size (s), bipolar (bip)
{
    setTooltip (tooltip);
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    param = apvts.getParameter (paramId);
    jassert (param != nullptr);
    if (param != nullptr)
    {
        attachment = std::make_unique<juce::ParameterAttachment> (*param, [this] (float) { repaint(); });
        attachment->sendInitialUpdate();
    }
    wasAwake = ! isAtRest();
    awake = wasAwake ? 1.0f : 0.0f;
}

bool MiniKnob::isAtRest() const noexcept
{
    if (param == nullptr) return true;
    const float v = param->convertFrom0to1 (param->getValue());
    const auto range = param->getNormalisableRange();
    if (bipolar) return std::abs (v) < 1.0e-3f;
    return v <= range.start + 1.0e-3f;
}

juce::Rectangle<float> MiniKnob::knobArea() const noexcept
{
    const auto b = getLocalBounds().toFloat();
    const float labelH = b.getHeight() * (size == Size::large ? 0.15f : 0.24f);
    const float d = std::min (b.getWidth(), b.getHeight() - labelH);
    return juce::Rectangle<float> (d, d).withCentre ({ b.getCentreX(), b.getY() + d * 0.5f });
}

void MiniKnob::tick()
{
    if (param == nullptr) return;
    bool changed = ui::approach (shown, param->getValue(), 0.30f);
    const bool awakeNow = ! isAtRest();
    if (awakeNow && ! wasAwake) bloom = 1.0f;   // réveil : une floraison de lumière
    wasAwake = awakeNow;
    changed |= ui::approach (awake, awakeNow ? 1.0f : 0.0f, 0.16f);
    changed |= ui::approach (glow, dragging ? 1.0f : (hovering ? 0.6f : 0.0f), 0.2f);
    changed |= ui::approach (valueShow, (dragging || hovering) ? 1.0f : 0.0f, 0.22f);
    if (bloom > 0.0f) { bloom = bloom < 0.02f ? 0.0f : bloom * 0.9f; changed = true; }
    if (activity) changed |= ui::approach (act, juce::jlimit (0.0f, 1.0f, activity()) * awake, 0.25f);
    const auto text = param->getCurrentValueAsText();
    if (text != shownText) { shownText = text; changed = true; }
    if (changed) repaint();
}

void MiniKnob::paint (juce::Graphics& g)
{
    const auto k = knobArea();
    const auto c = k.getCentre();
    const float R = k.getWidth() * 0.5f;
    const bool big = size == Size::large;
    const float aw = awake;
    const float ang = kStart + juce::jlimit (0.0f, 1.0f, shown) * (kEnd - kStart);
    const float origin = bipolar ? 0.0f : kStart;

    // halo : activité (queue de reverb, réduction...) et floraison du réveil ; tous les halos s'éteignent dans le cercle du potard
    const auto area = getLocalBounds().toFloat();
    softGlowIn (g, area, c, R * (1.12f + 0.3f * bloom), ui::ember, 0.20f * act + 0.32f * bloom + 0.05f * glow);

    // le potard ONIRIQUE « RONCE », le même dans tous les plugins (OniriqueKnob.h) : arc épineux, anneau de métal
    // liquide qui tourne avec le réglage, aiguille-épine rouge
    onirique::ui::drawKnob (g, k, juce::jlimit (0.0f, 1.0f, shown), kStart, kEnd, ui::ember, bipolar, glow > 0.3f);
    juce::ignoreUnused (big, aw, origin, ang);

    // nom, remplacé par la valeur au survol et pendant le réglage
    const auto b = getLocalBounds().toFloat();
    const auto labelR = b.withTop (k.getBottom() + R * (big ? 0.02f : 0.04f));
    const float es = ui::editorScale (*this);
    const float h = big ? std::max (13.0f * es, labelR.getHeight() * 0.62f)
                        : std::max (9.5f * es, std::min (labelR.getHeight() * 0.58f, 12.5f * es));
    const auto titleCol = ui::haze.interpolatedWith (ui::mist, 0.30f + 0.70f * aw);
    if (valueShow < 0.99f)
    {
        g.setColour (titleCol.withMultipliedAlpha (1.0f - valueShow));
        g.setFont (ui::fitFont (big ? ui::Face::display : ui::Face::body, h, labelR.getWidth(), title, ! big, big ? 0.16f : 0.12f));
        g.drawText (title, labelR.withHeight (h * 1.4f), juce::Justification::centredTop, false);
    }
    if (valueShow > 0.01f)
    {
        g.setColour (ui::lilac.interpolatedWith (ui::bone, 0.5f).withAlpha (valueShow));
        g.setFont (ui::fitFont (ui::Face::mono, h * 0.95f, labelR.getWidth(), shownText, true));
        g.drawText (shownText, labelR.withHeight (h * 1.4f), juce::Justification::centredTop, false);
    }
}

void MiniKnob::mouseEnter (const juce::MouseEvent&) { hovering = true; }
void MiniKnob::mouseExit (const juce::MouseEvent&)  { hovering = false; }

void MiniKnob::mouseDown (const juce::MouseEvent& e)
{
    if (param == nullptr || e.mods.isPopupMenu()) return;
    dragging = true;
    startNorm = param->getValue();
    attachment->beginGesture();
}

void MiniKnob::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging) return;
    const float range = e.mods.isShiftDown() ? 900.0f : 220.0f;   // Maj : réglage fin
    const float norm = juce::jlimit (0.0f, 1.0f, startNorm - (float) e.getDistanceFromDragStartY() / range);
    attachment->setValueAsPartOfGesture (param->convertFrom0to1 (norm));
}

void MiniKnob::mouseUp (const juce::MouseEvent&)
{
    if (! dragging) return;
    dragging = false;
    attachment->endGesture();
}

void MiniKnob::mouseDoubleClick (const juce::MouseEvent&)
{
    if (param != nullptr) attachment->setValueAsCompleteGesture (param->convertFrom0to1 (param->getDefaultValue()));
}

void MiniKnob::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (param == nullptr) return;
    const float step = (e.mods.isShiftDown() ? 0.005f : 0.025f) * (w.deltaY > 0.0f ? 1.0f : (w.deltaY < 0.0f ? -1.0f : 0.0f));
    attachment->setValueAsCompleteGesture (param->convertFrom0to1 (juce::jlimit (0.0f, 1.0f, param->getValue() + step)));
}

//==============================================================================
StepSelector::StepSelector (juce::AudioProcessorValueTreeState& apvts, const char* paramId, const juce::String& t,
                            const juce::String& tooltip)
    : title (t)
{
    setTooltip (tooltip);
    param = apvts.getParameter (paramId);
    if (param != nullptr)
    {
        attachment = std::make_unique<juce::ParameterAttachment> (*param, [this] (float) { repaint(); });
        attachment->sendInitialUpdate();
    }
}

juce::Rectangle<float> StepSelector::arrow (int side) const noexcept
{
    const auto b = getLocalBounds().toFloat();
    const float pillH = b.getHeight() * 0.42f, y = b.getY() + b.getHeight() * 0.20f;
    return side < 0 ? juce::Rectangle<float> (b.getX(), y, pillH, pillH) : juce::Rectangle<float> (b.getRight() - pillH, y, pillH, pillH);
}

void StepSelector::step (int delta)
{
    if (param == nullptr) return;
    const int count = param->getNumSteps();
    const int now = juce::roundToInt (param->convertFrom0to1 (param->getValue()));
    const int next = juce::jlimit (0, count - 1, now + delta);
    if (next != now) { attachment->setValueAsCompleteGesture ((float) next); flash = 1.0f; slide = (float) delta; }
}

void StepSelector::tick()
{
    bool changed = false;
    for (int s = 0; s < 2; ++s) changed |= ui::approach (glow[s], hover == (s == 0 ? -1 : 1) ? 1.0f : 0.0f, 0.2f);
    if (flash > 0.0f) { flash = flash < 0.02f ? 0.0f : flash * 0.88f; changed = true; }
    if (slide != 0.0f) { slide = std::abs (slide) < 0.02f ? 0.0f : slide * 0.75f; changed = true; }
    const int idx = param != nullptr ? juce::roundToInt (param->convertFrom0to1 (param->getValue())) : 0;
    if (idx != index) { index = idx; changed = true; }
    if (changed) repaint();
}

void StepSelector::paint (juce::Graphics& g)
{
    if (param == nullptr) return;
    const auto b = getLocalBounds().toFloat();
    const float es = ui::editorScale (*this);
    const auto left = arrow (-1), right = arrow (1);
    const auto pill = juce::Rectangle<float> (left.getX(), left.getY(), right.getRight() - left.getX(), left.getHeight());

    g.setColour (juce::Colours::black.withAlpha (0.40f));
    g.fillRoundedRectangle (pill, pill.getHeight() * 0.5f);
    g.setColour (ui::ember.withAlpha (0.08f + 0.22f * flash));
    g.fillRoundedRectangle (pill, pill.getHeight() * 0.5f);
    g.setColour (ui::line.interpolatedWith (ui::ember.withAlpha (0.8f), 0.30f + 0.70f * flash));
    g.drawRoundedRectangle (pill.reduced (0.5f), pill.getHeight() * 0.5f, std::max (1.0f, 1.1f * es));

    for (int s = 0; s < 2; ++s)
    {
        const auto a = (s == 0 ? left : right).reduced (left.getHeight() * 0.32f);
        juce::Path p;
        if (s == 0) { p.startNewSubPath (a.getRight() - a.getWidth() * 0.25f, a.getY()); p.lineTo (a.getX() + a.getWidth() * 0.25f, a.getCentreY()); p.lineTo (a.getRight() - a.getWidth() * 0.25f, a.getBottom()); }
        else        { p.startNewSubPath (a.getX() + a.getWidth() * 0.25f, a.getY()); p.lineTo (a.getRight() - a.getWidth() * 0.25f, a.getCentreY()); p.lineTo (a.getX() + a.getWidth() * 0.25f, a.getBottom()); }
        g.setColour (ui::haze.interpolatedWith (ui::bone, 0.3f + 0.7f * glow[s]));
        g.strokePath (p, juce::PathStrokeType (std::max (1.3f, 1.8f * es), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    const auto text = param->getCurrentValueAsText();
    const auto textR = pill.withTrimmedLeft (left.getWidth()).withTrimmedRight (right.getWidth()).translated (slide * 6.0f * es, 0.0f);
    g.setColour (ui::bone);
    g.setFont (ui::fitFont (ui::Face::mono, pill.getHeight() * 0.48f, textR.getWidth(), text, true));
    g.drawText (text, textR, juce::Justification::centred, false);

    const auto titleR = b.withTop (pill.getBottom() + 5.0f * es);
    g.setColour (ui::mist.withAlpha (0.85f));
    g.setFont (ui::fitFont (ui::Face::body, std::max (9.5f * es, std::min (titleR.getHeight() * 0.58f, 12.5f * es)),
                            titleR.getWidth(), title, true, 0.12f));
    g.drawText (title, titleR, juce::Justification::centredTop, false);
}

void StepSelector::mouseMove (const juce::MouseEvent& e)
{
    const int h = arrow (-1).expanded (3.0f).contains (e.position) ? -1 : (arrow (1).expanded (3.0f).contains (e.position) ? 1 : 0);
    if (h != hover) { hover = h; setMouseCursor (h != 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::UpDownResizeCursor); }
}
void StepSelector::mouseExit (const juce::MouseEvent&) { hover = 0; }

void StepSelector::mouseDown (const juce::MouseEvent& e)
{
    if (hover != 0) { step (hover); return; }
    dragStart = e.getPosition().y;
    dragIndex = param != nullptr ? juce::roundToInt (param->convertFrom0to1 (param->getValue())) : 0;
}

void StepSelector::mouseDrag (const juce::MouseEvent& e)
{
    if (hover != 0 || param == nullptr) return;
    const int target = juce::jlimit (0, param->getNumSteps() - 1, dragIndex + (dragStart - e.getPosition().y) / 14);
    const int now = juce::roundToInt (param->convertFrom0to1 (param->getValue()));
    if (target != now) step (target - now);
}

void StepSelector::mouseUp (const juce::MouseEvent&) {}

void StepSelector::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    if (w.deltaY != 0.0f) step (w.deltaY > 0.0f ? 1 : -1);
}

//==============================================================================
LevelBar::LevelBar (std::function<float()> c, std::function<float()> r, bool best)
    : consume (std::move (c)), rms (std::move (r)), showBest (best)
{
    setTooltip (best ? "Input level. Keep the voice in the BEST zone (AUTO INPUT does it for you): the compression, de-esser, "
                       "gate and drive are tuned for it."
                     : "Output level (peak, dBFS). VOCAL MINI PRO never goes over 0 dBFS.");
}

void LevelBar::tick()
{
    const float pk = consume();
    const float db = pk > 1.0e-6f ? 20.0f * std::log10 (pk) : -100.0f;
    bool changed = false;
    const float prevLevel = levelDb, prevHold = holdDb, prevAvg = avgDb;
    levelDb = db > levelDb ? db : std::max (db, levelDb - 0.45f);   // retombée ~27 dB/s
    if (levelDb >= holdDb) { holdDb = levelDb; holdFrames = 75; }
    else if (--holdFrames < 0) holdDb = std::max (levelDb, holdDb - 0.6f);
    if (rms)
    {
        const float r = rms();
        avgDb += 0.2f * (r - avgDb);
        const bool in = avgDb > -24.0f && avgDb < -13.0f;
        changed |= ui::approach (inBest, in ? 1.0f : 0.0f, 0.08f);
    }
    changed |= std::abs (levelDb - prevLevel) > 0.05f || std::abs (holdDb - prevHold) > 0.05f || std::abs (avgDb - prevAvg) > 0.05f;
    if (changed) repaint();
}

void LevelBar::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const float es = ui::editorScale (*this);
    const float th = std::max (5.0f, b.getHeight() * 0.28f);
    const auto track = juce::Rectangle<float> (b.getX(), b.getBottom() - th - 2.0f * es, b.getWidth(), th);
    const float rad = track.getHeight() * 0.5f;
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillRoundedRectangle (track, rad);
    g.setColour (ui::line);
    g.drawRoundedRectangle (track, rad, std::max (1.0f, 0.8f * es));

    // graduations discrètes (-48, -36, -24, -12, -6 dBFS)
    for (float d : { -48.0f, -36.0f, -24.0f, -12.0f, -6.0f })
    {
        const float x = track.getX() + track.getWidth() * norm (d);
        g.setColour (juce::Colours::white.withAlpha (0.10f));
        g.fillRect (juce::Rectangle<float> (std::max (1.0f, es), track.getHeight() * 0.5f).withCentre ({ x, track.getY() - 3.5f * es }));
    }

    if (showBest)
    {
        // zone BEST : là où la voix doit vivre (niveau moyen de -24 à -13 dBFS)
        const float x0 = track.getX() + track.getWidth() * norm (-24.0f), x1 = track.getX() + track.getWidth() * norm (-13.0f);
        const auto zone = juce::Rectangle<float> (x0, track.getY(), x1 - x0, track.getHeight());
        g.setColour (ui::ember.withAlpha (0.14f + 0.16f * inBest));
        g.fillRoundedRectangle (zone, rad);
        g.setColour (ui::lilac.withAlpha (0.35f + 0.55f * inBest));
        g.drawLine (x0, track.getY() - 3.0f * es, x1, track.getY() - 3.0f * es, 1.2f * es);
        g.setColour (ui::bone.withAlpha (0.5f + 0.5f * inBest));
        g.setFont (ui::font (ui::Face::mono, std::max (8.5f, 9.5f * es), true, 0.18f));
        g.drawText ("BEST", juce::Rectangle<float> (x0, b.getY(), x1 - x0, track.getY() - 5.0f * es), juce::Justification::centredBottom, false);
    }

    const float fillN = norm (showBest ? avgDb : levelDb);
    if (fillN > 0.0f)
    {
        const auto fill = track.withWidth (std::max (track.getHeight(), track.getWidth() * fillN));
        juce::ColourGradient grad (ui::blood.brighter (0.5f), track.getX(), 0.0f, ui::lilac, track.getRight(), 0.0f, false);
        grad.addColour (0.7, ui::ember);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (fill, rad);
        if (inBest > 0.01f)
        {
            g.setColour (ui::bone.withAlpha (0.16f * inBest));
            g.fillRoundedRectangle (fill.reduced (0.0f, fill.getHeight() * 0.3f), rad * 0.4f);
        }
    }
    // crête maintenue
    const float hx = track.getX() + track.getWidth() * norm (holdDb);
    if (holdDb > -60.0f)
    {
        g.setColour ((holdDb > -1.0f ? juce::Colour (0xffFF6B8A) : ui::bone).withAlpha (0.9f));
        g.fillRect (juce::Rectangle<float> (std::max (1.5f, 2.0f * es), track.getHeight() + 4.0f * es).withCentre ({ hx, track.getCentreY() }));
    }
}

//==============================================================================
void Readout::tick()
{
    bool changed = level ? ui::approach (lit, juce::jlimit (0.0f, 1.0f, level()), 0.25f) : false;
    if (text)
    {
        const auto t = text();
        if (t != shownText) { shownText = t; changed = true; }
    }
    if (changed) repaint();
}

void Readout::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const float h = b.getHeight();
    const float d = h * 0.34f;
    // témoin à une demi-hauteur du bord : son halo (rayon h/2) s'éteint juste avant les bords du composant
    const auto led = juce::Rectangle<float> (d, d).withCentre ({ b.getX() + h * 0.5f, b.getCentreY() });
    softGlowIn (g, b, led.getCentre(), h * 0.5f, colour, 0.60f * lit);
    g.setColour (juce::Colour (0xff3A3133).interpolatedWith (colour, lit));
    g.fillEllipse (led);
    const auto tr = b.withTrimmedLeft (h * 1.05f);
    g.setColour (ui::haze.interpolatedWith (ui::mist, 0.4f + 0.6f * lit));
    g.setFont (ui::fitFont (ui::Face::mono, h * 0.52f, tr.getWidth(), shownText, true, 0.04f));
    g.drawText (shownText, tr, juce::Justification::centredLeft, false);
}

//==============================================================================
void PanelButton::tick()
{
    bool changed = ui::approach (glow, isDown() ? 1.0f : (isOver() ? 0.7f : 0.0f), 0.2f);
    if (lit) changed |= ui::approach (led, juce::jlimit (0.0f, 1.0f, lit()), 0.2f);
    if (changed) repaint();
}

void PanelButton::paintButton (juce::Graphics& g, bool, bool)
{
    // marge autour de la pastille (son halo s'y éteint) ; pillSize : celle d'une pastille PillToggle (6/36 de la hauteur)
    const float room = pillSize ? (float) getHeight() * 6.0f / 36.0f : std::max (1.5f, getHeight() / 13.0f);
    const auto b = getLocalBounds().toFloat().reduced (room);
    const float h = b.getHeight(), u = pillSize ? (float) getHeight() / 36.0f : h / 26.0f;
    juce::Path shape;
    shape.addRoundedRectangle (b, h * 0.5f);
    ui::strokeGlow (g, shape, ui::ember, 1.0f * u, 0.55f * glow, 2.0f * room);
    g.setColour (juce::Colours::black.withAlpha (0.42f));
    g.fillPath (shape);
    g.setColour (ui::ember.withAlpha (0.10f + 0.16f * glow));
    g.fillPath (shape);
    g.setColour (ui::line.interpolatedWith (ui::ember, 0.35f + 0.65f * glow));
    g.strokePath (shape, juce::PathStrokeType (std::max (1.0f, (pillSize ? 1.4f : 1.1f) * u)));

    auto tr = b.reduced (h * 0.45f, 0.0f);
    if (lit)
    {
        // témoin : allumé quand le panneau a quelque chose d'actif (pastille rouge comme les interrupteurs) ; pillSize :
        // placé et dimensionné exactement comme celui d'une pastille
        const float d = h * 0.26f, pad = h * 0.40f;
        const float cx = pillSize ? b.getX() + pad + d * 0.5f : tr.getX() + d * 0.5f;
        const auto ledR = juce::Rectangle<float> (d, d).withCentre ({ cx, b.getCentreY() });
        g.setColour (ui::ember.withAlpha (0.35f * led));
        g.fillEllipse (ledR.expanded (d * 0.8f));
        g.setColour (ui::haze.withAlpha (0.35f).interpolatedWith (ui::ember, led));
        g.fillEllipse (ledR);
        if (pillSize) tr = b.withTrimmedLeft (pad * 2.0f + d).withTrimmedRight (pad * 0.8f);
        else tr.removeFromLeft (d * 1.6f);
    }
    if (withArrow)
    {
        const auto ar = tr.removeFromRight (h * 0.42f);
        juce::Path chev;
        const float cx = ar.getCentreX(), cy = ar.getCentreY(), s = h * 0.16f;
        chev.startNewSubPath (cx - s * 0.5f, cy - s);
        chev.lineTo (cx + s * 0.5f, cy);
        chev.lineTo (cx - s * 0.5f, cy + s);
        g.setColour (ui::lilac.interpolatedWith (ui::bone, glow));
        g.strokePath (chev, juce::PathStrokeType (std::max (1.2f, 1.7f * u), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    g.setColour (ui::mist.interpolatedWith (ui::bone, glow));
    g.setFont (pillSize ? ui::fitFont (ui::Face::body, h * 0.40f, tr.getWidth(), getButtonText(), true, 0.10f)
                        : ui::fitFont (ui::Face::body, h * 0.44f, tr.getWidth(), getButtonText(), true, 0.14f));
    g.drawText (getButtonText(), tr, juce::Justification::centred, false);
}

//==============================================================================
ReverbScope::ReverbScope (const mini::VoiceReverb& r, bool c) : rev (r), compact (c)
{
    setInterceptsMouseClicks (compact, false);
    if (compact) setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void ReverbScope::tick()
{
    const int pos = rev.getHistoryPos();
    bool changed = pos != lastPos || frozen;
    lastPos = pos;
    changed |= ui::approach (cleanShown, frozen ? 0.9f : juce::jlimit (0.0f, 1.0f, rev.getCleanAmount() * 3.0f), 0.12f);
    changed |= ui::approach (duckShown, frozen ? 0.8f : juce::jlimit (0.0f, 1.0f, -rev.getDuckDb() / 6.0f), 0.12f);
    changed |= ui::approach (hover, hovering ? 1.0f : 0.0f, 0.2f);
    if (changed) repaint();
}

void ReverbScope::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const float es = ui::editorScale (*this);
    const float rad = 10.0f * es;
    if (compact)
    {
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (b, rad);
        g.setColour (ui::line.interpolatedWith (ui::ember, 0.5f * hover));
        g.drawRoundedRectangle (b.reduced (0.5f), rad, std::max (1.0f, es));
    }
    const auto area = compact ? b.reduced (8.0f * es, 6.0f * es).withTrimmedBottom (14.0f * es) : b.reduced (4.0f * es, 6.0f * es);
    const float cy = area.getCentreY(), half = area.getHeight() * 0.5f;

    // fine ligne centrale
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.fillRect (juce::Rectangle<float> (area.getX(), cy - 0.5f * es, area.getWidth(), std::max (1.0f, es)));

    const int bars = compact ? 34 : 120, stride = compact ? 6 : 4;
    const int H = mini::VoiceReverb::kHistory;
    const int newest = rev.getHistoryPos() - 1;
    const float slotW = area.getWidth() / (float) bars;
    const float sub = (float) ((newest + 1) % stride) / (float) stride;   // décalage fin entre deux barres : défilement fluide
    auto h01 = [] (float v) { return v > 1.0e-6f ? std::pow (juce::jlimit (0.0f, 1.0f, (20.0f * std::log10 (v) + 54.0f) / 54.0f), 1.25f) : 0.0f; };
    for (int i = 0; i < bars; ++i)
    {
        float dry = 0.0f, wet = 0.0f, pre = 0.0f;
        const int last = newest - (bars - 1 - i) * stride - ((newest + 1) % stride);
        for (int s = 0; s < stride; ++s)
        {
            float d, w, p;
            if (frozen)
            {
                const float t = (float) (i * stride + s) / (float) (bars * stride);
                const float phrase = std::max (0.0f, std::sin (t * 7.5f)) * (0.6f + 0.4f * std::sin (t * 90.0f));
                d = 0.35f * phrase;
                w = 0.05f + 0.06f * std::max (0.0f, std::sin (t * 7.5f - 0.6f));
                p = w * 1.5f;
            }
            else rev.getHistory ((last - s + H) % H, d, w, p);
            dry = std::max (dry, d); wet = std::max (wet, w); pre = std::max (pre, p);
        }
        const float x = area.getX() + ((float) i + 0.5f - sub) * slotW;
        if (x < area.getX() || x > area.getRight()) continue;
        const float bw = std::max (1.2f, slotW * (compact ? 0.5f : 0.42f));
        auto bar = [&] (float v01, juce::Colour col)
        {
            if (v01 <= 0.002f) return;
            const float hh = std::max (bw * 0.5f, v01 * half);
            g.setColour (col);
            g.fillRoundedRectangle (juce::Rectangle<float> (bw, 2.0f * hh).withCentre ({ x, cy }), bw * 0.5f);
        };
        const float fade = 0.35f + 0.65f * (float) i / (float) bars;   // le passé s'estompe vers la gauche
        if (pre > wet * 1.15f) bar (h01 (pre), ui::lilac.withAlpha (0.16f * fade));                 // ce que CLEAN / DUCKING retirent
        bar (h01 (wet), ui::ember.interpolatedWith (ui::lilac, 0.25f).withAlpha (0.75f * fade));    // la queue entendue
        bar (h01 (dry) * 0.92f, ui::bone.withAlpha (0.85f * fade));                                  // la voix
    }

    if (compact)
    {
        g.setColour (ui::haze.interpolatedWith (ui::bone, hover));
        g.setFont (ui::font (ui::Face::body, 9.5f * es, true, 0.16f));
        g.drawText ("MORE", b.withTop (b.getBottom() - 16.0f * es).withTrimmedBottom (3.0f * es), juce::Justification::centred, false);
    }
}

//==============================================================================
VoiceOrb::VoiceOrb (OniriqueVocalMiniAudioProcessor& p)
    : processor (p),
      comp (p.apvts, ids::comp, "COMP", MiniKnob::Size::large, false,
            "One-knob vocal compression in two stages: an LA-2A style leveler holds the body of the voice (LEVEL), then a 1176 style "
            "stage and a soft catch hold the peaks (PEAK). AUTO GAIN keeps the level the same: you hear the voice get denser, not "
            "louder. Low: discreet. Around 40-55 %: dense and upfront. High: modern, assertive compression."),
      multi (p.apvts, ids::multi, "MULTIBAND", MiniKnob::Size::medium, false,
             "How much of the 4-band compression is applied (body, chest, presence, shine). Each band compresses only what goes "
             "over its threshold; open BANDS to set thresholds, ratios, times and band gains.")
{
    for (auto* k : { &comp, &multi })
    {
        addAndMakeVisible (*k);
        k->setBufferedToImage (true);   // l'orbe se redessine à chaque image : les potards gardent leur image
        kids.push_back (k);
    }
    addAndMakeVisible (autoGain);
    addAndMakeVisible (bandsButton);
    kids.push_back (&autoGain);
    kids.push_back (&bandsButton);
    autoGain.setTooltip ("AUTO GAIN: the compression's make-up gain is set for you, calibrated so the voice does not get louder "
                         "when you turn COMP up (fair comparison). Off: no make-up, the compressed voice comes out lower.");
    bandsButton.setTooltip ("Open the 4 bands: threshold, ratio, attack, release, knee and gain of each band, with live meters.");
    autoGainAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, ids::compAuto, autoGain);

    auto& chain = processor.getChain();
    comp.activity = [&chain] { return chain.getCompReductionDb() / 10.0f; };
    multi.activity = [&chain]
    {
        float m = 0.0f;
        for (int b = 0; b < 4; ++b) m = std::max (m, chain.getMultibandReductionDb (b));
        return m / 6.0f;
    };
    fifo.assign ((size_t) kN, 0.0f);
    window.resize ((size_t) kN);
    for (int i = 0; i < kN; ++i) window[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * kPi * (float) i / (float) (kN - 1));
    data.assign ((size_t) kN * 2, 0.0f);
    setInterceptsMouseClicks (false, true);
}

juce::Point<float> VoiceOrb::centre() const noexcept
{
    const auto k = comp.getBounds().toFloat();
    return { k.getCentreX(), k.getY() + k.getWidth() * 0.5f };
}

float VoiceOrb::ringInner() const noexcept { return comp.getWidth() * 0.5f * 1.04f; }

void VoiceOrb::resized()
{
    const auto b = getLocalBounds().toFloat();
    const float u = b.getWidth() / 392.0f;
    const float D = 178.0f * u;
    comp.setBounds (juce::Rectangle<float> (D, D * 1.17f).withCentre ({ b.getCentreX(), b.getY() + 170.0f * u + D * 0.085f }).toNearestInt());
    grArea = juce::Rectangle<float> (b.getCentreX() - 150.0f * u, b.getY() + 356.0f * u, 300.0f * u, 40.0f * u);
    autoGain.setBounds (juce::Rectangle<float> (112.0f * u, 28.0f * u).withCentre ({ b.getCentreX(), b.getY() + 414.0f * u }).toNearestInt());
    const float d = 78.0f * u;
    multi.setBounds (juce::Rectangle<float> (d * 1.3f, d * 1.3f).withCentre ({ b.getCentreX(), b.getBottom() - 72.0f * u }).toNearestInt());
    bandsButton.setBounds (juce::Rectangle<float> (88.0f * u, 26.0f * u).withCentre ({ b.getRight() - 56.0f * u, b.getBottom() - 22.0f * u }).toNearestInt());
    glowLayer = {};
}

void VoiceOrb::analyse()
{
    // la voix qui sort : les derniers kN échantillons, fenêtre de Hann, FFT
    float tmp[4096];
    int got = 0;
    for (int guard = 0; guard < 16; ++guard)
    {
        const int n = processor.outputTap.pull (tmp, 4096);
        if (n <= 0) break;
        got += n;
        if (n >= kN) std::copy (tmp + n - kN, tmp + n, fifo.begin());
        else
        {
            std::move (fifo.begin() + n, fifo.end(), fifo.begin());
            std::copy (tmp, tmp + n, fifo.end() - n);
        }
    }
    float e = 0.0f;
    for (int i = kN - 1024; i < kN; ++i) e += fifo[(size_t) i] * fifo[(size_t) i];
    e = std::sqrt (e / 1024.0f);
    if (got == 0) e = 0.0f;   // l'audio ne tourne plus : l'orbe s'apaise
    energy += (e > energy ? 0.5f : 0.08f) * (e - energy);
    energySlow += 0.02f * (energy - energySlow);

    std::fill (data.begin(), data.end(), 0.0f);
    for (int i = 0; i < kN; ++i) data[(size_t) i] = fifo[(size_t) i] * window[(size_t) i];
    fft.performFrequencyOnlyForwardTransform (data.data());
    const double fs = std::max (8000.0, processor.getSampleRate());
    for (int k = 0; k < kBars; ++k)
    {
        const double f0 = 90.0 * std::pow (14000.0 / 90.0, (double) k / kBars), f1 = 90.0 * std::pow (14000.0 / 90.0, (double) (k + 1) / kBars);
        const int b0 = juce::jlimit (1, kN / 2 - 1, (int) (f0 * kN / fs)), b1 = juce::jlimit (b0 + 1, kN / 2, (int) (f1 * kN / fs) + 1);
        float m = 0.0f;
        for (int i = b0; i < b1; ++i) m = std::max (m, data[(size_t) i]);
        // les aigus d'une voix sont plus faibles : pente de +3 dB/octave pour un anneau équilibré
        const float db = 20.0f * std::log10 (m / (float) kN * 4.0f + 1.0e-7f) + 3.0f * std::log2 ((float) (f0 / 400.0));
        bars[(size_t) k] = juce::jlimit (0.0f, 1.0f, (db + 72.0f) / 58.0f);
    }
}

void VoiceOrb::tick()
{
    for (auto* k : kids) k->tick();
    clock += 1.0 / 60.0;
    auto& chain = processor.getChain();
    if (! frozen)
    {
        analyse();
        for (int k = 0; k < kBars; ++k)
        {
            auto& s = barsShown[(size_t) k];
            const float t = bars[(size_t) k];
            s += (t > s ? 0.55f : 0.10f) * (t - s);
        }
        // étincelles : sur une syllabe forte, quelques points de lumière quittent l'anneau
        if (energy > 0.015f && energy > energySlow * 1.45f && sparks.size() < 36)
            for (int n = 0; n < 2 + rnd.nextInt (3); ++n)
                sparks.push_back ({ rnd.nextFloat() * 2.0f * kPi, 1.45f + 0.1f * rnd.nextFloat(), 0.004f + 0.006f * rnd.nextFloat(), 1.0f,
                                    0.6f + 0.9f * rnd.nextFloat(), (rnd.nextFloat() - 0.5f) * 0.004f });
        for (auto& s : sparks) { s.radius += s.speed; s.angle += s.drift; s.life -= 1.0f / 75.0f; }
        sparks.erase (std::remove_if (sparks.begin(), sparks.end(), [] (const Spark& s) { return s.life <= 0.0f; }), sparks.end());
        breath += 0.06f * (juce::jlimit (0.0f, 1.0f, energy * 6.0f) - breath);
        auto follow = [] (float& v, float target) { v += (target > v ? 0.35f : 0.12f) * (target - v); };
        follow (grShown, chain.getCompReductionDb());
        follow (levelShown, chain.getCompLevelDb());
        follow (peakShown, chain.getCompPeakDb());
        for (int b = 0; b < 4; ++b) follow (bandGr[(size_t) b], chain.getMultibandReductionDb (b));
    }
    else
    {
        // visuel de démo : une voix immobile, crédible
        for (int k = 0; k < kBars; ++k)
            barsShown[(size_t) k] = juce::jlimit (0.0f, 1.0f, 0.62f - 0.42f * std::pow ((float) k / kBars, 1.3f) + 0.12f * std::sin ((float) k * 1.7f));
        breath = 0.55f; grShown = 5.0f; levelShown = 4.2f; peakShown = 1.1f;
        bandGr = { 1.5f, 1.0f, 0.8f, 2.0f };
    }
    repaint();
}

void VoiceOrb::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const auto c = centre();
    const float r0 = ringInner();
    const float u = b.getWidth() / 392.0f;

    // ---- halo de fond (image prête, à la résolution de l'écran), son intensité suit la voix
    const float ps = juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    if (! glowLayer.isValid() || std::abs (ps - layerScale) > 1.0e-3f)
    {
        layerScale = ps;
        glowLayer = juce::Image (juce::Image::ARGB, juce::jmax (1, juce::roundToInt (b.getWidth() * ps)),
                                 juce::jmax (1, juce::roundToInt (b.getHeight() * ps)), true, juce::SoftwareImageType());
        juce::Graphics lg (glowLayer);
        lg.addTransform (juce::AffineTransform::scale (ps));
        softGlow (lg, c, r0 * 2.5f, ui::blood.brighter (0.5f), 0.30f);
        softGlow (lg, c, r0 * 1.55f, ui::ember, 0.20f);
    }
    g.setOpacity (0.40f + 0.60f * breath);
    g.drawImageTransformed (glowLayer, juce::AffineTransform::scale (1.0f / layerScale));
    g.setOpacity (1.0f);

    // ---- orbites : deux anneaux fins qui tournent lentement
    for (int o = 0; o < 2; ++o)
    {
        const float rr = r0 * (o == 0 ? 1.66f : 1.86f);
        const float rot = (float) clock * (o == 0 ? 0.10f : -0.06f);
        juce::Path arc;
        for (int s = 0; s < 6; ++s)
        {
            const float a0 = rot + (float) s * kPi / 3.0f;
            arc.addCentredArc (c.x, c.y, rr, rr, 0.0f, a0, a0 + kPi / 3.0f * 0.6f, true);
        }
        g.setColour (ui::lilac.withAlpha (o == 0 ? 0.08f : 0.045f));
        g.strokePath (arc, juce::PathStrokeType (std::max (1.0f, 1.0f * u)));
    }

    // ---- anneau de spectre : la voix, des graves (en bas) aux aigus (en haut), en miroir ; ouvert en bas
    const float L = r0 * 0.50f, gapHalf = 0.58f;
    for (int k = 0; k < kBars; ++k)
    {
        const float v = barsShown[(size_t) k];
        const float a = kPi - gapHalf - ((float) k + 0.5f) / (float) kBars * (kPi - gapHalf);
        const float len = std::max (1.5f * u, L * v);
        const auto col = ui::ember.interpolatedWith (ui::bone, std::pow ((float) k / kBars, 1.2f) * 0.8f);
        for (int side = 0; side < 2; ++side)
        {
            const float aa = side == 0 ? a : -a;
            const auto p0 = onCircle (c, r0 + 5.0f * u, aa), p1 = onCircle (c, r0 + 5.0f * u + len, aa);
            g.setColour (col.withAlpha (0.08f + 0.16f * v));
            g.drawLine ({ p0, p1 }, 5.0f * u);
            g.setColour (col.withAlpha (0.30f + 0.70f * v));
            g.drawLine ({ p0, p1 }, 2.0f * u);
        }
    }

    // ---- anneau de compression : se resserre et s'allume quand la voix est tenue (réduction totale réelle)
    {
        const float gr = juce::jlimit (0.0f, 1.0f, grShown / 12.0f);
        const float rr = r0 - 1.0f * u - 3.0f * u * gr;
        juce::Path ring;
        ring.addCentredArc (c.x, c.y, rr, rr, 0.0f, kStart, kStart + (kEnd - kStart) * (0.03f + 0.97f * gr), true);
        ui::strokeGlow (g, ring, ui::ember, (1.8f + 2.6f * gr) * u, 0.25f + 0.75f * gr);
    }

    // ---- étincelles : points de lumière
    for (const auto& s : sparks)
    {
        const auto p = onCircle (c, r0 * s.radius, s.angle);
        const float sz = s.size * 2.2f * u * (0.5f + 0.5f * s.life);
        softGlowIn (g, b, p, sz * 3.2f, ui::ember, 0.40f * s.life);
        g.setColour (ui::bone.withAlpha (0.85f * s.life * s.life));
        g.fillEllipse (juce::Rectangle<float> (sz, sz).withCentre (p));
    }

    // ---- réduction de gain : LEVEL (niveleur) et PEAK (crêtes), valeurs réelles du moteur
    {
        const auto a = grArea;
        const float rowH = a.getHeight() * 0.5f;
        const char* names[2] { "LEVEL", "PEAK" };
        const float vals[2] { levelShown, peakShown };
        const juce::Colour cols[2] { ui::ember, ui::lilac };
        for (int i = 0; i < 2; ++i)
        {
            auto row = juce::Rectangle<float> (a.getX(), a.getY() + rowH * (float) i, a.getWidth(), rowH);
            const auto nameR = row.removeFromLeft (54.0f * u), valR = row.removeFromRight (62.0f * u);
            g.setColour (ui::haze);
            g.setFont (ui::font (ui::Face::mono, 9.5f * u, true, 0.12f));
            g.drawText (names[i], nameR, juce::Justification::centredLeft, false);
            const auto track = row.withSizeKeepingCentre (row.getWidth(), 5.0f * u);
            g.setColour (juce::Colours::black.withAlpha (0.45f));
            g.fillRoundedRectangle (track, track.getHeight() * 0.5f);
            const float v = juce::jlimit (0.0f, 1.0f, vals[i] / 12.0f);
            if (v > 0.002f)
            {
                // la réduction grandit depuis la droite (comme une aiguille de VU qui recule)
                const auto fill = track.withLeft (track.getRight() - std::max (track.getHeight(), track.getWidth() * v));
                g.setGradientFill (juce::ColourGradient (cols[i].withAlpha (0.55f), fill.getX(), 0.0f, cols[i], fill.getRight(), 0.0f, false));
                g.fillRoundedRectangle (fill, track.getHeight() * 0.5f);
            }
            for (float d : { 3.0f, 6.0f, 9.0f })
            {
                const float x = track.getRight() - track.getWidth() * d / 12.0f;
                g.setColour (juce::Colours::white.withAlpha (0.12f));
                g.fillRect (juce::Rectangle<float> (std::max (1.0f, u), track.getHeight() + 4.0f * u).withCentre ({ x, track.getCentreY() }));
            }
            g.setColour (vals[i] > 0.1f ? ui::mist : ui::haze);
            g.setFont (ui::font (ui::Face::mono, 10.5f * u, true));
            g.drawText (dbText (-vals[i]), valR, juce::Justification::centredRight, false);
        }
    }

    // ---- MULTIBAND : la réduction des quatre bandes, de part et d'autre du potard
    {
        const auto m = multi.getBounds().toFloat();
        const char* names[4] { "BODY", "CHEST", "PRES", "SHINE" };
        const float bw = 5.0f * u, bh = m.getHeight() * 0.46f, gap = 14.0f * u;
        for (int k = 0; k < 4; ++k)
        {
            const bool leftSide = k < 2;
            const float x = leftSide ? m.getX() - gap - bw - (float) (1 - k) * 36.0f * u : m.getRight() + gap + (float) (k - 2) * 36.0f * u;
            const auto track = juce::Rectangle<float> (bw, bh).withPosition (x, m.getY() + m.getHeight() * 0.10f);
            g.setColour (juce::Colours::black.withAlpha (0.45f));
            g.fillRoundedRectangle (track, bw * 0.5f);
            const float v = juce::jlimit (0.0f, 1.0f, bandGr[(size_t) k] / 8.0f);
            if (v > 0.0f)
            {
                g.setColour (ui::ember.interpolatedWith (ui::lilac, 0.3f).withAlpha (0.45f + 0.55f * v));
                g.fillRoundedRectangle (track.withTrimmedBottom (track.getHeight() * (1.0f - v)), bw * 0.5f);   // la réduction descend du haut
            }
            g.setColour (ui::haze.withAlpha (0.85f));
            g.setFont (ui::font (ui::Face::mono, std::max (7.0f, 8.0f * u), true, 0.06f));
            const auto lr = juce::Rectangle<float> (42.0f * u, 12.0f * u).withCentre ({ track.getCentreX(), track.getBottom() + 10.0f * u });
            g.drawText (names[k], lr, juce::Justification::centred, false);
        }
    }
}

//==============================================================================
namespace
{
const char* kBandNames[4] { "BODY", "CHEST", "PRES", "SHINE" };
const char* kBandRanges[4] { "< 180 Hz", "180 Hz - 1.2 kHz", "1.2 - 5 kHz", "> 5 kHz" };
} // namespace

BandPanel::BandPanel (OniriqueVocalMiniAudioProcessor& p) : processor (p)
{
    auto& st = p.apvts;
    for (int b = 0; b < 4; ++b)
    {
        auto& col = cols[(size_t) b];
        auto make = [&] (const char* what, const char* title, bool bip, const juce::String& tip)
        { return std::make_unique<MiniKnob> (st, ids::band (what, b), title, MiniKnob::Size::small, bip, tip); };
        col.thr = make ("mbThr", "THRESH", false, "Threshold of this band's detector (dBFS): only what this band sends over it is compressed. "
                                                  "The white line on the meter.");
        col.ratio = make ("mbRatio", "RATIO", false, "How hard this band is compressed above its threshold.");
        col.atk = make ("mbAttack", "ATTACK", false, "How fast the band is caught when it goes over.");
        col.rel = make ("mbRelease", "RELEASE", false, "How fast the band comes back once it is under the threshold again.");
        col.knee = make ("mbKnee", "KNEE", false, "Width of the soft knee around the threshold: wider = smoother, more transparent onset.");
        col.gain = make ("mbGain", "GAIN", true, "Output level of this band, after its compression (-12 to +12 dB).");
        col.on = std::make_unique<PillToggle> ("ON", PillToggle::Style::small);
        col.on->setTooltip ("Band on / off. Off: no compression and no gain on this band, it passes untouched.");
        col.onAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (st, ids::band ("mbOn", b), *col.on);
        for (auto* k : { col.thr.get(), col.ratio.get(), col.atk.get(), col.rel.get(), col.knee.get(), col.gain.get() })
        {
            addAndMakeVisible (*k);
            kids.push_back (k);
        }
        addAndMakeVisible (*col.on);
        kids.push_back (col.on.get());
    }
    addAndMakeVisible (close);
    kids.push_back (&close);
    amountParam = st.getParameter (ids::multi);
}

void BandPanel::resized()
{
    const auto b = getLocalBounds().toFloat();
    const float u = b.getWidth() / 684.0f;
    close.setBounds (juce::Rectangle<float> (b.getRight() - 96.0f * u, b.getY() + 14.0f * u, 80.0f * u, 26.0f * u).toNearestInt());
    const float top = 58.0f * u, colW = (b.getWidth() - 24.0f * u) / 4.0f;
    for (int i = 0; i < 4; ++i)
    {
        auto& col = cols[(size_t) i];
        const float x = 12.0f * u + colW * (float) i;
        col.on->setBounds (juce::Rectangle<float> (x + colW - 66.0f * u, top + 4.0f * u, 56.0f * u, 26.0f * u).toNearestInt());
        col.meter = juce::Rectangle<float> (x + 14.0f * u, top + 58.0f * u, 16.0f * u, b.getHeight() - top - 84.0f * u);
        const float gx = x + 38.0f * u, gw = colW - 46.0f * u, cw = gw / 2.0f;
        const float gy = top + 50.0f * u, ch = (b.getHeight() - gy - 14.0f * u) / 3.0f;
        MiniKnob* grid[6] { col.thr.get(), col.ratio.get(), col.atk.get(), col.rel.get(), col.knee.get(), col.gain.get() };
        for (int k = 0; k < 6; ++k)
        {
            const float cx = gx + cw * (float) (k % 2), cy = gy + ch * (float) (k / 2);
            grid[k]->setBounds (juce::Rectangle<float> (cx, cy, cw, ch).reduced (4.0f * u, 2.0f * u).toNearestInt());
        }
    }
}

void BandPanel::tick()
{
    for (auto* k : kids) k->tick();
    auto& chain = processor.getChain();
    for (int b = 0; b < 4; ++b)
    {
        auto& col = cols[(size_t) b];
        static constexpr float demoL[4] { -24.0f, -14.5f, -26.0f, -35.0f }, demoR[4] { 1.8f, 0.9f, 1.3f, 2.6f };
        const float l = frozen ? demoL[b] : chain.getMultibandLevelDb (b), r = frozen ? demoR[b] : chain.getMultibandReductionDb (b);
        col.lvl = l > col.lvl ? l : col.lvl + 0.15f * (l - col.lvl);
        col.gr += (r > col.gr ? 0.4f : 0.15f) * (r - col.gr);
    }
    if (isShowing()) repaint (getLocalBounds().withTop (juce::roundToInt (50.0f * getWidth() / 684.0f)));
}

void BandPanel::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const float u = b.getWidth() / 684.0f;
    // panneau opaque : les rangées dessous ne transparaissent pas
    g.setColour (ui::night);
    g.fillRoundedRectangle (b, 14.0f * u);
    g.setColour (ui::deep2.withAlpha (0.55f));
    g.fillRoundedRectangle (b, 14.0f * u);
    glassPanel (g, b, 14.0f * u, u, 1.5f);

    g.setColour (ui::mist);
    g.setFont (ui::font (ui::Face::display, 17.0f * u, true, 0.06f));
    g.drawText ("MULTIBAND", juce::Rectangle<float> (18.0f * u, 10.0f * u, 260.0f * u, 24.0f * u), juce::Justification::centredLeft, false);
    const float amount = amountParam != nullptr ? amountParam->convertFrom0to1 (amountParam->getValue()) : 0.0f;
    const juce::String sub = "4 BANDS  /  LINKWITZ-RILEY 24 dB/OCT  /  AMOUNT " + juce::String (juce::roundToInt (amount)) + " %"
                           + (amount < 0.5f ? "  /  TURN MULTIBAND UP TO HEAR IT" : "");
    g.setColour (amount < 0.5f ? ui::lilac : ui::haze);
    g.setFont (ui::font (ui::Face::mono, 9.0f * u, false, 0.10f));
    g.drawText (sub, juce::Rectangle<float> (18.0f * u, 32.0f * u, b.getWidth() - 140.0f * u, 14.0f * u), juce::Justification::centredLeft, false);

    const float top = 58.0f * u, colW = (b.getWidth() - 24.0f * u) / 4.0f;
    auto& st = processor.apvts;
    for (int i = 0; i < 4; ++i)
    {
        const auto& col = cols[(size_t) i];
        const float x = 12.0f * u + colW * (float) i;
        if (i > 0)
        {
            g.setColour (ui::line);
            g.fillRect (juce::Rectangle<float> (x, top, std::max (1.0f, u), b.getHeight() - top - 14.0f * u));
        }
        const bool on = col.on->getToggleState();
        g.setColour (on ? ui::mist : ui::haze);
        g.setFont (ui::font (ui::Face::display, 12.5f * u, true, 0.08f));
        g.drawText (kBandNames[i], juce::Rectangle<float> (x + 14.0f * u, top, colW - 80.0f * u, 18.0f * u), juce::Justification::centredLeft, false);
        g.setColour (ui::haze);
        g.setFont (ui::font (ui::Face::mono, 8.5f * u, false, 0.06f));
        g.drawText (kBandRanges[i], juce::Rectangle<float> (x + 14.0f * u, top + 18.0f * u, colW - 80.0f * u, 14.0f * u), juce::Justification::centredLeft, false);

        // mètre : niveau de la bande (rose pâle), seuil (trait blanc), réduction (rouge, depuis le haut)
        const auto m = col.meter;
        auto y01 = [&m] (float db) { return m.getBottom() - m.getHeight() * juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f); };
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRoundedRectangle (m, m.getWidth() * 0.35f);
        if (col.lvl > -60.0f)
        {
            const auto lv = m.withTop (y01 (col.lvl)).reduced (3.0f * u, 0.0f);
            g.setColour (ui::lilac.withAlpha (0.22f));
            g.fillRoundedRectangle (lv, 2.0f * u);
            g.setColour (ui::lilac.withAlpha (0.85f));
            g.fillRect (lv.withHeight (std::max (1.5f, 2.0f * u)));   // le niveau : un trait clair au sommet
        }
        if (col.gr > 0.05f)
        {
            const auto gr = m.withHeight (m.getHeight() * juce::jlimit (0.0f, 1.0f, col.gr / 24.0f));
            g.setColour (ui::ember.withAlpha (0.85f));
            g.fillRoundedRectangle (gr, m.getWidth() * 0.35f);
        }
        if (auto* tp = st.getParameter (ids::band ("mbThr", i)))
        {
            const float ty = y01 (tp->convertFrom0to1 (tp->getValue()));
            g.setColour (ui::bone);
            g.fillRect (juce::Rectangle<float> (m.getX() - 3.0f * u, ty - 0.75f * u, m.getWidth() + 6.0f * u, std::max (1.5f, 1.5f * u)));
        }
        g.setColour (col.gr > 0.1f ? ui::mist : ui::haze);
        g.setFont (ui::font (ui::Face::mono, 8.5f * u, true));
        g.drawText (dbText (-col.gr), juce::Rectangle<float> (m.getCentreX() - 30.0f * u, m.getBottom() + 4.0f * u, 60.0f * u, 12.0f * u), juce::Justification::centred, false);
    }
}

//==============================================================================
ReverbPanel::ReverbPanel (OniriqueVocalMiniAudioProcessor& p)
    : scope (p.getChain().getReverb(), false), processor (p),
      mode (p.apvts, ids::revMode, prm::revModeNames(),
            { "ROOM: a small room, strong early reflections: thickens and places the voice.",
              "PLATE 1: a bright, dense plate, no separate echoes: smooth and present.",
              "PLATE 2: a darker, wider, more modulated plate: soft and enveloping.",
              "HALL: a large hall, distant reflections, a deep tail.",
              "SHIMMER: the octave above is fed back into the tail: an airy, harmonic veil." }),
      pulseRate (p.apvts, ids::revPulseRate, "PULSE RATE", "Note value of one PULSE breath, locked to your project's tempo.")
{
    auto& st = p.apvts;
    struct K { const char* id; const char* title; const char* tip; };
    const K list[] {
        { ids::revMix, "MIX", "How much voice goes into the reverb (aux send): the dry voice always stays whole." },
        { ids::revDecay, "DECAY", "Reverb time (RT60), 0.2 to 12 s." },
        { ids::revPre, "PRE-DELAY", "Time before the reverb starts: keeps the attack and intelligibility of the voice." },
        { ids::revSize, "SIZE", "Size of the space. Moving it glides smoothly, without clicks." },
        { ids::revDiff, "DIFFUSION", "Low: separate echoes, grainy. High: a dense, smooth tail." },
        { ids::revRate, "MOD RATE", "Speed of the tail's modulation." },
        { ids::revDepth, "MOD DEPTH", "Depth of the modulation: breaks metallic resonances; the pitch of the tail never wobbles." },
        { ids::revLowCut, "LOW CUT", "Low cut on the tail only (the dry voice is never touched)." },
        { ids::revHiCut, "HIGH CUT", "High cut on the tail only." },
        { ids::revWidth, "WIDTH", "Stereo width of the tail. Mono-safe: nothing cancels when summed to mono." },
        { ids::revDuck, "DUCKING", "The tail steps back under the voice and comes back in the gaps, without pumping." },
        { ids::revClean, "CLEAN", "Each new phrase starts clean: the previous tail is emptied when you start singing again, "
                                  "sibilants never go into the reverb, low-mids are carved out of the tail." },
        { ids::revPulse, "PULSE", "The tail breathes in time with your project (locked to the bar when the DAW plays)." },
    };
    for (const auto& k : list)
    {
        auto knob = std::make_unique<MiniKnob> (st, k.id, k.title, MiniKnob::Size::small, false, k.tip);
        addAndMakeVisible (*knob);
        kids.push_back (knob.get());
        knobs.push_back (std::move (knob));
    }
    auto& chain = p.getChain();
    knobs[0]->activity = [&chain] { return chain.getReverbTail() * 8.0f; };
    knobs[11]->activity = [this] { return scope.cleaned(); };
    knobs[10]->activity = [this] { return scope.ducked(); };
    mode.setTitle ("MODE");
    for (juce::Component* c : { (juce::Component*) &mode, (juce::Component*) &pulseRate, (juce::Component*) &close, (juce::Component*) &scope })
        addAndMakeVisible (*c);
    for (Animated* a : { (Animated*) &mode, (Animated*) &pulseRate, (Animated*) &close, (Animated*) &scope }) kids.push_back (a);
}

void ReverbPanel::resized()
{
    const auto b = getLocalBounds().toFloat();
    const float u = b.getWidth() / 684.0f;
    close.setBounds (juce::Rectangle<float> (b.getRight() - 96.0f * u, b.getY() + 14.0f * u, 80.0f * u, 26.0f * u).toNearestInt());
    mode.setBounds (juce::Rectangle<float> (150.0f * u, 14.0f * u, b.getWidth() - 270.0f * u, 26.0f * u).toNearestInt());
    scope.setBounds (juce::Rectangle<float> (16.0f * u, 54.0f * u, b.getWidth() - 32.0f * u, 168.0f * u).toNearestInt());
    const float top = 262.0f * u, rowH = (b.getHeight() - top - 10.0f * u) / 2.0f, slot = (b.getWidth() - 24.0f * u) / 7.0f;
    for (int i = 0; i < 14; ++i)
    {
        const int row = i / 7, col = i % 7;
        const auto cell = juce::Rectangle<float> (12.0f * u + slot * (float) col, top + rowH * (float) row, slot, rowH).reduced (6.0f * u, 6.0f * u);
        if (i < (int) knobs.size()) knobs[(size_t) i]->setBounds (cell.toNearestInt());
        else pulseRate.setBounds (cell.withSizeKeepingCentre (cell.getWidth(), std::min (cell.getHeight(), 62.0f * u)).toNearestInt());
    }
}

void ReverbPanel::tick()
{
    for (auto* k : kids) k->tick();
    bool changed = ui::approach (cleanLit, scope.cleaned(), 0.15f);
    changed |= ui::approach (duckLit, scope.ducked(), 0.15f);
    if (changed) repaint (getLocalBounds().withTop (juce::roundToInt (222.0f * getWidth() / 684.0f)).withHeight (juce::roundToInt (40.0f * getWidth() / 684.0f)));
}

void ReverbPanel::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const float u = b.getWidth() / 684.0f;
    // panneau opaque : les rangées dessous ne transparaissent pas
    g.setColour (ui::night);
    g.fillRoundedRectangle (b, 14.0f * u);
    g.setColour (ui::deep2.withAlpha (0.55f));
    g.fillRoundedRectangle (b, 14.0f * u);
    glassPanel (g, b, 14.0f * u, u, 1.5f);

    g.setColour (ui::mist);
    g.setFont (ui::font (ui::Face::display, 17.0f * u, true, 0.06f));
    g.drawText ("REVERB", juce::Rectangle<float> (18.0f * u, 14.0f * u, 130.0f * u, 26.0f * u), juce::Justification::centredLeft, false);

    // légende et témoins, sous la visualisation (comme sur un rack)
    const float ly = 230.0f * u;
    float x = 20.0f * u;
    struct Item { const char* name; juce::Colour col; };
    for (const auto& it : { Item { "VOICE", ui::bone }, Item { "TAIL", ui::ember.interpolatedWith (ui::lilac, 0.25f) }, Item { "CLEANED", ui::lilac.withAlpha (0.45f) } })
    {
        g.setColour (it.col);
        g.fillRoundedRectangle (juce::Rectangle<float> (x, ly + 6.0f * u, 14.0f * u, 2.0f * u), u);
        g.setColour (ui::haze);
        g.setFont (ui::font (ui::Face::mono, 9.0f * u, true, 0.14f));
        const float w = ui::textWidth (ui::font (ui::Face::mono, 9.0f * u, true, 0.14f), it.name);
        g.drawText (it.name, juce::Rectangle<float> (x + 20.0f * u, ly, w + 4.0f * u, 14.0f * u), juce::Justification::centredLeft, false);
        x += 20.0f * u + w + 22.0f * u;
    }
    g.setColour (ui::line);
    g.fillRect (juce::Rectangle<float> (16.0f * u, ly + 20.0f * u, b.getWidth() - 32.0f * u, std::max (1.0f, u)));
    auto status = [&] (const juce::String& text, float lit, float right)
    {
        const auto f = ui::font (ui::Face::mono, 9.0f * u, true, 0.14f);
        const float w = ui::textWidth (f, text);
        const auto tr = juce::Rectangle<float> (right - w, ly, w + 2.0f * u, 14.0f * u);
        const auto led = juce::Rectangle<float> (6.0f * u, 6.0f * u).withCentre ({ tr.getX() - 10.0f * u, tr.getCentreY() });
        softGlow (g, led.getCentre(), 9.0f * u, ui::mint, 0.5f * lit);
        g.setColour (juce::Colour (0xff3A3133).interpolatedWith (ui::mint, lit));
        g.fillEllipse (led);
        g.setColour (ui::haze.interpolatedWith (ui::mist, lit));
        g.setFont (f);
        g.drawText (text, tr, juce::Justification::centredLeft, false);
        return tr.getX() - 26.0f * u;
    };
    const float r1 = status ("DUCKING", duckLit, b.getRight() - 20.0f * u);
    status ("TAILS CLEANED", cleanLit, r1);
}
