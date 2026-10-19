#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "DSP/VoiceDelay.h"

namespace onirique
{
namespace ids
{
    inline constexpr const char* inGain    = "inGain";      // IN (dB)
    inline constexpr const char* outGain   = "outGain";     // OUT (dB)
    inline constexpr const char* autoInput = "autoInput";   // AUTO INPUT : gain d'entrée automatique
    inline constexpr const char* lowCut    = "lowCut";      // LOW CUT 90 Hz
    inline constexpr const char* low       = "eqLow";       // EQ, -10..+10
    inline constexpr const char* mid       = "eqMid";
    inline constexpr const char* hi        = "eqHi";
    inline constexpr const char* air       = "eqAir";
    inline constexpr const char* comp      = "comp";        // COMP (%)
    inline constexpr const char* multi     = "multiband";   // MULTIBAND (%)
    inline constexpr const char* deess     = "deess";       // FX (%)
    inline constexpr const char* drive     = "drive";
    inline constexpr const char* phone     = "phone";
    inline constexpr const char* doubler   = "doubler";
    inline constexpr const char* chorus    = "chorus";
    inline constexpr const char* delay     = "delay";       // DELAY : envoi (%), réinjection (%), filtre (-100..+100)
    inline constexpr const char* feed      = "delayFeed";
    inline constexpr const char* filter    = "delayFilter";
    inline constexpr const char* time      = "delayTime";   // valeur de note (kDivisionNames)
    inline constexpr const char* ping      = "pingPong";
    inline constexpr const char* rShort    = "revShort";    // REVERBS (%)
    inline constexpr const char* rSmall    = "revSmall";
    inline constexpr const char* rBig      = "revBig";
    inline constexpr const char* rLush     = "revLush";
    inline constexpr const char* mode      = "mode";        // LIVE (latence nulle, enregistrement) / MIX (qualité maximale)
    inline constexpr const char* bypass    = "bypass";
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    using Attr = AudioParameterFloatAttributes;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;

    auto db = [] (float v, int) { v = std::round (v * 10.0f) / 10.0f + 0.0f; return (v > 0.0f ? "+" : "") + String (v, 1) + " dB"; };
    auto pct = [] (float v, int) { return String (roundToInt (v)) + " %"; };
    auto bip = [] (float v, int) { v = std::round (v * 10.0f) / 10.0f + 0.0f; return v == 0.0f ? String ("0") : (v > 0.0f ? "+" : "") + String (v, 1); };
    auto add = [&] (const char* id, const char* name, NormalisableRange<float> r, float def, Attr a)
    { p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, r, def, a)); };

    add (ids::inGain,  "Input",  { -24.0f, 24.0f, 0.1f }, 0.0f, Attr().withLabel ("dB").withStringFromValueFunction (db));
    add (ids::outGain, "Output", { -24.0f, 12.0f, 0.1f }, 0.0f, Attr().withLabel ("dB").withStringFromValueFunction (db));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids::autoInput, 1 }, "Auto Input", true));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids::lowCut, 1 }, "Low Cut", true));

    // EQ : réglage par défaut = une voix déjà un peu ouverte (HI +1.5, AIR +3)
    add (ids::low, "Low", { -10.0f, 10.0f, 0.1f }, 0.0f, Attr().withStringFromValueFunction (bip));
    add (ids::mid, "Mid", { -10.0f, 10.0f, 0.1f }, 0.0f, Attr().withStringFromValueFunction (bip));
    add (ids::hi,  "Hi",  { -10.0f, 10.0f, 0.1f }, 1.5f, Attr().withStringFromValueFunction (bip));
    add (ids::air, "Air", { -10.0f, 10.0f, 0.1f }, 3.0f, Attr().withStringFromValueFunction (bip));

    const NormalisableRange<float> percent { 0.0f, 100.0f, 1.0f };
    add (ids::comp,    "Comp",      percent, 35.0f, Attr().withLabel ("%").withStringFromValueFunction (pct));
    add (ids::multi,   "Multiband", percent, 20.0f, Attr().withLabel ("%").withStringFromValueFunction (pct));
    add (ids::deess,   "De-ess",    percent, 30.0f, Attr().withLabel ("%").withStringFromValueFunction (pct));
    add (ids::drive,   "Drive",     percent, 10.0f, Attr().withLabel ("%").withStringFromValueFunction (pct));
    add (ids::phone,   "Phone",     percent, 0.0f,  Attr().withLabel ("%").withStringFromValueFunction (pct));
    add (ids::doubler, "Doubler",   percent, 0.0f,  Attr().withLabel ("%").withStringFromValueFunction (pct));
    add (ids::chorus,  "Chorus",    percent, 0.0f,  Attr().withLabel ("%").withStringFromValueFunction (pct));

    add (ids::delay,  "Delay",       percent, 0.0f,  Attr().withLabel ("%").withStringFromValueFunction (pct));
    add (ids::feed,   "Delay Feed",  percent, 35.0f, Attr().withLabel ("%").withStringFromValueFunction (pct));
    add (ids::filter, "Delay Filter", { -100.0f, 100.0f, 1.0f }, 0.0f, Attr().withStringFromValueFunction ([] (float v, int)
    {
        const int i = roundToInt (v);
        return i == 0 ? String ("Warm") : (i < 0 ? "Dark " + String (-i) : "Thin " + String (i));
    }));
    StringArray divs;
    for (auto* n : mini::kDivisionNames) divs.add (n);
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ids::time, 1 }, "Delay Time", divs, 5));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids::ping, 1 }, "Ping-Pong", true));

    add (ids::rShort, "Reverb Short", percent, 0.0f,  Attr().withLabel ("%").withStringFromValueFunction (pct));
    add (ids::rSmall, "Reverb Small", percent, 15.0f, Attr().withLabel ("%").withStringFromValueFunction (pct));
    add (ids::rBig,   "Reverb Big",   percent, 0.0f,  Attr().withLabel ("%").withStringFromValueFunction (pct));
    add (ids::rLush,  "Reverb Lush",  percent, 0.0f,  Attr().withLabel ("%").withStringFromValueFunction (pct));

    // LIVE par défaut : on enregistre en s'écoutant sans latence ; MIX une fois l'enregistrement fini
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ids::mode, 1 }, "Mode", StringArray { "Live", "Mix" }, 0));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids::bypass, 1 }, "Bypass", false));
    return { p.begin(), p.end() };
}
} // namespace onirique
