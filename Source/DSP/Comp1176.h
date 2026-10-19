#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include "Biquad.h"
#include "Common.h"

namespace onirique
{
/*  1176 : modèle comportemental d'un limiteur FET (inspiré du UREI/UA 1176LN ; aucun schéma ni donnée constructeur,
    modèle original). Le modèle de VOCALBOX, retravaillé pour la voix dans VOCAL MINI.

    Ce qui vient de VOCALBOX :
    - Topologie en contre-réaction comme l'original : le détecteur regarde la SORTIE de l'atténuateur FET. Résolue
      sans délai (formule implicite) : stable même à l'attaque la plus rapide. En contre-réaction, une pente
      k = ratio - 1 appliquée au niveau de sortie donne le ratio voulu.
    - INPUT : gain avant l'étage (seuil fixe, comme le vrai : on « pousse » dans le compresseur). OUTPUT : niveau de
      sortie. ATTACK 1..7 : 800 µs -> 20 µs. RELEASE 1..7 : 1.1 s -> 50 ms. RATIO 4 / 8 / 12 / 20 : le seuil monte
      avec le ratio et le genou se durcit, comme sur l'appareil.
    - Couleur : le FET ajoute de l'harmonique 2 proportionnelle à la réduction ; liaison stéréo (un seul détecteur).

    Ce qui est nouveau :
    - DÉTECTEUR SANS GRAVES : le détecteur passe par un passe-haut à 90 Hz. Les plosives (P, B) et l'effet de
      proximité ne font plus plonger la compression : elle répond au corps de la voix, pas au souffle dans le micro.
    - DÉTECTION CRÊTE + RMS COURT : le niveau mesuré mêle la crête (le 1176 attrape toujours les pics) et une
      moyenne de 3 ms. Sur une voyelle tenue, la réduction ne suit plus chaque oscillation de l'onde : plus de
      « grésillement » de modulation aux relâchements rapides, la compression reste lisse et chantante.
    - RELÂCHEMENT À MÉMOIRE : quand la compression dure (une phrase entière), une mémoire lente se charge et le
      relâchement s'allonge (jusqu'à 1.8x) ; sur un mot isolé il reste vif. Fini le pompage en fin de phrase.
    - CONSONNES IGNORÉES : une réduction très brève (un K, un T, une attaque de consonne) se relâche en 25 ms au lieu du
      relâchement normal : la voyelle qui suit n'est pas étouffée, aucun « pompage » sur les syllabes dures. Une
      réduction qui dure (une phrase forte) garde son relâchement normal. Le détecteur est aussi un peu moins sensible
      aux aigus (-3 dB au-dessus de 4 kHz) : les consonnes, riches en aigus, ne le déclenchent plus autant que la voix.
    - FER DE SORTIE : le transformateur ne sature que dans le grave (< 120 Hz), un peu plus quand la réduction est
      forte : la voix comprimée garde son poids au lieu de s'amincir. L'étage classe A arrondit les crêtes.
*/
class FetCompressor
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        inS.reset (fs, 0.03);
        outS.reset (fs, 0.03);
        dc.setCoeffs (BiquadCoeffs::highpass (fs, 10.0, 0.707));
        scHp.setCoeffs (BiquadCoeffs::highpass (fs, 90.0, 0.707));
        scHf.setCoeffs (BiquadCoeffs::highShelf (fs, std::min (4000.0, 0.4 * fs), 0.7, -3.0));
        slowC = (float) (1.0 - std::exp (-1.0 / (0.040 * fs)));
        briefRel = (float) (1.0 - std::exp (-1.0 / (0.025 * fs)));
        rmsC = (float) (1.0 - std::exp (-1.0 / (0.003 * fs)));
        memUp = (float) (1.0 - std::exp (-1.0 / (0.8 * fs)));
        memDown = (float) (1.0 - std::exp (-1.0 / (2.5 * fs)));
        ironC = (float) (1.0 - std::exp (-6.283185307179586 * 120.0 / fs));
        reset();
        updateTimes();
    }

    void reset() noexcept
    {
        gr = mem = ms = grSlow = 0.0f;
        dc.reset(); scHp.reset(); scHf.reset();
        iron[0] = iron[1] = 0.0f;
        grMeter.store (0.0f, std::memory_order_relaxed);
    }

    // inDb -12..+36, outDb -24..+24, attack/release 1..7, ratioIdx 0..3 (4, 8, 12, 20)
    void setParams (float inDb, float outDb, float attack, float release, int ratioIdx, bool snap) noexcept
    {
        const float gi = dbToGain (inDb), go = dbToGain (outDb);
        if (snap) { inS.setCurrentAndTargetValue (gi); outS.setCurrentAndTargetValue (go); }
        else      { inS.setTargetValue (gi); outS.setTargetValue (go); }

        ratioIdx = juce::jlimit (0, 3, ratioIdx);
        if (attack != atkKnob || release != relKnob || ratioIdx != ratio)
        {
            atkKnob = juce::jlimit (1.0f, 7.0f, attack);
            relKnob = juce::jlimit (1.0f, 7.0f, release);
            ratio = ratioIdx;
            updateTimes();
        }
    }

    static float attackSeconds (float knob) noexcept  { return 800.0e-6f * std::pow (20.0f / 800.0f, (knob - 1.0f) / 6.0f); }
    static float releaseSeconds (float knob) noexcept { return 1.1f * std::pow (0.05f / 1.1f, (knob - 1.0f) / 6.0f); }

    void process (float* const* ch, int numCh, int n) noexcept
    {
        static constexpr float kSlope[] { 3.0f, 7.0f, 11.0f, 19.0f };      // ratio - 1 (contre-réaction)
        static constexpr float kThr[]   { -20.0f, -18.0f, -17.0f, -16.0f };  // le seuil monte avec le ratio
        static constexpr float kKnee[]  { 8.0f, 6.0f, 5.0f, 4.0f };
        const float k = kSlope[ratio], thr = kThr[ratio], knee = kKnee[ratio];

        float peakGr = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float gi = inS.getNextValue(), go = outS.getNextValue();

            // ---- détecteur : sans graves, crête et RMS court mêlés
            float pk = 0.0f, p2 = 0.0f;
            for (int c = 0; c < numCh; ++c)
            {
                const float s = scHf.process (scHp.process (ch[c][i] * gi, c), c);
                pk = std::max (pk, std::abs (s));
                p2 = std::max (p2, s * s);
            }
            ms += rmsC * (p2 - ms);
            const float det = std::sqrt (0.6f * pk * pk + 0.4f * 2.0f * ms);
            const float lvl = det > 1.0e-6f ? gainToDb (det) : -120.0f;
            const float over = lvl - thr;

            // genou doux : la pente monte progressivement autour du seuil (estimée sur le niveau de sortie précédent)
            const float outEst = over - gr;
            const float kk = k * juce::jlimit (0.0f, 1.0f, (outEst + 0.5f * knee) / knee);

            float next;
            // réduction brève (consonne) : relâchement rapide ; réduction qui dure : relâchement normal
            const float brief = gr > 0.5f ? juce::jlimit (0.0f, 1.0f, 2.0f * (gr - grSlow) / gr) : 0.0f;
            const float rn = relCoef();
            const float rel = rn + brief * (briefRel - rn);
            if (over <= 0.0f)
                next = gr * (1.0f - rel);
            else
            {
                // contre-réaction résolue : gr' = gr + c (kk (over - gr') - gr)
                auto solve = [&] (float c) { return (gr * (1.0f - c) + c * kk * over) / (1.0f + c * kk); };
                next = solve (atk);
                if (next < gr) next = solve (rel);
            }
            gr = juce::jlimit (0.0f, 40.0f, next);
            grSlow += slowC * (gr - grSlow);
            mem += (gr > mem ? memUp : memDown) * (gr - mem);
            peakGr = std::max (peakGr, gr);

            const float g = dbToGain (-gr) * gi;
            const float fet = 0.07f * std::min (1.0f, gr / 12.0f);
            const float ironDrive = 1.2f + 0.08f * gr;
            for (int c = 0; c < numCh; ++c)
            {
                float y = ch[c][i] * g;
                y += fet * y * y;                            // FET : harmonique 2 avec la réduction (DC retiré ensuite)
                y = y / (1.0f + 0.06f * y * y);              // étage classe A : crêtes arrondies
                iron[c] += ironC * (y - iron[c]);            // fer : seul le grave sature
                y += std::tanh (ironDrive * iron[c]) / ironDrive - iron[c];
                ch[c][i] = dc.process (y, c) * go;
            }
        }
        grMeter.store (peakGr, std::memory_order_relaxed);
    }

    float getGainReductionDb() const noexcept { return grMeter.load (std::memory_order_relaxed); }

private:
    float coefFor (double seconds) const noexcept { return (float) (1.0 - std::exp (-1.0 / (seconds * fs))); }

    void updateTimes() noexcept
    {
        atk = coefFor (attackSeconds (atkKnob));
        relBase = releaseSeconds (relKnob);
    }

    // relâchement dépendant du programme : plus lent quand la réduction est profonde, et quand elle dure (mémoire)
    float relCoef() const noexcept
    {
        return coefFor (relBase * (1.0f + 0.4f * std::min (1.0f, gr / 20.0f)) * (1.0f + 0.8f * std::min (1.0f, mem / 8.0f)));
    }

    double fs = 44100.0;
    float atkKnob = 3.0f, relKnob = 5.0f;
    int ratio = 0;
    float atk = 0.5f, relBase = 0.2f, gr = 0.0f, mem = 0.0f, ms = 0.0f;
    float rmsC = 0.0f, memUp = 0.0f, memDown = 0.0f, ironC = 0.0f, iron[2] {};
    float slowC = 0.0f, briefRel = 0.0f, grSlow = 0.0f;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> inS { 1.0f }, outS { 1.0f };
    Biquad dc, scHp, scHf;
    std::atomic<float> grMeter { 0.0f };
};
} // namespace onirique
