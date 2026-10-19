#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include "Biquad.h"
#include "Common.h"

namespace onirique
{
/*  LA-2A : modèle comportemental d'un niveleur optique à lampes (inspiré du Teletronix LA-2A ; aucun schéma ni donnée
    constructeur, modèle original). Le modèle de VOCALBOX, retravaillé pour la voix dans VOCAL MINI.

    Ce qui vient de VOCALBOX :
    - Cellule optique (panneau électroluminescent + photorésistance) : c'est elle qui fait le son. Lumière L : attaque
      ~10 ms, chute rapide ~60 ms. Mémoire M de la photorésistance : se charge lentement (~1.5 s) quand on compresse
      longtemps et se vide en ~3.5 s. Réduction réelle = max(L, 0.5 M) : relâchement en DEUX TEMPS, rapide sur les
      mots isolés, de plus en plus long sur une phrase compressée en continu.
    - Contre-réaction (le détecteur voit la sortie de la cellule), résolue sans délai. PEAK REDUCTION 0..100 = gain du
      circuit de détection (seuil qui descend de 0 à -45 dBFS). GAIN : niveau de sortie. COMPRESS : ~3:1, genou très doux.
    - Liaison stéréo : un seul détecteur, même gain sur les deux canaux.

    Ce qui est nouveau :
    - DÉTECTEUR VOCAL : comme la compensation d'aigus du circuit de détection des vrais appareils, le détecteur est
      un peu plus sensible au-dessus de 3 kHz (+4 dB), et ignore le grave sous 60 Hz. La cellule nivelle la présence
      et les passages perçants en douceur, sans se laisser tirer par le grave : la voix est tenue là où l'oreille
      l'entend.
    - ATTAQUE SELON LE NIVEAU : la cellule s'allume plus vite quand le dépassement est fort (un cri, une consonne
      explosive est saisi en ~4 ms), et garde ses ~10 ms sur une montée douce : rien ne déborde, rien n'est écrasé.
    - LAMPES VIVANTES : l'harmonique 2 des lampes grandit avec le niveau (au lieu d'être constante) : discrète sur
      une voix douce, chaleureuse quand elle pousse. Le transformateur de sortie arrondit les crêtes.
*/
class OptoCompressor
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        gainS.reset (fs, 0.03);
        prS.reset (fs, 0.05);
        dc.setCoeffs (BiquadCoeffs::highpass (fs, 10.0, 0.707));
        scHp.setCoeffs (BiquadCoeffs::highpass (fs, 60.0, 0.707));
        scHf.setCoeffs (BiquadCoeffs::highShelf (fs, std::min (3000.0, 0.4 * fs), 0.7, 4.0));
        lAtkSlow = coef (0.010);
        lAtkFast = coef (0.004);
        lRel = coef (0.060);
        mUp = coef (1.5);
        mDown = coef (3.5);
        lvlC = coef (0.050);
        reset();
    }

    void reset() noexcept
    {
        L = M = gr = lvl = 0.0f;
        dc.reset(); scHp.reset(); scHf.reset();
        grMeter.store (0.0f, std::memory_order_relaxed);
    }

    // gainDb -12..+30, peak 0..100, limit = mode LIMIT
    void setParams (float gainDb, float peak, bool isLimit, bool snap) noexcept
    {
        const float g = dbToGain (gainDb), p = juce::jlimit (0.0f, 100.0f, peak);
        if (snap) { gainS.setCurrentAndTargetValue (g); prS.setCurrentAndTargetValue (p); }
        else      { gainS.setTargetValue (g); prS.setTargetValue (p); }
        limit = isLimit;
    }

    void process (float* const* ch, int numCh, int n) noexcept
    {
        const float k = limit ? 9.0f : 2.0f, knee = limit ? 6.0f : 14.0f;
        float peakGr = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            const float go = gainS.getNextValue();
            const float pr = prS.getNextValue();

            float pk = 0.0f;
            for (int c = 0; c < numCh; ++c) pk = std::max (pk, std::abs (scHf.process (scHp.process (ch[c][i], c), c)));
            // PEAK REDUCTION = gain du détecteur : à 0 rien ne compresse, à 100 le seuil est à -45 dBFS
            const float over = (pk > 1.0e-6f ? gainToDb (pk) : -120.0f) - (0.0f - 0.45f * pr);

            const float outEst = over - gr;
            const float kk = k * juce::jlimit (0.0f, 1.0f, (outEst + 0.5f * knee) / knee);

            // lumière : contre-réaction résolue (la cellule voit sa propre sortie) ; attaque plus vive si ça déborde fort
            float next;
            if (over <= 0.0f || pr < 0.01f)
                next = L * (1.0f - lRel);
            else
            {
                const float atk = lAtkSlow + (lAtkFast - lAtkSlow) * juce::jlimit (0.0f, 1.0f, (outEst - 4.0f) / 10.0f);
                auto solve = [&] (float c) { return (L * (1.0f - c) + c * kk * over) / (1.0f + c * kk); };
                next = solve (atk);
                if (next < L) next = solve (lRel);
            }
            L = juce::jlimit (0.0f, 40.0f, next);
            M += (L > M ? mUp : mDown) * (L - M);
            gr = std::max (L, 0.5f * M);
            peakGr = std::max (peakGr, gr);

            const float g = dbToGain (-gr);
            float mx = 0.0f;
            for (int c = 0; c < numCh; ++c) mx = std::max (mx, std::abs (ch[c][i] * g));
            lvl += lvlC * (mx - lvl);
            const float h2 = 0.015f + 0.05f * std::min (1.0f, lvl * 2.0f);   // lampes : plus elles sont poussées, plus elles chantent
            for (int c = 0; c < numCh; ++c)
            {
                float y = ch[c][i] * g;
                y += h2 * y * y;                          // harmonique 2 (DC retiré ensuite)
                y = y / (1.0f + 0.04f * y * y);           // transformateur de sortie
                ch[c][i] = dc.process (y, c) * go;
            }
        }
        grMeter.store (peakGr, std::memory_order_relaxed);
    }

    float getGainReductionDb() const noexcept { return grMeter.load (std::memory_order_relaxed); }

private:
    float coef (double seconds) const noexcept { return (float) (1.0 - std::exp (-1.0 / (seconds * fs))); }

    double fs = 44100.0;
    bool limit = false;
    float lAtkSlow = 0, lAtkFast = 0, lRel = 0, mUp = 0, mDown = 0, lvlC = 0;
    float L = 0, M = 0, gr = 0, lvl = 0;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> gainS { 1.0f };
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> prS;
    Biquad dc, scHp, scHf;
    std::atomic<float> grMeter { 0.0f };
};
} // namespace onirique
