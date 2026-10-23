#pragma once

#include "PresetManager.h"

#include <array>
#include <map>

namespace onirique::ui
{
// largeur de référence de la fenêtre : toutes les tailles de l'interface sont données pour elle, puis mises à l'échelle
inline constexpr float kEditorBaseW = 1120.0f, kEditorBaseH = 700.0f;

// ---- VOCAL MINI PRO : le noir et rouge d'ONIRIQUE, en plus moderne : noir profond, charbon neutre, rouge sombre pour la
// profondeur ; le rouge vif est l'accent, utilisé avec mesure (arcs, témoins, choix actif) ; rose pâle pour les reflets ;
// blanc cassé pour les textes, gris doux pour le secondaire. Pas de néon : des lueurs discrètes, du verre sombre.
// (Les noms historiques sont gardés : « ember » est l'accent, « lilac » le reflet clair.)
inline const juce::Colour night  { 0xff09080A };   // noir profond (fond)
inline const juce::Colour blood  { 0xff3D0910 };   // rouge sombre (dégradés, profondeur)
inline const juce::Colour deep   { 0xff121012 };   // charbon très sombre (panneaux)
inline const juce::Colour deep2  { 0xff1C1517 };   // charbon chaud (menus, info-bulles, panneaux ouverts)
inline const juce::Colour mist   { 0xffF2ECEC };   // blanc cassé (textes)
inline const juce::Colour haze   { 0xff978C8F };   // gris doux (informations secondaires)
inline const juce::Colour ember  { 0xffE5384A };   // rouge ONIRIQUE (accent)
inline const juce::Colour lilac  { 0xffFFB3BC };   // rose pâle (queues de reverb, reflets, extrémités d'arcs)
inline const juce::Colour bone   { 0xffFFF4F5 };   // blanc le plus clair (aiguilles, crêtes)
inline const juce::Colour mint   { 0xff7DE3B6 };   // témoin discret « ça agit » (comme le voyant d'un rack)
inline const juce::Colour line   { (juce::uint8) 255, (juce::uint8) 214, (juce::uint8) 218, 0.10f };

// Polices : display (titres), body (libellés), mono (valeurs) : Unbounded, Hanken Grotesk et JetBrains Mono, les polices
// du site ONIRIQUE (fournies avec le module de licence). Elles sont chargées par FontScope (une par interface ouverte,
// rien de statique qui survive au déchargement du plugin) ; sans interface ouverte, des équivalents système.
enum class Face { display, body, mono };
juce::Font font (Face, float height, bool bold = false, float kerning = 0.0f);
struct FontScope
{
    FontScope();
    ~FontScope();
    JUCE_DECLARE_NON_COPYABLE (FontScope)
};
float textWidth (const juce::Font&, const juce::String&);
// police de hauteur maxH, réduite si nécessaire pour que le texte tienne dans maxW (jamais coupé)
juce::Font fitFont (Face, float maxH, float maxW, const juce::String&, bool bold = false, float kerning = 0.0f);

// échelle de la fenêtre (1 = 1480 px de large) : sert de PLANCHER aux tailles de texte, pour qu'un petit bouton
// garde des libellés lisibles
float editorScale (const juce::Component&);

// trait lumineux : plusieurs passes de plus en plus larges et transparentes sous le trait net (jusqu'à 5.6 fois la largeur ;
// maxWidth > 0 limite la passe la plus large, pour que le halo reste dans le composant au lieu d'être coupé net par son bord)
void strokeGlow (juce::Graphics&, const juce::Path&, juce::Colour, float width, float intensity, float maxWidth = 0.0f);

// ombre portée d'un rectangle arrondi (corner >= 0) ou d'une ellipse (corner < 0) : même rendu que
// juce::DropShadow::drawForPath, mais le flou est calculé une seule fois par forme puis gardé en cache
// (sur Mac, Core Graphics redessine de grandes zones à chaque image : recalculer les flous y faisait ramer l'interface).
// Le cache appartient à l'interface (jamais statique) : ses images disparaissent avec elle, avant que l'hôte
// décharge le plugin (un cache statique libéré au déchargement de la DLL bloquait FL Studio à la suppression).
struct ShadowCache
{
    std::map<std::array<int, 8>, juce::Image> images;
};
void drawCachedShadow (juce::Graphics&, ShadowCache&, const juce::DropShadow&, juce::Rectangle<float> shape, float corner);

// approche exponentielle d'une valeur animée ; renvoie true tant qu'elle bouge
inline bool approach (float& v, float target, float k) noexcept
{
    if (std::abs (target - v) < 1.0e-3f) { const bool moved = v != target; v = target; return moved; }
    v += k * (target - v);
    return true;
}

// ressort légèrement sous-amorti (glissements de pastilles, d'onglets, de pastilles d'ordre) : part vite, dépasse à peine
// (~3 %), se pose sans à-coup. vel : vitesse gardée entre deux images (60 Hz).
inline bool spring (float& v, float& vel, float target, float stiffness = 0.16f, float damping = 0.70f) noexcept
{
    vel = vel * damping + (target - v) * stiffness;
    v += vel;
    if (std::abs (target - v) < 1.0e-3f && std::abs (vel) < 1.0e-3f) { const bool moved = v != target; v = target; vel = 0.0f; return moved; }
    return true;
}
} // namespace onirique::ui

