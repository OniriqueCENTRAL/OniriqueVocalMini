#pragma once

#include <array>
#include <atomic>
#include "MiniDSP.h"

namespace onirique::mini
{
/*  MULTIBAND : un compresseur multibande pour la voix. Aucune latence.

    Découpage en 4 bandes pensées pour la voix, par filtres Linkwitz-Riley 24 dB/oct, phases alignées par des
    passe-tout : la somme des bandes est un passe-tout (module plat, aucune bosse ni aucun creux aux fréquences de
    coupure ; à 0 ou quand rien ne dépasse, le timbre ne change pas).
      BODY      < 180 Hz       : boom, plosives, effet de proximité
      CHEST     180 Hz-1.2 kHz : bas-médiums, son « boîte », nasal
      PRES      1.2-5 kHz      : intelligibilité, dureté, résonances criardes
      SHINE     > 5 kHz        : sifflantes, souffle, air

    Chaque bande a ses propres réglages (panneau BANDS) :
      THRESHOLD (dBFS) : le seuil du DÉTECTEUR de la bande (RMS court sur la bande seule). Rien n'est simulé : seule la
                         partie du signal de la bande qui dépasse ce seuil est comprimée.
      RATIO, KNEE      : la pente au-dessus du seuil et la largeur du genou (courbe douce, en dB).
      ATTACK, RELEASE  : les temps de la réduction (en dB : le relâchement est une descente régulière).
      GAIN             : le niveau de sortie de la bande (-12..+12 dB), appliqué après la compression.
      ON               : bande coupée = ni compression ni gain, elle passe telle quelle.
    Le bouton MULTIBAND (0..100 %) dose l'ensemble : la réduction calculée par chaque bande est appliquée à
    amount^0.6 (à 25 % : 44 % de la réduction, ratio effectif ~1.4:1 pour un ratio réglé à 3.5:1 ; à 100 % : les
    réglages tels quels). À 0, le module est contourné. Les gains de bande, eux, sont appliqués dès que le module est
    allumé.

    Détecteurs adaptés à chaque bande (RMS 10 ms dans le grave, 5, 3 puis 2 ms dans l'aigu) : aucune distorsion de
    modulation dans les graves, des aigus saisis à temps. Lissage final de 1.5 ms.
    VOLUME TENU : seule la perte de niveau due à la compression est rattrapée (moyenne lente, gelée dans les silences) ;
    les GAIN de bande, réglés à la main, ne sont jamais annulés par ce rattrapage.
    Mesures publiées : réduction et niveau détecté de chaque bande (pour les mètres de l'interface).
*/
class VoiceMultiband
{
public:
    static constexpr int kBands = 4;

    VoiceMultiband() { for (auto& m : levelMeter) m.store (-100.0f, std::memory_order_relaxed); }
    static constexpr float kCross[3] { 180.0f, 1200.0f, 5000.0f };

    struct Band
    {
        float thrDb = -30.0f, ratio = 3.5f, atkMs = 10.0f, relMs = 150.0f, kneeDb = 8.0f, gainDb = 0.0f;
        bool on = true;
    };

    // réglages d'usine de chaque bande. Seuils calés sur des voix réelles au niveau BEST, après le COMP par défaut : chacun
    // tombe vers le 70e centile du niveau de la bande (mesuré : BODY -27.3, CHEST -15.2, PRES -29.0, SHINE -40.0 dBFS),
    // le genou de 10 dB fait entrer la compression en douceur dès ~50 % : seuls les passages forts de la bande sont tenus,
    // le timbre moyen reste celui de la voix.
    static Band defaultBand (int b) noexcept
    {
        switch (b)
        {
            case 0:  return { -28.0f, 3.5f, 25.0f, 250.0f, 10.0f, 0.0f, true };
            case 1:  return { -16.0f, 3.0f, 12.0f, 180.0f, 10.0f, 0.0f, true };
            case 2:  return { -29.0f, 3.5f,  6.0f, 120.0f, 10.0f, 0.0f, true };
            default: return { -40.0f, 3.5f,  2.5f,  80.0f, 10.0f, 0.0f, true };
        }
    }

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        amt.prepare (fs, 0.08, amt.target());
        onS.prepare (fs, 0.03, onS.target());
        for (int b = 0; b < kBands; ++b)
        {
            bandOn[(size_t) b].prepare (fs, 0.03, bandOn[(size_t) b].target());
            const float t = bandGain[(size_t) b].getTargetValue(), g = t > 0.0f ? t : 1.0f;   // jamais 0 (lissage multiplicatif)
            bandGain[(size_t) b].reset (fs, 0.03);
            bandGain[(size_t) b].setCurrentAndTargetValue (g);
        }
        match.prepare (fs, 1.5, 1.0);
        constexpr double q = 0.7071067811865476;   // Butterworth : 2 en cascade = Linkwitz-Riley 24 dB/oct
        const double f1 = kCross[0], f2 = kCross[1], f3 = std::min ((double) kCross[2], 0.4 * fs);
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

