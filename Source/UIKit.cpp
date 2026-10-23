#include "UIKit.h"
#include "OniriqueKnob.h"
#include "OniriqueWindowSize.h"
#include <BinaryData.h>
#include <OniriqueLicenseData.h>
#include <array>
#include <map>

using namespace onirique;

namespace
{
constexpr float kPi = juce::MathConstants<float>::pi;

juce::Point<float> onCircle (juce::Point<float> c, float r, float ang)
{
    return { c.x + std::sin (ang) * r, c.y - std::cos (ang) * r };
}

// pastille de valeur (boutons, curseurs, champs) : fond sombre, liseré, point braise
void drawValuePill (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, float glow, float u)
{
    const float rad = r.getHeight() * 0.5f;
    g.setColour (juce::Colours::black.withAlpha (0.34f));
    g.fillRoundedRectangle (r, rad);
    g.setColour (ui::ember.withAlpha (0.14f * glow));
    g.fillRoundedRectangle (r, rad);
    g.setColour (ui::line.interpolatedWith (ui::ember.withAlpha (0.8f), glow));
    g.drawRoundedRectangle (r.reduced (0.5f), rad, std::max (1.0f, 1.2f * u));

    const float dot = r.getHeight() * 0.22f;
    const auto dotR = juce::Rectangle<float> (dot, dot).withCentre ({ r.getX() + rad * 0.95f, r.getCentreY() });
    g.setColour (ui::ember.withAlpha (0.25f + 0.35f * glow));
    g.fillEllipse (dotR.expanded (dot * 0.6f * glow));
    g.setColour (ui::ember.interpolatedWith (ui::bone, 0.35f * glow));
    g.fillEllipse (dotR);

    const auto textR = r.withTrimmedLeft (rad * 1.5f).withTrimmedRight (rad * 0.6f);
    g.setColour (ui::mist);
    g.setFont (ui::fitFont (ui::Face::mono, r.getHeight() * 0.56f, textR.getWidth(), text));
    g.drawText (text, textR, juce::Justification::centred, false);
}
} // namespace

//==============================================================================
namespace
{
// les polices de la charte, partagées par les interfaces ouvertes (thread de l'interface uniquement) : créées à
// l'ouverture de la première, détruites à la fermeture de la dernière. Seuls un compteur et un pointeur brut sont
// statiques : rien n'est détruit au déchargement du plugin.
struct Faces
{
    juce::Typeface::Ptr display, body, mono, monoBold;
};
Faces* faces = nullptr;
int faceUsers = 0;
} // namespace

ui::FontScope::FontScope()
{
    if (faceUsers++ == 0)
    {
        faces = new Faces();
        faces->display = juce::Typeface::createSystemTypefaceFor (OniriqueLicenseData::Unbounded800_ttf, OniriqueLicenseData::Unbounded800_ttfSize);
        faces->body = juce::Typeface::createSystemTypefaceFor (OniriqueLicenseData::HankenGrotesk600_ttf, OniriqueLicenseData::HankenGrotesk600_ttfSize);
        faces->mono = juce::Typeface::createSystemTypefaceFor (OniriqueLicenseData::JetBrainsMono500_ttf, OniriqueLicenseData::JetBrainsMono500_ttfSize);
        faces->monoBold = juce::Typeface::createSystemTypefaceFor (OniriqueLicenseData::JetBrainsMono600_ttf, OniriqueLicenseData::JetBrainsMono600_ttfSize);
    }
}

ui::FontScope::~FontScope()
{
    if (--faceUsers == 0) { delete faces; faces = nullptr; }
}

juce::Font ui::font (Face f, float h, bool bold, float kerning)
{
    h = std::max (4.0f, h);
    const juce::Typeface::Ptr tf = faces == nullptr ? nullptr
                                 : f == Face::display ? faces->display : f == Face::body ? faces->body : (bold ? faces->monoBold : faces->mono);
    // Unbounded est très large : un peu plus petit que la hauteur demandée, pour garder les mêmes encombrements
    juce::Font fnt = tf != nullptr ? juce::Font (juce::FontOptions (tf).withHeight (f == Face::display ? h * 0.86f : h))
                                   : juce::Font (juce::FontOptions (f == Face::display ? "Bahnschrift" : f == Face::body ? "Segoe UI" : "Cascadia Mono",
                                                                    h, bold ? juce::Font::bold : juce::Font::plain));
    if (kerning != 0.0f) fnt.setExtraKerningFactor (kerning);
    return fnt;
}

float ui::editorScale (const juce::Component& c)
{
    if (auto* ed = c.findParentComponentOfClass<juce::AudioProcessorEditor>()) return (float) ed->getWidth() / kEditorBaseW;
    if (auto* ed = dynamic_cast<const juce::AudioProcessorEditor*> (&c)) return (float) ed->getWidth() / kEditorBaseW;
    return 1.0f;
}

float ui::textWidth (const juce::Font& f, const juce::String& t)
{
    juce::GlyphArrangement ga;
    ga.addLineOfText (f, t, 0.0f, 0.0f);
    return ga.getBoundingBox (0, -1, true).getWidth();
}

juce::Font ui::fitFont (Face face, float maxH, float maxW, const juce::String& text, bool bold, float kerning)
{
    auto f = font (face, maxH, bold, kerning);
    const float w = textWidth (f, text);
    if (w > maxW && w > 0.0f) f.setHeight (std::max (4.0f, f.getHeight() * maxW / w));
    return f;
}

