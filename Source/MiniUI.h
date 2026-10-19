#pragma once

#include "UIKit.h"
#include "PluginProcessor.h"

#include <array>

/*  Les pièces de l'interface de VOCAL MINI.

    MiniKnob : le potard ONIRIQUE (OniriqueKnob.h), son nom dessous. Il « dort » à zéro (graduations éteintes, nom
    estompé) et s'allume en braise dès qu'on le tourne, avec une petite floraison de lumière au réveil. Le nom laisse
    place à la valeur au survol et pendant le réglage. Une activité (queue de reverb, répétitions, réduction du
    de-esser...) le fait respirer d'un halo. Glisser verticalement (Maj : réglage fin), molette, double-clic = défaut.

    VoiceOrb : le cœur de l'interface. Le gros potard COMP au centre d'une orbe vivante : un anneau de spectre circulaire
    (la voix qui sort, des graves en bas aux aigus en haut, en miroir) qui respire avec le chant, un anneau intérieur qui
    se resserre et rougeoie avec la compression, des étincelles qui s'échappent sur les syllabes fortes, et un halo de
    fond qui suit l'énergie de la voix. Le potard MULTIBAND dessous, avec ses quatre bandes qui s'allument.

    StepSelector : une valeur à choix entre deux flèches (TIME du delay) ; clic sur une flèche, molette, glisser.

    LevelBar : mètre horizontal avec la zone BEST (IN) ; crête, retombée douce, niveau moyen en filet.
*/
class MiniKnob : public juce::Component, public juce::SettableTooltipClient, public Animated
{
public:
    enum class Size { small, medium, large };

    MiniKnob (juce::AudioProcessorValueTreeState&, const char* paramId, const juce::String& title, Size, bool bipolar,
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

class VoiceOrb : public juce::Component, public Animated
{
public:
    explicit VoiceOrb (OniriqueVocalMiniAudioProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick() override;

    // essais locaux (visuel de démo) : animations figées, chaque image est exacte
    bool frozen = false;

private:
    void analyse();
    juce::Point<float> centre() const noexcept;
    float ringInner() const noexcept;

    OniriqueVocalMiniAudioProcessor& processor;
    MiniKnob comp, multi;
    std::vector<Animated*> kids;

    static constexpr int kBars = 36, kFft = 11, kN = 1 << kFft;
    juce::dsp::FFT fft { kFft };
    std::vector<float> fifo, window, data;
    int fifoFill = 0;
    std::array<float, kBars> bars {}, barsShown {};
    float energy = 0.0f, energySlow = 0.0f, grShown = 0.0f, breath = 0.0f;
    std::array<float, 4> bandGr {};
    double clock = 0.0;

    struct Spark { float angle, radius, speed, life, size, drift; };
    std::vector<Spark> sparks;
    juce::Random rnd { 7 };

    juce::Image glowLayer;   // halo de fond, redessiné seulement au redimensionnement
    float layerScale = 0.0f;
};
