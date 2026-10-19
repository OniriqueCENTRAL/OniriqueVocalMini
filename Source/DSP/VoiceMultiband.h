#pragma once

#include <array>
#include <atomic>
#include "MiniDSP.h"

namespace onirique::mini
{
/*  MULTIBAND : un compresseur multibande pour la voix, en un seul bouton. Aucune latence.

    Découpage en 4 bandes pensées pour la voix, par filtres Linkwitz-Riley 24 dB/oct, phases alignées par des
    passe-tout : la somme des bandes est plate (à 0, ou quand rien ne dépasse, le timbre ne change pas).
      BODY      < 180 Hz       : boom, plosives, effet de proximité
      CHEST     180 Hz-1.2 kHz : bas-médiums, son « boîte », nasal
      PRESENCE  1.2-5 kHz      : intelligibilité, dureté, résonances criardes
      SHINE     > 5 kHz        : sifflantes, souffle, air

    Le seuil de chaque bande n'est pas fixe : il suit le niveau HABITUEL de la bande dans cette voix (moyenne lente,
    sur les passages chantés seulement). Une bande n'est comprimée que quand elle dépasse sa propre habitude : une
    note qui gonfle dans les graves, une voyelle qui sonne boîte, un passage qui devient criard, une sifflante qui
    perce. Le bouton rapproche le seuil de cette habitude et durcit le ratio (1.5:1 -> 4:1) : à faible dose la voix
    reste naturelle et se tient d'elle-même, à fond elle devient régulière et dense dans tout le spectre.
    Le résultat ne dépend pas du niveau d'entrée.

    Texture de chaque bande : détection RMS courte, genou très large (10 dB), réduction optique (attaque puis mémoire
    lente : une phrase comprimée longtemps relâche plus doucement), lissage final de 2 ms. Temps adaptés à chaque
    bande : graves plus lents (aucune distorsion de modulation), présence et aigus plus vifs.
    TIMBRE GARDÉ : chaque bande récupère en continu sa réduction MOYENNE (85 %, moyenne lente sur le chant) : le
    multibande ne retire que les dépassements, jamais la couleur de la voix (l'air reste l'air, le corps reste le
    corps). Volume tenu : la sortie est ramenée au niveau d'entrée (moyenne lente, gelée dans les silences).
*/
class VoiceMultiband
{
public:
    static constexpr int kBands = 4;

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        amt.prepare (fs, 0.08, amt.target());
        onS.prepare (fs, 0.03, onS.target());
        match.prepare (fs, 1.5, 1.0);
        constexpr double q = 0.7071067811865476;   // Butterworth : 2 en cascade = Linkwitz-Riley 24 dB/oct
        const double f1 = 180.0, f2 = 1200.0, f3 = std::min (5000.0, 0.4 * fs);
        const auto l1 = BiquadCoeffs::lowpass (fs, f1, q), h1 = BiquadCoeffs::highpass (fs, f1, q);
        const auto l2 = BiquadCoeffs::lowpass (fs, f2, q), h2 = BiquadCoeffs::highpass (fs, f2, q);
        const auto l3 = BiquadCoeffs::lowpass (fs, f3, q), h3 = BiquadCoeffs::highpass (fs, f3, q);
        lp1a.setCoeffs (l1); lp1b.setCoeffs (l1); hp1a.setCoeffs (h1); hp1b.setCoeffs (h1);
        lp2a.setCoeffs (l2); lp2b.setCoeffs (l2); hp2a.setCoeffs (h2); hp2b.setCoeffs (h2);
        lp3a.setCoeffs (l3); lp3b.setCoeffs (l3); hp3a.setCoeffs (h3); hp3b.setCoeffs (h3);
        // somme LP + HP d'un Linkwitz-Riley 24 dB = passe-tout du 2e ordre : les bandes basses passent par ceux des
        // coupures supérieures, toutes les bandes ont la même phase
        ap2a.setCoeffs (BiquadCoeffs::allpass (fs, f2, q));
        ap3a.setCoeffs (BiquadCoeffs::allpass (fs, f3, q));
        ap3b.setCoeffs (BiquadCoeffs::allpass (fs, f3, q));

        static constexpr float atkMs[kBands] { 22.0f, 12.0f, 6.0f, 2.5f }, relMs[kBands] { 280.0f, 180.0f, 120.0f, 80.0f };
        for (int b = 0; b < kBands; ++b)
        {
            atkC[(size_t) b] = coefFor (atkMs[b] * 0.001, fs);
            relC[(size_t) b] = coefFor (relMs[b] * 0.001, fs);
        }
        rmsC = coefFor (0.005, fs);
        habitC = coefFor (2.5, fs);      // habitude d'une bande : moyenne sur ~2.5 s de chant
        avgC = coefFor (3.0, fs);        // réduction moyenne d'une bande (rendue en gain : le timbre ne change pas)
        smoothC = coefFor (0.002, fs);
        memUp = coefFor (2.0, fs); memDown = coefFor (4.0, fs);
        gateC = coefFor (0.05, fs);
        reset();
    }

    void reset() noexcept
    {
        for (auto* f : { &lp1a, &lp1b, &hp1a, &hp1b, &lp2a, &lp2b, &hp2a, &hp2b, &lp3a, &lp3b, &hp3a, &hp3b, &ap2a, &ap3a, &ap3b })
            f->reset();
        // habitudes de départ : la pente moyenne d'une voix (les graves et les aigus plus bas que le médium)
        static constexpr float startDb[kBands] { -30.0f, -22.0f, -26.0f, -36.0f };
        for (int b = 0; b < kBands; ++b)
        {
            const auto s = (size_t) b;
            ms[s] = L[s] = M[s] = gr[s] = avgGr[s] = 0.0f;
            habitDb[s] = startDb[b];
            meter[s].store (0.0f, std::memory_order_relaxed);
        }
        fullEnv = 0.0f;
        match.reset();
    }

