#pragma once

#include "MiniDSP.h"
#include <array>
#include <atomic>
#include <complex>

namespace onirique::mini
{
/*  PARAMETRIC : l'EQ paramétrique de VOCAL MINI PRO, 8 bandes, en plus des boutons LOW / MID / HI / AIR.

    Place dans la chaîne : après le COMP et le MULTIBAND, avant le DRIVE et HI / AIR (dans le coeur suréchantillonné x2
    en MIX). La courbe dessinée est celle qu'on entend : le MULTIBAND, en amont, ne peut pas « reprendre » une bande
    poussée ou creusée, et le DE-ESS, en aval, voit les S d'un aigu poussé ici. Le mode dynamique y trouve une voix
    déjà tenue : ses seuils restent valables d'une prise à l'autre.

    Filtres « appariés à l'analogique » (Vicanek, 2016 ; même conception que l'EQ de VOCALBOX) : les pôles sont ceux du
    filtre analogique idéal, le numérateur est calculé pour que la courbe numérique coïncide avec la courbe analogique
    à 0 Hz, à la fréquence de la bande et à Nyquist. Une cloche ou un shelf à 10-20 kHz garde exactement sa forme, même
    en LIVE à 44.1 kHz (pas de resserrement près de Nyquist comme avec la transformée bilinéaire). À 0 dB, une cloche
    ou un shelf est EXACTEMENT neutre (b = a). Aucune latence. Calcul et états en double précision.

    Types : BELL, LOW SHELF, HIGH SHELF (gain +/-18 dB, Q 0.1..10), LOW CUT, HIGH CUT (12 / 24 / 36 / 48 dB/oct :
    Butterworth en cascade, le Q règle la résonance de la dernière section, 0.71 = parfaitement plat), NOTCH (zéro exact
    sur la fréquence).

    Sans clic ni zipper :
    - fréquence, gain et Q glissent (50 ms) et les coefficients suivent ÉCHANTILLON PAR ÉCHANTILLON (forme directe I,
      interpolation linéaire des coefficients entre deux calculs : un biquad stable reste stable sur tout le trajet,
      le triangle de stabilité est convexe) ;
    - allumer / couper une cloche ou un shelf fait glisser son gain depuis / vers 0 dB (le filtre reste exact) ; une
      coupe ou un notch entre et sort en fondu (30 ms) ; une bande coupée ne coûte rien ;
    - changer de type ou de pente : le nouveau filtre démarre en parallèle, muet le temps de s'établir (3 à 150 ms selon
      fréquence et Q), puis fondu de 15 ms de l'ancien au nouveau : la bande ne disparaît jamais, rien ne claque ;
    - tous les fondus sont en S quintique (ni coin ni courbure brusque : pas même le petit « tic » d'un fondu linéaire).

    Mode DYNAMIQUE (cloche, shelfs) : la bande ne touche le son que lorsque sa zone dépasse le SEUIL. Détecteur passe-bande
    (cloche) / passe-bas / passe-haut (shelfs) sur l'entrée de la bande, enveloppe crête 3 ms / 90 ms, liaison stéréo ;
    le gain appliqué glisse de 0 dB (au seuil) au gain réglé (9 dB au-dessus), genou doux, lissé sur 4 ms. Une coupe
    dynamique ne creuse que quand la résonance ou la dureté apparaît : la voix garde son corps le reste du temps. */
class VoiceParametric
{
public:
    static constexpr int kBands = 8, kMaxStages = 4;
    static constexpr float kDynRangeDb = 9.0f;
    enum Type { bell = 0, lowShelf, highShelf, lowCut, highCut, notch, numTypes };

    struct Band
    {
        bool on = false;
        int type = bell;
        float freq = 1000.0f, gainDb = 0.0f, q = 1.0f;
        int slope = 1;                 // 0..3 : 12 / 24 / 36 / 48 dB/oct (LOW CUT / HIGH CUT)
        bool dyn = false;              // mode dynamique (cloche, shelfs)
        float thrDb = -30.0f;
    };