// Tout ce qui s'anime reçoit tick() à 60 Hz depuis l'éditeur (une seule horloge pour toute l'interface).
struct Animated
{
    virtual ~Animated() = default;
    virtual void tick() = 0;
};

class OniriqueLookAndFeel : public juce::LookAndFeel_V4
{
public:
    OniriqueLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    int getSliderThumbRadius (juce::Slider&) override;
    juce::Font getPopupMenuFont() override { return onirique::ui::font (onirique::ui::Face::body, 15.0f); }

    // menus (presets) : verre sombre arrondi, pastille braise au survol, point braise sur l'élément actif
    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                            bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                            const juce::String& shortcutKeyText, const juce::Drawable* icon, const juce::Colour* textColour) override;
    void drawPopupMenuSectionHeader (juce::Graphics&, const juce::Rectangle<int>& area, const juce::String& name) override;
    void getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int standardMenuItemHeight,
                                    int& idealWidth, int& idealHeight) override;
    int getPopupMenuBorderSize() override { return 7; }

private:
    onirique::ui::ShadowCache shadows;   // ombres des boutons et des curseurs
};

// Bouton rotatif (ou curseur vertical, Layout::fader) complet : nom + contrôle + valeur dans une
// pastille. L'arc lumineux suit la valeur en glissant ; il s'illumine au survol et pendant le réglage.
class KnobComponent : public juce::Component, public Animated
{
public:
    enum class Layout { textBelow, textRight, fader, horizontal };   // horizontal : NOM  ━━●━━  [valeur]

    KnobComponent (juce::AudioProcessorValueTreeState&, const char* paramId, const juce::String& title,
                   const juce::String& minLabel, const juce::String& maxLabel, Layout layout,
                   const juce::String& tooltip, juce::LookAndFeel& lnf, bool bipolar = false);
    ~KnobComponent() override;

    // taille de référence max (diamètre d'un bouton) : aligne un curseur sur les boutons voisins
    void setMaxDiameter (float d) { maxD = d; resized(); }
    // appelé quand le survol / réglage commence ou finit (surbrillance de la bande dans l'afficheur)
    std::function<void (bool)> onFocusChange;
    // activité (ex. réduction en dB) : une LED braise s'allume à côté du nom quand elle dépasse 0.5
    std::function<float()> activity;

    void paint (juce::Graphics&) override;
    void resized() override;
    void tick() override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    void setFocus (bool hover, bool drag);

    juce::Slider slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    juce::String title;
    Layout layout;
    bool dragging = false, hovering = false;
    float shown = 0.0f, glow = 0.0f, act = 0.0f;
    juce::Rectangle<float> nameArea, valueArea;
    float unit = 1.0f, maxD = 1.0e6f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KnobComponent)
};

// Valeur réglable dans une pastille (glisser verticalement, molette, double-clic = défaut).
class DragValue : public juce::Component, public juce::SettableTooltipClient, public Animated
{
public:
    DragValue (juce::AudioProcessorValueTreeState&, const char* paramId, const juce::String& tooltip);
    std::function<void (bool)> onFocusChange;
    void setPrefix (const juce::String& p) { prefix = p; repaint(); }   // ex. "GAIN " devant la valeur

    void paint (juce::Graphics&) override;
    void tick() override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    juce::RangedAudioParameter* param = nullptr;
    std::unique_ptr<juce::ParameterAttachment> attachment;
    float startNorm = 0.0f, glow = 0.0f;
    juce::String prefix;
    bool hovering = false, dragging = false;
};

// Bouton power d'un module : anneau braise lumineux quand le module est actif.
class PowerButton : public juce::ToggleButton, public Animated
{
public:
    PowerButton() { setClickingTogglesState (true); }
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    void tick() override;

private:
    float on = 1.0f, ripple = 0.0f;   // ripple : onde qui part du bouton quand on l'allume
    bool was = true;
};