void ui::strokeGlow (juce::Graphics& g, const juce::Path& p, juce::Colour c, float width, float intensity, float maxWidth)
{
    if (intensity <= 0.002f) return;
    const float widest = maxWidth > 0.0f ? juce::jlimit (width, width * 5.6f, maxWidth) : width * 5.6f;
    for (int i = 4; i >= 1; --i)
    {
        g.setColour (c.withAlpha (juce::jlimit (0.0f, 1.0f, intensity * 0.17f / (float) i)));
        g.strokePath (p, juce::PathStrokeType (width + (widest - width) * (float) i / 4.0f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
    }
}

void ui::drawCachedShadow (juce::Graphics& g, ShadowCache& shadowCache, const juce::DropShadow& ds, juce::Rectangle<float> shape, float corner)
{
    // clé : taille et position dans le pixel de la forme, coin, flou, décalage (au 1/64 de pixel près)
    using Key = std::array<int, 8>;
    auto& cache = shadowCache.images;   // thread de l'interface uniquement

    const auto ib = shape.getSmallestIntegerContainer();
    const auto q = [] (float v) { return juce::roundToInt (v * 64.0f); };
    const Key key { q (shape.getWidth()), q (shape.getHeight()), q (shape.getX() - (float) ib.getX()), q (shape.getY() - (float) ib.getY()),
                    corner < 0.0f ? -1 : q (corner), ds.radius, ds.offset.x, ds.offset.y };

    // zone de l'ombre relative au coin de la forme, comme DropShadow::drawForPath (sans le découpage par la zone à redessiner)
    const auto area = (ib.withZeroOrigin() + ds.offset).expanded (ds.radius + 1);
    auto it = cache.find (key);
    if (it == cache.end())
    {
        if (cache.size() > 512) cache.clear();
        // image en mémoire ordinaire : rien qui dépende des ressources graphiques du système (Direct2D, Core Graphics)
        juce::Image pathImage (juce::Image::SingleChannel, area.getWidth(), area.getHeight(), true, juce::SoftwareImageType());
        {
            juce::Graphics g2 (pathImage);
            g2.setColour (juce::Colours::white);
            const auto local = shape.translated ((float) (ds.offset.x - ib.getX() - area.getX()), (float) (ds.offset.y - ib.getY() - area.getY()));
            if (corner < 0.0f) g2.fillEllipse (local);
            else               g2.fillRoundedRectangle (local, corner);
        }
        juce::Image blurred;
        juce::ImageEffects::applySingleChannelBoxBlurEffect (ds.radius, pathImage, blurred);
        it = cache.emplace (key, blurred).first;
    }
    g.setColour (ds.colour);
    g.drawImageAt (it->second, ib.getX() + area.getX(), ib.getY() + area.getY(), true);
}

//==============================================================================
OniriqueLookAndFeel::OniriqueLookAndFeel()
{
    setDefaultSansSerifTypefaceName ("Segoe UI");
    setColour (juce::TooltipWindow::backgroundColourId, ui::deep2);
    setColour (juce::TooltipWindow::textColourId, ui::mist);
    setColour (juce::TooltipWindow::outlineColourId, ui::line);
    setColour (juce::PopupMenu::backgroundColourId, ui::deep2.withAlpha (0.99f));   // non opaque : coins arrondis transparents
    setColour (juce::PopupMenu::textColourId, ui::mist);
    setColour (juce::PopupMenu::headerTextColourId, ui::haze);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, ui::ember.withAlpha (0.35f));
    setColour (juce::PopupMenu::highlightedTextColourId, ui::bone);
}

void OniriqueLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                            float a0, float a1, juce::Slider& s)
{
    const auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (2.0f);
    const float D = juce::jmin (b.getWidth(), b.getHeight());
    if (D < 8.0f) return;

    const auto c = b.getCentre();
    const float R = D * 0.5f, u = D / 150.0f;
    const auto& props = s.getProperties();
    const float shown = (float) props.getWithDefault ("shown", pos);
    const float glow = (float) props.getWithDefault ("glow", 0.0f);
    const bool bipolar = (bool) props.getWithDefault ("bipolar", false);

    // ---- le potard ONIRIQUE, le même dans tous les plugins de la marque (OniriqueKnob.h) : graduations allumées
    //      jusqu'à la valeur (depuis le centre si bipolaire), corps biseauté, aiguille à l'étoile
    onirique::ui::drawKnob (g, b, shown, a0, a1, ui::ember, bipolar, s.isMouseOverOrDragging() || glow > 0.5f);

    // ---- bornes de l'échelle
    const auto minL = props["minLabel"].toString(), maxL = props["maxLabel"].toString();
    if (minL.isNotEmpty() || maxL.isNotEmpty())
    {
        g.setColour (ui::haze.withAlpha (0.8f));
        for (int side = 0; side < 2; ++side)
        {
            const auto& txt = side == 0 ? minL : maxL;
            const float es = ui::editorScale (s);   // plancher lisible même sur un petit bouton
            const auto f = ui::fitFont (ui::Face::mono, std::max (11.0f * u, 10.0f * es), std::max (0.34f * D, 30.0f * es), txt);
            const float tw = ui::textWidth (f, txt) + 2.0f;
            const auto pt = onCircle (c, R * 0.9f, side == 0 ? a0 : a1);
            const float cx = juce::jlimit (b.getX() + tw * 0.5f, b.getRight() - tw * 0.5f, pt.x);
            g.setFont (f);
            g.drawText (txt, juce::Rectangle<float> (tw, f.getHeight() * 1.3f).withCentre ({ cx, pt.y + 0.17f * R }),
                        juce::Justification::centred, false);
        }
    }
}

//==============================================================================
void OniriqueLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    const auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);
    g.setGradientFill (juce::ColourGradient (ui::deep2, 0.0f, 0.0f, ui::deep, 0.0f, (float) height, false));
    g.fillRoundedRectangle (r, 12.0f);
    g.setGradientFill (juce::ColourGradient (ui::ember.withAlpha (0.10f), (float) width * 0.8f, 0.0f,
                                             ui::ember.withAlpha (0.0f), (float) width * 0.8f, (float) height * 0.6f, true));
    g.fillRoundedRectangle (r, 12.0f);
    g.setColour (ui::line);
    g.drawRoundedRectangle (r, 12.0f, 1.0f);
}

void OniriqueLookAndFeel::getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int, int& w, int& h)
{
    if (isSeparator) { w = 50; h = 11; return; }
    w = juce::roundToInt (ui::textWidth (getPopupMenuFont(), text)) + 56;
    h = 30;
}

void OniriqueLookAndFeel::drawPopupMenuSectionHeader (juce::Graphics& g, const juce::Rectangle<int>& area, const juce::String& name)
{
    const auto r = area.toFloat().withTrimmedLeft (16.0f).withTrimmedTop (4.0f);
    const auto f = ui::font (ui::Face::mono, 10.5f, true, 0.18f);
    g.setFont (f);
    g.setColour (ui::haze);
    g.drawText (name, r, juce::Justification::centredLeft, false);
    const float tw = ui::textWidth (f, name);
    g.setColour (ui::line);
    g.fillRect (juce::Rectangle<float> (r.getX() + tw + 10.0f, r.getCentreY(), r.getWidth() - tw - 26.0f, 1.0f));
}

void OniriqueLookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                                             bool isHighlighted, bool isTicked, bool, const juce::String& text,
                                             const juce::String&, const juce::Drawable*, const juce::Colour*)
{
    if (isSeparator)
    {
        g.setColour (ui::line);
        g.fillRect (juce::Rectangle<float> (14.0f, (float) area.getCentreY(), (float) area.getWidth() - 28.0f, 1.0f));
        return;
    }

    const auto r = area.toFloat().reduced (5.0f, 2.0f);
    if (isHighlighted && isActive)
    {
        juce::Path p;
        p.addRoundedRectangle (r, r.getHeight() * 0.5f);
        ui::strokeGlow (g, p, ui::ember, 1.0f, 0.45f, 4.0f);   // 2 px de marge en haut et en bas : le halo y tient
        g.setGradientFill (juce::ColourGradient (ui::ember.withAlpha (0.38f), r.getX(), r.getY(),
                                                 ui::blood.withAlpha (0.35f), r.getRight(), r.getBottom(), false));
        g.fillPath (p);
        g.setColour (ui::ember.withAlpha (0.7f));
        g.strokePath (p, juce::PathStrokeType (1.0f));
    }

    // élément actif (preset chargé) : point braise lumineux
    const juce::Point<float> dot { r.getX() + 14.0f, r.getCentreY() };
    if (isTicked)
    {
        g.setColour (ui::ember.withAlpha (0.35f));
        g.fillEllipse (juce::Rectangle<float> (14.0f, 14.0f).withCentre (dot));
        g.setColour (ui::ember.interpolatedWith (ui::bone, 0.25f));
        g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (dot));
    }

    g.setColour (! isActive ? ui::haze.withAlpha (0.45f)
                            : (isHighlighted || isTicked ? ui::bone : ui::mist.withAlpha (0.88f)));
    g.setFont (getPopupMenuFont().withStyle (isTicked ? juce::Font::bold : juce::Font::plain));
    g.drawText (text, r.withTrimmedLeft (28.0f).withTrimmedRight (10.0f), juce::Justification::centredLeft, true);
}

// curseur vertical : la hauteur du curseur vaut D (même unité que les boutons rotatifs)
int OniriqueLookAndFeel::getSliderThumbRadius (juce::Slider& s)
{
    if (s.getSliderStyle() == juce::Slider::LinearVertical)
        return juce::roundToInt (s.getHeight() / 150.0f * 9.0f);
    if (s.getSliderStyle() == juce::Slider::LinearHorizontal)
        return juce::roundToInt (s.getHeight() * 0.32f);
    return LookAndFeel_V4::getSliderThumbRadius (s);
}

void OniriqueLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                            float minPos, float maxPos, juce::Slider::SliderStyle style, juce::Slider& s)
{
    if (style == juce::Slider::LinearHorizontal)
    {
        // curseur horizontal : rail fin, remplissage braise lumineux depuis la gauche, pastille claire
        const auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
        const float u = b.getHeight() / 30.0f, cy = b.getCentreY();
        const float r = (float) getSliderThumbRadius (s);
        const float left = b.getX() + r, right = b.getRight() - r;
        const float shown = (float) s.getProperties().getWithDefault ("shown", s.valueToProportionOfLength (s.getValue()));
        const float glow = (float) s.getProperties().getWithDefault ("glow", 0.0f);
        const float tx = left + shown * (right - left);
        const float trackH = std::max (3.0f, 4.0f * u);

        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.fillRoundedRectangle (juce::Rectangle<float> (left, cy - trackH * 0.5f, right - left, trackH), trackH * 0.5f);
        if (tx - left > 0.5f)
        {
            juce::Path fill;
            fill.startNewSubPath (left, cy);
            fill.lineTo (tx, cy);
            ui::strokeGlow (g, fill, ui::ember, trackH, 0.5f + 0.5f * glow);
            g.setGradientFill (juce::ColourGradient (ui::ember, left, cy, ui::bone, tx, cy, false));
            g.strokePath (fill, juce::PathStrokeType (trackH, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        const auto thumb = juce::Rectangle<float> (1.3f * r, 2.0f * r).withCentre ({ tx, cy });
        g.setColour (ui::ember.withAlpha (0.22f + 0.25f * glow));
        g.fillRoundedRectangle (thumb.expanded (3.0f * u), thumb.getWidth() * 0.5f + 3.0f * u);
        g.setGradientFill (juce::ColourGradient (ui::mist, thumb.getX(), thumb.getY(), juce::Colour (0xffd9c9cb), thumb.getX(), thumb.getBottom(), false));
        g.fillRoundedRectangle (thumb, thumb.getWidth() * 0.5f);
        g.setColour (ui::ember);
        g.drawLine (tx, thumb.getY() + 4.0f * u, tx, thumb.getBottom() - 4.0f * u, std::max (1.2f, 1.8f * u));
        return;
    }

    if (style != juce::Slider::LinearVertical)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, minPos, maxPos, style, s);
        return;
    }

    const auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    const float u = b.getHeight() / 150.0f, cx = b.getCentreX();
    const float thumbR = (float) getSliderThumbRadius (s);
    const float top = b.getY() + thumbR, bottom = b.getBottom() - thumbR;
    const auto& props = s.getProperties();
    const float shown = (float) props.getWithDefault ("shown", s.valueToProportionOfLength (s.getValue()));
    const float glow = (float) props.getWithDefault ("glow", 0.0f);
    const float ty = bottom - shown * (bottom - top);

    // ---- graduations : points discrets, ceux atteints s'allument
    for (int i = 0; i <= 10; ++i)
    {
        const float ti = (float) i / 10.0f;
        const float yy = bottom - ti * (bottom - top);
        const float d = (i % 5 == 0 ? 3.4f : 2.2f) * u;
        g.setColour (ti <= shown + 1.0e-3f ? ui::ember.withAlpha (0.75f) : juce::Colours::white.withAlpha (0.16f));
        for (float dx : { -13.0f * u, 13.0f * u })
            g.fillEllipse (juce::Rectangle<float> (d, d).withCentre ({ cx + dx, yy }));
    }

    // ---- rail + remplissage lumineux du bas jusqu'au curseur
    const float trackW = std::max (3.0f, 5.0f * u);
    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.fillRoundedRectangle (juce::Rectangle<float> (cx - trackW * 0.5f, top, trackW, bottom - top), trackW * 0.5f);
    if (bottom - ty > 0.5f)
    {
        juce::Path fill;
        fill.startNewSubPath (cx, bottom);
        fill.lineTo (cx, ty);
        ui::strokeGlow (g, fill, ui::ember, trackW, 0.5f + 0.5f * glow);
        g.setGradientFill (juce::ColourGradient (ui::ember, cx, bottom, ui::bone, cx, ty, false));
        g.strokePath (fill, juce::PathStrokeType (trackW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // ---- curseur : pastille claire avec halo braise (comme les curseurs du site)
    const float tw = std::min (b.getWidth() * 0.56f, 34.0f * u), th = 1.5f * thumbR;
    const auto thumb = juce::Rectangle<float> (tw, th).withCentre ({ cx, ty });
    g.setColour (ui::ember.withAlpha (0.22f + 0.25f * glow));
    g.fillRoundedRectangle (thumb.expanded (4.0f * u), th * 0.5f + 4.0f * u);
    juce::Path tp;
    tp.addRoundedRectangle (thumb, th * 0.5f);
    ui::drawCachedShadow (g, shadows, juce::DropShadow (juce::Colours::black.withAlpha (0.5f), juce::roundToInt (8.0f * u), { 0, juce::roundToInt (3.0f * u) }),
                          thumb, th * 0.5f);
    g.setGradientFill (juce::ColourGradient (ui::mist, thumb.getX(), thumb.getY(), juce::Colour (0xffd9c9cb),
                                             thumb.getX(), thumb.getBottom(), false));
    g.fillPath (tp);
    g.setColour (ui::ember);
    g.drawLine (thumb.getX() + 7.0f * u, ty, thumb.getRight() - 7.0f * u, ty, std::max (1.5f, 2.4f * u));
}

//==============================================================================
KnobComponent::KnobComponent (juce::AudioProcessorValueTreeState& apvts, const char* paramId,
                              const juce::String& t, const juce::String& minLabel, const juce::String& maxLabel,
                              Layout l, const juce::String& tooltip, juce::LookAndFeel& lnf, bool bipolar)
    : title (t), layout (l)
{
    slider.setSliderStyle (l == Layout::fader ? juce::Slider::LinearVertical : l == Layout::horizontal ? juce::Slider::LinearHorizontal
                                              : juce::Slider::RotaryHorizontalVerticalDrag);   // glisser vertical OU horizontal
    slider.setRotaryParameters (1.25f * kPi, 2.75f * kPi, true);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setMouseDragSensitivity (220);
    slider.setLookAndFeel (&lnf);
    slider.setTooltip (tooltip);
    auto& props = slider.getProperties();
    props.set ("minLabel", minLabel);
    props.set ("maxLabel", maxLabel);
    props.set ("bipolar", bipolar);
    props.set ("shown", 0.0f);     // à l'ouverture, les arcs se déploient jusqu'à leur valeur
    props.set ("glow", 0.0f);

    if (auto* param = apvts.getParameter (paramId))
        slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));   // double-clic = reset

    slider.onDragStart = [this] { setFocus (hovering, true); };
    slider.onDragEnd   = [this] { setFocus (hovering, false); };
    slider.onValueChange = [this] { repaint (valueArea.getSmallestIntegerContainer().expanded (2)); };
    slider.addMouseListener (this, false);

    addAndMakeVisible (slider);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, paramId, slider);
    setBufferedToImage (true);   // repos : une image prête (les photos des pages plein écran et les fondus ne le redessinent pas)
}

KnobComponent::~KnobComponent()
{
    slider.removeMouseListener (this);
    slider.setLookAndFeel (nullptr);
}

void KnobComponent::setFocus (bool hover, bool drag)
{
    const bool was = hovering || dragging;
    hovering = hover;
    dragging = drag;
    if ((hovering || dragging) != was && onFocusChange) onFocusChange (hovering || dragging);
}

void KnobComponent::mouseEnter (const juce::MouseEvent&) { setFocus (true, dragging); }
void KnobComponent::mouseExit (const juce::MouseEvent&)  { setFocus (false, dragging); }

void KnobComponent::tick()
{
    const float target = (float) slider.valueToProportionOfLength (slider.getValue());
    bool changed = ui::approach (shown, target, 0.30f);
    if (activity) changed |= ui::approach (act, activity() > 0.5f ? 1.0f : 0.0f, 0.3f);
    changed |= ui::approach (glow, dragging ? 1.0f : (hovering ? 0.6f : 0.0f), 0.18f);
    if (changed)
    {
        slider.getProperties().set ("shown", shown);
        slider.getProperties().set ("glow", glow);
        slider.repaint();
        repaint (nameArea.getUnion (valueArea).getSmallestIntegerContainer().expanded (2));
    }
}

void KnobComponent::resized()
{
    const auto area = getLocalBounds().toFloat();
    juce::Rectangle<float> knob;

    if (layout == Layout::horizontal)
    {
        // une seule ligne : nom (36 %) | curseur | pastille de valeur (24 %)
        const float h = area.getHeight();
        unit = h / 30.0f;
        nameArea = area.withWidth (area.getWidth() * 0.36f);
        valueArea = area.withLeft (area.getRight() - area.getWidth() * 0.24f).reduced (0.0f, h * 0.08f);
        knob = area.withLeft (nameArea.getRight() + 4.0f * unit).withRight (valueArea.getX() - 6.0f * unit);
    }
    else if (layout != Layout::textRight)
    {
        // nom + bouton ou curseur (D) + pastille de valeur. Le nom et la pastille ont une taille MINIMALE liée à
        // la fenêtre : sur les petits boutons (multibande, 1176...) les textes restent lisibles, c'est le bouton
        // qui rétrécit un peu pour leur laisser la place.
        const bool fader = (layout == Layout::fader);
        const float es = ui::editorScale (*this);
        float D = std::max (10.0f, juce::jmin (maxD, fader ? area.getHeight() / 1.47f
                                                           : juce::jmin (area.getWidth() - 2.0f, area.getHeight() / 1.47f)));
        // curseur vertical : textes calés sur la largeur (un grand curseur, comme celui du NOISE SUPPRESSOR, garde un nom et
        // une pastille de valeur à taille normale)
        const float Dt = fader ? std::min (D, area.getWidth() * 1.3f) : D;
        const float nameH = std::max (0.15f * Dt, 17.0f * es), pillH = std::max (0.21f * Dt, 23.0f * es);
        D = std::max (10.0f, std::min (D, (area.getHeight() - nameH - pillH) / 1.11f));
        unit = D / 150.0f;
        const float total = nameH + 0.05f * D + D + 0.06f * D + pillH;
        const float top = area.getY() + (area.getHeight() - total) * 0.5f;
        nameArea = { area.getX(), top, area.getWidth(), nameH };
        const float knobTop = top + nameH + 0.05f * D;
        knob = fader ? juce::Rectangle<float> (area.getX(), knobTop, area.getWidth(), D)
                     : juce::Rectangle<float> (D, D).withCentre ({ area.getCentreX(), knobTop + D * 0.5f });
        const float pillW = juce::jmin (area.getWidth() * (fader ? 0.98f : 0.92f), std::max (Dt * 0.95f, 92.0f * es));
        valueArea = juce::Rectangle<float> (pillW, pillH).withCentre ({ area.getCentreX(), knob.getBottom() + 0.06f * D + pillH * 0.5f });
    }
    else
    {
        const float D = std::max (10.0f, juce::jmin (area.getHeight() - 2.0f, area.getWidth() * 0.50f));
        unit = D / 150.0f;
        knob = juce::Rectangle<float> (D, D).withCentre ({ area.getX() + D * 0.5f + 2.0f, area.getCentreY() });
        const float tx = knob.getRight() + 0.12f * D;
        const float tw = std::max (10.0f, area.getRight() - tx - 2.0f);
        const float blockH = 0.17f * D + 0.08f * D + 0.28f * D;
        const float ty = area.getCentreY() - blockH * 0.5f;
        nameArea  = { tx, ty, tw, 0.17f * D };
        valueArea = { tx, ty + 0.25f * D, std::min (tw, D * 1.3f), 0.28f * D };
    }

    slider.setBounds (knob.toNearestInt());
}

void KnobComponent::paint (juce::Graphics& g)
{
    const float u = unit;
    auto nameR = nameArea;
    if (activity)
    {
        // LED d'activité devant le nom (s'allume quand le module agit, ex. plosive détectée)
        const float d = nameR.getHeight() * 0.36f;
        // LED (et son halo) entièrement dans le composant : le halo n'est plus coupé par le bord gauche
        const auto led = juce::Rectangle<float> (d, d).withCentre ({ nameR.getX() + d * 1.6f, nameR.getCentreY() });
        g.setColour (ui::ember.withAlpha (0.45f * act));
        g.fillEllipse (led.expanded (d * 0.8f));
        g.setColour (ui::haze.withAlpha (0.35f).interpolatedWith (ui::ember, act));
        g.fillEllipse (led);
        nameR.removeFromLeft (d * 3.0f);
    }
    g.setColour (ui::haze.interpolatedWith (ui::mist, glow));
    g.setFont (ui::fitFont (ui::Face::body, layout == Layout::horizontal ? nameArea.getHeight() * 0.48f
                                          : std::max ({ 8.0f, (layout == Layout::textRight ? 18.0f : 15.0f) * u, nameArea.getHeight() * 0.72f }),
                            nameR.getWidth() - 2.0f, title, true, 0.14f));
    g.drawText (title, nameR, (layout == Layout::textRight || layout == Layout::horizontal) ? juce::Justification::centredLeft
                                                                                           : juce::Justification::centred, false);

    drawValuePill (g, valueArea, slider.getTextFromValue (slider.getValue()), glow, u);
}

//==============================================================================
DragValue::DragValue (juce::AudioProcessorValueTreeState& apvts, const char* paramId, const juce::String& tooltip)
{
    setTooltip (tooltip);
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    param = apvts.getParameter (paramId);
    if (param != nullptr)
    {
        attachment = std::make_unique<juce::ParameterAttachment> (*param, [this] (float) { repaint(); });
        attachment->sendInitialUpdate();
    }
}

void DragValue::tick()
{
    if (ui::approach (glow, dragging ? 1.0f : (hovering ? 0.6f : 0.0f), 0.18f)) repaint();
}

void DragValue::mouseEnter (const juce::MouseEvent&)
{
    hovering = true;
    if (! dragging && onFocusChange) onFocusChange (true);
}

void DragValue::mouseExit (const juce::MouseEvent&)
{
    hovering = false;
    if (! dragging && onFocusChange) onFocusChange (false);
}

void DragValue::mouseDown (const juce::MouseEvent&)
{
    if (param == nullptr) return;
    dragging = true;
    startNorm = param->getValue();
    attachment->beginGesture();
}

void DragValue::mouseDrag (const juce::MouseEvent& e)
{
    if (param == nullptr) return;
    const float range = e.mods.isShiftDown() ? 900.0f : 220.0f;   // Maj = réglage fin
    const float norm = juce::jlimit (0.0f, 1.0f, startNorm - (float) e.getDistanceFromDragStartY() / range);
    attachment->setValueAsPartOfGesture (param->convertFrom0to1 (norm));
}

void DragValue::mouseUp (const juce::MouseEvent&)
{
    if (param == nullptr) return;
    dragging = false;
    attachment->endGesture();
    if (! hovering && onFocusChange) onFocusChange (false);
}

void DragValue::mouseDoubleClick (const juce::MouseEvent&)
{
    if (param != nullptr) attachment->setValueAsCompleteGesture (param->convertFrom0to1 (param->getDefaultValue()));
}

void DragValue::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    if (param == nullptr) return;
    const float norm = juce::jlimit (0.0f, 1.0f, param->getValue() + w.deltaY * 0.06f);
    attachment->setValueAsCompleteGesture (param->convertFrom0to1 (norm));
}

