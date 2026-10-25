#pragma once

#include "License/LicenseOverlay.h"
#include "ParametricUI.h"
#include "ui/OniriqueBar.h"

// ONIRIQUE VOCAL MINI PRO : sous la barre du haut (OniriqueBar.h : titre, presets, IN, A/B, licence), à gauche le COMP et
// le MULTIBAND ; à droite cinq rangées (CLEAN, EQ, FX, DELAY, REVERB) ; IN et OUT en bas. Les panneaux BANDS et REVERB
// s'ouvrent par-dessus les rangées, PARAMETRIC (l'EQ paramétrique) par-dessus le COMP et les rangées.
class VocalMiniView : public juce::Component, private juce::Timer
{
public:
    explicit VocalMiniView (OniriqueVocalMiniAudioProcessor&);
    ~VocalMiniView() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

private:
    using K = MiniKnob::Size;
    void timerCallback() override;
    float scale() const noexcept { return (float) getWidth() / onirique::ui::kEditorBaseW; }
    void paintBackdrop (juce::Graphics&);
    void openPanel (int which);   // -1 : aucun, 0 : BANDS, 1 : REVERB, 2 : PARAMETRIC
    juce::Rectangle<int> panelArea (int which) const;

    OniriqueVocalMiniAudioProcessor& processor;
    onirique::ui::FontScope fonts;   // les polices de la charte, le temps que l'interface est ouverte
    OniriqueLookAndFeel lnf;
    juce::TooltipWindow tooltipWindow { this, 600 };
    juce::AudioProcessorValueTreeState& st = processor.apvts;

    SegmentSelector modeSel { st, onirique::ids::mode, { "LIVE", "MIX" },
        { "LIVE: zero latency, to record while listening to yourself through VOCAL MINI PRO. Already the full sound.",
          "MIX: maximum quality once the recording is done (oversampled core, de-esser, de-plosive and gate that see what is "
          "coming, spectral noise suppressor). About 27 ms of latency, compensated by your DAW." } };
    PillToggle lowCut { "LOW CUT", PillToggle::Style::small }, autoInput { "AUTO INPUT", PillToggle::Style::small },
               ping { "PING-PONG", PillToggle::Style::small };
    PanelButton peqButton { "PARAMETRIC" };
    PillToggle plosOn { "PLOSIVE", PillToggle::Style::small }, nsOn { "NOISE", PillToggle::Style::small },
               gateOn { "GATE", PillToggle::Style::small };
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> buttonAtts;

    VoiceOrb orb { processor };

    // CLEAN
    MiniKnob plosAmt { st, onirique::ids::plosAmount, "AMOUNT", K::small, false,
                       "DE-PLOSIVE: how hard the P and B pops are removed. Only the pops: the body of the voice stays." };
    MiniKnob nsAmt   { st, onirique::ids::nsAmount, "AMOUNT", K::small, false,
                       "NOISE SUPPRESSOR: the higher, the more the steady background noise (hiss, fan, room) goes down, also "
                       "while you sing. Tuned for transparency: the voice is never metallic." };
    MiniKnob gateThr { st, onirique::ids::gateThr, "THRESH", K::small, false,
                       "Gate threshold: the gate opens above it and closes 6 dB below (no chattering). Set it just above the "
                       "noise between your phrases." };
    MiniKnob gateRange { st, onirique::ids::gateRange, "RANGE", K::small, false,
                         "How much the silences go down when the gate closes (18 dB keeps a natural room; 60 dB = silence)." };
    MiniKnob gateRel { st, onirique::ids::gateRel, "RELEASE", K::small, false,
                       "How smoothly the gate closes after the voice: longer keeps word endings and soft breaths." };
    Readout plosRead, nsRead, gateRead;

