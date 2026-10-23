#pragma once

#include "UIKit.h"
#include "PluginProcessor.h"

#include <array>

/*  Les pièces de l'interface de VOCAL MINI PRO (noir profond, charbon, accent rouge).

    MiniKnob : le potard de VOCAL MINI PRO, dessiné pour lui (pas le potard commun des autres plugins) : une piste fine,
    l'arc de valeur en dégradé rouge qui s'éclaire, un corps de charbon éclairé d'en haut, une aiguille nette et un
    point lumineux au bout de l'arc. Le centre reste vierge (ni croix, ni étoile). Au repos (à zéro), l'arc s'éteint et
    l'aiguille pâlit ; au réveil, une floraison de lumière. Le nom laisse place à la valeur au survol et pendant le réglage.
    Glisser verticalement (Maj : réglage fin), molette, double-clic = valeur par défaut. Le grand potard (COMP) a en plus
    une couronne de points qui s'allument jusqu'à la valeur.

    VoiceOrb : la colonne de gauche. Le grand COMP au centre d'un anneau de spectre (la voix qui sort, réelle), un anneau
    intérieur qui se resserre avec la réduction, les mètres LEVEL (niveleur) et PEAK (crêtes) en dessous, AUTO GAIN ;
    puis le MULTIBAND avec la réduction de ses quatre bandes et le bouton qui ouvre le panneau BANDS.

    StepSelector, LevelBar, Readout : valeur à choix entre deux flèches, mètre horizontal (zone BEST), témoin + texte.
    ReverbScope : la voix (blanc), la queue entendue (rouge) et ce que CLEAN / DUCKING retirent (rose pâle), en direct.
    BandPanel, ReverbPanel : les panneaux détaillés du MULTIBAND et de la REVERB, ouverts par-dessus les rangées.
    (Le panneau PARAMETRIC, l'EQ paramétrique, est dans ParametricUI.h.)
*/

// dessins partagés par les panneaux
namespace minidraw
{
// halo doux (dégradé radial) centré sur c
void softGlow (juce::Graphics&, juce::Point<float> c, float r, juce::Colour, float alpha);
// le même halo, rayon limité pour s'éteindre avant le bord de la zone (sinon le bord du composant le coupe net, en carré)
void softGlowIn (juce::Graphics&, juce::Rectangle<float> area, juce::Point<float> c, float r, juce::Colour, float alpha);
// panneau de verre sombre (rangées, panneaux détaillés)
void glassPanel (juce::Graphics&, juce::Rectangle<float> r, float radius, float u, float strength = 1.0f);
}
class MiniKnob : public juce::Component, public juce::SettableTooltipClient, public Animated
{
public:
    enum class Size { small, medium, large };

    MiniKnob (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& title, Size, bool bipolar,
              const juce::String& tooltip);

    // activité (0..1) : fait respirer un halo autour du potard (queue de reverb, réduction...)
    std::function<float()> activity;

    void paint (juce::Graphics&) override;
    void tick() override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    juce::Rectangle<float> knobArea() const noexcept;
    float awakeAmount() const noexcept { return awake; }

private:
    bool isAtRest() const noexcept;

    juce::RangedAudioParameter* param = nullptr;
    std::unique_ptr<juce::ParameterAttachment> attachment;
    juce::String title;
    Size size;
    bool bipolar;
    float shown = -1.0f, awake = 0.0f, glow = 0.0f, valueShow = 0.0f, bloom = 0.0f, act = 0.0f;
    float startNorm = 0.0f;
    bool hovering = false, dragging = false, wasAwake = false;
    juce::String shownText;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MiniKnob)
};

class StepSelector : public juce::Component, public juce::SettableTooltipClient, public Animated
{
public:
    StepSelector (juce::AudioProcessorValueTreeState&, const char* paramId, const juce::String& title, const juce::String& tooltip);
    void paint (juce::Graphics&) override;
    void tick() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    juce::Rectangle<float> arrow (int side) const noexcept;
    void step (int delta);

    juce::RangedAudioParameter* param = nullptr;
    std::unique_ptr<juce::ParameterAttachment> attachment;
    juce::String title;
    int hover = 0, index = -1, dragStart = 0, dragIndex = 0;
    float glow[2] {}, flash = 0.0f, slide = 0.0f;
};

class LevelBar : public juce::Component, public juce::SettableTooltipClient, public Animated
{
public:
    // consume : crête linéaire depuis la dernière lecture ; rmsDb : niveau moyen (peut être vide) ; best : zone BEST
    LevelBar (std::function<float()> consume, std::function<float()> rmsDb, bool best);
    void paint (juce::Graphics&) override;
    void tick() override;

private:
    static float norm (float db) noexcept { return juce::jlimit (0.0f, 1.0f, (db + 54.0f) / 54.0f); }   // -54 .. 0 dBFS
    std::function<float()> consume, rms;
    bool showBest;
    float levelDb = -100.0f, holdDb = -100.0f, avgDb = -100.0f, inBest = 0.0f;
    int holdFrames = 0;
};