    // positions de départ (toutes les bandes coupées) : une coupe de grave, le corps, la boîte, le nasal, la dureté,
    // la présence, l'air, une coupe d'aigu
    static Band defaultBand (int b) noexcept
    {
        static constexpr int types[kBands] { lowCut, lowShelf, bell, bell, bell, bell, highShelf, highCut };
        static constexpr float freqs[kBands] { 80.0f, 160.0f, 350.0f, 900.0f, 2500.0f, 5000.0f, 10000.0f, 18000.0f };
        Band d;
        b = juce::jlimit (0, kBands - 1, b);
        d.type = types[b];
        d.freq = freqs[b];
        d.q = d.type == bell ? 1.0f : 0.71f;
        return d;
    }

    static bool hasGain (int type) noexcept      { return type == bell || type == lowShelf || type == highShelf; }
    static bool isCut (int type) noexcept        { return type == lowCut || type == highCut; }
    static bool canBeDynamic (int type) noexcept { return hasGain (type); }
    static int numStages (int type, int slope) noexcept { return isCut (type) ? juce::jlimit (1, kMaxStages, slope + 1) : 1; }

    // ---- conception (partagée avec l'interface : la courbe affichée est celle qui tourne)
    static BiquadCoeffs design (double fs, int type, double f0, double gainDb, double Q) noexcept
    {
        using C = std::complex<double>;
        f0 = juce::jlimit (10.0, 0.49 * fs, f0);
        Q = juce::jlimit (0.05, 30.0, Q);
        const double A = std::pow (10.0, gainDb / 40.0), sA = std::sqrt (A);

        // prototype analogique normalisé (s en unités de 2 pi f0) : N(s) / D(s), coefficients [s^0, s^1, s^2]
        double n[3] { 1, 0, 0 }, d[3] { 1, 1.0 / Q, 1 };
        switch (type)
        {
            case bell:      n[0] = 1; n[1] = A / Q; n[2] = 1;  d[0] = 1; d[1] = 1.0 / (A * Q); d[2] = 1; break;
            case lowShelf:  n[0] = A * A; n[1] = A * sA / Q; n[2] = A;  d[0] = 1; d[1] = sA / Q; d[2] = A; break;
            case highShelf: n[0] = A; n[1] = A * sA / Q; n[2] = A * A;  d[0] = A; d[1] = sA / Q; d[2] = 1; break;
            case lowCut:    n[0] = 0; n[1] = 0; n[2] = 1; break;
            case highCut:   n[0] = 1; n[1] = 0; n[2] = 0; break;
            default: break;
        }
        auto mag2 = [&] (double hz)
        {
            const C s (0.0, hz / f0);
            return std::norm ((n[0] + n[1] * s + n[2] * s * s) / (d[0] + d[1] * s + d[2] * s * s));
        };

        double a1, a2;
        matchedPoles (fs, f0, d, a1, a2);

        // numérateur : |H|^2 numérique = (B0 p0 + B1 p1 + B2 p2) / (A0 p0 + A1 p1 + A2 p2), appariement à 0 Hz,
        // à Nyquist et à la fréquence de la bande
        const double A0 = (1.0 + a1 + a2) * (1.0 + a1 + a2), A1 = (1.0 - a1 + a2) * (1.0 - a1 + a2), A2 = -4.0 * a2;
        const double fm = std::min (f0, 0.45 * fs);
        const double p1 = std::pow (std::sin (juce::MathConstants<double>::pi * fm / fs), 2.0), p0 = 1.0 - p1, p2 = 4.0 * p0 * p1;
        const double B0 = A0 * mag2 (0.0);
        const double B1 = A1 * mag2 (0.5 * fs);
        const double B2 = (mag2 (fm) * (A0 * p0 + A1 * p1 + A2 * p2) - B0 * p0 - B1 * p1) / p2;

        const double sB0 = std::sqrt (std::max (0.0, B0)), sB1 = std::sqrt (std::max (0.0, B1));
        const double W = 0.5 * (sB0 + sB1);
        const double b0 = 0.5 * (W + std::sqrt (std::max (0.0, W * W + B2)));
        const double b1 = 0.5 * (sB0 - sB1);
        const double b2 = b0 > 1.0e-12 ? -B2 / (4.0 * b0) : 0.0;

        BiquadCoeffs c;
        c.b0 = b0; c.b1 = b1; c.b2 = b2; c.a1 = a1; c.a2 = a2;
        return c;
    }