void DragValue::paint (juce::Graphics& g)
{
    if (param == nullptr) return;
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    const float u = r.getHeight() / 30.0f;
    drawValuePill (g, r, prefix + param->getCurrentValueAsText(), glow, u);
}

//==============================================================================
void PowerButton::tick()
{
    const bool st = getToggleState();
    if (st && ! was) ripple = 1.0f;   // allumé : une onde braise part du bouton
    was = st;
    bool changed = ui::approach (on, st ? 1.0f : 0.0f, 0.2f);
    if (ripple > 0.0f) { ripple = std::max (0.0f, ripple - 0.045f); changed = true; }
    if (changed) repaint();
}

void PowerButton::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    const auto b = getLocalBounds().toFloat();
    const float d = std::min (b.getWidth(), b.getHeight()) * 0.72f;
    if (d < 6.0f) return;
    const auto c = b.getCentre();
    const auto disk = juce::Rectangle<float> (d, d).withCentre (c);
    const float u = d / 24.0f;

    juce::Path ring;
    ring.addEllipse (disk);
    if (ripple > 0.0f)
    {
        const float e = 1.0f - ripple * ripple;   // part vite, ralentit en s'élargissant
        g.setColour (ui::ember.withAlpha (0.55f * ripple));
        g.drawEllipse (disk.expanded (d * 0.18f * e), std::max (1.0f, 1.6f * u * ripple));
    }
    ui::strokeGlow (g, ring, ui::ember, 1.4f * u, on * (highlighted ? 1.0f : 0.7f));
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.fillEllipse (disk);
    g.setColour (ui::ember.withAlpha (0.16f * on));
    g.fillEllipse (disk);
    g.setColour (ui::line.interpolatedWith (ui::ember, on).withMultipliedAlpha (highlighted ? 1.0f : 0.9f));
    g.drawEllipse (disk, std::max (1.0f, 1.4f * u));

    // pictogramme power : arc ouvert en haut + barre verticale, extrémités arrondies
    const float r = 0.26f * d, stroke = std::max (1.3f, 0.09f * d);
    juce::Path p;
    p.addCentredArc (c.x, c.y + 0.02f * d, r, r, 0.0f, 0.23f * kPi, 1.77f * kPi, true);
    p.startNewSubPath (c.x, c.y - 0.32f * d);
    p.lineTo (c.x, c.y - 0.04f * d);
    g.setColour (ui::haze.interpolatedWith (ui::bone, juce::jmax (on, highlighted ? 0.5f : 0.0f)));
    g.strokePath (p, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

//==============================================================================
void PillToggle::tick()
{
    const bool st = getToggleState();
    if (st && ! was) ripple = 1.0f;
    was = st;
    bool changed = ui::approach (on, st ? 1.0f : 0.0f, 0.16f);
    if (ripple > 0.0f) { ripple = std::max (0.0f, ripple - 0.05f); changed = true; }
    if (changed) repaint();
}

void PillToggle::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    const auto b = getLocalBounds().toFloat();
    const bool big = (style == Style::big);
    const float u = big ? std::min (b.getWidth() / 220.0f, b.getHeight() / 100.0f) : b.getHeight() / 36.0f;
    const float inset = big ? 10.0f * u : (style == Style::mini ? 2.0f * u : 6.0f * u);
    const auto r = b.reduced (inset);
    const float radius = big ? 18.0f * u : r.getHeight() * 0.5f;

    juce::Path shape;
    shape.addRoundedRectangle (r, radius);
    ui::strokeGlow (g, shape, ui::ember, 1.5f * u, on * 0.8f, 2.0f * inset);   // le halo s'éteint dans la marge
    g.setColour (juce::Colours::black.withAlpha (highlighted ? 0.28f : 0.36f));
    g.fillPath (shape);
    if (ripple > 0.0f)
    {
        // allumé : un reflet traverse la pastille de gauche à droite
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (shape);
        const float x = r.getX() + (1.0f - ripple) * (r.getWidth() + r.getHeight()) - r.getHeight();
        juce::ColourGradient sheen (juce::Colours::white.withAlpha (0.0f), x, 0.0f, juce::Colours::white.withAlpha (0.0f), x + r.getHeight() * 1.6f, 0.0f, false);
        sheen.addColour (0.5, juce::Colours::white.withAlpha (0.22f * std::sqrt (ripple)));
        g.setGradientFill (sheen);
        g.fillRect (juce::Rectangle<float> (x, r.getY(), r.getHeight() * 1.6f, r.getHeight()));
    }
    if (on > 0.001f)
    {
        g.setGradientFill (juce::ColourGradient (ui::ember.withAlpha (0.30f * on), r.getX(), r.getY(),
                                                 ui::blood.withAlpha (0.25f * on), r.getRight(), r.getBottom(), false));
        g.fillPath (shape);
    }
    g.setColour (ui::line.interpolatedWith (ui::ember, on));
    g.strokePath (shape, juce::PathStrokeType (std::max (1.0f, 1.4f * u)));

    if (big)
    {
        const bool hasCaption = caption.isNotEmpty() && on > 0.5f;
        auto top = r.reduced (12.0f * u, 8.0f * u);
        auto capArea = hasCaption ? top.removeFromBottom (top.getHeight() * 0.28f) : juce::Rectangle<float>();

        const juce::String main = getToggleState() ? "ON" : "OFF";
        g.setColour (ui::haze.interpolatedWith (ui::bone, on));
        g.setFont (ui::fitFont (ui::Face::display, top.getHeight() * 0.56f, top.getWidth(), main, true, 0.12f));
        g.drawText (main, top, juce::Justification::centred, false);

        if (hasCaption)
        {
            g.setColour (ui::mist.withAlpha (0.8f));
            g.setFont (ui::fitFont (ui::Face::mono, std::max (8.0f, 15.0f * u), capArea.getWidth(), caption));
            g.drawText (caption, capArea, juce::Justification::centred, false);
        }
    }
    else
    {
        const float d = r.getHeight() * 0.26f;
        const float pad = r.getHeight() * 0.40f;
        const auto ledR = juce::Rectangle<float> (d, d).withCentre ({ r.getX() + pad + d * 0.5f, r.getCentreY() });
        g.setColour (ui::ember.withAlpha (0.35f * on));
        g.fillEllipse (ledR.expanded (d * 0.8f));
        g.setColour (ui::haze.withAlpha (0.35f).interpolatedWith (ui::ember, on));
        g.fillEllipse (ledR);

        const auto textR = r.withTrimmedLeft (pad * 2.0f + d).withTrimmedRight (pad * 0.8f);
        g.setColour (ui::haze.interpolatedWith (ui::bone, juce::jmax (on, highlighted ? 0.6f : 0.0f)));
        g.setFont (ui::fitFont (ui::Face::body, r.getHeight() * (style == Style::mini ? 0.52f : 0.40f), textR.getWidth(), getButtonText(), true, 0.10f));
        g.drawText (getButtonText(), textR, juce::Justification::centred, false);
    }
}




