#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <map>
#include "DSP/MiniChain.h"

namespace onirique
{
/*  Tous les paramètres de VOCAL MINI PRO dans UNE table : identifiant (jamais modifié une fois publié : sessions et
    automations en dépendent), nom affiché au DAW, unité, plage, pas, valeur par défaut et centre de la course (les
    fréquences et les temps ont une course logarithmique : le milieu du potard tombe sur une valeur utile).
    La même table sert à l'hôte (createLayout), aux presets et aux essais du moteur (toChain) : ce qui est testé est
    exactement ce que le plugin fait tourner. */
namespace ids
{
    inline constexpr const char* inGain    = "inGain";      // IN (dB)
    inline constexpr const char* outGain   = "outGain";     // OUT (dB)
    inline constexpr const char* autoInput = "autoInput";   // AUTO INPUT : gain d'entrée automatique
    inline constexpr const char* lowCut    = "lowCut";      // LOW CUT 90 Hz
    inline constexpr const char* plosOn    = "plosiveOn";   // DE-PLOSIVE
    inline constexpr const char* plosAmount = "plosiveAmount";
    inline constexpr const char* nsOn      = "nsOn";        // NOISE SUPPRESSOR
    inline constexpr const char* nsAmount  = "nsAmount";
    inline constexpr const char* gateOn    = "gateOn";      // NOISE GATE
    inline constexpr const char* gateThr   = "gateThresh";
    inline constexpr const char* gateRange = "gateRange";
    inline constexpr const char* gateRel   = "gateRelease";
    inline constexpr const char* low       = "eqLow";       // EQ, -10..+10
    inline constexpr const char* mid       = "eqMid";
    inline constexpr const char* hi        = "eqHi";
    inline constexpr const char* air       = "eqAir";
    inline constexpr const char* comp      = "comp";        // COMP (%)
    inline constexpr const char* compAuto  = "compAutoGain";
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
    inline constexpr const char* revMode   = "revMode";     // REVERB
    inline constexpr const char* revMix    = "revMix";
    inline constexpr const char* revDecay  = "revDecay";
    inline constexpr const char* revPre    = "revPreDelay";
    inline constexpr const char* revSize   = "revSize";
    inline constexpr const char* revDiff   = "revDiffusion";
    inline constexpr const char* revRate   = "revModRate";
    inline constexpr const char* revDepth  = "revModDepth";
    inline constexpr const char* revLowCut = "revLowCut";
    inline constexpr const char* revHiCut  = "revHighCut";
    inline constexpr const char* revWidth  = "revWidth";
    inline constexpr const char* revDuck   = "revDucking";
    inline constexpr const char* revClean  = "revClean";
    inline constexpr const char* revPulse  = "revPulse";
    inline constexpr const char* revPulseRate = "revPulseRate";
    inline constexpr const char* peqPower  = "peqPower";    // EQ PARAMÉTRIQUE : interrupteur général
    inline constexpr const char* mode      = "mode";        // LIVE (latence nulle, enregistrement) / MIX (qualité maximale)
    inline constexpr const char* bypass    = "bypass";

    // MULTIBAND, bande b (0..3) : "mbThr1", "mbRatio1", ...
    inline juce::String band (const char* what, int b) { return juce::String (what) + juce::String (b + 1); }

