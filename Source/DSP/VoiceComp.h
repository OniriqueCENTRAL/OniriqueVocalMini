#pragma once

#include <atomic>
#include "MiniDSP.h"
#include "Comp1176.h"
#include "CompLA2A.h"

namespace onirique::mini
{
/*  COMP : la voix tenue dans un bloc, en un seul bouton, à la manière d'un compresseur de voix « tout-en-un » (seuil,
    rattrapage et limitation pilotés ensemble), avec la couleur d'un LA-2A et d'un 1176.

    1) OPTO (modèle de LA-2A retravaillé) : NIVELLE le corps de la voix. Son détecteur suit le niveau moyen (et un peu
       les crêtes) ; le bouton descend son seuil de -10 à -36 dBFS, ratio ~3:1 à genou très doux. Les mots forts et les
       mots doux se rapprochent : la voix ne monte plus et ne descend plus.
    2) FET (modèle de 1176 retravaillé) : attrape les crêtes que le niveleur laisse passer (consonnes, attaques), 4:1,
       attaque rapide, relâchement vif ; le bouton y pousse un peu plus la voix.
    3) GARDE : un dernier étage très rapide, à genou doux, qui tient les pointes restantes sous un plafond (de -4 à
       -8 dBFS avec le bouton) : rien ne dépasse du bloc, même à la reprise après un silence. Une pointe brève (un K,
       un T) est relâchée en 20 ms : la syllabe qui suit garde son niveau, pas de pompage sur les consonnes dures.
    4) PRÉSENCE GARDÉE : une voix tenue perd un peu de présence (les voyelles fortes, riches en 1-4 kHz, sont celles qui
       descendent le plus) ; une cloche large à 2.5 kHz la rend à mesure qu'on monte le bouton (jusqu'à +1.4 dB) : la
       voix reste devant au lieu de reculer.
    5) RATTRAPAGE FIXE : le gain de sortie dépend seulement du bouton, calibré sur des voix réelles au niveau BEST pour
       que monter le bouton NE MONTE PAS le niveau de la voix : on entend la voix se tenir et devenir dense, pas plus
       forte. Rien ne s'ajuste tout seul dans le temps : aucun saut de volume après un silence.
    À 0 le module est contourné (fondu) : rien ne colore la voix.
*/
class VoiceComp
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        fet.prepare (fs);
        opto.prepare (fs);
        amt.prepare (fs, 0.08, amt.target());
        catchAtk = coefFor (0.0002, fs);
        catchRel = coefFor (0.060, fs);
        catchBrief = coefFor (0.020, fs);
        catchSlowC = coefFor (0.040, fs);
        catchGr = catchSlow = 0.0f;
        presence.reset();
        lastA = -1.0f;
        apply (amt.target(), true);
    }

    void reset() noexcept { fet.reset(); opto.reset(); presence.reset(); catchGr = catchSlow = 0.0f; }

    void set (float amount01, bool snap) noexcept
    {
        amt.set (juce::jlimit (0.0f, 1.0f, amount01), snap);
        if (snap) apply (amount01, true);
    }

    float getReductionDb() const noexcept { return grMeter.load (std::memory_order_relaxed); }

    // rattrapage (dB) pour un réglage : calibré pour que le niveau des passages chantés ne bouge pas (voix au niveau BEST)
    static float makeupDb (float a) noexcept { return 13.0f * std::pow (a, 1.25f) + 1.6f * std::pow (a, 0.4f); }
    static float ceilingDb (float a) noexcept { return -4.0f - 4.0f * a; }

    void process (float* const* ch, int numCh, int n, float* const* scratch) noexcept
    {
        if (amt.idle()) { if (! wasIdle) { reset(); wasIdle = true; } grMeter.store (0.0f, std::memory_order_relaxed); return; }
        wasIdle = false;

        float peakCatch = 0.0f;
        // réglages suivis par tranches de 32 échantillons (les étages lissent eux-mêmes leurs gains)
        for (int pos = 0; pos < n; pos += 32)
        {
            const int m = std::min (32, n - pos);
            const float a = amt.current();
            if (std::abs (a - lastA) > 1.0e-4f) apply (a, false);
            float* x[2] { ch[0] + pos, numCh > 1 ? ch[1] + pos : nullptr };
            float* d[2] { scratch[0], scratch[1] };
            for (int c = 0; c < numCh; ++c) std::copy (x[c], x[c] + m, d[c]);

            opto.process (x, numCh, m);
            fet.process (x, numCh, m);
            for (int c = 0; c < numCh; ++c)
                for (int i = 0; i < m; ++i) x[c][i] = presence.process (x[c][i], c);

            for (int i = 0; i < m; ++i)
            {
                const float w = amt.next();
                const float mk = dbToGain (makeupDb (w));
                // garde : plafond à genou doux (3 dB), 8:1, attaque 0.2 ms, relâchement 60 ms
                float pk = 0.0f;
                for (int c = 0; c < numCh; ++c) pk = std::max (pk, std::abs (x[c][i] * mk));
                const float over = (pk > 1.0e-6f ? gainToDb (pk) : -120.0f) - ceilingDb (w);
                float target = 0.0f;
                if (over > 1.5f) target = 0.875f * over;
                else if (over > -1.5f) target = 0.875f * (over + 1.5f) * (over + 1.5f) / 6.0f;
                // une pointe brève (consonne) est relâchée en 20 ms : la voyelle qui suit n'est pas touchée
                const float brief = catchGr > 0.3f ? juce::jlimit (0.0f, 1.0f, 2.0f * (catchGr - catchSlow) / catchGr) : 0.0f;
                catchGr += (target > catchGr ? catchAtk : catchRel + brief * (catchBrief - catchRel)) * (target - catchGr);
                catchSlow += catchSlowC * (catchGr - catchSlow);
                peakCatch = std::max (peakCatch, catchGr);
                const float g = mk * dbToGain (-catchGr);
                const float mix = std::min (1.0f, w * 20.0f);   // de 0 à 5 % : on entre en fondu dans le module
                for (int c = 0; c < numCh; ++c) x[c][i] = d[c][i] + mix * (x[c][i] * g - d[c][i]);
            }
        }
        grMeter.store (fet.getGainReductionDb() + opto.getGainReductionDb() + peakCatch, std::memory_order_relaxed);
    }

private:
    void apply (float a, bool snap) noexcept
    {
        lastA = a;
        const float thr = -10.0f - 26.0f * std::pow (a, 0.8f);          // seuil du niveleur (dBFS)
        opto.setParams (0.0f, -thr / 0.45f, false, snap);
        const float push = -10.0f + 14.0f * a;
        fet.setParams (push, -push, 3.6f, 5.4f, 0, snap);
        presence.setCoeffs (BiquadCoeffs::peaking (fs, std::min (2500.0, 0.4 * fs), 0.8, 1.4 * a));
    }

    double fs = 48000.0;
    FetCompressor fet;
    OptoCompressor opto;
    Biquad presence;
    Smooth amt;
    float catchAtk = 0.0f, catchRel = 0.0f, catchBrief = 0.0f, catchSlowC = 0.0f, catchGr = 0.0f, catchSlow = 0.0f;
    float lastA = -1.0f;
    bool wasIdle = true;
    std::atomic<float> grMeter { 0.0f };
};
} // namespace onirique::mini
