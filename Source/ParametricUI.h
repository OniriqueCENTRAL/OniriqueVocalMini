#pragma once

#include "MiniUI.h"
#include "SpectrumSmoother.h"

/*  L'écran PARAMETRIC de VOCAL MINI PRO (bouton PARAMETRIC de la rangée EQ) : un EQ paramétrique 8 bandes en grand.

    EqDisplay : l'analyseur et les courbes.
      - spectres en direct : la voix qui entre dans le coeur (gris) et celle qui en sort (clair), FFT 8192, lissage
        1/6 d'octave, montée vive et retombée douce ;
      - la courbe des boutons LOW / MID / HI / AIR et du LOW CUT (rose pâle, pointillés), EXACTE (mêmes filtres que le son,
        à la cadence du coeur), avec une poignée par bouton qu'on peut tirer (c'est le bouton lui-même qui tourne) ;
      - la courbe de l'EQ paramétrique (rouge, lumineuse), exacte, gain dynamique en direct, et la zone de la bande choisie ;
      - la courbe TOTALE (blanc) : ce que la voix reçoit de l'ensemble des EQ.
      Points numérotés : tirer = fréquence / gain (Maj : fin), molette = Q, double-clic = allumer / couper, double-clic
      dans le vide = nouvelle bande à cet endroit, clic droit = menu (type, pente, dynamique). Avec LISTEN, tenir un point
      fait entendre la zone de sa bande, seule.

    ParametricPanel : l'écran entier (titre, EQ ON, échelle, DONE ; l'analyseur ; les 8 bandes et les réglages de la bande
    choisie : type, pente, FREQ, GAIN, Q, mode dynamique et son seuil, marche, écoute, remise à zéro). */
class EqDisplay : public juce::Component, public juce::SettableTooltipClient, public Animated
{
public:
    static constexpr int kBands = onirique::mini::VoiceParametric::kBands;

    explicit EqDisplay (OniriqueVocalMiniAudioProcessor&);
    ~EqDisplay() override;

    std::function<void (int)> onSelect;   // bande choisie (clic, glisser, double-clic)
    void select (int band);
    int getSelected() const noexcept { return selected; }
    void setRange (float db) { if (db != rangeDb) { rangeDb = db; gridLayer = {}; repaint(); } }
    void setListenArmed (bool a) noexcept { listenArmed = a; }
    bool frozen = false;   // essais locaux : spectre de démonstration immobile

    void paint (juce::Graphics&) override;
    void resized() override { gridLayer = {}; }
    void tick() override;
    juce::String getTooltip() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    // menu d'une bande : type, pente, dynamique, marche, remise à zéro (s'ouvre à l'endroit du clic)
    static void showBandMenu (juce::Component& target, juce::AudioProcessorValueTreeState&, int band, juce::Point<int> screenPos);

   #if ONIRIQUE_UI_TEST
    // essais locaux : gestes de souris simulés sur l'écran, résultats en texte (une ligne par contrôle)
    juce::String runSelfTest (int& failures);
   #endif

private:
    static constexpr int kOrder = 13, kN = 1 << kOrder, kPts = 360, kKnobs = 4;

    onirique::mini::VoiceParametric::Band band (int b) const;
    float value (const juce::String& id) const;
    void setValue (const juce::String& id, float v, bool gesture = true);
    float hzAt (int i) const noexcept { return 20.0f * std::pow (1000.0f, (float) i / (float) (kPts - 1)); }
    juce::Rectangle<float> plotArea() const noexcept;
    float xOf (float hz) const noexcept;
    float hzOf (float x) const noexcept;
    float yOf (float db) const noexcept;
    float dbOf (float y) const noexcept;
    float ySpec (float db) const noexcept;
    juce::Point<float> nodePos (int b) const;
    int nodeAt (juce::Point<float>) const;
    juce::Point<float> handlePos (int k) const;
    int handleAt (juce::Point<float>) const;
    void buildMap (double fs);
    bool analyse (onirique::SpectrumTap&, std::vector<float>& hist, int& writePos, std::array<float, kPts>& spec);
    bool updateCurves();
    void flushDrag();
    juce::Path smoothPath (const std::array<float, kPts>& ys, int step = 1) const;
    void paintGrid (juce::Graphics&);
    void demoSpectrum();

