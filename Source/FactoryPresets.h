#pragma once

#include <juce_core/juce_core.h>
#include <map>
#include <vector>

namespace onirique
{
/*  Presets d'usine de VOCAL MINI PRO. Valeurs en unités réelles (celles affichées) ; un réglage non cité reprend sa
    valeur par défaut (celle de « Lead Clean Vocal »). EQ -10..+10, le reste en %, sauf : delayFilter -100..+100,
    revDecay en s, revPreDelay en ms, revLowCut / revHighCut en Hz, gateThresh / gateRange en dB, gateRelease en ms.
    revMode : 0 ROOM, 1 PLATE 1, 2 PLATE 2, 3 HALL, 4 SHIMMER.
    delayTime : 0 1/32, 1 1/16T, 2 1/16, 3 1/16D, 4 1/8T, 5 1/8, 6 1/8D, 7 1/4T, 8 1/4, 9 1/4D, 10 1/2, 11 1/2D, 12 1/1
    Ce sont des points de départ : aucun ne pousse tout au maximum ; chacun est passé dans les essais du moteur sur des
    voix réelles (niveau, crêtes, réduction, stabilité). Le DE-PLOSIVE, le NOISE SUPPRESSOR et le GATE ne
    sont allumés que là où la prise est supposée imparfaite (Home Recording Cleanup, Podcast Voice ;
    Intimate Vocal, chanté près du micro, a le DE-PLOSIVE). */
struct FactoryPreset
{
    juce::String name;
    std::map<juce::String, float> values;
};

inline const std::vector<FactoryPreset>& factoryPresets()
{
    static const std::vector<FactoryPreset> list = []
    {
        using V = std::map<juce::String, float>;
        return std::vector<FactoryPreset> {
            { "Lead Clean Vocal", V {} },
            { "Natural Vocal", V { { "eqMid", 0.0f }, { "eqHi", 1.0f }, { "eqAir", 1.5f }, { "comp", 25.0f }, { "multiband", 10.0f }, { "deess", 25.0f },
                                   { "drive", 0.0f }, { "revMode", 0.0f }, { "revMix", 12.0f }, { "revDecay", 0.7f }, { "revPreDelay", 5.0f } } },
            { "Modern Melodic Rap", V { { "eqLow", 0.5f }, { "eqMid", -1.5f }, { "eqHi", 2.5f }, { "eqAir", 4.5f }, { "comp", 55.0f },
                                        { "multiband", 30.0f }, { "deess", 40.0f }, { "drive", 15.0f }, { "chorus", 10.0f }, { "delay", 18.0f },
                                        { "delayFeed", 38.0f }, { "delayFilter", -30.0f }, { "delayTime", 8.0f }, { "revMode", 2.0f }, { "revMix", 18.0f },
                                        { "revDecay", 1.8f }, { "revPreDelay", 25.0f }, { "revClean", 60.0f }, { "revDucking", 35.0f } } },
            { "French Rap Lead", V { { "eqLow", 1.5f }, { "eqMid", 2.0f }, { "eqHi", 2.5f }, { "eqAir", 3.0f }, { "comp", 60.0f }, { "multiband", 35.0f },
                                     { "deess", 45.0f }, { "drive", 20.0f }, { "revMode", 0.0f }, { "revMix", 14.0f }, { "revDecay", 0.5f },
                                     { "revPreDelay", 5.0f }, { "revClean", 50.0f } } },
            { "Aggressive Rap", V { { "eqLow", 1.0f }, { "eqMid", 3.0f }, { "eqHi", 2.5f }, { "eqAir", 2.0f }, { "comp", 75.0f }, { "multiband", 45.0f },
                                    { "deess", 45.0f }, { "drive", 40.0f }, { "delay", 10.0f }, { "delayFeed", 25.0f }, { "delayTime", 5.0f },
                                    { "revMode", 0.0f }, { "revMix", 10.0f }, { "revDecay", 0.5f }, { "revPreDelay", 4.0f } } },
            { "Dry And Present", V { { "eqMid", 2.0f }, { "eqHi", 2.5f }, { "eqAir", 3.0f }, { "comp", 55.0f }, { "multiband", 30.0f }, { "deess", 40.0f },
                                     { "drive", 15.0f }, { "revMix", 0.0f } } },
            { "Intimate Vocal", V { { "plosiveOn", 1.0f }, { "plosiveAmount", 45.0f },
                                    { "eqLow", 2.5f }, { "eqMid", -1.0f }, { "eqHi", 1.0f }, { "eqAir", 2.0f }, { "comp", 30.0f }, { "multiband", 15.0f },
                                    { "deess", 25.0f }, { "drive", 10.0f }, { "revMode", 0.0f }, { "revMix", 18.0f }, { "revDecay", 0.6f },
                                    { "revPreDelay", 4.0f } } },
            { "Smooth Pop Vocal", V { { "eqLow", 1.0f }, { "eqMid", -1.5f }, { "eqHi", 2.5f }, { "eqAir", 5.0f }, { "comp", 45.0f }, { "multiband", 30.0f },
                                      { "deess", 40.0f }, { "drive", 8.0f }, { "delay", 12.0f }, { "delayFeed", 30.0f }, { "delayFilter", -20.0f },
                                      { "delayTime", 6.0f }, { "revMix", 18.0f }, { "revDecay", 1.6f }, { "revPreDelay", 20.0f } } },
            { "Bright Vocal", V { { "eqLow", -1.0f }, { "eqMid", 0.5f }, { "eqHi", 3.5f }, { "eqAir", 6.0f }, { "comp", 45.0f }, { "multiband", 30.0f },
                                  { "deess", 50.0f }, { "drive", 8.0f } } },
            { "Dark Vocal", V { { "eqLow", 2.0f }, { "eqMid", -1.0f }, { "eqHi", -2.0f }, { "eqAir", -3.0f }, { "comp", 45.0f }, { "multiband", 20.0f },
                                { "deess", 25.0f }, { "drive", 25.0f }, { "revMode", 2.0f }, { "revDecay", 1.6f }, { "revHighCut", 6000.0f } } },
            { "Heavy Vocal Compression", V { { "eqMid", 1.5f }, { "comp", 85.0f }, { "multiband", 60.0f }, { "deess", 45.0f }, { "drive", 20.0f },
                                             { "revMix", 12.0f } } },
            { "Wide Atmospheric Vocal", V { { "eqLow", -1.0f }, { "eqAir", 5.0f }, { "deess", 40.0f }, { "doubler", 45.0f }, { "chorus", 25.0f },
                                            { "delay", 25.0f }, { "delayFeed", 45.0f }, { "delayFilter", -25.0f }, { "delayTime", 9.0f },
                                            { "revMode", 3.0f }, { "revMix", 28.0f }, { "revDecay", 3.5f }, { "revPreDelay", 30.0f },
                                            { "revClean", 60.0f }, { "revDucking", 40.0f }, { "revWidth", 130.0f } } },
            { "Plate Vocal", V { { "revMode", 1.0f }, { "revMix", 25.0f }, { "revDecay", 1.8f }, { "revPreDelay", 20.0f }, { "revClean", 50.0f },
                                 { "revDucking", 30.0f }, { "revHighCut", 10000.0f } } },
            { "Hall Vocal", V { { "revMode", 3.0f }, { "revMix", 22.0f }, { "revDecay", 2.8f }, { "revPreDelay", 35.0f }, { "revLowCut", 250.0f },
                                { "revHighCut", 7500.0f }, { "revClean", 55.0f }, { "revDucking", 35.0f } } },
            { "Home Recording Cleanup", V { { "plosiveOn", 1.0f }, { "plosiveAmount", 60.0f },
                                            { "nsOn", 1.0f }, { "nsAmount", 55.0f }, { "gateOn", 1.0f }, { "gateThresh", -50.0f }, { "gateRange", 15.0f },
                                            { "gateRelease", 250.0f }, { "eqLow", -2.0f }, { "eqMid", -2.0f }, { "eqAir", 2.5f }, { "multiband", 35.0f },
                                            { "deess", 40.0f }, { "drive", 8.0f }, { "revMix", 10.0f }, { "revDecay", 1.0f } } },
            { "Dream Hook", V { { "eqAir", 5.0f }, { "comp", 40.0f }, { "doubler", 45.0f }, { "chorus", 20.0f }, { "delay", 25.0f }, { "delayFeed", 45.0f },
                                { "delayTime", 9.0f }, { "revMode", 4.0f }, { "revMix", 30.0f }, { "revDecay", 4.5f }, { "revPreDelay", 25.0f },
                                { "revClean", 50.0f } } },
            { "Ad-libs Wide", V { { "eqLow", -3.0f }, { "eqMid", 2.0f }, { "eqHi", 3.0f }, { "comp", 55.0f }, { "doubler", 60.0f }, { "chorus", 25.0f },
                                  { "delay", 30.0f }, { "delayFeed", 35.0f }, { "revMix", 20.0f } } },
            { "Backs & Harmonies", V { { "eqLow", -4.0f }, { "eqMid", -2.0f }, { "eqHi", 1.0f }, { "eqAir", 4.0f }, { "comp", 50.0f }, { "multiband", 40.0f },
                                       { "deess", 45.0f }, { "doubler", 40.0f }, { "chorus", 35.0f }, { "revMode", 2.0f }, { "revMix", 25.0f },
                                       { "revDecay", 2.2f } } },
            { "Radio Phone", V { { "eqLow", -4.0f }, { "comp", 55.0f }, { "drive", 30.0f }, { "phone", 75.0f }, { "revMode", 0.0f }, { "revMix", 15.0f },
                                 { "revDecay", 0.45f } } },
            { "Podcast Voice", V { { "plosiveOn", 1.0f }, { "plosiveAmount", 50.0f },
                                   { "nsOn", 1.0f }, { "nsAmount", 40.0f }, { "gateOn", 1.0f }, { "gateThresh", -55.0f }, { "gateRange", 12.0f },
                                   { "eqLow", 1.0f }, { "eqMid", 1.5f }, { "eqHi", 1.5f }, { "eqAir", 1.0f }, { "comp", 55.0f }, { "multiband", 45.0f },
                                   { "deess", 35.0f }, { "drive", 5.0f }, { "revMix", 0.0f } } },
            { "Neutral", V { { "eqMid", 0.0f }, { "eqHi", 0.0f }, { "eqAir", 0.0f }, { "comp", 0.0f }, { "multiband", 0.0f }, { "deess", 0.0f }, { "drive", 0.0f },
                             { "revMix", 0.0f } } },
        };
    }();
    return list;
}
} // namespace onirique
