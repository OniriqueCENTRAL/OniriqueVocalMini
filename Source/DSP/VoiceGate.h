#pragma once

#include <atomic>
#include "MiniDSP.h"

namespace onirique::mini
{
/*  NOISE GATE : nettoie les silences entre les phrases sans jamais toucher la voix (le gate de VOCALBOX / BROADCAST,
    réglé pour la voix chantée et rappée). Il est placé tout au début de la chaîne, après le NOISE SUPPRESSOR et
    AVANT la compression : le gain de rattrapage du COMP ne peut plus faire remonter le souffle entre les phrases. Le
    DELAY et la REVERB sont des pistes auxiliaires nourries par la voix déjà nettoyée : leurs queues continuent de
    s'éteindre naturellement quand le gate se ferme (le gate ne les coupe jamais).

    - Détecteur sur une chaîne latérale filtrée (passe-haut 80 Hz, passe-bas doux 10 kHz) : le grondement et les chocs
      n'ouvrent pas le gate, les sifflantes d'une fin de mot (« -s », « -ch ») si.
    - Niveau = RMS court (3 ms) combiné à la crête (au tiers) : réagit au corps de la voix, pas à un clic isolé ni aux
      pointes d'un bruit de fond (un souffle à -60 dBFS RMS se lit vers -57, sous un seuil réglé 7 dB au-dessus).
    - Hystérésis de 6 dB (ouverture au seuil, fermeture 6 dB plus bas) : aucun battement autour du seuil, les fins de
      phrases qui s'éteignent doucement restent ouvertes plus longtemps.
    - HOLD de 90 ms : le gate reste ouvert entre deux mots, les respirations courtes et les consonnes faibles ne sont
      pas hachées.
    - Attaque : 0.4 ms, et en MIX 1.5 ms d'anticipation (le gate est déjà ouvert quand la première consonne arrive).
    - RELEASE réglable (30 ms à 1.5 s, 200 ms par défaut) : la fermeture est une descente progressive, en dB, puis
      lissée encore une fois (1 ms) : courbe en S, aucun angle, aucune distorsion de modulation.
    - RANGE : le gate n'éteint pas, il ABAISSE (18 dB par défaut) : le fond de la pièce reste naturel derrière la voix ;
      à 60 dB il devient un vrai silence.
    - Coupé (OFF) : le gain revient en douceur à 0 dB (aucun saut) et le module ne modifie plus rien (seule l'anticipation
      reste en MIX, pour que la latence annoncée au DAW ne change jamais).
    Mesures publiées : niveau détecté, gain appliqué, état ouvert / fermé, plancher de bruit estimé (minimum glissant). */
class VoiceGate
{
public:
    static constexpr double kLookaheadSec = 0.0015;

    void prepare (double sampleRate, bool live)
    {
        fs = sampleRate;
        lookahead = live ? 0 : std::max (1, (int) std::lround (kLookaheadSec * fs));
        for (auto& d : delay) d.prepare (lookahead);
        scHp.setCoeffs (BiquadCoeffs::highpass (fs, 80.0, 0.707));
        scLp.setCoeffs (BiquadCoeffs::lowpass (fs, std::min (10000.0, 0.45 * fs), 0.6));
        rmsC = coefFor (0.003, fs);
        peakRel = coefFor (0.040, fs);
        linC = coefFor (0.001, fs);
        atkC = coefFor (0.0004 / 2.3, fs);
        floorWin = coefFor (0.060, fs);
        floorRise = (float) (3.0 / fs);
        holdSamples = (int) (0.090 * fs);
        updateRelease();
        reset();
    }

    void reset() noexcept
    {
        for (auto& d : delay) d.reset();
        scHp.reset(); scLp.reset();
        ms = peak = 0.0f;
        gDb = 0.0f; gLin = 1.0f;
        holdLeft = 0;
        isOpen = true;
        floorMs = 0.0f; floorDb = -90.0f;
    }

    int getLatencySamples() const noexcept { return lookahead; }

    // thresholdDb -80..-10, rangeDb 0..60, releaseMs 30..1500
    void set (bool enabled, float thresholdDb, float rangeDb, float releaseMs) noexcept
    {
        on = enabled;
        thr = juce::jlimit (-90.0f, 0.0f, thresholdDb);
        range = juce::jlimit (0.0f, 80.0f, rangeDb);
        if (releaseMs != relMs) { relMs = juce::jlimit (20.0f, 3000.0f, releaseMs); updateRelease(); }
    }