    OniriqueVocalMiniAudioProcessor& processor;
    juce::AudioProcessorValueTreeState& st;
    juce::dsp::FFT fft { kOrder };
    std::vector<float> window, histIn, histOut, scratch, fftBuf;
    onirique::ui::SpectrumSmoother smoother;   // lissage gaussien des spectres, poids calculés une fois
    double mapFs = 0.0;
    int writeIn = 0, writeOut = 0, frame = 0;
    float windowGain = 1.0f;
    std::array<float, kPts> specIn {}, specOut {}, peqCurve {}, peqMax {}, knobCurve {}, total {}, selCurve {};
    std::vector<float> lastSig;
    juce::Image gridLayer;
    float rangeDb = 12.0f, eqAmt = 1.0f, lowCutAmt = 1.0f;
    std::array<float, kKnobs> knobShown {};   // LOW, MID, HI, AIR (-1..1), animés
    std::array<float, kBands> liveGain {}, nodeGlow {};
    std::array<float, kKnobs> handleGlow {};
    bool anyDyn = false, listenArmed = false;
    int selected = 0, hoverNode = -1, dragNode = -1, hoverHandle = -1, dragHandle = -1;
    juce::Point<float> dragStart;
    float dragF0 = 1000.0f, dragG0 = 0.0f, dragK0 = 0.0f, pendingF = 0.0f, pendingG = 0.0f, pendingK = 0.0f;
    bool pending = false;
    int qFlash = 0;
};

// une bande dans la barre du bas : témoin (marche / arrêt), numéro, petit dessin de son type
class PeqChip : public juce::Component, public juce::SettableTooltipClient, public Animated
{
public:
    PeqChip (juce::AudioProcessorValueTreeState&, int band);
    std::function<void (int)> onClick;
    void setSelected (bool s) { if (s != selected) { selected = s; repaint(); } }
    void paint (juce::Graphics&) override;
    void tick() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { hover = true; }
    void mouseExit (const juce::MouseEvent&) override { hover = false; }

private:
    juce::AudioProcessorValueTreeState& st;
    int band, shownType = -1;
    bool selected = false, hover = false;
    float lit = 0.0f, glow = 0.0f;
};

// type de la bande choisie : six petits dessins (cloche, shelfs, coupes, notch)
class PeqTypeSelector : public juce::Component, public juce::TooltipClient, public Animated
{
public:
    explicit PeqTypeSelector (juce::AudioProcessorValueTreeState& s) : st (s) {}
    void setBand (int b) { band = b; repaint(); }
    void paint (juce::Graphics&) override;
    void tick() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hover = -1; }
    void mouseDown (const juce::MouseEvent&) override;
    juce::String getTooltip() override;

private:
    int cellAt (float x) const noexcept;
    juce::AudioProcessorValueTreeState& st;
    int band = 0, hover = -1;
    float slide = -1.0f;
    std::array<float, 6> glow {};
};

// pente des coupes : 12 / 24 / 36 / 48 dB/oct (estompée pour les autres types)
class PeqSlopeSelector : public juce::Component, public juce::TooltipClient, public Animated
{
public:
    explicit PeqSlopeSelector (juce::AudioProcessorValueTreeState& s) : st (s) {}
    void setBand (int b) { band = b; repaint(); }
    void paint (juce::Graphics&) override;
    void tick() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hover = -1; }
    void mouseDown (const juce::MouseEvent&) override;
    juce::String getTooltip() override;

private:
    juce::AudioProcessorValueTreeState& st;
    int band = 0, hover = -1;
    float slide = -1.0f, enabled = 1.0f;
    std::array<float, 4> glow {};
};

// échelle de l'affichage (±6 / ±12 / ±18 dB), réglage d'affichage gardé avec le projet
class RangeSelector : public juce::Component, public Animated
{
public:
    std::function<void (int)> onChange;
    void setIndex (int i) { index = juce::jlimit (0, 2, i); repaint(); }
    void paint (juce::Graphics&) override;
    void tick() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hover = -1; }
    void mouseDown (const juce::MouseEvent&) override;

private:
    int index = 1, hover = -1;
    float slide = 1.0f;
    std::array<float, 3> glow {};
};

class ParametricPanel : public juce::Component, public Animated
{
public:
    explicit ParametricPanel (OniriqueVocalMiniAudioProcessor&);
    ~ParametricPanel() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick() override;
    void visibilityChanged() override;
    void selectBand (int b);
   #if ONIRIQUE_UI_TEST
    juce::String runSelfTest();   // essais locaux : l'écran puis les pastilles, le type, la pente, RESET BAND
   #endif

    PanelButton close { "DONE", false };
    EqDisplay display;

private:
    struct Set
    {
        std::unique_ptr<MiniKnob> freq, gain, q, thr;
        std::unique_ptr<PillToggle> on, dyn;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> onAtt, dynAtt;
    };
    void resetBand (int b);

    OniriqueVocalMiniAudioProcessor& processor;
    juce::AudioProcessorValueTreeState& st;
    std::array<std::unique_ptr<PeqChip>, EqDisplay::kBands> chips;
    std::array<Set, EqDisplay::kBands> sets;
    PeqTypeSelector typeSel { st };
    PeqSlopeSelector slopeSel { st };
    RangeSelector range;
    PillToggle power { "EQ ON", PillToggle::Style::small }, listen { "LISTEN", PillToggle::Style::small };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> powerAtt;
    PanelButton reset { "RESET BAND", false };
    std::vector<Animated*> kids;
    int selected = 0;
    float gainAlpha = 1.0f, thrAlpha = 1.0f;
    juce::String shownTitle;
};