        static constexpr double rmsSec[kBands] { 0.010, 0.005, 0.003, 0.002 };
        for (int b = 0; b < kBands; ++b) rmsC[(size_t) b] = coefFor (rmsSec[b], fs);
        smoothC = coefFor (0.0015, fs);
        for (int b = 0; b < kBands; ++b) updateTimes (b);
        reset();
    }

    void reset() noexcept
    {
        for (auto* f : { &lp1a, &lp1b, &hp1a, &hp1b, &lp2a, &lp2b, &hp2a, &hp2b, &lp3a, &lp3b, &hp3a, &hp3b, &ap2a, &ap3a, &ap3b })
            f->reset();
        for (int b = 0; b < kBands; ++b)
        {
            const auto s = (size_t) b;
            ms[s] = env[s] = gr[s] = 0.0f;
            meter[s].store (0.0f, std::memory_order_relaxed);
            levelMeter[s].store (-100.0f, std::memory_order_relaxed);
        }
        match.reset();
    }

    void set (float amount01, const std::array<Band, kBands>& bands, bool snap) noexcept
    {
        amt.set (juce::jlimit (0.0f, 1.0f, amount01), snap);
        onS.set (amount01 > 0.0005f ? 1.0f : 0.0f, snap);
        for (int b = 0; b < kBands; ++b)
        {
            const auto s = (size_t) b;
            const auto& in = bands[s];
            Band& p = prm[s];
            const bool times = in.atkMs != p.atkMs || in.relMs != p.relMs;
            p.thrDb = juce::jlimit (-70.0f, 0.0f, in.thrDb);
            p.ratio = juce::jlimit (1.0f, 20.0f, in.ratio);
            p.kneeDb = juce::jlimit (0.0f, 24.0f, in.kneeDb);
            p.atkMs = juce::jlimit (0.1f, 300.0f, in.atkMs);
            p.relMs = juce::jlimit (5.0f, 3000.0f, in.relMs);
            p.gainDb = juce::jlimit (-24.0f, 24.0f, in.gainDb);
            p.on = in.on;
            if (times || snap) updateTimes (b);
            bandOn[s].set (p.on ? 1.0f : 0.0f, snap);
            const float g = dbToGain (p.on ? p.gainDb : 0.0f);
            if (snap) bandGain[s].setCurrentAndTargetValue (g); else bandGain[s].setTargetValue (g);
        }
    }

    // pour l'interface : réduction (dB) et niveau détecté (dBFS) de chaque bande
    float getBandReductionDb (int b) const noexcept { return meter[(size_t) juce::jlimit (0, kBands - 1, b)].load (std::memory_order_relaxed); }
    float getBandLevelDb (int b) const noexcept { return levelMeter[(size_t) juce::jlimit (0, kBands - 1, b)].load (std::memory_order_relaxed); }

