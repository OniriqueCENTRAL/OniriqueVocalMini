#pragma once

#include <atomic>
#include "MiniDSP.h"

namespace onirique::mini
{
/*  DE-ESS : un seul bouton, la fréquence se trouve toute seule.

    - Cinq détecteurs (4.3, 5.3, 6.5, 8 et 9.8 kHz) mesurent où se trouve l'énergie des sifflantes ; le de-esser se
      place au centre de gravité de ces énergies (moyenne en fréquence logarithmique, lissée en 20 ms). Une voix grave
      aux S sourds et une voix fine aux S perçants sont traitées chacune à sa fréquence, sans réglage.
    - Détection RELATIVE (énergie de la bande / énergie de la voix entière) : indépendante du niveau, seuls les S
      ressortent, pas les passages forts.
    - Action à bande divisée, en parallèle : y = x + (g - 1) * bande. Quand rien ne siffle, le signal passe intact ;
      seule la zone sibilante descend, le reste de la voix garde tout son brillant.
    - Genou doux de 4 dB, attaque 0.3 ms, relâchement 35 ms : aucun zézaiement. En LIVE : aucune latence. En MIX :
      0.6 ms d'anticipation, la réduction est déjà en place quand le S arrive (son attaque n'est jamais attrapée en retard).
    - Le bouton dose à la fois le seuil, la pente et la réduction maximale (jusqu'à 14 dB).
*/
class SmartDeEsser
{
public:
    static constexpr int kDet = 5;

    // lookahead : anticipation en échantillons (0 en LIVE)
    void prepare (double sampleRate, int lookaheadSamples)
    {
        fs = sampleRate;
        lookahead = std::max (0, lookaheadSamples);
        for (int c = 0; c < 2; ++c) { dx[c].prepare (lookahead); db[c].prepare (lookahead); }
        amt.prepare (fs, 0.03, amt.target());
        static constexpr double f[kDet] { 4300.0, 5300.0, 6500.0, 8000.0, 9800.0 };
        for (int b = 0; b < kDet; ++b)
        {
            det[b].setCoeffs (BiquadCoeffs::bandpass (fs, std::min (f[b], 0.42 * fs), 2.2));
            det[b].reset();
            env[b] = 0.0f;
            logF[b] = (float) std::log (f[b]);
        }
        full.reset();
        bandAtk = coefFor (0.0003, fs); bandRel = coefFor (0.012, fs);
        fullAtk = coefFor (0.003, fs);  fullRel = coefFor (0.045, fs);
        redAtk = coefFor (0.0003, fs);  redRel = coefFor (0.035, fs);
        freqGlide = coefFor (0.020, fs);
        envFull = red = 0.0f;
        centreLog = (float) std::log (6500.0);
        act.reset(); detN.reset();
        countdown = 0;
        updateCoeffs();
    }

    void set (float amount01, bool snap) noexcept { amt.set (juce::jlimit (0.0f, 1.0f, amount01), snap); }

    int getLatencySamples() const noexcept { return lookahead; }
    float getReductionDb() const noexcept { return grMeter.load (std::memory_order_relaxed); }
    float getFrequency() const noexcept { return freqMeter.load (std::memory_order_relaxed); }