    // NOTCH : pôles appariés, zéros exactement sur la fréquence (creux infini), gain 1 en continu
    static BiquadCoeffs designNotch (double fs, double f0, double Q) noexcept
    {
        f0 = juce::jlimit (10.0, 0.49 * fs, f0);
        const double d[3] { 1.0, 1.0 / juce::jlimit (0.05, 30.0, Q), 1.0 };
        double a1, a2;
        matchedPoles (fs, f0, d, a1, a2);
        const double cw = std::cos (2.0 * juce::MathConstants<double>::pi * f0 / fs);
        const double gn = (1.0 + a1 + a2) / std::max (1.0e-12, 2.0 - 2.0 * cw);
        BiquadCoeffs c;
        c.b0 = gn; c.b1 = -2.0 * cw * gn; c.b2 = gn; c.a1 = a1; c.a2 = a2;
        return c;
    }

    // section k d'une bande (LOW CUT / HIGH CUT en ont jusqu'à 4)
    static BiquadCoeffs designStage (double fs, int type, double f0, double gainDb, double Q, int slope, int stage) noexcept
    {
        if (isCut (type))
        {
            const int N = numStages (type, slope);
            double qk = 1.0 / (2.0 * std::cos ((2.0 * stage + 1.0) * juce::MathConstants<double>::pi / (4.0 * N)));
            if (stage == N - 1) qk *= Q / 0.70710678118;      // résonance réglable sur la section la plus pointue
            return design (fs, type, f0, 0.0, qk);
        }
        if (type == notch) return designNotch (fs, f0, Q);
        return design (fs, type, f0, gainDb, Q);
    }

    // réponse complexe d'une bande (toutes ses sections), avec un gain imposé (gain dynamique en cours)
    static std::complex<double> bandResponse (double fs, const Band& b, double hz, double gainDb) noexcept
    {
        std::complex<double> h (1.0, 0.0);
        const int ns = numStages (b.type, b.slope);
        for (int k = 0; k < ns; ++k)
            h *= designStage (fs, b.type, b.freq, gainDb, b.q, b.slope, k).response (fs, hz);
        return h;
    }

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        atkC = coefFor (0.003, fs);
        relC = coefFor (0.090, fs);
        dynC = coefFor (0.004, fs);
        xfStep = (float) (1.0 / (0.015 * fs));
        for (auto& s : st)
        {
            const float f = s.f.getTargetValue(), q = s.q.getTargetValue(), g = s.g.getTargetValue(), lv = s.level.getTargetValue();
            s.f.reset (fs, 0.05); s.q.reset (fs, 0.05); s.g.reset (fs, 0.05); s.level.reset (fs, 0.03);
            s.f.setCurrentAndTargetValue (f > 0.0f ? f : 1000.0f);
            s.q.setCurrentAndTargetValue (q > 0.0f ? q : 1.0f);
            s.g.setCurrentAndTargetValue (g);
            s.level.setCurrentAndTargetValue (lv);
            s.type = s.pendType; s.slope = s.pendSlope; s.nStages = numStages (s.type, s.slope);
            s.trans = false;
            for (auto& b : s.stage) b.reset();
            s.det.reset();
            s.env = 0.0f;
            s.dynS = s.dyn ? 0.0f : 1.0f;
            s.dirty = true;
            s.detDirty = true;
            s.countdown = 0;
            s.hold = 0;
            s.active = lv > 0.0f;
        }
    }

