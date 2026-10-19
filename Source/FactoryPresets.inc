    // Presets d'usine (inclus dans PresetManager). Valeurs en unités réelles ; un réglage non cité reprend sa valeur par
    // défaut (celle de « Lead Clean Vocal »). EQ -10..+10, le reste en %, filtre du delay -100..+100.
    // delayTime : 0 1/32, 1 1/16T, 2 1/16, 3 1/16D, 4 1/8T, 5 1/8, 6 1/8D, 7 1/4T, 8 1/4, 9 1/4D, 10 1/2, 11 1/2D, 12 1/1
    static Factory preset (const char* name, std::initializer_list<std::pair<const char*, float>> v)
    {
        Factory f { name, {} };
        for (const auto& [id, value] : v) f.values[id] = value;
        return f;
    }

    static std::vector<Factory> makeFactory()
    {
        return {
            preset ("Lead Clean Vocal", {}),
            preset ("Neutral", { { "eqMid", 0 }, { "eqHi", 0 }, { "eqAir", 0 }, { "comp", 0 }, { "multiband", 0 }, { "deess", 0 }, { "drive", 0 },
                                 { "revSmall", 0 } }),
            preset ("Rap Lead", { { "eqLow", 1.5f }, { "eqMid", 2 }, { "eqHi", 2.5f }, { "eqAir", 3 }, { "comp", 60 }, { "multiband", 35 },
                                  { "deess", 40 }, { "drive", 20 }, { "revShort", 20 }, { "revSmall", 0 } }),
            preset ("Pop Shine", { { "eqLow", 1 }, { "eqMid", -1.5f }, { "eqHi", 3 }, { "eqAir", 5.5f }, { "comp", 45 }, { "multiband", 30 },
                                   { "deess", 40 }, { "drive", 8 }, { "delay", 12 }, { "delayFeed", 30 }, { "delayFilter", -20 },
                                   { "delayTime", 6 }, { "revSmall", 18 }, { "revBig", 10 } }),
            preset ("Melodic Trap", { { "eqMid", -2 }, { "eqHi", 2.5f }, { "eqAir", 4 }, { "comp", 50 }, { "multiband", 25 }, { "deess", 35 },
                                      { "drive", 15 }, { "chorus", 15 }, { "delay", 20 }, { "delayFeed", 40 }, { "delayFilter", -30 },
                                      { "delayTime", 8 }, { "revSmall", 0 }, { "revLush", 18 } }),
            preset ("Dream Hook", { { "eqAir", 5 }, { "comp", 40 }, { "doubler", 45 }, { "chorus", 20 }, { "delay", 25 }, { "delayFeed", 45 },
                                    { "delayTime", 9 }, { "revSmall", 0 }, { "revBig", 15 }, { "revLush", 35 } }),
            preset ("Ad-libs Wide", { { "eqLow", -3 }, { "eqMid", 2 }, { "eqHi", 3 }, { "comp", 55 }, { "doubler", 60 }, { "chorus", 25 },
                                      { "delay", 30 }, { "delayFeed", 35 }, { "revSmall", 20 } }),
            preset ("Backs & Harmonies", { { "eqLow", -4 }, { "eqMid", -2 }, { "eqHi", 1 }, { "eqAir", 4 }, { "comp", 50 }, { "multiband", 40 },
                                           { "deess", 45 }, { "doubler", 40 }, { "chorus", 35 }, { "revSmall", 0 }, { "revBig", 25 } }),
            preset ("Radio Phone", { { "eqLow", -4 }, { "comp", 55 }, { "drive", 30 }, { "phone", 75 }, { "revSmall", 0 }, { "revShort", 15 } }),
            preset ("Intimate", { { "eqLow", 2.5f }, { "eqMid", -1 }, { "eqHi", 1 }, { "eqAir", 2 }, { "comp", 30 }, { "multiband", 15 },
                                  { "deess", 25 }, { "drive", 12 }, { "revSmall", 0 }, { "revShort", 25 } }),
            preset ("Gritty Rock", { { "eqMid", 3 }, { "eqHi", 2 }, { "comp", 65 }, { "drive", 55 }, { "delay", 10 }, { "delayTime", 8 } }),
            preset ("Podcast Voice", { { "eqLow", 1 }, { "eqMid", 1.5f }, { "eqHi", 1.5f }, { "eqAir", 1 }, { "comp", 55 }, { "multiband", 45 },
                                       { "deess", 35 }, { "drive", 5 }, { "revSmall", 0 } }),
        };
    }