// un témoin (point lumineux) et un court texte, mis à jour par des fonctions (état d'un module : GATE ouvert, réduction)
class Readout : public juce::Component, public juce::SettableTooltipClient, public Animated
{
public:
    std::function<float()> level;            // 0..1 : intensité du témoin
    std::function<juce::String()> text;
    juce::Colour colour = onirique::ui::ember;
    void paint (juce::Graphics&) override;
    void tick() override;

private:
    float lit = 0.0f;
    juce::String shownText;
};

// petit bouton texte (ouvrir / fermer un panneau), même verre que les pastilles
class PanelButton : public juce::Button, public Animated
{
public:
    explicit PanelButton (const juce::String& text, bool arrow = true) : juce::Button (text), withArrow (arrow) {}
    std::function<float()> lit;   // facultatif : témoin à gauche (0..1), quelque chose est allumé dans le panneau
    bool pillSize = false;        // mêmes proportions qu'une pastille PillToggle (marge, texte, témoin) : à côté d'elle, même taille
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    void tick() override;

private:
    bool withArrow;
    float glow = 0.0f, led = 0.0f;
};

// la voix, la queue de reverb entendue et ce que CLEAN / DUCKING ont retiré, en barres miroir qui défilent
class ReverbScope : public juce::Component, public Animated
{
public:
    ReverbScope (const onirique::mini::VoiceReverb&, bool compact);
    void paint (juce::Graphics&) override;
    void tick() override;
    std::function<void()> onClick;
    void mouseUp (const juce::MouseEvent& e) override { if (onClick && getLocalBounds().contains (e.getPosition())) onClick(); }
    void mouseEnter (const juce::MouseEvent&) override { hovering = true; }
    void mouseExit (const juce::MouseEvent&) override { hovering = false; }

    float cleaned() const noexcept { return cleanShown; }
    float ducked() const noexcept { return duckShown; }
    bool frozen = false;   // essais locaux (visuel de démo)

private:
    const onirique::mini::VoiceReverb& rev;
    bool compact, hovering = false;
    int lastPos = -1;
    float cleanShown = 0.0f, duckShown = 0.0f, hover = 0.0f;
};

class VoiceOrb : public juce::Component, public Animated
{
public:
    explicit VoiceOrb (OniriqueVocalMiniAudioProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick() override;

    PanelButton bandsButton { "BANDS" };

    // essais locaux (visuel de démo) : animations figées, chaque image est exacte
    bool frozen = false;

private:
    void analyse();
    juce::Point<float> centre() const noexcept;
    float ringInner() const noexcept;

    OniriqueVocalMiniAudioProcessor& processor;
    MiniKnob comp, multi;
    PillToggle autoGain { "AUTO GAIN", PillToggle::Style::small };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> autoGainAtt;
    std::vector<Animated*> kids;

    static constexpr int kBars = 40, kFft = 11, kN = 1 << kFft;
    juce::dsp::FFT fft { kFft };
    std::vector<float> fifo, window, data;
    std::array<float, kBars> bars {}, barsShown {};
    float energy = 0.0f, energySlow = 0.0f, grShown = 0.0f, levelShown = 0.0f, peakShown = 0.0f, breath = 0.0f;
    std::array<float, 4> bandGr {};
    juce::Rectangle<float> grArea;
    double clock = 0.0;

    struct Spark { float angle, radius, speed, life, size, drift; };
    std::vector<Spark> sparks;
    juce::Random rnd { 7 };

    juce::Image glowLayer;   // halo de fond, redessiné seulement au redimensionnement
    float layerScale = 0.0f;
};

// panneau MULTIBAND : les quatre bandes, chacune avec son mètre (niveau, seuil, réduction) et ses réglages
class BandPanel : public juce::Component, public Animated
{
public:
    explicit BandPanel (OniriqueVocalMiniAudioProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick() override;
    PanelButton close { "DONE", false };
    bool frozen = false;   // essais locaux (visuel de démo) : mètres figés

private:
    struct Column
    {
        std::unique_ptr<MiniKnob> thr, ratio, atk, rel, knee, gain;
        std::unique_ptr<PillToggle> on;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> onAtt;
        juce::Rectangle<float> meter;
        float lvl = -100.0f, gr = 0.0f;
    };
    OniriqueVocalMiniAudioProcessor& processor;
    std::array<Column, 4> cols;
    std::vector<Animated*> kids;
    juce::RangedAudioParameter* amountParam = nullptr;
};

// panneau REVERB : la visualisation en grand, le mode, et tous les réglages de l'espace
class ReverbPanel : public juce::Component, public Animated
{
public:
    explicit ReverbPanel (OniriqueVocalMiniAudioProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick() override;
    PanelButton close { "DONE", false };
    ReverbScope scope;

private:
    OniriqueVocalMiniAudioProcessor& processor;
    SegmentSelector mode;
    std::vector<std::unique_ptr<MiniKnob>> knobs;
    StepSelector pulseRate;
    std::vector<Animated*> kids;
    float cleanLit = 0.0f, duckLit = 0.0f;
};