// Interrupteur : grand (AUTO GAIN, ON/OFF + correction en dB) ou petit (BYPASS avec LED)
class PillToggle : public juce::ToggleButton, public Animated
{
public:
    enum class Style { big, small, mini };   // mini : interrupteur compact dans une bande de sélecteurs
    PillToggle (const juce::String& label, Style s) : juce::ToggleButton (label), style (s) { setClickingTogglesState (true); }

    void setCaption (const juce::String& c) { if (c != caption) { caption = c; repaint(); } }
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    void tick() override;

private:
    Style style;
    juce::String caption;
    float on = 0.0f, ripple = 0.0f;
    bool was = true;
};


// Petit bouton à pictogramme (plein écran / sortie du plein écran), même verre sombre que les pastilles.
class IconButton : public juce::Button, public Animated
{
public:
    enum class Icon { expand, collapse, headphones };   // headphones : interrupteur (écoute de la bande), braise quand il est allumé
    explicit IconButton (Icon i) : juce::Button ("icon"), icon (i) { if (i == Icon::headphones) setClickingTogglesState (true); }
    void setText (const juce::String& t) { text = t; repaint(); }
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    void tick() override;

private:
    Icon icon;
    juce::String text;
    float glow = 0.0f, lit = 0.0f;
};


// Barre de presets (en-tête) : précédent / nom (menu) / suivant + SAVE (champ de nom intégré).
class PresetBar : public juce::Component, public Animated
{
public:
    explicit PresetBar (onirique::PresetManager&);

    void paint (juce::Graphics&) override;
    void resized() override;
    void tick() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    enum Zone { none = -1, prev, name, next, save };
    Zone zoneAt (juce::Point<float>) const noexcept;
    void showMenu();
    void startSave();
    void endSave (bool commit);

    onirique::PresetManager& pm;
    juce::TextEditor editor;
    juce::Rectangle<float> zones[4];
    juce::String shownName;
    bool modified = false;
    Zone hover = none;
    float glow[4] {};
    int frame = 0;
};


// Mètre de niveau vertical stéréo (L / R) : crête instantanée, retombée douce (~20 dB/s), maintien de crête 1.5 s,
// zone braise près de 0 dBFS, rouge au-delà ; valeur crête maintenue affichée sous les barres (clic = remise à zéro).
class LevelMeter : public juce::Component, public juce::SettableTooltipClient, public Animated
{
public:
    explicit LevelMeter (std::function<float (int)> consumePeak) : consume (std::move (consumePeak))
    {
        setTooltip ("Peak level (dBFS), left / right. The number is the highest recent peak: click to reset it.");
    }
    void paint (juce::Graphics&) override;
    void tick() override;
    void mouseDown (const juce::MouseEvent&) override { maxDb = -100.0f; repaint(); }

private:
    static float norm (float db) noexcept { return juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 62.0f); }   // -60 .. +2 dBFS

    std::function<float (int)> consume;
    float levelDb[2] { -100.0f, -100.0f }, holdDb[2] { -100.0f, -100.0f }, maxDb = -100.0f;
    int holdFrames[2] {};
    int maxFrames = 0;
};

// Sélecteur segmenté relié à un paramètre à choix ; la pastille braise glisse vers le choix.
class SegmentSelector : public juce::Component, public juce::TooltipClient, public Animated
{
public:
    SegmentSelector (juce::AudioProcessorValueTreeState&, const char* paramId,
                     const juce::StringArray& labels, const juce::StringArray& tooltips);

    void paint (juce::Graphics&) override;
    void tick() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    juce::String getTooltip() override;

    // titre court à gauche, dans la pastille (ex. "OVERSAMPLING")
    void setTitle (const juce::String& t, const juce::String& tip = {}) { title = t; titleTip = tip; repaint(); }
    // verrouillé : estompé, clics ignorés, message par-dessus et info-bulle d'explication
    void setLocked (bool locked, const juce::String& text, const juce::String& tip);

private:
    int segmentAt (float x) const noexcept;
    juce::Rectangle<float> segmentArea() const noexcept;

    juce::StringArray labels, tips;
    std::unique_ptr<juce::ParameterAttachment> attachment;
    int selected = 0, hover = -1;
    float slide = -1.0f, slideVel = 0.0f, lockAmt = 0.0f;
    juce::String title, titleTip, lockedText, lockedTip;
    bool locked = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SegmentSelector)
};