    // on : bande allumée ET l'EQ paramétrique allumé
    void setBands (const std::array<Band, kBands>& b, bool eqOn, bool snap) noexcept
    {
        for (int i = 0; i < kBands; ++i)
        {
            auto& s = st[(size_t) i];
            const auto& v = b[(size_t) i];
            const float f = juce::jlimit (20.0f, 20000.0f, v.freq), g = juce::jlimit (-18.0f, 18.0f, v.gainDb),
                        q = juce::jlimit (0.1f, 10.0f, v.q), lv = v.on && eqOn ? 1.0f : 0.0f;
            const int type = juce::jlimit (0, numTypes - 1, v.type), slope = juce::jlimit (0, kMaxStages - 1, v.slope);
            s.pendType = type; s.pendSlope = slope;
            s.dyn = v.dyn && canBeDynamic (type);
            s.thr = juce::jlimit (-80.0f, 0.0f, v.thrDb);
            if (snap)
            {
                s.f.setCurrentAndTargetValue (f); s.g.setCurrentAndTargetValue (g); s.q.setCurrentAndTargetValue (q);
                s.level.setCurrentAndTargetValue (lv);
                s.type = type; s.slope = slope; s.nStages = numStages (type, slope);
                s.trans = false;
                s.dynS = s.dyn ? 0.0f : 1.0f;
                s.dirty = s.detDirty = true;
                s.active = lv > 0.0f;
                s.hold = 0;
                for (auto& k : s.stage) k.reset();
            }
            else
            {
                s.f.setTargetValue (f); s.g.setTargetValue (g); s.q.setTargetValue (q); s.level.setTargetValue (lv);
                if (lv > 0.0f && ! s.active)
                {
                    // réveil : état vierge, type et réglages pris tels quels (le fondu d'entrée part de « neutre » : une
                    // cloche ou un shelf à 0 dB est l'identité exacte dès le premier échantillon)
                    s.type = type; s.slope = slope; s.nStages = numStages (type, slope);
                    s.trans = false;
                    s.f.setCurrentAndTargetValue (f); s.q.setCurrentAndTargetValue (q); s.g.setCurrentAndTargetValue (g);
                    for (auto& k : s.stage) k.reset();
                    s.det.reset();
                    s.env = 0.0f;
                    s.dynS = s.dyn ? 0.0f : 1.0f;
                    s.dirty = s.detDirty = true;
                    s.countdown = 0;
                    s.active = true;
                }
                // un changement de type ou de pente est pris en charge par process() (transition en parallèle)
            }
        }
    }