//==============================================================================
void IconButton::tick()
{
    bool changed = ui::approach (glow, isDown() ? 1.0f : (isOver() ? 0.7f : 0.0f), 0.2f);
    changed |= ui::approach (lit, getToggleState() ? 1.0f : 0.0f, 0.2f);
    if (changed || lit > 0.01f) repaint();   // allumé : la pastille respire
}

void IconButton::paintButton (juce::Graphics& g, bool, bool)
{
    const auto b = getLocalBounds().toFloat().reduced (1.0f);
    const float h = b.getHeight(), u = h / 28.0f;
    juce::Path shape;
    shape.addRoundedRectangle (b, h * 0.5f);
    ui::strokeGlow (g, shape, ui::ember, 1.2f * u, 0.6f * glow);
    g.setColour (juce::Colours::black.withAlpha (0.38f));
    g.fillPath (shape);
    g.setColour (ui::ember.withAlpha (0.14f * glow));
    g.fillPath (shape);
    if (lit > 0.001f)
    {
        // allumé (écoute) : pastille braise qui respire doucement
        const float breathe = 0.85f + 0.15f * std::sin ((float) juce::Time::getMillisecondCounter() * 0.004f);
        g.setGradientFill (juce::ColourGradient (ui::ember.withAlpha (0.75f * lit * breathe), b.getX(), b.getY(),
                                                 ui::blood.withAlpha (0.8f * lit), b.getRight(), b.getBottom(), false));
        g.fillPath (shape);
    }
    g.setColour (ui::line.interpolatedWith (ui::ember, juce::jmax (lit, 0.25f + 0.75f * glow)));
    g.strokePath (shape, juce::PathStrokeType (std::max (1.0f, 1.2f * u)));

    const float s = h * 0.42f;
    const float cx = text.isEmpty() ? b.getCentreX() : b.getX() + h * 0.62f, cy = b.getCentreY();
    if (icon == Icon::headphones)
    {
        // casque : arceau + deux écouteurs arrondis
        juce::Path hp;
        const float r = s * 0.62f;
        hp.addCentredArc (cx, cy + r * 0.15f, r, r, 0.0f, -kPi * 0.62f, kPi * 0.62f, true);
        g.setColour (ui::haze.interpolatedWith (ui::bone, juce::jmax (0.45f, juce::jmax (glow, lit))));
        g.strokePath (hp, juce::PathStrokeType (std::max (1.3f, 1.8f * u), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        for (float sx : { -1.0f, 1.0f })
        {
            const auto cup = juce::Rectangle<float> (r * 0.5f, r * 0.82f).withCentre ({ cx + sx * r * 0.86f, cy + r * 0.62f });
            g.fillRoundedRectangle (cup, r * 0.2f);
        }
        if (text.isNotEmpty())
        {
            const auto tr = b.withTrimmedLeft (h * 1.15f).withTrimmedRight (h * 0.45f);
            g.setFont (ui::fitFont (ui::Face::body, h * 0.44f, tr.getWidth(), text, true, 0.10f));
            g.drawText (text, tr, juce::Justification::centred, false);
        }
        return;
    }

    // pictogramme : 4 coins vers l'extérieur (plein écran) ou vers l'intérieur (sortie)
    const float k = s * 0.36f;
    juce::Path ic;
    for (int q = 0; q < 4; ++q)
    {
        const float sx = (q & 1) ? 1.0f : -1.0f, sy = (q & 2) ? 1.0f : -1.0f;
        const float x = cx + sx * s * 0.5f, y = cy + sy * s * 0.5f;
        if (icon == Icon::expand)
        {
            ic.startNewSubPath (x - sx * k, y);
            ic.lineTo (x, y);
            ic.lineTo (x, y - sy * k);
        }
        else
        {
            const float ix = x - sx * k, iy = y - sy * k;
            ic.startNewSubPath (ix + sx * k, iy);
            ic.lineTo (ix, iy);
            ic.lineTo (ix, iy + sy * k);
        }
    }
    g.setColour (ui::haze.interpolatedWith (ui::bone, juce::jmax (0.4f, glow)));
    g.strokePath (ic, juce::PathStrokeType (std::max (1.3f, 1.9f * u), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (text.isNotEmpty())
    {
        const auto tr = b.withTrimmedLeft (h * 1.15f).withTrimmedRight (h * 0.45f);
        g.setColour (ui::haze.interpolatedWith (ui::bone, juce::jmax (0.5f, glow)));
        g.setFont (ui::fitFont (ui::Face::body, h * 0.44f, tr.getWidth(), text, true, 0.10f));
        g.drawText (text, tr, juce::Justification::centred, false);
    }
}


//==============================================================================
PresetBar::PresetBar (onirique::PresetManager& p) : pm (p)
{
    addChildComponent (editor);
    editor.setJustification (juce::Justification::centred);
    editor.setColour (juce::TextEditor::backgroundColourId, ui::night.withAlpha (0.9f));
    editor.setColour (juce::TextEditor::textColourId, ui::mist);
    editor.setColour (juce::TextEditor::outlineColourId, ui::ember);
    editor.setColour (juce::TextEditor::focusedOutlineColourId, ui::ember);
    editor.setColour (juce::TextEditor::highlightColourId, ui::ember.withAlpha (0.4f));
    editor.setColour (juce::CaretComponent::caretColourId, ui::bone);
    editor.setTextToShowWhenEmpty ("Preset name", ui::haze);
    editor.onReturnKey = [this] { endSave (true); };
    editor.onEscapeKey = [this] { endSave (false); };
    editor.onFocusLost = [this] { endSave (false); };
    shownName = pm.getCurrentName();
}

void PresetBar::resized()
{
    const auto r = getLocalBounds().toFloat();
    const float h = r.getHeight();
    auto rest = r;
    zones[save] = rest.removeFromRight (h * 2.1f);
    rest.removeFromRight (h * 0.2f);
    zones[prev] = rest.removeFromLeft (h);
    zones[next] = rest.removeFromRight (h);
    zones[name] = rest;
    editor.setBounds (zones[name].reduced (2.0f, h * 0.14f).toNearestInt());
    editor.setFont (ui::font (ui::Face::body, h * 0.40f, true));
}

PresetBar::Zone PresetBar::zoneAt (juce::Point<float> p) const noexcept
{
    for (int z = 0; z < 4; ++z) if (zones[z].contains (p)) return (Zone) z;
    return none;
}

void PresetBar::tick()
{
    bool changed = false;
    for (int z = 0; z < 4; ++z) changed |= ui::approach (glow[z], hover == z ? 1.0f : 0.0f, 0.2f);
    if ((++frame % 10) == 0)
    {
        const auto n = pm.getCurrentName();
        const bool mod = pm.isModified();
        if (n != shownName || mod != modified) { shownName = n; modified = mod; changed = true; }
    }
    if (changed) repaint();
}

void PresetBar::mouseMove (const juce::MouseEvent& e)
{
    const auto z = zoneAt (e.position);
    if (z != hover) { hover = z; repaint(); }
}

void PresetBar::mouseExit (const juce::MouseEvent&) { hover = none; }

void PresetBar::mouseDown (const juce::MouseEvent& e)
{
    switch (zoneAt (e.position))
    {
        case prev: pm.step (-1); break;
        case next: pm.step (1); break;
        case name: showMenu(); break;
        case save: startSave(); break;
        default: break;
    }
    shownName = pm.getCurrentName();
    modified = pm.isModified();
    repaint();
}

void PresetBar::showMenu()
{
    const auto entries = pm.list();
    const auto current = pm.getCurrentName();
    juce::PopupMenu menu;
    bool userHeader = false;
    menu.addSectionHeader ("FACTORY");
    for (int i = 0; i < (int) entries.size(); ++i)
    {
        const auto& e = entries[(size_t) i];
        if (! e.factory && ! userHeader) { menu.addSectionHeader ("USER"); userHeader = true; }
        menu.addItem (i + 1, e.name, true, e.name == current);
    }
    menu.addSeparator();
    menu.addItem (1000, "Save as new preset...");
    bool currentIsUser = false;
    for (const auto& e : entries) if (! e.factory && e.name == current) currentIsUser = true;
    menu.addItem (1001, "Delete \"" + current + "\"", currentIsUser);
    menu.addItem (1002, "Open presets folder");

    menu.setLookAndFeel (&getLookAndFeel());   // sinon JUCE juge la fenêtre opaque avec le style système (coins blancs)
    juce::Component::SafePointer<PresetBar> self (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMinimumWidth (getWidth())
                            .withParentComponent (findParentComponentOfClass<juce::AudioProcessorEditor>()),
                        [self, entries] (int r)
                        {
                            if (self == nullptr || r == 0) return;
                            auto& bar = *self;
                            if (r >= 1 && r <= (int) entries.size()) bar.pm.load (entries[(size_t) (r - 1)]);
                            else if (r == 1000) bar.startSave();
                            else if (r == 1001)
                            {
                                for (const auto& e : entries)
                                    if (! e.factory && e.name == bar.pm.getCurrentName()) bar.pm.remove (e);
                                bar.pm.load (entries.front());   // retour sur Init
                            }
                            else if (r == 1002) onirique::PresetManager::folder().startAsProcess();
                            bar.repaint();
                        });
}

void PresetBar::startSave()
{
    editor.setText (shownName == "Init" ? juce::String() : shownName, false);
    editor.setVisible (true);
    editor.selectAll();
    editor.grabKeyboardFocus();
    repaint();
}

void PresetBar::endSave (bool commit)
{
    if (! editor.isVisible()) return;
    const auto text = editor.getText();
    editor.setVisible (false);
    if (commit) pm.save (text);
    shownName = pm.getCurrentName();
    modified = pm.isModified();
    repaint();
}

void PresetBar::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    const float h = r.getHeight(), u = h / 40.0f;

    // conteneur : précédent | nom | suivant
    const auto box = zones[prev].getUnion (zones[next]).reduced (0.0f, 1.0f);
    g.setColour (juce::Colours::black.withAlpha (0.36f));
    g.fillRoundedRectangle (box, box.getHeight() * 0.5f);
    g.setColour (ui::line.interpolatedWith (ui::ember, 0.6f * glow[name]));
    g.drawRoundedRectangle (box.reduced (0.5f), box.getHeight() * 0.5f, std::max (1.0f, 1.2f * u));

    for (int z : { (int) prev, (int) next })
    {
        const auto c = zones[z].getCentre();
        const float d = (z == prev ? -1.0f : 1.0f) * 4.0f * u;
        juce::Path chev;
        chev.startNewSubPath (c.x - d, c.y - 6.0f * u);
        chev.lineTo (c.x + d, c.y);
        chev.lineTo (c.x - d, c.y + 6.0f * u);
        g.setColour (ui::haze.interpolatedWith (ui::bone, glow[z]));
        g.strokePath (chev, juce::PathStrokeType (std::max (1.3f, 2.0f * u), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    if (! editor.isVisible())
    {
        auto nr = zones[name].reduced (4.0f * u, 0.0f);
        g.setColour (ui::haze.withAlpha (0.8f));
        g.setFont (ui::font (ui::Face::mono, 9.0f * u, false, 0.1f));
        g.drawText ("PRESET", nr.removeFromTop (h * 0.42f).withTrimmedTop (h * 0.10f), juce::Justification::centredBottom, false);
        const juce::String txt = shownName + (modified ? " *" : "");
        g.setColour (ui::mist.interpolatedWith (ui::bone, glow[name]));
        g.setFont (ui::fitFont (ui::Face::body, h * 0.36f, nr.getWidth() - 18.0f * u, txt, true, 0.04f));
        g.drawText (txt, nr.withTrimmedBottom (h * 0.08f), juce::Justification::centredTop, false);
        // petit triangle : la liste s'ouvre au clic
        const float tx = zones[name].getRight() - 8.0f * u, ty = zones[name].getCentreY();
        juce::Path tri;
        tri.addTriangle (tx - 4.0f * u, ty - 2.0f * u, tx + 4.0f * u, ty - 2.0f * u, tx, ty + 3.0f * u);
        g.setColour (ui::haze.interpolatedWith (ui::ember, glow[name]));
        g.fillPath (tri);
    }

    // SAVE
    const float room = std::max (1.5f, 2.5f * u);   // marge au bord du composant : le halo s'y éteint au lieu d'être coupé
    const auto s = zones[save].reduced (room);
    juce::Path sp;
    sp.addRoundedRectangle (s, s.getHeight() * 0.5f);
    ui::strokeGlow (g, sp, ui::ember, 1.2f * u, glow[save] * 0.8f, 2.0f * room);
    g.setColour (juce::Colours::black.withAlpha (0.36f));
    g.fillPath (sp);
    g.setColour (ui::ember.withAlpha (0.18f * glow[save]));
    g.fillPath (sp);
    g.setColour (ui::line.interpolatedWith (ui::ember, glow[save]));
    g.strokePath (sp, juce::PathStrokeType (std::max (1.0f, 1.2f * u)));
    g.setColour (ui::haze.interpolatedWith (ui::bone, glow[save]));
    g.setFont (ui::fitFont (ui::Face::body, h * 0.36f, s.getWidth() - 10.0f * u, "SAVE", true, 0.16f));
    g.drawText ("SAVE", s, juce::Justification::centred, false);
}


//==============================================================================
void LevelMeter::tick()
{
    float peakNow = -100.0f;
    for (int c = 0; c < 2; ++c)
    {
        const float pk = consume ? consume (c) : 0.0f;
        const float db = pk > 1.0e-5f ? 20.0f * std::log10 (pk) : -100.0f;
        peakNow = std::max (peakNow, db);
        levelDb[c] = db > levelDb[c] ? db : std::max (-100.0f, levelDb[c] - 0.33f);   // retombée ~20 dB/s
        if (db >= holdDb[c]) { holdDb[c] = db; holdFrames[c] = 90; }                // maintien 1.5 s
        else if (--holdFrames[c] <= 0) holdDb[c] = std::max (levelDb[c], holdDb[c] - 0.5f);
    }
    if (peakNow >= maxDb) { maxDb = peakNow; maxFrames = 180; }                      // valeur affichée : 3 s
    else if (--maxFrames <= 0) maxDb = std::max (holdDb[0], holdDb[1]);
    repaint();
}

void LevelMeter::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    const float u = area.getWidth() / 46.0f;
    auto r = area;
    const auto readout = r.removeFromBottom (18.0f * u);
    r.removeFromBottom (4.0f * u);
    const auto scale = r.removeFromRight (18.0f * u);
    const float barW = (r.getWidth() - 3.0f * u) * 0.5f;
    auto yOf = [&] (float db) { return r.getBottom() - norm (db) * r.getHeight(); };

    // graduations
    g.setFont (ui::font (ui::Face::mono, std::max (8.0f * u, 9.0f * ui::editorScale (*this))));
    for (int db : { 0, -6, -12, -24, -48 })
    {
        const float y = yOf ((float) db);
        g.setColour (db == 0 ? ui::ember.withAlpha (0.8f) : ui::haze.withAlpha (0.75f));
        g.fillRect (juce::Rectangle<float> (scale.getX() + 1.0f * u, y, 3.0f * u, 1.0f));
        if (db != -6)   // -6 : simple repère, sinon les chiffres du haut se chevauchent
            g.drawText (juce::String (db), juce::Rectangle<float> (scale.getX() + 4.0f * u, y - 6.0f * u, scale.getWidth() - 4.0f * u, 12.0f * u),
                    juce::Justification::centredLeft, false);
    }

    for (int c = 0; c < 2; ++c)
    {
        const auto bar = juce::Rectangle<float> (r.getX() + (float) c * (barW + 3.0f * u), r.getY(), barW, r.getHeight());
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRoundedRectangle (bar, 2.5f * u);
        g.setColour (ui::line);
        g.drawRoundedRectangle (bar.reduced (0.5f), 2.5f * u, 1.0f);

        // remplissage : os -> braise près de 0 dBFS -> rouge au-delà
        const float top = yOf (levelDb[c]);
        if (top < bar.getBottom() - 0.5f)
        {
            juce::ColourGradient cg (ui::bone.withAlpha (0.30f), 0.0f, bar.getBottom(), juce::Colour (0xffff2a3a), 0.0f, yOf (2.0f), false);
            cg.addColour ((double) norm (-12.0f), ui::bone.withAlpha (0.85f));   // positions = niveau normalisé (bas = -60, haut = +2)
            cg.addColour ((double) norm (-3.0f), ui::ember);
            g.setGradientFill (cg);
            g.fillRoundedRectangle (bar.withTop (top).reduced (1.0f, 0.0f), 2.0f * u);
        }
        // maintien de crête
        if (holdDb[c] > -60.0f)
        {
            g.setColour (holdDb[c] > 0.0f ? juce::Colour (0xffff2a3a) : ui::bone);
            g.fillRect (juce::Rectangle<float> (bar.getX() + 1.0f, yOf (holdDb[c]) - 1.0f * u, bar.getWidth() - 2.0f, 2.0f * u));
        }
    }

    // valeur crête (3 s) en clair
    const juce::String txt = maxDb <= -99.0f ? juce::String ("-inf") : (maxDb > 0.0f ? "+" : "") + juce::String (maxDb, 1);
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.fillRoundedRectangle (readout, readout.getHeight() * 0.5f);
    g.setColour (maxDb > 0.0f ? juce::Colour (0xffff2a3a) : ui::line);
    g.drawRoundedRectangle (readout.reduced (0.5f), readout.getHeight() * 0.5f, 1.0f);
    g.setColour (maxDb > 0.0f ? ui::bone : ui::mist);
    g.setFont (ui::fitFont (ui::Face::mono, readout.getHeight() * 0.62f, readout.getWidth() - 6.0f * u, txt, true));
    g.drawText (txt, readout, juce::Justification::centred, false);
}

SegmentSelector::SegmentSelector (juce::AudioProcessorValueTreeState& apvts, const char* paramId,
                                  const juce::StringArray& l, const juce::StringArray& t)
    : labels (l), tips (t)
{
    if (auto* param = apvts.getParameter (paramId))
    {
        attachment = std::make_unique<juce::ParameterAttachment> (*param, [this] (float v)
        {
            const int idx = juce::jlimit (0, labels.size() - 1, juce::roundToInt (v));
            if (idx != selected) { selected = idx; repaint(); }
        });
        attachment->sendInitialUpdate();
    }
}

juce::Rectangle<float> SegmentSelector::segmentArea() const noexcept
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    if (title.isNotEmpty())
    {
        const auto f = ui::font (ui::Face::mono, r.getHeight() * 0.40f, true, 0.08f);
        r.removeFromLeft (ui::textWidth (f, title) + r.getHeight() * 0.9f);
    }
    return r;
}

int SegmentSelector::segmentAt (float x) const noexcept
{
    const auto a = segmentArea();
    if (labels.isEmpty() || a.getWidth() <= 0.0f || x < a.getX()) return -1;
    return juce::jlimit (0, labels.size() - 1, (int) ((x - a.getX()) * (float) labels.size() / a.getWidth()));
}

void SegmentSelector::setLocked (bool shouldLock, const juce::String& text, const juce::String& tip)
{
    if (shouldLock == locked && text == lockedText) return;
    locked = shouldLock;
    lockedText = text;
    lockedTip = tip;
    setMouseCursor (locked ? juce::MouseCursor::NormalCursor : juce::MouseCursor::PointingHandCursor);
    repaint();
}

void SegmentSelector::tick()
{
    bool changed = false;
    if (slide < 0.0f) { slide = (float) selected; changed = true; }
    changed |= ui::spring (slide, slideVel, (float) selected);   // la pastille glisse avec un léger ressort
    changed |= ui::approach (lockAmt, locked ? 1.0f : 0.0f, 0.18f);
    if (changed) repaint();
}

void SegmentSelector::mouseDown (const juce::MouseEvent& e)
{
    if (locked) return;
    const int idx = segmentAt (e.position.x);
    if (idx >= 0 && idx != selected && attachment != nullptr)
        attachment->setValueAsCompleteGesture ((float) idx);
}

void SegmentSelector::mouseMove (const juce::MouseEvent& e)
{
    const int h = locked ? -1 : segmentAt (e.position.x);
    if (h != hover) { hover = h; repaint(); }
}

void SegmentSelector::mouseExit (const juce::MouseEvent&)
{
    if (hover != -1) { hover = -1; repaint(); }
}

juce::String SegmentSelector::getTooltip()
{
    if (locked && lockedTip.isNotEmpty()) return lockedTip;
    const int idx = segmentAt ((float) getMouseXYRelative().x);
    return juce::isPositiveAndBelow (idx, tips.size()) ? tips[idx] : titleTip;
}

void SegmentSelector::paint (juce::Graphics& g)
{
    const int n = labels.size();
    if (n == 0) return;

    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    const float u = r.getHeight() / 26.0f;
    const float radius = r.getHeight() * 0.5f;
    const auto a = segmentArea();
    const float segW = a.getWidth() / (float) n;
    const float pos = slide < 0.0f ? (float) selected : slide;
    const float dim = 1.0f - 0.65f * lockAmt;   // verrouillé : tout s'estompe

    g.setColour (juce::Colours::black.withAlpha (0.34f));
    g.fillRoundedRectangle (r, radius);
    g.setColour (ui::line);
    g.drawRoundedRectangle (r.reduced (0.5f), radius, std::max (1.0f, 1.1f * u));

    if (title.isNotEmpty())
    {
        g.setColour (ui::haze.withMultipliedAlpha (dim));
        g.setFont (ui::font (ui::Face::mono, r.getHeight() * 0.40f, true, 0.08f));
        g.drawText (title, juce::Rectangle<float> (r.getX(), r.getY(), a.getX() - r.getX(), r.getHeight()), juce::Justification::centred, false);
    }

    // pastille braise qui glisse vers le choix
    // segment étroit (ex. OFF / 2X / 4X / 8X) : pastille un peu moins haute, jamais un ovale vertical ; le texte garde
    // toute la largeur du segment (plus de pastille ronde qui écrasait les libellés)
    auto pill = juce::Rectangle<float> (a.getX() + segW * pos, a.getY(), segW, a.getHeight()).reduced (2.0f * u, 2.5f * u);
    if (pill.getWidth() < pill.getHeight() * 1.2f)
        pill = pill.withSizeKeepingCentre (pill.getWidth(), std::max (pill.getHeight() * 0.72f, std::min (pill.getHeight(), pill.getWidth() * 0.8f)));
    juce::Path pp;
    pp.addRoundedRectangle (pill, pill.getHeight() * 0.5f);
    // pastille du choix : verre rouge sombre, liseré rouge, une lueur discrète (qui s'éteint avant le bord du composant)
    const float room = std::min ({ pill.getX(), (float) getWidth() - pill.getRight(), pill.getY(), (float) getHeight() - pill.getBottom() });
    ui::strokeGlow (g, pp, ui::ember, 1.0f * u, 0.45f * dim, 2.0f * room);
    g.setGradientFill (juce::ColourGradient (ui::ember.withAlpha (0.30f * dim), pill.getX(), pill.getY(),
                                             ui::blood.withAlpha (0.65f * dim), pill.getX(), pill.getBottom(), false));
    g.fillPath (pp);
    g.setColour (ui::ember.interpolatedWith (ui::lilac, 0.35f).withMultipliedAlpha (dim));
    g.strokePath (pp, juce::PathStrokeType (std::max (1.0f, 1.1f * u)));

    // une seule taille de texte pour tous les choix, qui tient dans la pastille (ronde si le segment est étroit)
    const float fitW = pill.getWidth() - 4.0f * u;
    auto labelFont = ui::font (ui::Face::body, r.getHeight() * 0.48f, true, 0.04f);
    for (const auto& l : labels)
    {
        const float tw = ui::textWidth (labelFont, l);
        if (tw > fitW && tw > 0.0f) labelFont.setHeight (std::max (4.0f, labelFont.getHeight() * fitW / tw));
    }

    for (int i = 0; i < n; ++i)
    {
        const juce::Rectangle<float> seg (a.getX() + segW * (float) i, a.getY(), segW, a.getHeight());
        const float sel = juce::jlimit (0.0f, 1.0f, 1.0f - std::abs (pos - (float) i));
        const auto base = i == hover ? ui::mist : ui::haze;
        g.setColour (base.interpolatedWith (ui::bone, sel).withMultipliedAlpha (dim));
        g.setFont (labelFont);
        g.drawText (labels[i], seg, juce::Justification::centred, false);
    }

    // verrouillé : message par-dessus (ex. "MIX MODE ONLY")
    if (lockAmt > 0.01f && lockedText.isNotEmpty())
    {
        const auto f = ui::fitFont (ui::Face::mono, r.getHeight() * 0.42f, a.getWidth() - 10.0f * u, lockedText, true, 0.08f);
        const float tw = ui::textWidth (f, lockedText) + 16.0f * u;
        const auto box = juce::Rectangle<float> (tw, r.getHeight() - 6.0f * u).withCentre (a.getCentre());
        g.setColour (ui::night.withAlpha (0.92f * lockAmt));
        g.fillRoundedRectangle (box, box.getHeight() * 0.5f);
        g.setColour (ui::line.withMultipliedAlpha (2.0f * lockAmt));
        g.drawRoundedRectangle (box, box.getHeight() * 0.5f, std::max (1.0f, u));
        g.setColour (ui::mist.withAlpha (lockAmt));
        g.setFont (f);
        g.drawText (lockedText, box, juce::Justification::centred, false);
    }
}