    // EQ PARAMÉTRIQUE, bande b (0..7) : "peq1On", "peq1Freq", ... (mêmes identifiants que l'EQ de VOCALBOX)
    inline juce::String peq (const char* what, int b) { return "peq" + juce::String (b + 1) + what; }
}

namespace prm
{
enum Id : int
{
    inGain = 0, outGain, autoInput, lowCut,
    plosOn, plosAmount, nsOn, nsAmount, gateOn, gateThr, gateRange, gateRel,
    low, mid, hi, air,
    comp, compAuto, multi,
    mbThr, mbRatio = mbThr + 4, mbAttack = mbRatio + 4, mbRelease = mbAttack + 4, mbKnee = mbRelease + 4,
    mbGain = mbKnee + 4, mbOn = mbGain + 4,
    deess = mbOn + 4, drive, phone, doubler, chorus,
    delay, feed, filter, time, ping,
    revMode, revMix, revDecay, revPre, revSize, revDiff, revRate, revDepth, revLowCut, revHiCut, revWidth,
    revDuck, revClean, revPulse, revPulseRate,
    peqPower, peqOn, peqType = peqOn + 8, peqFreq = peqType + 8, peqGain = peqFreq + 8, peqQ = peqGain + 8,
    peqSlope = peqQ + 8, peqDyn = peqSlope + 8, peqThr = peqDyn + 8,
    mode = peqThr + 8, bypass,
    count
};

enum class Unit { db, pct, bipolar, hz, ms, sec, ratio, choice, toggle, delayFilter, q };

inline const juce::StringArray& peqTypeNames()
{
    static const juce::StringArray n { "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut", "Notch" };
    return n;
}
inline const juce::StringArray& peqSlopeNames() { static const juce::StringArray n { "12 dB/oct", "24 dB/oct", "36 dB/oct", "48 dB/oct" }; return n; }

inline const juce::StringArray& revModeNames() { static const juce::StringArray n { "ROOM", "PLATE 1", "PLATE 2", "HALL", "SHIMMER" }; return n; }
inline const juce::StringArray& pulseRateNames()
{
    static const juce::StringArray n = [] { juce::StringArray a; for (auto* s : mini::kPulseRateNames) a.add (s); return a; }();
    return n;
}
inline const juce::StringArray& divisionNames()
{
    static const juce::StringArray n = [] { juce::StringArray a; for (auto* s : mini::kDivisionNames) a.add (s); return a; }();
    return n;
}
inline const juce::StringArray& modeNames() { static const juce::StringArray n { "Live", "Mix" }; return n; }

struct Spec
{
    juce::String id, name;
    Unit unit = Unit::pct;
    float min = 0.0f, max = 100.0f, step = 1.0f, def = 0.0f, centre = 0.0f;   // centre > 0 : milieu de la course
    const juce::StringArray* choices = nullptr;
    bool logScale = false;   // course exactement logarithmique (fréquence et Q de l'EQ : une octave = la même distance)
};

inline const std::array<Spec, count>& specs()
{
    static const std::array<Spec, count> s = []
    {
        std::array<Spec, count> t;
        auto set = [&t] (int i, const char* id, const char* name, Unit u, float mn, float mx, float st, float def, float c = 0.0f,
                         const juce::StringArray* ch = nullptr)
        { t[(size_t) i] = Spec { id, name, u, mn, mx, st, def, c, ch }; };
        auto bandSet = [&t] (int first, const char* id, const char* name, Unit u, float mn, float mx, float st,
                             std::array<float, 4> def, float c = 0.0f)
        {
            static const char* bandNames[4] { "Body", "Chest", "Pres", "Shine" };
            for (int b = 0; b < 4; ++b)
                t[(size_t) (first + b)] = Spec { ids::band (id, b), juce::String ("MB ") + bandNames[b] + " " + name, u, mn, mx, st,
                                                 def[(size_t) b], c, nullptr };
        };
        set (inGain,    ids::inGain,    "Input",  Unit::db, -24.0f, 24.0f, 0.1f, 0.0f);
        set (outGain,   ids::outGain,   "Output", Unit::db, -24.0f, 12.0f, 0.1f, 0.0f);
        set (autoInput, ids::autoInput, "Auto Input", Unit::toggle, 0, 1, 1, 1);
        set (lowCut,    ids::lowCut,    "Low Cut", Unit::toggle, 0, 1, 1, 1);

        // nettoyage : prudent par défaut (coupé), à allumer quand la prise en a besoin
        set (plosOn,    ids::plosOn,    "De-Plosive", Unit::toggle, 0, 1, 1, 0);
        set (plosAmount, ids::plosAmount, "De-Plosive Amount", Unit::pct, 0.0f, 100.0f, 1.0f, 50.0f);
        set (nsOn,      ids::nsOn,      "Noise Suppressor", Unit::toggle, 0, 1, 1, 0);
        set (nsAmount,  ids::nsAmount,  "Noise Amount", Unit::pct, 0.0f, 100.0f, 1.0f, 50.0f);
        set (gateOn,    ids::gateOn,    "Gate", Unit::toggle, 0, 1, 1, 0);
        set (gateThr,   ids::gateThr,   "Gate Threshold", Unit::db, -80.0f, -10.0f, 0.5f, -50.0f);
        set (gateRange, ids::gateRange, "Gate Range", Unit::db, 0.0f, 60.0f, 0.5f, 18.0f);
        set (gateRel,   ids::gateRel,   "Gate Release", Unit::ms, 30.0f, 1500.0f, 1.0f, 200.0f, 200.0f);

        // EQ : réglage par défaut = une voix qui ressort déjà (MID +1, HI +2, AIR +3.5)
        set (low, ids::low, "Low", Unit::bipolar, -10.0f, 10.0f, 0.1f, 0.0f);
        set (mid, ids::mid, "Mid", Unit::bipolar, -10.0f, 10.0f, 0.1f, 1.0f);
        set (hi,  ids::hi,  "Hi",  Unit::bipolar, -10.0f, 10.0f, 0.1f, 2.0f);
        set (air, ids::air, "Air", Unit::bipolar, -10.0f, 10.0f, 0.1f, 3.5f);

        set (comp,     ids::comp,     "Comp", Unit::pct, 0.0f, 100.0f, 1.0f, 45.0f);
        set (compAuto, ids::compAuto, "Comp Auto Gain", Unit::toggle, 0, 1, 1, 1);
        set (multi,    ids::multi,    "Multiband", Unit::pct, 0.0f, 100.0f, 1.0f, 25.0f);
        {
            using MB = mini::VoiceMultiband;
            std::array<float, 4> thr {}, ratio {}, atk {}, rel {}, knee {};
            for (int b = 0; b < 4; ++b)
            {
                const auto d = MB::defaultBand (b);
                thr[(size_t) b] = d.thrDb; ratio[(size_t) b] = d.ratio; atk[(size_t) b] = d.atkMs; rel[(size_t) b] = d.relMs;
                knee[(size_t) b] = d.kneeDb;
            }
            bandSet (mbThr,     "mbThr",     "Threshold", Unit::db, -60.0f, 0.0f, 0.5f, thr);
            bandSet (mbRatio,   "mbRatio",   "Ratio", Unit::ratio, 1.0f, 10.0f, 0.1f, ratio, 3.0f);
            bandSet (mbAttack,  "mbAttack",  "Attack", Unit::ms, 0.5f, 100.0f, 0.1f, atk, 10.0f);
            bandSet (mbRelease, "mbRelease", "Release", Unit::ms, 10.0f, 1000.0f, 1.0f, rel, 150.0f);
            bandSet (mbKnee,    "mbKnee",    "Knee", Unit::db, 0.0f, 24.0f, 0.5f, knee);
            bandSet (mbGain,    "mbGain",    "Gain", Unit::db, -12.0f, 12.0f, 0.1f, { 0.0f, 0.0f, 0.0f, 0.0f });
            bandSet (mbOn,      "mbOn",      "On", Unit::toggle, 0, 1, 1, { 1.0f, 1.0f, 1.0f, 1.0f });
        }

        set (deess,   ids::deess,   "De-ess",  Unit::pct, 0.0f, 100.0f, 1.0f, 35.0f);
        set (drive,   ids::drive,   "Drive",   Unit::pct, 0.0f, 100.0f, 1.0f, 12.0f);
        set (phone,   ids::phone,   "Phone",   Unit::pct, 0.0f, 100.0f, 1.0f, 0.0f);
        set (doubler, ids::doubler, "Doubler", Unit::pct, 0.0f, 100.0f, 1.0f, 0.0f);
        set (chorus,  ids::chorus,  "Chorus",  Unit::pct, 0.0f, 100.0f, 1.0f, 0.0f);

        set (delay,  ids::delay,  "Delay",        Unit::pct, 0.0f, 100.0f, 1.0f, 0.0f);
        set (feed,   ids::feed,   "Delay Feed",   Unit::pct, 0.0f, 100.0f, 1.0f, 35.0f);
        set (filter, ids::filter, "Delay Filter", Unit::delayFilter, -100.0f, 100.0f, 1.0f, 0.0f);
        set (time,   ids::time,   "Delay Time",   Unit::choice, 0, (float) mini::kNumDivisions - 1, 1, 5, 0.0f, &divisionNames());
        set (ping,   ids::ping,   "Ping-Pong",    Unit::toggle, 0, 1, 1, 1);

        // REVERB : PLATE 1 en envoi léger (la reverb de voix de tous les jours)
        set (revMode,   ids::revMode,   "Reverb Mode",       Unit::choice, 0, 4, 1, 1, 0.0f, &revModeNames());
        set (revMix,    ids::revMix,    "Reverb Mix",        Unit::pct, 0.0f, 100.0f, 1.0f, 15.0f);
        set (revDecay,  ids::revDecay,  "Reverb Decay",      Unit::sec, 0.2f, 12.0f, 0.01f, 1.2f, 1.6f);
        set (revPre,    ids::revPre,    "Reverb Pre-Delay",  Unit::ms, 0.0f, 250.0f, 0.5f, 15.0f, 40.0f);
        set (revSize,   ids::revSize,   "Reverb Size",       Unit::pct, 0.0f, 100.0f, 1.0f, 50.0f);
        set (revDiff,   ids::revDiff,   "Reverb Diffusion",  Unit::pct, 0.0f, 100.0f, 1.0f, 80.0f);
        set (revRate,   ids::revRate,   "Reverb Mod Rate",   Unit::hz, 0.05f, 5.0f, 0.01f, 0.6f, 0.6f);
        set (revDepth,  ids::revDepth,  "Reverb Mod Depth",  Unit::pct, 0.0f, 100.0f, 1.0f, 35.0f);
        set (revLowCut, ids::revLowCut, "Reverb Low Cut",    Unit::hz, 20.0f, 1000.0f, 1.0f, 200.0f, 180.0f);
        set (revHiCut,  ids::revHiCut,  "Reverb High Cut",   Unit::hz, 1000.0f, 20000.0f, 10.0f, 9000.0f, 6000.0f);
        set (revWidth,  ids::revWidth,  "Reverb Width",      Unit::pct, 0.0f, 150.0f, 1.0f, 100.0f);
        set (revDuck,   ids::revDuck,   "Reverb Ducking",    Unit::pct, 0.0f, 100.0f, 1.0f, 25.0f);
        set (revClean,  ids::revClean,  "Reverb Clean",      Unit::pct, 0.0f, 100.0f, 1.0f, 40.0f);
        set (revPulse,  ids::revPulse,  "Reverb Pulse",      Unit::pct, 0.0f, 100.0f, 1.0f, 0.0f);
        set (revPulseRate, ids::revPulseRate, "Reverb Pulse Rate", Unit::choice, 0, (float) mini::kNumPulseRates - 1, 1, 2, 0.0f,
             &pulseRateNames());

        // EQ PARAMÉTRIQUE : allumé, ses 8 bandes coupées (positions de départ utiles pour la voix) ; les presets d'usine
        // ne s'en servent pas, il reste neutre tant qu'on n'y touche pas
        set (peqPower, ids::peqPower, "Parametric EQ", Unit::toggle, 0, 1, 1, 1);
        for (int b = 0; b < 8; ++b)
        {
            using PE = mini::VoiceParametric;
            const auto d = PE::defaultBand (b);
            const juce::String n = "EQ " + juce::String (b + 1) + " ";
            auto band = [&t, b, &n] (int first, const char* what, const char* name, Unit u, float mn, float mx, float st, float def,
                                     const juce::StringArray* ch = nullptr, bool lg = false)
            { t[(size_t) (first + b)] = Spec { ids::peq (what, b), n + name, u, mn, mx, st, def, 0.0f, ch, lg }; };
            band (peqOn,    "On",     "On",        Unit::toggle, 0, 1, 1, 0);
            band (peqType,  "Type",   "Type",      Unit::choice, 0, (float) PE::numTypes - 1, 1, (float) d.type, &peqTypeNames());
            band (peqFreq,  "Freq",   "Freq",      Unit::hz, 20.0f, 20000.0f, 0.1f, d.freq, nullptr, true);
            band (peqGain,  "Gain",   "Gain",      Unit::db, -18.0f, 18.0f, 0.1f, 0.0f);
            band (peqQ,     "Q",      "Q",         Unit::q, 0.1f, 10.0f, 0.01f, d.q, nullptr, true);
            band (peqSlope, "Slope",  "Slope",     Unit::choice, 0, 3, 1, (float) d.slope, &peqSlopeNames());
            band (peqDyn,   "Dyn",    "Dynamic",   Unit::toggle, 0, 1, 1, 0);
            band (peqThr,   "Thresh", "Threshold", Unit::db, -60.0f, 0.0f, 0.1f, d.thrDb);
        }

        // LIVE par défaut : on enregistre en s'écoutant sans latence ; MIX une fois l'enregistrement fini
        set (mode,   ids::mode,   "Mode",   Unit::choice, 0, 1, 1, 0, 0.0f, &modeNames());
        set (bypass, ids::bypass, "Bypass", Unit::toggle, 0, 1, 1, 0);
        return t;
    }();
    return s;
}

inline const Spec& spec (int i) { return specs()[(size_t) i]; }
inline int indexOf (const juce::String& id)
{
    for (int i = 0; i < count; ++i) if (spec (i).id == id) return i;
    return -1;
}

// texte affiché pour une valeur (unités réelles)
inline juce::String toText (const Spec& s, float v)
{
    using juce::String;
    auto oneDec = [] (float x) { return std::round (x * 10.0f) / 10.0f + 0.0f; };
    switch (s.unit)
    {
        case Unit::db:      { v = oneDec (v); return (v > 0.0f ? "+" : "") + String (v, 1) + " dB"; }
        case Unit::pct:     return String (juce::roundToInt (v)) + " %";
        case Unit::bipolar: { v = oneDec (v); return v == 0.0f ? String ("0") : (v > 0.0f ? "+" : "") + String (v, 1); }
        case Unit::hz:      return v >= 1000.0f ? String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz" : String (v, v < 10.0f ? 2 : 0) + " Hz";
        case Unit::ms:      return v < 10.0f ? String (v, 1) + " ms" : String (juce::roundToInt (v)) + " ms";
        case Unit::sec:     return String (v, v < 10.0f ? 2 : 1) + " s";
        case Unit::ratio:   return String (v, 1) + ":1";
        case Unit::delayFilter:
        {
            const int i = juce::roundToInt (v);
            return i == 0 ? String ("Warm") : (i < 0 ? "Dark " + String (-i) : "Thin " + String (i));
        }
        case Unit::choice:  return s.choices != nullptr ? (*s.choices)[juce::roundToInt (v)] : String (juce::roundToInt (v));
        case Unit::toggle:  return v > 0.5f ? "On" : "Off";
        case Unit::q:       return String (v, 2);
    }
    return String (v);
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    for (int i = 0; i < count; ++i)
    {
        const auto& s = spec (i);
        const ParameterID pid { s.id, 1 };
        if (s.unit == Unit::toggle)
            p.push_back (std::make_unique<AudioParameterBool> (pid, s.name, s.def > 0.5f));
        else if (s.unit == Unit::choice)
            p.push_back (std::make_unique<AudioParameterChoice> (pid, s.name, *s.choices, roundToInt (s.def)));
        else
        {
            NormalisableRange<float> r { s.min, s.max, s.step };
            if (s.logScale)
            {
                const float step = s.step;
                r = NormalisableRange<float> (s.min, s.max,
                    [] (float a, float b, float x) { return a * std::pow (b / a, juce::jlimit (0.0f, 1.0f, x)); },
                    [] (float a, float b, float v) { return std::log (juce::jlimit (a, b, v) / a) / std::log (b / a); },
                    [step] (float a, float b, float v) { return juce::jlimit (a, b, std::round (v / step) * step); });
            }
            else if (s.centre > s.min && s.centre < s.max) r.setSkewForCentre (s.centre);
            const auto* sp = &s;
            auto a = AudioParameterFloatAttributes().withStringFromValueFunction ([sp] (float v, int) { return toText (*sp, v); });
            p.push_back (std::make_unique<AudioParameterFloat> (pid, s.name, r, s.def, a));
        }
    }
    return { p.begin(), p.end() };
}

using Values = std::array<float, count>;

inline Values defaults()
{
    Values v {};
    for (int i = 0; i < count; ++i) v[(size_t) i] = spec (i).def;
    return v;
}

// valeurs réelles -> réglages du moteur (le DAW, les presets et les essais passent tous par ici)
inline mini::MiniChain::Params toChain (const Values& v)
{
    auto f = [&v] (int i) { return v[(size_t) i]; };
    auto on = [&v] (int i) { return v[(size_t) i] > 0.5f; };
    mini::MiniChain::Params p;
    p.inDb = f (inGain);
    p.outDb = f (outGain);
    p.autoInput = on (autoInput);
    p.lowCut = on (lowCut);
    p.plosiveOn = on (plosOn);
    p.plosiveAmount = f (plosAmount) * 0.01f;
    p.nsOn = on (nsOn);
    p.nsAmount = f (nsAmount) * 0.01f;
    p.gateOn = on (gateOn);
    p.gateThrDb = f (gateThr);
    p.gateRangeDb = f (gateRange);
    p.gateReleaseMs = f (gateRel);
    p.low = f (low) * 0.1f;
    p.mid = f (mid) * 0.1f;
    p.hi = f (hi) * 0.1f;
    p.air = f (air) * 0.1f;
    p.comp = f (comp) * 0.01f;
    p.compAutoGain = on (compAuto);
    p.multi = f (multi) * 0.01f;
    for (int b = 0; b < 4; ++b)
    {
        auto& band = p.bands[(size_t) b];
        band.thrDb = f (mbThr + b);
        band.ratio = f (mbRatio + b);
        band.atkMs = f (mbAttack + b);
        band.relMs = f (mbRelease + b);
        band.kneeDb = f (mbKnee + b);
        band.gainDb = f (mbGain + b);
        band.on = on (mbOn + b);
    }
    p.deess = f (deess) * 0.01f;
    p.drive = f (drive) * 0.01f;
    p.phone = f (phone) * 0.01f;
    p.doubler = f (doubler) * 0.01f;
    p.chorus = f (chorus) * 0.01f;
    p.delay = f (delay) * 0.01f;
    p.feed = f (feed) * 0.01f;
    p.filter = f (filter) * 0.01f;
    p.division = juce::roundToInt (f (time));
    p.ping = on (ping);
    auto& r = p.reverb;
    r.mode = juce::roundToInt (f (revMode));
    r.send = f (revMix) * 0.01f;
    r.decay = f (revDecay);
    r.preDelayMs = f (revPre);
    r.size = f (revSize) * 0.01f;
    r.diffusion = f (revDiff) * 0.01f;
    r.modRate = f (revRate);
    r.modDepth = f (revDepth) * 0.01f;
    r.lowCut = f (revLowCut);
    r.highCut = f (revHiCut);
    r.width = f (revWidth) * 0.01f;
    r.ducking = f (revDuck) * 0.01f;
    r.clean = f (revClean) * 0.01f;
    r.pulse = f (revPulse) * 0.01f;
    r.pulseRate = juce::roundToInt (f (revPulseRate));
    p.peqOn = on (peqPower);
    for (int b = 0; b < 8; ++b)
    {
        auto& e = p.peq[(size_t) b];
        e.on = on (peqOn + b);
        e.type = juce::roundToInt (f (peqType + b));
        e.freq = f (peqFreq + b);
        e.gainDb = f (peqGain + b);
        e.q = f (peqQ + b);
        e.slope = juce::roundToInt (f (peqSlope + b));
        e.dyn = on (peqDyn + b);
        e.thrDb = f (peqThr + b);
    }
    p.bypass = on (bypass);
    return p;
}

// valeurs d'un preset (identifiant -> valeur réelle) par-dessus les valeurs par défaut
inline Values fromMap (const std::map<juce::String, float>& m)
{
    auto v = defaults();
    for (const auto& [id, value] : m)
        if (const int i = indexOf (id); i >= 0) v[(size_t) i] = juce::jlimit (spec (i).min, spec (i).max, value);
    return v;
}

/*  Réglages des versions précédentes de VOCAL MINI (avant PRO) : les quatre envois de reverb SHORT / SMALL / BIG / LUSH
    deviennent un seul espace. L'envoi le plus fort donne le mode et son caractère (SHORT -> ROOM 0.45 s, SMALL -> PLATE 1
    1.1 s, BIG -> HALL 2.6 s et 35 ms de pre-delay, LUSH -> SHIMMER 4.5 s), l'envoi devient MIX. Un projet ou un preset
    enregistré avant PRO se rouvre avec une reverb du même caractère. Ne fait rien si le réglage contient déjà revMode. */
inline void migrateLegacy (std::map<juce::String, float>& m)
{
    if (m.count (ids::revMode) > 0) return;
    static const char* oldIds[4] { "revShort", "revSmall", "revBig", "revLush" };
    bool any = false;
    int best = -1;
    float bestV = 0.0f;
    for (int k = 0; k < 4; ++k)
        if (const auto it = m.find (oldIds[k]); it != m.end())
        {
            any = true;
            if (it->second > bestV) { bestV = it->second; best = k; }
        }
    if (! any) return;
    for (auto* id : oldIds) m.erase (id);
    if (best < 0) { m[ids::revMix] = 0.0f; return; }
    static constexpr float modeOf[4] { 0, 1, 3, 4 }, decayOf[4] { 0.45f, 1.1f, 2.6f, 4.5f }, preOf[4] { 4.0f, 15.0f, 35.0f, 25.0f };
    m[ids::revMode] = modeOf[best];
    m[ids::revMix] = bestV;
    m[ids::revDecay] = decayOf[best];
    m[ids::revPre] = preOf[best];
}
} // namespace prm
} // namespace onirique