    void process (float* const* ch, int numCh, int n) noexcept
    {
        if (onS.idle())
        {
            if (! wasIdle) { reset(); wasIdle = true; }
            for (int b = 0; b < kBands; ++b)
            {
                meter[(size_t) b].store (0.0f, std::memory_order_relaxed);
                levelMeter[(size_t) b].store (-100.0f, std::memory_order_relaxed);
            }
            return;
        }
        wasIdle = false;

        std::array<float, kBands> peakGr {}, peakLvl { -100.0f, -100.0f, -100.0f, -100.0f };
        const float inv = 1.0f / (float) numCh;
        for (int i = 0; i < n; ++i)
        {
            const float depth = std::pow (amt.next(), 0.6f);
            float band[kBands][2] {};
            float pin = 0.0f;
            for (int c = 0; c < numCh; ++c)
            {
                const float x = ch[c][i];
                pin += x * x;
                const float lp1 = lp1b.process (lp1a.process (x, c), c);
                const float hp1 = hp1b.process (hp1a.process (x, c), c);
                const float lp2 = lp2b.process (lp2a.process (hp1, c), c);
                const float hp2 = hp2b.process (hp2a.process (hp1, c), c);
                band[0][c] = ap3a.process (ap2a.process (lp1, c), c);
                band[1][c] = ap3b.process (lp2, c);
                band[2][c] = lp3b.process (lp3a.process (hp2, c), c);
                band[3][c] = hp3b.process (hp3a.process (hp2, c), c);
            }

            float gc[kBands], gu[kBands];
            for (int b = 0; b < kBands; ++b)
            {
                const auto s = (size_t) b;
                float pw = 0.0f;
                for (int c = 0; c < numCh; ++c) pw = std::max (pw, band[b][c] * band[b][c]);
                ms[s] += rmsC[s] * (pw - ms[s]);
                const float lvl = 10.0f * std::log10 (ms[s] * 2.0f + 1.0e-12f);   // RMS ramené à la crête d'un sinus
                peakLvl[s] = std::max (peakLvl[s], lvl);

                const float target = std::min (staticReductionDb (prm[s], lvl), 30.0f);
                env[s] += (target > env[s] ? atkC[s] : relC[s]) * (target - env[s]);
                const float on = bandOn[s].next();
                gr[s] += smoothC * (env[s] * depth * on - gr[s]);
                peakGr[s] = std::max (peakGr[s], gr[s]);
                gc[b] = dbToGain (-gr[s]);
                gu[b] = bandGain[s].getNextValue();
            }

            // rattrapage : seulement la perte due à la compression (sans les GAIN de bande)
            float pcomp = 0.0f, y[2] {};
            for (int c = 0; c < numCh; ++c)
            {
                float yc = 0.0f, yu = 0.0f;
                for (int b = 0; b < kBands; ++b) { const float v = band[b][c] * gc[b]; yc += v; yu += v * gu[b]; }
                pcomp += yc * yc;
                y[c] = yu;
            }
            const float mk = match.next (pin * inv, pcomp * inv, 0.5f, 4.0f);
            const float on = onS.next();
            for (int c = 0; c < numCh; ++c) ch[c][i] += on * (y[c] * mk - ch[c][i]);
        }
        for (int b = 0; b < kBands; ++b)
        {
            meter[(size_t) b].store (peakGr[(size_t) b], std::memory_order_relaxed);
            levelMeter[(size_t) b].store (peakLvl[(size_t) b], std::memory_order_relaxed);
        }
    }

    // courbe statique d'une bande (aussi pour l'interface) : réduction en dB pour un niveau détecté donné
    static float staticReductionDb (const Band& p, float levelDb) noexcept
    {
        const float slope = 1.0f - 1.0f / std::max (1.0f, p.ratio), over = levelDb - p.thrDb, k = p.kneeDb;
        if (2.0f * over >= k) return slope * over;
        if (2.0f * over > -k) return slope * (over + 0.5f * k) * (over + 0.5f * k) / (2.0f * std::max (k, 1.0e-3f));
        return 0.0f;
    }

private:
    // en dB, la constante de temps est calée pour que le temps réglé corresponde à ~90 % du trajet
    void updateTimes (int b) noexcept
    {
        const auto s = (size_t) b;
        atkC[s] = coefFor (prm[s].atkMs * 0.001 / 2.3, fs);
        relC[s] = coefFor (prm[s].relMs * 0.001 / 2.3, fs);
    }

    double fs = 48000.0;
    Smooth amt, onS;
    std::array<Smooth, kBands> bandOn;
    std::array<juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>, kBands> bandGain;
    std::array<Band, kBands> prm { defaultBand (0), defaultBand (1), defaultBand (2), defaultBand (3) };
    LevelMatch match;
    Biquad lp1a, lp1b, hp1a, hp1b, lp2a, lp2b, hp2a, hp2b, lp3a, lp3b, hp3a, hp3b, ap2a, ap3a, ap3b;
    std::array<float, kBands> ms {}, env {}, gr {}, atkC {}, relC {}, rmsC {};
    float smoothC = 0.0f;
    bool wasIdle = true;
    std::array<std::atomic<float>, kBands> meter {}, levelMeter {};
};
} // namespace onirique::mini