    void process (float* const* ch, int numCh, int n) noexcept
    {
        if (amt.idle())
        {
            // coupé : seul le retard d'anticipation tourne (la latence annoncée ne change pas avec le bouton)
            if (lookahead > 0)
                for (int c = 0; c < numCh; ++c)
                    for (int i = 0; i < n; ++i) { ch[c][i] = dx[c].process (ch[c][i]); db[c].process (0.0f); }
            red = 0.0f;
            grMeter.store (0.0f, std::memory_order_relaxed);
            return;
        }
        float peakRed = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float a = amt.next();

            // ---- où sont les sifflantes : énergie de chaque détecteur, centre de gravité
            float fb = 0.0f, wSum = 0.0f, wLog = 0.0f;
            float xb[2] {};
            for (int c = 0; c < numCh; ++c) fb = std::max (fb, std::abs (ch[c][i]));
            for (int b = 0; b < kDet; ++b)
            {
                float m = 0.0f;
                for (int c = 0; c < numCh; ++c) m = std::max (m, std::abs (det[b].process (ch[c][i], c)));
                env[b] += (m > env[b] ? bandAtk : bandRel) * (m - env[b]);
                const float w = env[b] * env[b];
                wSum += w;
                wLog += w * logF[b];
            }
            envFull += (fb > envFull ? fullAtk : fullRel) * (fb - envFull);
            // la fréquence ne bouge que quand ça siffle vraiment : entre deux S, elle garde celle de la voix
            if (wSum > 1.0e-10f && std::sqrt (wSum) > 0.22f * envFull)
                centreLog += freqGlide * (wLog / wSum - centreLog);

            if (--countdown <= 0) { updateCoeffs(); countdown = 16; }

            // ---- détection relative sur la bande centrée, réduction à genou doux
            float bandNow = 0.0f;
            for (int c = 0; c < numCh; ++c)
            {
                xb[c] = act.process (ch[c][i], c);
                bandNow = std::max (bandNow, std::abs (detN.process (ch[c][i], c)));
            }
            envBand += (bandNow > envBand ? bandAtk : bandRel) * (bandNow - envBand);

            float t = 0.0f;
            if (a > 0.001f && envBand > 1.0e-5f)
            {
                const float ratio = envBand / (envFull + 1.0e-6f);
                const float thr = 0.52f * std::pow (0.30f, a);
                t = reduction (ratio, thr, 14.0f * std::pow (a, 0.8f), 0.35f + 0.55f * a);
            }
            red += (t > red ? redAtk : redRel) * (t - red);
            peakRed = std::max (peakRed, red);
            const float gm = std::exp (-red * 0.11512925f) - 1.0f;
            // le signal et la bande sont retardés de l'anticipation : la réduction calculée maintenant s'applique au S
            // qui arrive (en LIVE, retard nul)
            for (int c = 0; c < numCh; ++c) ch[c][i] = dx[c].process (ch[c][i]) + gm * db[c].process (xb[c]);
        }
        grMeter.store (peakRed, std::memory_order_relaxed);
        freqMeter.store (std::exp (centreLog), std::memory_order_relaxed);
    }

private:
    static float reduction (float ratio, float thr, float maxRed, float slope) noexcept
    {
        if (ratio < thr * 0.6f) return 0.0f;
        constexpr float knee = 4.0f;
        const float over = 8.68588963807f * std::log (ratio / thr);
        float r;
        if (over <= -0.5f * knee)    return 0.0f;
        else if (over < 0.5f * knee) r = slope * (over + 0.5f * knee) * (over + 0.5f * knee) / (2.0f * knee);
        else                         r = slope * over;
        return std::min (r, maxRed);
    }

    void updateCoeffs() noexcept
    {
        const double f = juce::jlimit (3800.0, std::min (10500.0, 0.42 * fs), (double) std::exp (centreLog));
        detN.setCoeffs (BiquadCoeffs::bandpass (fs, f, 1.4));          // détection étroite, centrée sur les S
        act.setCoeffs (BiquadCoeffs::bandpass (fs, std::min (f * 1.15, 0.42 * fs), 0.7));   // action large, un peu au-dessus
    }

    double fs = 48000.0;
    Smooth amt;
    Biquad det[kDet], full, act, detN;
    float env[kDet] {}, logF[kDet] {};
    float bandAtk = 0, bandRel = 0, fullAtk = 0, fullRel = 0, redAtk = 0, redRel = 0, freqGlide = 0;
    float envFull = 0, envBand = 0, red = 0, centreLog = 8.8f;
    int countdown = 0, lookahead = 0;
    DelayLine dx[2], db[2];
    std::atomic<float> grMeter { 0.0f }, freqMeter { 6500.0f };
};
} // namespace onirique::mini