    void process (float* const* ch, int numCh, int n) noexcept
    {
        // coupé et revenu à 0 dB : plus rien à calculer, seule l'anticipation (MIX) retarde le signal
        if (! on && gDb > -0.001f && gLin > 0.99999f)
        {
            if (lookahead > 0)
                for (int c = 0; c < numCh; ++c)
                    for (int i = 0; i < n; ++i) ch[c][i] = delay[c].process (ch[c][i]);
            gDb = 0.0f; gLin = 1.0f; isOpen = true;
            levelMeter.store (-120.0f, std::memory_order_relaxed);
            gainMeter.store (0.0f, std::memory_order_relaxed);
            openMeter.store (true, std::memory_order_relaxed);
            return;
        }

        float maxLvl = -120.0f, minGain = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            float sc = 0.0f;
            for (int c = 0; c < numCh; ++c) sc = std::max (sc, std::abs (scLp.process (scHp.process (ch[c][i], c), c)));
            ms += rmsC * (sc * sc - ms);
            peak = sc > peak ? sc : peak + peakRel * (sc - peak);
            const float det = std::max (std::sqrt (2.0f * ms), 0.35f * peak);   // la crête compte peu : les pointes du bruit ne rouvrent pas le gate
            const float lvl = det > 1.0e-6f ? gainToDb (det) : -120.0f;
            maxLvl = std::max (maxLvl, lvl);

            // plancher de bruit : minimum glissant du RMS (60 ms), remonte de 3 dB/s
            floorMs += floorWin * (sc * sc - floorMs);
            const float fl = floorMs > 1.0e-12f ? 10.0f * std::log10 (2.0f * floorMs) : -120.0f;
            floorDb = fl < floorDb ? fl : floorDb + floorRise;

            if (! on || lvl >= thr) { isOpen = true; holdLeft = holdSamples; }
            else if (isOpen && lvl < thr - kHyst)
            {
                if (holdLeft > 0) --holdLeft;
                else isOpen = false;
            }
            else if (isOpen && holdLeft < holdSamples) holdLeft = std::min (holdSamples, holdLeft + 1);

            const float target = isOpen ? 0.0f : -range;
            gDb += (target > gDb ? atkC : relC) * (target - gDb);
            const float gt = gDb > -89.5f ? dbToGain (gDb) : 0.0f;
            gLin += linC * (gt - gLin);
            minGain = std::min (minGain, gDb);
            for (int c = 0; c < numCh; ++c) ch[c][i] = delay[c].process (ch[c][i]) * gLin;
        }
        levelMeter.store (maxLvl, std::memory_order_relaxed);
        gainMeter.store (minGain, std::memory_order_relaxed);
        openMeter.store (isOpen, std::memory_order_relaxed);
        floorMeter.store (floorDb, std::memory_order_relaxed);
    }

    // pour l'interface
    float getLevelDb() const noexcept { return levelMeter.load (std::memory_order_relaxed); }
    float getGainDb() const noexcept { return gainMeter.load (std::memory_order_relaxed); }
    bool isGateOpen() const noexcept { return openMeter.load (std::memory_order_relaxed); }
    float getFloorDb() const noexcept { return floorMeter.load (std::memory_order_relaxed); }

private:
    static constexpr float kHyst = 6.0f;
    // en dB, la constante de temps est calée pour que le temps affiché corresponde à ~90 % du trajet
    void updateRelease() noexcept { relC = coefFor (relMs * 0.001 / 2.3, fs); }

    double fs = 48000.0;
    int lookahead = 0, holdSamples = 4320, holdLeft = 0;
    DelayLine delay[2];
    Biquad scHp, scLp;
    bool on = false, isOpen = true;
    float thr = -50.0f, range = 18.0f, relMs = 200.0f;
    float rmsC = 0, peakRel = 0, linC = 0, atkC = 0, relC = 0;
    float ms = 0, peak = 0, gDb = 0, gLin = 1.0f, floorMs = 0, floorDb = -90.0f, floorWin = 0, floorRise = 0;
    std::atomic<float> levelMeter { -120.0f }, gainMeter { 0.0f }, floorMeter { -90.0f };
    std::atomic<bool> openMeter { true };
};
} // namespace onirique::mini