    void process (float* const* ch, int numCh, int n) noexcept
    {
        for (size_t bi = 0; bi < (size_t) kBands; ++bi)
        {
            auto& s = st[bi];
            if (! s.active)
            {
                // bande coupée : rien à calculer (les réglages continuent de glisser vers leur cible)
                s.f.skip (n); s.g.skip (n); s.q.skip (n); s.level.skip (n);
                gainMeter[bi].store (0.0f, std::memory_order_relaxed);
                continue;
            }

            // changement de type ou de pente : le nouveau filtre démarre EN PARALLÈLE, muet, le temps que son démarrage
            // (un filtre qui part d'un état vide « sonne » un instant) s'éteigne ; puis fondu en S de 15 ms de l'ancien au
            // nouveau. Deux filtres en régime établi sur le même signal : le passage est continu, rien ne claque, et la
            // bande ne disparaît jamais pendant le changement. Un nouveau choix arrivé pendant l'attente la relance ;
            // pendant le fondu, il attend la fin.
            const bool differs = s.pendType != s.type || (isCut (s.type) && s.pendSlope != s.slope);
            const bool nextDiffers = s.trans && (s.pendType != s.nType || (isCut (s.pendType) && s.pendSlope != s.nSlope));
            if ((! s.trans && differs) || (s.trans && s.xf <= 0.0f && nextDiffers))
                startTransition (s);

            float shown = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                const float f = s.f.getNextValue(), g = s.g.getNextValue(), q = s.q.getNextValue(), lv = s.level.getNextValue();

                if (s.dyn)
                {
                    if (s.detDirty || f != s.detF || q != s.detQ) updateDetector (s, f, q);
                    float d = 0.0f;
                    for (int c = 0; c < numCh; ++c) d = std::max (d, std::abs (s.det.process (ch[c][i], c)));
                    s.env += (d > s.env ? atkC : relC) * (d - s.env);
                }

                if (--s.countdown <= 0)
                {
                    // pendant qu'un réglage bouge (ou en mode dynamique) : nouveaux coefficients tous les 4 échantillons ;
                    // l'écart entre la glissade linéaire et la vraie trajectoire est 16 fois plus petit qu'à 16 (aucune
                    // trace de la cadence de calcul dans le son)
                    const bool moving = s.dyn || s.trans || s.f.isSmoothing() || s.g.isSmoothing() || s.q.isSmoothing()
                                     || s.level.isSmoothing();
                    const int step = moving ? 4 : 16;
                    s.countdown = step;
                    if (s.dyn)
                    {
                        // 0 au seuil -> 1 à kDynRangeDb au-dessus, genou doux (smoothstep)
                        const float over = gainToDb (s.env + 1.0e-9f) - s.thr;
                        const float t = juce::jlimit (0.0f, 1.0f, over / kDynRangeDb);
                        s.dynTarget = t * t * (3.0f - 2.0f * t);
                    }
                    else
                        s.dynTarget = 1.0f;

                    // cloche / shelf : le gain glisse vers 0 dB pour s'éteindre (le filtre reste exact) ;
                    // coupe / notch : fondu de mélange
                    const float gl = g * s.dynS * ease (lv);
                    const float gEff = hasGain (s.type) ? gl : 0.0f;
                    const bool changed = s.dirty || f != s.lf || std::abs (gEff - s.lg) > 0.002f || q != s.lq;
                    if (changed)
                    {
                        for (int k = 0; k < s.nStages; ++k)
                        {
                            const auto c = designStage (fs, s.type, f, gEff, q, s.slope, k);
                            if (s.dirty) s.stage[k].setNow (c); else s.stage[k].glideTo (c, step);
                        }
                        s.lf = f; s.lg = gEff; s.lq = q; s.dirty = false;
                    }
                    if (s.trans && (changed || s.nDirty || std::abs (gl - s.lgN) > 0.002f))
                    {
                        const float gN = hasGain (s.nType) ? gl : 0.0f;
                        for (int k = 0; k < s.nnStages; ++k)
                        {
                            const auto c = designStage (fs, s.nType, f, gN, q, s.nSlope, k);
                            if (s.nDirty) s.next[k].setNow (c); else s.next[k].glideTo (c, step);
                        }
                        s.lgN = gl; s.nDirty = false;
                    }
                    shown = s.trans && s.xf > 0.5f ? (hasGain (s.nType) ? gl : 0.0f) : gEff;
                }
                s.dynS += dynC * (s.dynTarget - s.dynS);

                for (int k = 0; k < s.nStages; ++k) s.stage[k].tick();
                const float mix = hasGain (s.type) ? 1.0f : ease (lv);
                if (! s.trans)
                {
                    for (int c = 0; c < numCh; ++c)
                    {
                        const float x = ch[c][i];
                        float y = x;
                        for (int k = 0; k < s.nStages; ++k) y = s.stage[k].process (y, c);
                        ch[c][i] = mix >= 1.0f ? y : x + mix * (y - x);
                    }
                    continue;
                }

                for (int k = 0; k < s.nnStages; ++k) s.next[k].tick();
                float w = 0.0f;
                if (s.settle > 0) --s.settle;
                else { s.xf = std::min (1.0f, s.xf + xfStep); w = ease (s.xf); }
                const float mixN = hasGain (s.nType) ? 1.0f : ease (lv);
                for (int c = 0; c < numCh; ++c)
                {
                    const float x = ch[c][i];
                    float yo = x, yn = x;
                    for (int k = 0; k < s.nStages; ++k) yo = s.stage[k].process (yo, c);
                    for (int k = 0; k < s.nnStages; ++k) yn = s.next[k].process (yn, c);
                    yo = x + mix * (yo - x);
                    yn = x + mixN * (yn - x);
                    ch[c][i] = yo + w * (yn - yo);
                }
                if (s.xf >= 1.0f)
                {
                    // le nouveau filtre a pris la place de l'ancien
                    std::swap (s.stage, s.next);
                    s.type = s.nType; s.slope = s.nSlope; s.nStages = s.nnStages;
                    s.lg = s.lgN = hasGain (s.type) ? s.lgN : 0.0f;
                    s.trans = false;
                    s.detDirty = true;
                }
            }
            gainMeter[bi].store (shown, std::memory_order_relaxed);

            // éteinte : retirée de la chaîne une fois son fondu terminé. Coupe / notch : tout de suite (la sortie est
            // déjà l'entrée). Cloche / shelf : le filtre, revenu à 0 dB, est exactement neutre, mais son état garde un
            // écart avec l'entrée qui s'amortit (sa résonance) : on attend qu'il soit à -60 dB (50 ms à 2 s selon Q et
            // fréquence) pour qu'aucune marche ne reste dans l'onde.
            if (s.level.getTargetValue() <= 0.0f && ! s.level.isSmoothing() && ! s.trans)
            {
                const double need = hasGain (s.type) ? juce::jlimit (0.05, 2.0, 7.0 * s.lq / (juce::MathConstants<double>::pi * std::max (10.0f, s.lf))) : 0.0;
                if ((s.hold += n) >= (int) (need * fs)) { s.active = false; s.hold = 0; }
            }
            else
                s.hold = 0;
        }
    }

    // gain appliqué en ce moment par la bande (dB) : suit le mode dynamique, pour l'affichage
    float getBandGainDb (int b) const noexcept { return gainMeter[(size_t) juce::jlimit (0, kBands - 1, b)].load (std::memory_order_relaxed); }