    void set (float amount01, bool snap) noexcept
    {
        amt.set (juce::jlimit (0.0f, 1.0f, amount01), snap);
        onS.set (amount01 > 0.0005f ? 1.0f : 0.0f, snap);
    }

    // réduction de chaque bande (dB), pour l'interface
    float getBandReductionDb (int b) const noexcept { return meter[(size_t) juce::jlimit (0, kBands - 1, b)].load (std::memory_order_relaxed); }

    void process (float* const* ch, int numCh, int n) noexcept
    {
        if (onS.idle()) { if (! wasIdle) { reset(); wasIdle = true; } for (auto& m : meter) m.store (0.0f, std::memory_order_relaxed); return; }
        wasIdle = false;

        // le bouton : seuil au-dessus de l'habitude (+9 dB -> -6 dB), ratio 1.5:1 -> 4:1 ; chaque bande a son caractère
        static constexpr float weight[kBands] { 1.0f, 0.9f, 1.1f, 1.0f };
        std::array<float, kBands> peakGr {};
        const float inv = 1.0f / (float) numCh;

        for (int i = 0; i < n; ++i)
        {
            const float a = amt.next();
            const float offset = 18.0f - 24.0f * std::pow (a, 0.6f);   // à 0 rien n'atteint le seuil
            const float slope = 1.0f - 1.0f / (1.3f + 2.7f * a);

            float band[kBands][2] {};
            float pin = 0.0f, fpk = 0.0f;
            for (int c = 0; c < numCh; ++c)
            {
                const float x = ch[c][i];
                pin += x * x;
                fpk = std::max (fpk, std::abs (x));
                const float lp1 = lp1b.process (lp1a.process (x, c), c);
                const float hp1 = hp1b.process (hp1a.process (x, c), c);
                const float lp2 = lp2b.process (lp2a.process (hp1, c), c);
                const float hp2 = hp2b.process (hp2a.process (hp1, c), c);
                band[0][c] = ap3a.process (ap2a.process (lp1, c), c);
                band[1][c] = ap3b.process (lp2, c);
                band[2][c] = lp3b.process (lp3a.process (hp2, c), c);
                band[3][c] = hp3b.process (hp3a.process (hp2, c), c);
            }
            fullEnv += gateC * (fpk - fullEnv);
            const bool singing = fullEnv > 0.003f;   // ~ -50 dBFS : en dessous, les habitudes ne bougent pas

            float g[kBands];
            for (int b = 0; b < kBands; ++b)
            {
                const auto s = (size_t) b;
                float p = 0.0f;
                for (int c = 0; c < numCh; ++c) p = std::max (p, band[b][c] * band[b][c]);
                ms[s] += rmsC * (p - ms[s]);
                const float lvl = 10.0f * std::log10 (ms[s] * 2.0f + 1.0e-12f);
                if (singing && lvl > habitDb[s] - 30.0f) habitDb[s] += habitC * (lvl - habitDb[s]);

                constexpr float knee = 10.0f;
                const float over = lvl - (habitDb[s] + offset);
                float target = 0.0f;
                const float k = slope * weight[b];
                if (over >= 0.5f * knee)      target = k * over;
                else if (over > -0.5f * knee) target = k * (over + 0.5f * knee) * (over + 0.5f * knee) / (2.0f * knee);
                target = std::min (target, 16.0f);

                L[s] += (target > L[s] ? atkC[s] : relC[s]) * (target - L[s]);
                M[s] += (L[s] > M[s] ? memUp : memDown) * (L[s] - M[s]);
                gr[s] += smoothC * (std::max (L[s], 0.35f * M[s]) - gr[s]);
                peakGr[s] = std::max (peakGr[s], gr[s]);
                if (singing) avgGr[s] += avgC * (gr[s] - avgGr[s]);
                g[b] = dbToGain (0.85f * avgGr[s] - gr[s]);
            }

            float pout = 0.0f, y[2] {};
            for (int c = 0; c < numCh; ++c)
            {
                y[c] = band[0][c] * g[0] + band[1][c] * g[1] + band[2][c] * g[2] + band[3][c] * g[3];
                pout += y[c] * y[c];
            }
            const float mk = match.next (pin * inv, pout * inv, 0.5f, 6.0f);
            // entrée et sortie du module en fondu (30 ms) seulement quand le bouton quitte ou rejoint 0 : la force de
            // la compression, elle, vient du seuil et du ratio (aucun mélange déphasé en dessous)
            const float on = onS.next();
            for (int c = 0; c < numCh; ++c) ch[c][i] += on * (y[c] * mk - ch[c][i]);
        }
        for (int b = 0; b < kBands; ++b) meter[(size_t) b].store (peakGr[(size_t) b], std::memory_order_relaxed);
    }

private:
    double fs = 48000.0;
    Smooth amt, onS;
    LevelMatch match;
    Biquad lp1a, lp1b, hp1a, hp1b, lp2a, lp2b, hp2a, hp2b, lp3a, lp3b, hp3a, hp3b, ap2a, ap3a, ap3b;
    std::array<float, kBands> ms {}, L {}, M {}, gr {}, avgGr {}, habitDb {}, atkC {}, relC {};
    float avgC = 0, rmsC = 0, habitC = 0, smoothC = 0, memUp = 0, memDown = 0, gateC = 0, fullEnv = 0;
    bool wasIdle = true;
    std::array<std::atomic<float>, kBands> meter {};
};
} // namespace onirique::mini
