#include "MiniUI.h"
#include "OniriqueKnob.h"

using namespace onirique;

namespace
{
constexpr float kPi = juce::MathConstants<float>::pi;
constexpr float kStart = -2.42f, kEnd = 2.42f;   // course des potards (radians, 0 en haut)
const juce::Colour sleepColour { 0xff5E4F52 };   // potard au repos : gris rosé, sans lueur

juce::Point<float> onCircle (juce::Point<float> c, float r, float a) { return { c.x + std::sin (a) * r, c.y - std::cos (a) * r }; }

// halo doux (dégradé radial) centré sur c
void softGlow (juce::Graphics& g, juce::Point<float> c, float r, juce::Colour col, float alpha)
{
    if (alpha <= 0.003f || r < 1.0f) return;
    juce::ColourGradient grad (col.withAlpha (alpha), c.x, c.y, col.withAlpha (0.0f), c.x + r, c.y, true);
    grad.addColour (0.45, col.withAlpha (alpha * 0.45f));
    g.setGradientFill (grad);
    g.fillEllipse (juce::Rectangle<float> (2.0f * r, 2.0f * r).withCentre (c));
}
} // namespace

//==============================================================================
MiniKnob::MiniKnob (juce::AudioProcessorValueTreeState& apvts, const char* paramId, const juce::String& t, Size s, bool bip,
                    const juce::String& tooltip)
    : title (t), size (s), bipolar (bip)
{
    setTooltip (tooltip);
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    param = apvts.getParameter (paramId);
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
    return bipolar ? std::abs (v) < 1.0e-3f : v <= 1.0e-3f;
}

juce::Rectangle<float> MiniKnob::knobArea() const noexcept
{
    const auto b = getLocalBounds().toFloat();
    const float labelH = b.getHeight() * (size == Size::large ? 0.17f : 0.24f);
    const float d = std::min (b.getWidth(), b.getHeight() - labelH);
    return juce::Rectangle<float> (d, d).withCentre ({ b.getCentreX(), b.getY() + d * 0.5f });
}