private:
    // fondu en S quintique (6t^5 - 15t^4 + 10t^3) : pente ET courbure nulles au départ comme à l'arrivée. Un smoothstep
    // (3t^2 - 2t^3) part sans coin mais avec une courbure qui démarre d'un coup : sa trace dans les aigus restait mesurable
    // (-80 dB sous la voix) ; ici elle est sous -100 dB.
    static float ease (float t) noexcept { t = juce::jlimit (0.0f, 1.0f, t); return t * t * t * (t * (6.0f * t - 15.0f) + 10.0f); }

    // pôles appariés : mêmes fréquence propre et amortissement que l'analogique (dénominateur d = [s^0, s^1, s^2])
    static void matchedPoles (double fs, double f0, const double d[3], double& a1, double& a2) noexcept
    {
        const double wp = 2.0 * juce::MathConstants<double>::pi * f0 * std::sqrt (d[0] / d[2]) / fs;
        const double qp = d[1] / (2.0 * std::sqrt (d[0] * d[2]));
        a2 = std::exp (-2.0 * qp * wp);
        if (qp <= 1.0) a1 = -2.0 * std::exp (-qp * wp) * std::cos (std::sqrt (1.0 - qp * qp) * wp);
        else           a1 = -2.0 * std::exp (-qp * wp) * std::cosh (std::sqrt (qp * qp - 1.0) * wp);
    }

    struct State
    {
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> f { 1000.0f }, q { 1.0f };
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> g, level;
        SmoothBiquad stage[kMaxStages], next[kMaxStages];   // next : le filtre du nouveau type, pendant une transition
        Biquad det;                    // détecteur du mode dynamique
        int type = bell, slope = 1, nStages = 1, pendType = bell, pendSlope = 1, countdown = 0, hold = 0;
        int nType = bell, nSlope = 1, nnStages = 1, settle = 0;
        bool dyn = false, dirty = true, detDirty = true, active = false, trans = false, nDirty = true;
        float xf = 0.0f, thr = -30.0f, env = 0.0f, dynS = 1.0f, dynTarget = 1.0f;
        float lf = -1.0f, lg = -100.0f, lq = -1.0f, lgN = 0.0f, detF = -1.0f, detQ = -1.0f;
    };

    // départ d'une transition vers le type / la pente demandés : nouveau filtre vierge, muet le temps de s'établir
    // (7 constantes de temps de son pôle le plus résonant : son démarrage est alors 60 dB sous lui-même), 3 à 150 ms
    void startTransition (State& s) noexcept
    {
        s.nType = s.pendType; s.nSlope = s.pendSlope; s.nnStages = numStages (s.nType, s.nSlope);
        for (auto& k : s.next) k.reset();
        const double f = std::max (20.0f, s.f.getTargetValue()), q = s.q.getTargetValue();
        const double qPole = isCut (s.nType) ? 2.62 * q / 0.70710678118 : (s.nType == notch ? q : q * 2.82);
        s.settle = (int) (fs * juce::jlimit (0.003, 0.150, 7.0 * qPole / (juce::MathConstants<double>::pi * f)));
        s.xf = 0.0f;
        s.trans = true;
        s.nDirty = true;
        s.countdown = 0;
    }

    void updateDetector (State& s, float f, float q) noexcept
    {
        if (s.type == lowShelf)       s.det.setCoeffs (BiquadCoeffs::lowpass (fs, f, 0.707));
        else if (s.type == highShelf) s.det.setCoeffs (BiquadCoeffs::highpass (fs, f, 0.707));
        else                          s.det.setCoeffs (BiquadCoeffs::bandpass (fs, f, std::max (0.7f, q)));
        s.detF = f; s.detQ = q; s.detDirty = false;
    }

    double fs = 48000.0;
    float atkC = 0.01f, relC = 0.001f, dynC = 0.01f, xfStep = 0.002f;
    std::array<State, kBands> st;
    std::array<std::atomic<float>, kBands> gainMeter {};
};
} // namespace onirique::mini
