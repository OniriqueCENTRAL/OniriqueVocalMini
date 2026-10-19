#pragma once

#include "License/LicenseOverlay.h"
#include "MiniUI.h"

// ONIRIQUE VOCAL MINI : en-tête (logo, LIVE / MIX, LOW CUT, AUTO INPUT, presets, licence), l'orbe de la voix à gauche
// (COMP, MULTIBAND), quatre rangées à droite (EQ, FX, DELAY, REVERB), IN et OUT en bas.
class OniriqueVocalMiniAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit OniriqueVocalMiniAudioProcessorEditor (OniriqueVocalMiniAudioProcessor&);
    ~OniriqueVocalMiniAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using K = MiniKnob::Size;
    void timerCallback() override;
    float scale() const noexcept { return (float) getWidth() / onirique::ui::kEditorBaseW; }
    void paintBackdrop (juce::Graphics&);

    OniriqueVocalMiniAudioProcessor& processor;
    OniriqueLookAndFeel lnf;
    juce::TooltipWindow tooltipWindow { this, 600 };
    juce::AudioProcessorValueTreeState& st = processor.apvts;

    PresetBar presetBar { processor.presets };
    SegmentSelector modeSel { st, onirique::ids::mode, { "LIVE", "MIX" },
        { "LIVE: zero latency, to record while listening to yourself through VOCAL MINI. Already the full sound.",
          "MIX: maximum quality once the recording is done (oversampled core, de-esser that sees the S coming). "
          "About 1 ms of latency, compensated by your DAW." } };
    PillToggle bypassBtn { "BYPASS", PillToggle::Style::small };
    PillToggle lowCut { "LOW CUT", PillToggle::Style::small }, autoInput { "AUTO INPUT", PillToggle::Style::small },
               ping { "PING-PONG", PillToggle::Style::small };
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> buttonAtts;

    VoiceOrb orb { processor };

    MiniKnob low   { st, onirique::ids::low, "LOW", K::small, true,
                     "Up: the chest of the voice, warmer and fuller (without mud). Down: removes boom and proximity, lighter voice." };
    MiniKnob mid   { st, onirique::ids::mid, "MID", K::small, true,
                     "Up: the voice comes forward (presence and bite). Down: cleans the boxy, nasal sound, never hollow." };
    MiniKnob hi    { st, onirique::ids::hi, "HI", K::small, true,
                     "Up: clarity and presence, opened smoothly. Down: removes harshness. Pulled back automatically on the S." };
    MiniKnob air   { st, onirique::ids::air, "AIR", K::small, true,
                     "Up: air with a silky grain. A very wide shelf on top, plus real harmonics of the voice above 6.5 kHz: "
                     "detailed, breathy shine, never whistling. Down: rounder, darker, vintage top." };
    MiniKnob deess { st, onirique::ids::deess, "DE-ESS", K::small, false,
                     "Tames the S. It finds the frequency of THIS voice's sibilants by itself; only the S go down." };
    MiniKnob drive { st, onirique::ids::drive, "DRIVE", K::small, false,
                     "Warmth of a class-A preamp with transformers: harmonics and weight, the level stays the same." };
    MiniKnob phone { st, onirique::ids::phone, "PHONE", K::small, false,
                     "From a full voice to a phone: narrower band, small-speaker resonance, soft breakup." };
    MiniKnob doubler { st, onirique::ids::doubler, "DOUBLER", K::small, false,
                       "A real double, without singing it twice: two copies left and right, slightly detuned and drifting "
                       "like a second take. The lead stays centred and untouched." };
    MiniKnob chorus { st, onirique::ids::chorus, "CHORUS", K::small, false, "Soft widening, no vibrato." };
    MiniKnob delay { st, onirique::ids::delay, "DELAY", K::small, false,
                     "Echo on an aux send: the voice stays whole, this sets how much goes into the echo. Ducks under the voice." };
    MiniKnob feed  { st, onirique::ids::feed, "FEED", K::small, false, "How many repeats." };
    MiniKnob filter { st, onirique::ids::filter, "FILTER", K::small, true,
                      "Colour of the repeats: left darker, centre warm studio echo, right thinner and further away." };
    StepSelector time { st, onirique::ids::time, "TIME", "Note value of the echo, locked to your project's tempo." };
    MiniKnob rShort { st, onirique::ids::rShort, "SHORT", K::small, false,
                      "Short tight ambience (0.4 s): thickens and places the voice, no audible tail. Aux send." };
    MiniKnob rSmall { st, onirique::ids::rSmall, "SMALL", K::small, false,
                      "Small bright studio plate (1.1 s): the everyday vocal reverb. Aux send." };
    MiniKnob rBig   { st, onirique::ids::rBig, "BIG", K::small, false,
                      "Big hall (2.6 s) with pre-delay: the voice stays sharp in front of a huge space. Aux send." };
    MiniKnob rLush  { st, onirique::ids::rLush, "LUSH", K::small, false,
                      "Dreamy plate (4.5 s), shimmering with a veil of octave. Aux send." };
    MiniKnob inK    { st, onirique::ids::inGain, "IN", K::small, true, "Input gain (before AUTO INPUT)." };
    MiniKnob outK   { st, onirique::ids::outGain, "OUT", K::small, true, "Output level." };
    LevelBar inBar  { [this] { return processor.getChain().consumeInputPeak(); }, [this] { return processor.getChain().getInputRmsDb(); }, true };
    LevelBar outBar { [this] { return processor.getChain().consumeOutputPeak(); }, nullptr, false };

    std::vector<Animated*> animated;
    juce::Image logo, logoScaled, logoGlow, backdropLayer;
    float layersScale = 0.0f;
    juce::Rectangle<float> logoRect, autoRect;
    std::array<juce::Rectangle<float>, 4> rows;
    float autoShown = 99.0f;
    double clock = 0.0;
    int frame = 0;
    bool restoringSize = true;

    std::unique_ptr<onirique::license::Overlay> licenseOverlay;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OniriqueVocalMiniAudioProcessorEditor)
};