void MiniKnob::tick()
{
    if (param == nullptr) return;
    bool changed = ui::approach (shown, param->getValue(), 0.32f);
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

    // halo : activité (queue de reverb, réduction...) et floraison du réveil
    softGlow (g, c, R * (1.25f + 0.35f * bloom), ui::ember, 0.22f * act + 0.45f * bloom);

    const auto accent = sleepColour.interpolatedWith (ui::ember, awake);
    onirique::ui::drawKnob (g, k.reduced (R * 0.04f), shown, kStart, kEnd, accent, bipolar, glow > 0.3f);

    // nom, remplacé par la valeur au survol et pendant le réglage
    const auto b = getLocalBounds().toFloat();
    const auto labelR = b.withTop (k.getBottom() + R * (size == Size::large ? 0.04f : 0.06f));
    const float es = ui::editorScale (*this);
    const float h = size == Size::large ? std::max (15.0f * es, labelR.getHeight() * 0.62f)
                                        : std::max (10.0f * es, std::min (labelR.getHeight() * 0.62f, 14.0f * es));
    const auto titleCol = ui::haze.interpolatedWith (ui::mist, 0.35f + 0.65f * awake);
    if (valueShow < 0.99f)
    {
        g.setColour (titleCol.withMultipliedAlpha (1.0f - valueShow));
        g.setFont (ui::fitFont (size == Size::large ? ui::Face::display : ui::Face::body, h, labelR.getWidth(), title, true,
                                size == Size::large ? 0.18f : 0.12f));
        g.drawText (title, labelR.withHeight (h * 1.4f), juce::Justification::centredTop, false);
    }
    if (valueShow > 0.01f)
    {
        g.setColour (ui::bone.withAlpha (valueShow));
        g.setFont (ui::fitFont (ui::Face::mono, h * 0.95f, labelR.getWidth(), shownText));
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
    const float pillH = b.getHeight() * 0.46f, y = b.getY() + b.getHeight() * 0.18f;
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

    g.setColour (juce::Colours::black.withAlpha (0.38f));
    g.fillRoundedRectangle (pill, pill.getHeight() * 0.5f);
    g.setColour (ui::ember.withAlpha (0.10f + 0.25f * flash));
    g.fillRoundedRectangle (pill, pill.getHeight() * 0.5f);
    g.setColour (ui::line.interpolatedWith (ui::ember.withAlpha (0.7f), 0.35f + 0.65f * flash));
    g.drawRoundedRectangle (pill.reduced (0.5f), pill.getHeight() * 0.5f, std::max (1.0f, 1.2f * es));

    for (int s = 0; s < 2; ++s)
    {
        const auto a = (s == 0 ? left : right).reduced (left.getHeight() * 0.3f);
        juce::Path p;
        if (s == 0) p.addTriangle (a.getRight(), a.getY(), a.getRight(), a.getBottom(), a.getX(), a.getCentreY());
        else        p.addTriangle (a.getX(), a.getY(), a.getX(), a.getBottom(), a.getRight(), a.getCentreY());
        g.setColour (ui::ember.interpolatedWith (ui::bone, 0.5f * glow[s]));
        g.fillPath (p);
    }

    const auto text = param->getCurrentValueAsText();
    const auto textR = pill.withTrimmedLeft (left.getWidth()).withTrimmedRight (right.getWidth()).translated (slide * 6.0f * es, 0.0f);
    g.setColour (ui::bone);
    g.setFont (ui::fitFont (ui::Face::mono, pill.getHeight() * 0.5f, textR.getWidth(), text, true));
    g.drawText (text, textR, juce::Justification::centred, false);

    const auto titleR = b.withTop (pill.getBottom() + 4.0f * es);
    g.setColour (ui::mist.withAlpha (0.85f));
    g.setFont (ui::fitFont (ui::Face::body, std::max (10.0f * es, std::min (titleR.getHeight() * 0.62f, 14.0f * es)),
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
    setTooltip (best ? "Input level. Keep the voice in the BEST zone (AUTO INPUT does it for you): the compression, de-esser and "
                       "drive are tuned for it."
                     : "Output level (peak, dBFS). VOCAL MINI never goes over 0 dBFS.");
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
    const auto track = b.withSizeKeepingCentre (b.getWidth(), std::max (6.0f, b.getHeight() * 0.34f)).withY (b.getBottom() - std::max (6.0f, b.getHeight() * 0.34f));
    const float rad = track.getHeight() * 0.5f;
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillRoundedRectangle (track, rad);

    if (showBest)
    {
        // zone BEST : là où la voix doit vivre (niveau moyen de -24 à -13 dBFS)
        const float x0 = track.getX() + track.getWidth() * norm (-24.0f), x1 = track.getX() + track.getWidth() * norm (-13.0f);
        const auto zone = juce::Rectangle<float> (x0, track.getY(), x1 - x0, track.getHeight());
        g.setColour (ui::ember.withAlpha (0.16f + 0.18f * inBest));
        g.fillRoundedRectangle (zone, rad);
        g.setColour (ui::ember.withAlpha (0.45f + 0.5f * inBest));
        g.drawLine (x0, track.getY() - 3.0f * es, x1, track.getY() - 3.0f * es, 1.2f * es);
        g.setColour (ui::bone.withAlpha (0.55f + 0.45f * inBest));
        g.setFont (ui::font (ui::Face::mono, std::max (9.0f, 10.5f * es), true, 0.18f));
        g.drawText ("BEST", juce::Rectangle<float> (x0, b.getY(), x1 - x0, track.getY() - 4.0f * es), juce::Justification::centredBottom, false);
    }

    const float fillN = norm (showBest ? avgDb : levelDb);
    if (fillN > 0.0f)
    {
        const auto fill = track.withWidth (std::max (track.getHeight(), track.getWidth() * fillN));
        juce::ColourGradient grad (ui::blood, track.getX(), 0.0f, ui::ember, track.getRight(), 0.0f, false);
        grad.addColour (0.75, ui::ember.interpolatedWith (ui::bone, 0.2f));
        g.setGradientFill (grad);
        g.fillRoundedRectangle (fill, rad);
        if (inBest > 0.01f)
        {
            g.setColour (ui::bone.withAlpha (0.18f * inBest));
            g.fillRoundedRectangle (fill.reduced (0.0f, fill.getHeight() * 0.3f), rad * 0.4f);
        }
    }
    // crête maintenue
    const float hx = track.getX() + track.getWidth() * norm (holdDb);
    if (holdDb > -60.0f)
    {
        g.setColour ((holdDb > -1.0f ? juce::Colour (0xffff5a68) : ui::bone).withAlpha (0.9f));
        g.fillRect (juce::Rectangle<float> (std::max (1.5f, 2.0f * es), track.getHeight() + 4.0f * es).withCentre ({ hx, track.getCentreY() }));
    }
}

//==============================================================================
VoiceOrb::VoiceOrb (OniriqueVocalMiniAudioProcessor& p)
    : processor (p),
      comp (p.apvts, ids::comp, "COMP", MiniKnob::Size::large, false,
            "One-knob vocal compression: a 1176 catches the peaks, then an LA-2A levels the body. The level stays the "
            "same: you hear the voice hold and get denser, not louder. Around 30-50 % for most vocals."),
      multi (p.apvts, ids::multi, "MULTIBAND", MiniKnob::Size::medium, false,
             "Vocal multiband in one knob: body, chest, presence and shine are each tamed only when they go over their "
             "usual level in THIS voice (a booming note, a boxy vowel, a harsh passage, a piercing S). The tone stays the same.")
{
    for (auto* k : { &comp, &multi })
    {
        addAndMakeVisible (*k);
        k->setBufferedToImage (true);   // l'orbe se redessine à chaque image : les potards gardent leur image
        kids.push_back (k);
    }
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
    const float D = std::min (b.getWidth() * 0.50f, b.getHeight() * 0.40f);
    comp.setBounds (juce::Rectangle<float> (D, D * 1.2f).withCentre ({ b.getCentreX(), b.getY() + b.getHeight() * 0.40f + D * 0.1f }).toNearestInt());
    const float d = D * 0.40f;
    multi.setBounds (juce::Rectangle<float> (d * 1.35f, d * 1.32f).withCentre ({ b.getCentreX(), b.getBottom() - d * 0.78f }).toNearestInt());
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
        // étincelles : sur une syllabe forte, quelques éclats partent de l'anneau
        if (energy > 0.015f && energy > energySlow * 1.45f && sparks.size() < 40)
            for (int n = 0; n < 2 + rnd.nextInt (3); ++n)
                sparks.push_back ({ rnd.nextFloat() * 2.0f * kPi, 1.42f + 0.1f * rnd.nextFloat(), 0.004f + 0.006f * rnd.nextFloat(), 1.0f,
                                    0.6f + 0.9f * rnd.nextFloat(), (rnd.nextFloat() - 0.5f) * 0.004f });
        for (auto& s : sparks) { s.radius += s.speed; s.angle += s.drift; s.life -= 1.0f / 75.0f; }
        sparks.erase (std::remove_if (sparks.begin(), sparks.end(), [] (const Spark& s) { return s.life <= 0.0f; }), sparks.end());
        breath += 0.06f * (juce::jlimit (0.0f, 1.0f, energy * 6.0f) - breath);
        grShown += 0.25f * (chain.getCompReductionDb() - grShown);
        for (int b = 0; b < 4; ++b) bandGr[(size_t) b] += 0.25f * (chain.getMultibandReductionDb (b) - bandGr[(size_t) b]);
    }
    else
    {
        // visuel de démo : une voix immobile, crédible
        for (int k = 0; k < kBars; ++k)
            barsShown[(size_t) k] = juce::jlimit (0.0f, 1.0f, 0.62f - 0.42f * std::pow ((float) k / kBars, 1.3f) + 0.12f * std::sin ((float) k * 1.7f));
        breath = 0.55f; grShown = 4.0f;
        bandGr = { 1.5f, 2.5f, 1.0f, 2.0f };
    }
    repaint();
}

void VoiceOrb::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const auto c = centre();
    const float r0 = ringInner();
    const float es = ui::editorScale (*this);

    // ---- halo de fond (image prête, à la résolution de l'écran), son intensité suit la voix
    const float ps = juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    if (! glowLayer.isValid() || std::abs (ps - layerScale) > 1.0e-3f)
    {
        layerScale = ps;
        glowLayer = juce::Image (juce::Image::ARGB, juce::jmax (1, juce::roundToInt (b.getWidth() * ps)),
                                 juce::jmax (1, juce::roundToInt (b.getHeight() * ps)), true, juce::SoftwareImageType());
        juce::Graphics lg (glowLayer);
        lg.addTransform (juce::AffineTransform::scale (ps));
        softGlow (lg, c, r0 * 2.4f, ui::ember, 0.20f);
        softGlow (lg, c, r0 * 1.5f, ui::blood.brighter (0.2f), 0.55f);
    }
    g.setOpacity (0.45f + 0.55f * breath);
    g.drawImageTransformed (glowLayer, juce::AffineTransform::scale (1.0f / layerScale));
    g.setOpacity (1.0f);

    // ---- orbites : deux anneaux fins qui tournent lentement
    for (int o = 0; o < 2; ++o)
    {
        const float rr = r0 * (o == 0 ? 1.62f : 1.82f);
        const float rot = (float) clock * (o == 0 ? 0.12f : -0.07f);
        juce::Path arc;
        for (int s = 0; s < 6; ++s)
        {
            const float a0 = rot + (float) s * kPi / 3.0f;
            arc.addCentredArc (c.x, c.y, rr, rr, 0.0f, a0, a0 + kPi / 3.0f * 0.62f, true);
        }
        g.setColour (ui::mist.withAlpha (o == 0 ? 0.09f : 0.05f));
        g.strokePath (arc, juce::PathStrokeType (std::max (1.0f, 1.0f * es)));
    }

    // ---- anneau de spectre : la voix, des graves (en bas) aux aigus (en haut), en miroir ; ouvert en bas sur 64° pour
    // laisser respirer le nom du potard
    const float L = r0 * 0.52f, gapHalf = 0.56f;
    for (int k = 0; k < kBars; ++k)
    {
        const float v = barsShown[(size_t) k];
        const float a = kPi - gapHalf - ((float) k + 0.5f) / (float) kBars * (kPi - gapHalf);
        const float len = std::max (1.5f * es, L * v);
        const auto col = ui::ember.interpolatedWith (ui::bone, std::pow ((float) k / kBars, 1.4f) * 0.75f);
        for (int side = 0; side < 2; ++side)
        {
            const float aa = side == 0 ? a : -a;
            const auto p0 = onCircle (c, r0 + 4.0f * es, aa), p1 = onCircle (c, r0 + 4.0f * es + len, aa);
            g.setColour (col.withAlpha (0.10f + 0.20f * v));
            g.drawLine ({ p0, p1 }, 5.5f * es);
            g.setColour (col.withAlpha (0.35f + 0.65f * v));
            g.drawLine ({ p0, p1 }, 2.2f * es);
        }
    }

    // ---- anneau de compression : se resserre et rougeoie quand la voix est tenue
    {
        const float gr = juce::jlimit (0.0f, 1.0f, grShown / 10.0f);
        const float rr = r0 - 1.0f * es - 3.0f * es * gr;
        juce::Path ring;
        ring.addCentredArc (c.x, c.y, rr, rr, 0.0f, kStart, kStart + (kEnd - kStart) * (0.04f + 0.96f * gr), true);
        ui::strokeGlow (g, ring, ui::ember, (2.0f + 3.0f * gr) * es, 0.25f + 0.75f * gr);
    }

    // ---- étincelles
    for (const auto& s : sparks)
    {
        const auto p = onCircle (c, r0 * s.radius, s.angle);   // rayon en unités de l'anneau, l'éclat s'éloigne
        const float sz = s.size * 4.5f * es * (0.5f + 0.5f * s.life);
        g.setColour (ui::bone.withAlpha (0.85f * s.life * s.life));
        g.fillPath (onirique::ui::knobStar (p, sz));
        softGlow (g, p, sz * 2.6f, ui::ember, 0.35f * s.life);
    }

    // ---- MULTIBAND : les quatre bandes, de part et d'autre du potard
    {
        const auto m = multi.getBounds().toFloat();
        const char* names[4] { "BODY", "CHEST", "PRES", "SHINE" };
        const float bw = 5.0f * es, bh = m.getHeight() * 0.42f, gap = 12.0f * es;
        for (int k = 0; k < 4; ++k)
        {
            const bool leftSide = k < 2;
            const float x = leftSide ? m.getX() - gap - bw - (float) (1 - k) * 34.0f * es : m.getRight() + gap + (float) (k - 2) * 34.0f * es;
            const auto track = juce::Rectangle<float> (bw, bh).withPosition (x, m.getY() + m.getHeight() * 0.12f);
            g.setColour (juce::Colours::black.withAlpha (0.4f));
            g.fillRoundedRectangle (track, bw * 0.5f);
            const float v = juce::jlimit (0.0f, 1.0f, bandGr[(size_t) k] / 8.0f);
            if (v > 0.0f)
            {
                g.setColour (ui::ember.withAlpha (0.4f + 0.6f * v));
                g.fillRoundedRectangle (track.withTrimmedBottom (track.getHeight() * (1.0f - v)), bw * 0.5f);   // la réduction descend du haut
            }
            g.setColour (ui::haze.withAlpha (0.8f));
            g.setFont (ui::font (ui::Face::mono, std::max (7.5f, 8.5f * es), true, 0.08f));
            const auto lr = juce::Rectangle<float> (40.0f * es, 12.0f * es).withCentre ({ track.getCentreX(), track.getBottom() + 9.0f * es });
            g.drawText (names[k], lr, juce::Justification::centred, false);
        }
    }
}
