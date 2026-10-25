#pragma once

#include "Parameters.h"
#include "ui/OniriquePresets.h"

namespace onirique
{
/*  Presets d'usine de VOCAL MINI PRO, rangés par usage pour le navigateur de la barre (les tags). Valeurs en unités
    réelles (celles affichées) ; un réglage non cité reprend sa valeur par défaut (celle de « Lead Clean Vocal »).
    EQ -10..+10, le reste en %, sauf : delayFilter -100..+100, revDecay en s, revPreDelay en ms, revLowCut / revHighCut
    en Hz, gateThresh / gateRange en dB, gateRelease en ms.
    revMode : 0 ROOM, 1 PLATE 1, 2 PLATE 2, 3 HALL, 4 SHIMMER.
    delayTime : 0 1/32, 1 1/16T, 2 1/16, 3 1/16D, 4 1/8T, 5 1/8, 6 1/8D, 7 1/4T, 8 1/4, 9 1/4D, 10 1/2, 11 1/2D, 12 1/1
    Ce sont des points de départ : aucun ne pousse tout au maximum. Le DE-PLOSIVE, le NOISE SUPPRESSOR et le GATE ne
    sont allumés que là où la prise est supposée imparfaite. */
namespace vocalmini
{
enum Tags { lead = 1, rap = 2, pop = 4, rnb = 8, backs = 16, effects = 32, cleanup = 64, spoken = 128 };

inline juce::StringArray tagNames() { return { "Lead", "Rap", "Pop", "R&B", "Backs & Ad-libs", "Effects", "Clean-up", "Spoken" }; }

/** Les réglages qui disent comment on écoute le plugin plutôt que ce qu'il fait au son : un preset les laisse. */
inline juce::StringArray notStored() { return { ids::bypass, ids::mode }; }

inline std::vector<PresetBank::Stock> stockPresets()
{
    return {
        { "Lead Clean Vocal", lead | pop, {} },
        { "Natural Lead", lead | pop | rnb,
          { { "eqMid", 0.0f }, { "eqHi", 1.0f }, { "eqAir", 1.5f }, { "comp", 25.0f }, { "multiband", 10.0f }, { "deess", 25.0f },
            { "drive", 0.0f }, { "revMode", 0.0f }, { "revMix", 12.0f }, { "revDecay", 0.7f }, { "revPreDelay", 5.0f } } },
        { "Dry And Present", lead | rap,
          { { "eqMid", 2.0f }, { "eqHi", 2.5f }, { "eqAir", 3.0f }, { "comp", 55.0f }, { "multiband", 30.0f }, { "deess", 40.0f },
            { "drive", 15.0f }, { "revMix", 0.0f } } },
        { "Melodic Rap Lead", lead | rap,
          { { "eqLow", 0.5f }, { "eqMid", -1.5f }, { "eqHi", 2.5f }, { "eqAir", 4.5f }, { "comp", 55.0f }, { "multiband", 30.0f },
            { "deess", 40.0f }, { "drive", 15.0f }, { "chorus", 10.0f }, { "delay", 18.0f }, { "delayFeed", 38.0f },
            { "delayFilter", -30.0f }, { "delayTime", 8.0f }, { "revMode", 2.0f }, { "revMix", 18.0f }, { "revDecay", 1.8f },
            { "revPreDelay", 25.0f }, { "revClean", 60.0f }, { "revDucking", 35.0f } } },
        { "French Rap Lead", lead | rap,
          { { "eqLow", 1.5f }, { "eqMid", 2.0f }, { "eqHi", 2.5f }, { "eqAir", 3.0f }, { "comp", 60.0f }, { "multiband", 35.0f },
            { "deess", 45.0f }, { "drive", 20.0f }, { "revMode", 0.0f }, { "revMix", 14.0f }, { "revDecay", 0.5f },
            { "revPreDelay", 5.0f }, { "revClean", 50.0f } } },
        { "Aggressive Rap", rap,
          { { "eqLow", 1.0f }, { "eqMid", 3.0f }, { "eqHi", 2.5f }, { "eqAir", 2.0f }, { "comp", 75.0f }, { "multiband", 45.0f },
            { "deess", 45.0f }, { "drive", 40.0f }, { "delay", 10.0f }, { "delayFeed", 25.0f }, { "delayTime", 5.0f },
            { "revMode", 0.0f }, { "revMix", 10.0f }, { "revDecay", 0.5f }, { "revPreDelay", 4.0f } } },
        { "Drill Upfront", rap,
          { { "eqLow", 1.5f }, { "eqMid", 2.5f }, { "eqHi", 3.0f }, { "eqAir", 2.5f }, { "comp", 70.0f }, { "multiband", 40.0f },
            { "deess", 45.0f }, { "drive", 30.0f }, { "delay", 8.0f }, { "delayFeed", 20.0f }, { "delayTime", 2.0f },
            { "revMode", 0.0f }, { "revMix", 8.0f }, { "revDecay", 0.4f }, { "revPreDelay", 3.0f } } },
        { "Heavy Compression", lead | rap,
          { { "eqMid", 1.5f }, { "comp", 85.0f }, { "multiband", 60.0f }, { "deess", 45.0f }, { "drive", 20.0f }, { "revMix", 12.0f } } },
        { "Smooth Pop Lead", lead | pop,
          { { "eqLow", 1.0f }, { "eqMid", -1.5f }, { "eqHi", 2.5f }, { "eqAir", 5.0f }, { "comp", 45.0f }, { "multiband", 30.0f },
            { "deess", 40.0f }, { "drive", 8.0f }, { "delay", 12.0f }, { "delayFeed", 30.0f }, { "delayFilter", -20.0f },
            { "delayTime", 6.0f }, { "revMix", 18.0f }, { "revDecay", 1.6f }, { "revPreDelay", 20.0f } } },
        { "Bright Pop Air", lead | pop,
          { { "eqLow", -1.0f }, { "eqMid", 0.5f }, { "eqHi", 3.5f }, { "eqAir", 6.0f }, { "comp", 45.0f }, { "multiband", 30.0f },
            { "deess", 50.0f }, { "drive", 8.0f } } },
        { "R&B Silk", lead | rnb,
          { { "eqLow", 1.5f }, { "eqMid", -1.5f }, { "eqHi", 1.5f }, { "eqAir", 4.5f }, { "comp", 40.0f }, { "multiband", 30.0f },
            { "deess", 40.0f }, { "drive", 6.0f }, { "chorus", 12.0f }, { "delay", 14.0f }, { "delayFeed", 35.0f },
            { "delayFilter", -30.0f }, { "delayTime", 8.0f }, { "revMode", 2.0f }, { "revMix", 22.0f }, { "revDecay", 2.4f },
            { "revPreDelay", 30.0f }, { "revClean", 55.0f }, { "revDucking", 35.0f } } },
        { "Intimate Close Mic", lead | rnb,
          { { "plosiveOn", 1.0f }, { "plosiveAmount", 45.0f }, { "eqLow", 2.5f }, { "eqMid", -1.0f }, { "eqHi", 1.0f }, { "eqAir", 2.0f },
            { "comp", 30.0f }, { "multiband", 15.0f }, { "deess", 25.0f }, { "drive", 10.0f }, { "revMode", 0.0f }, { "revMix", 18.0f },
            { "revDecay", 0.6f }, { "revPreDelay", 4.0f } } },
        { "Plate Vocal", lead | pop | rnb,
          { { "revMode", 1.0f }, { "revMix", 25.0f }, { "revDecay", 1.8f }, { "revPreDelay", 20.0f }, { "revClean", 50.0f },
            { "revDucking", 30.0f }, { "revHighCut", 10000.0f } } },
        { "Hall Vocal", lead | rnb,
          { { "revMode", 3.0f }, { "revMix", 22.0f }, { "revDecay", 2.8f }, { "revPreDelay", 35.0f }, { "revLowCut", 250.0f },
            { "revHighCut", 7500.0f }, { "revClean", 55.0f }, { "revDucking", 35.0f } } },
        { "Dark Vintage", lead | effects,
          { { "eqLow", 2.0f }, { "eqMid", -1.0f }, { "eqHi", -2.0f }, { "eqAir", -3.0f }, { "comp", 45.0f }, { "multiband", 20.0f },
            { "deess", 25.0f }, { "drive", 25.0f }, { "revMode", 2.0f }, { "revDecay", 1.6f }, { "revHighCut", 6000.0f } } },
        { "Wide Atmosphere", effects | pop,
          { { "eqLow", -1.0f }, { "eqAir", 5.0f }, { "deess", 40.0f }, { "doubler", 45.0f }, { "chorus", 25.0f }, { "delay", 25.0f },
            { "delayFeed", 45.0f }, { "delayFilter", -25.0f }, { "delayTime", 9.0f }, { "revMode", 3.0f }, { "revMix", 28.0f },
            { "revDecay", 3.5f }, { "revPreDelay", 30.0f }, { "revClean", 60.0f }, { "revDucking", 40.0f }, { "revWidth", 130.0f } } },
        { "Dream Hook", effects | pop,
          { { "eqAir", 5.0f }, { "comp", 40.0f }, { "doubler", 45.0f }, { "chorus", 20.0f }, { "delay", 25.0f }, { "delayFeed", 45.0f },
            { "delayTime", 9.0f }, { "revMode", 4.0f }, { "revMix", 30.0f }, { "revDecay", 4.5f }, { "revPreDelay", 25.0f },
            { "revClean", 50.0f } } },
        { "Radio Phone", effects,
          { { "eqLow", -4.0f }, { "comp", 55.0f }, { "drive", 30.0f }, { "phone", 75.0f }, { "revMode", 0.0f }, { "revMix", 15.0f },
            { "revDecay", 0.45f } } },
        { "Ad-libs Wide", backs | rap,
          { { "eqLow", -3.0f }, { "eqMid", 2.0f }, { "eqHi", 3.0f }, { "comp", 55.0f }, { "doubler", 60.0f }, { "chorus", 25.0f },
            { "delay", 30.0f }, { "delayFeed", 35.0f }, { "revMix", 20.0f } } },
        { "Ad-libs Telephone", backs | effects,
          { { "eqLow", -4.0f }, { "comp", 60.0f }, { "drive", 35.0f }, { "phone", 85.0f }, { "doubler", 30.0f }, { "delay", 22.0f },
            { "delayFeed", 30.0f }, { "delayFilter", 30.0f }, { "delayTime", 5.0f }, { "revMode", 0.0f }, { "revMix", 12.0f },
            { "revDecay", 0.5f } } },
        { "Backs & Harmonies", backs | pop | rnb,
          { { "eqLow", -4.0f }, { "eqMid", -2.0f }, { "eqHi", 1.0f }, { "eqAir", 4.0f }, { "comp", 50.0f }, { "multiband", 40.0f },
            { "deess", 45.0f }, { "doubler", 40.0f }, { "chorus", 35.0f }, { "revMode", 2.0f }, { "revMix", 25.0f }, { "revDecay", 2.2f } } },
        { "Home Recording Cleanup", cleanup | lead,
          { { "plosiveOn", 1.0f }, { "plosiveAmount", 60.0f }, { "nsOn", 1.0f }, { "nsAmount", 55.0f }, { "gateOn", 1.0f },
            { "gateThresh", -50.0f }, { "gateRange", 15.0f }, { "gateRelease", 250.0f }, { "eqLow", -2.0f }, { "eqMid", -2.0f },
            { "eqAir", 2.5f }, { "multiband", 35.0f }, { "deess", 40.0f }, { "drive", 8.0f }, { "revMix", 10.0f }, { "revDecay", 1.0f } } },
        { "Podcast Voice", spoken | cleanup,
          { { "plosiveOn", 1.0f }, { "plosiveAmount", 50.0f }, { "nsOn", 1.0f }, { "nsAmount", 40.0f }, { "gateOn", 1.0f },
            { "gateThresh", -55.0f }, { "gateRange", 12.0f }, { "eqLow", 1.0f }, { "eqMid", 1.5f }, { "eqHi", 1.5f }, { "eqAir", 1.0f },
            { "comp", 55.0f }, { "multiband", 45.0f }, { "deess", 35.0f }, { "drive", 5.0f }, { "revMix", 0.0f } } },
        { "Voice-over Broadcast", spoken,
          { { "plosiveOn", 1.0f }, { "plosiveAmount", 45.0f }, { "nsOn", 1.0f }, { "nsAmount", 30.0f }, { "gateOn", 1.0f },
            { "gateThresh", -55.0f }, { "gateRange", 10.0f }, { "eqLow", 2.0f }, { "eqMid", 1.0f }, { "eqHi", 1.5f }, { "eqAir", 1.5f },
            { "comp", 60.0f }, { "multiband", 50.0f }, { "deess", 35.0f }, { "drive", 8.0f }, { "revMix", 0.0f } } },
        { "Neutral", 0,
          { { "eqMid", 0.0f }, { "eqHi", 0.0f }, { "eqAir", 0.0f }, { "comp", 0.0f }, { "multiband", 0.0f }, { "deess", 0.0f },
            { "drive", 0.0f }, { "revMix", 0.0f } } },
    };
}

/** Les presets enregistrés avec l'ancienne barre (fichiers .ovmpreset du même dossier) passent dans le navigateur,
    une fois : chacun est recopié au format de la nouvelle barre, l'original reste en place. */
inline void importOldUserPresets (const PresetBank& bank)
{
    for (const auto& file : bank.getFolder().findChildFiles (juce::File::findFiles, false, "*.ovmpreset"))
    {
        const auto target = file.withFileExtension (bank.getExtension());
        if (target.existsAsFile())
            continue;

        if (const auto xml = juce::XmlDocument::parse (file))
        {
            std::map<juce::String, float> values;
            for (auto* p : xml->getChildWithTagNameIterator ("PARAM"))
                values[p->getStringAttribute ("id")] = (float) p->getDoubleAttribute ("value");
            prm::migrateLegacy (values);

            juce::ValueTree snapshot ("Snapshot");
            for (const auto& [id, value] : values)
                if (! notStored().contains (id))
                    snapshot.setProperty (id, value, nullptr);

            if (const auto out = snapshot.createXml())
                out->writeTo (target);
        }
    }
}
} // namespace vocalmini
} // namespace onirique