    // EQ, FX, DELAY
    MiniKnob low   { st, onirique::ids::low, "LOW", K::small, true,
                     "Up: the chest of the voice, warmer and fuller (without mud). Down: removes boom and proximity, lighter voice." };
    MiniKnob mid   { st, onirique::ids::mid, "MID", K::small, true,
                     "Up: the voice comes forward (presence and bite). Down: cleans the boxy, nasal sound, never hollow." };
    MiniKnob hi    { st, onirique::ids::hi, "HI", K::small, true,
                     "Up: clarity and presence, opened smoothly (the harsh 2.5-4 kHz zone is never pushed hard). Down: removes "
                     "harshness. Pulled back automatically on the S." };
    MiniKnob air   { st, onirique::ids::air, "AIR", K::small, true,
                     "Up: air with a silky grain: a very wide shelf on top plus real harmonics of the voice above 6.5 kHz. "
                     "Down: rounder, darker, vintage top." };
    MiniKnob deess { st, onirique::ids::deess, "DE-ESS", K::small, false,
                     "Tames the S (and the French CH). It finds the frequency of THIS voice's sibilants by itself; only the S go down." };
    MiniKnob drive { st, onirique::ids::drive, "DRIVE", K::small, false,
                     "Warmth of a class-A preamp with transformers: harmonics and weight, the level stays the same." };
    MiniKnob phone { st, onirique::ids::phone, "PHONE", K::small, false,
                     "From a full voice to a phone: narrower band, small-speaker resonance, soft breakup." };
    MiniKnob doubler { st, onirique::ids::doubler, "DOUBLER", K::small, false,
                       "A real double, without singing it twice: two copies left and right, slightly detuned and drifting "
                       "like a second take. The lead stays centred and untouched. Mono-safe." };
    MiniKnob chorus { st, onirique::ids::chorus, "CHORUS", K::small, false, "Soft widening, no vibrato. Mono-safe." };
    MiniKnob delay { st, onirique::ids::delay, "DELAY", K::small, false,
                     "Echo on an aux send: the voice stays whole, this sets how much goes into the echo. Ducks under the voice." };
    MiniKnob feed  { st, onirique::ids::feed, "FEED", K::small, false, "How many repeats." };
    MiniKnob filter { st, onirique::ids::filter, "FILTER", K::small, true,
                      "Colour of the repeats: left darker, centre warm studio echo, right thinner and further away." };
    StepSelector time { st, onirique::ids::time, "TIME", "Note value of the echo, locked to your project's tempo." };

    // REVERB
    SegmentSelector revMode { st, onirique::ids::revMode, onirique::prm::revModeNames(),
        { "ROOM: a small room, strong early reflections: thickens and places the voice.",
          "PLATE 1: a bright, dense plate, no separate echoes: smooth and present.",
          "PLATE 2: a darker, wider, more modulated plate: soft and enveloping.",
          "HALL: a large hall, distant reflections, a deep tail.",
          "SHIMMER: the octave above is fed back into the tail: an airy, harmonic veil." } };
    MiniKnob revMix   { st, onirique::ids::revMix, "MIX", K::small, false,
                        "How much voice goes into the reverb (aux send): the dry voice always stays whole." };
    MiniKnob revDecay { st, onirique::ids::revDecay, "DECAY", K::small, false, "Reverb time (RT60), 0.2 to 12 s." };
    MiniKnob revPre   { st, onirique::ids::revPre, "PRE-DELAY", K::small, false,
                        "Time before the reverb starts: keeps the attack and intelligibility of the voice." };
    MiniKnob revClean { st, onirique::ids::revClean, "CLEAN", K::small, false,
                        "Each new phrase starts clean: the previous tail is emptied when you sing again, sibilants never go "
                        "into the reverb, low-mids are carved out of the tail." };
    ReverbScope revScope { processor.getChain().getReverb(), true };

    MiniKnob inK    { st, onirique::ids::inGain, "IN", K::small, true, "Input gain (before AUTO INPUT)." };
    MiniKnob outK   { st, onirique::ids::outGain, "OUT", K::small, true, "Output level." };
    LevelBar inBar  { [this] { return processor.getChain().consumeInputPeak(); }, [this] { return processor.getChain().getInputRmsDb(); }, true };
    LevelBar outBar { [this] { return processor.getChain().consumeOutputPeak(); }, nullptr, false };

    BandPanel bandPanel { processor };
    ReverbPanel reverbPanel { processor };
    ParametricPanel peqPanel { processor };
    std::array<juce::Component*, 3> panels { &bandPanel, &reverbPanel, &peqPanel };
    std::array<float, 3> panelShown {};
    int panelWanted = -1;

    std::vector<Animated*> animated;
    juce::Image backdropLayer;
    float layersScale = 0.0f;
    juce::Rectangle<float> autoRect;
    std::array<juce::Rectangle<float>, 5> rows;
    float autoShown = 99.0f, sweep = 0.0f;
    juce::String lastPreset;
    double clock = 0.0;
    int frame = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalMiniView)
};

// La fenêtre du plugin : la barre du haut des plugins ONIRIQUE (presets, IN, A/B, copier / coller, licence, taille de
// la fenêtre), et l'interface dessous. Les deux se zooment ensemble, de 75 à 200 %.
class OniriqueVocalMiniAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit OniriqueVocalMiniAudioProcessorEditor (OniriqueVocalMiniAudioProcessor&);
    ~OniriqueVocalMiniAudioProcessorEditor() override;

    void paint (juce::Graphics&) override {}
    void resized() override;

private:
    void timerCallback() override;
    float titleWidth() const;
    void paintTitle (juce::Graphics&, juce::Rectangle<float> area) const;   // le titre, dans la barre (en points)

    OniriqueVocalMiniAudioProcessor& processor;
    VocalMiniView view;
    onirique::nuit::Bar bar;
    juce::Image logo, logoGlow;   // le logo du titre et sa lueur, qui respire
    std::unique_ptr<onirique::license::Overlay> licenseOverlay;
    bool restoringSize = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OniriqueVocalMiniAudioProcessorEditor)
};
