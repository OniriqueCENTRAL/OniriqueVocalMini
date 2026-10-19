#pragma once

#include <atomic>
#include "MiniDSP.h"

namespace onirique::mini
{
/*  Les reverbs de VOCAL MINI : le moteur d'ONIRIQUE VERB, retravaillé pour la voix et poussé plus loin.

    Moteur : réseau de 16 lignes à retards modulés (FDN, matrice de Hadamard), amortissement calculé pour le temps de
    décroissance (méthode de Jot : graves et aigus décroissent chacun à leur temps). Ajouts par rapport à VERB :
      - entrée STÉRÉO : le milieu et les côtés de la voix entrent dans le réseau par des chemins différents (un
        doubler ou un chorus garde sa largeur dans la reverb) ;
      - DENSITÉ : un passe-tout dans chaque ligne, à l'intérieur de la boucle : la queue devient dense dès les
        premières millisecondes, sans « flutter » ni échos séparés, même sur les petits espaces ;
      - diffusion d'entrée en 4 passe-tout, puis décorrélation de sortie (deux passe-tout différents à gauche et à
        droite) : une image large et enveloppante, sans trou au centre ;
      - VOIX PROPRE : les sifflantes sont retirées de ce qui entre dans la reverb, les bas-médiums (350 Hz) sont
        creusés dans la queue, et le retour s'efface un peu sous la voix (ducking propre à chaque espace).
    C'est une piste auxiliaire : la voix reste entière, le bouton règle ce qui entre (comme un envoi) ; quand on le
    baisse, la queue déjà partie s'éteint naturellement. Un espace à 0 dont la queue est éteinte ne coûte rien.

    Les quatre espaces :
      SHORT : une ambiance courte et serrée (0.45 s), surtout des premières réflexions : épaissit et place la voix
              sans queue audible.
      SMALL : une petite plaque de studio (1.1 s), brillante et dense : la reverb de voix de tous les jours.
      BIG   : une grande salle (2.6 s), pré-délai de 35 ms : la voix garde sa netteté devant un grand espace.
      LUSH  : une plaque de rêve (4.5 s), très modulée, avec un voile d'octave : la queue chatoie et s'étire.
*/
class VoiceReverb
{
public:
    static constexpr int kLines = 16;
    enum Kind { shortRoom = 0, smallPlate, bigHall, lushPlate };

    struct Character
    {
        float rt, hfRatio, size, preMs, erGain, erSize, modDepth, modRate, shimmer, lowCut, highCut, duck, outGain, density;
    };
    static Character character (int kind) noexcept
    {
        switch (kind)
        {
            case shortRoom:  return { 0.45f, 0.55f, 0.30f,  4.0f, 0.75f, 0.45f, 0.15f, 0.7f, 0.00f, 260.0f, 11000.0f, 0.00f, 0.95f, 0.65f };
            case bigHall:    return { 2.60f, 0.50f, 1.25f, 35.0f, 0.30f, 1.10f, 0.35f, 0.5f, 0.00f, 200.0f,  7500.0f, 0.35f, 1.00f, 0.55f };
            case lushPlate:  return { 4.50f, 0.58f, 1.05f, 25.0f, 0.00f, 0.00f, 1.00f, 0.8f, 0.09f, 300.0f,  8000.0f, 0.30f, 1.05f, 0.60f };
            default:         return { 1.10f, 0.70f, 0.62f, 15.0f, 0.15f, 0.60f, 0.30f, 0.7f, 0.00f, 220.0f,  9500.0f, 0.20f, 1.00f, 0.60f };
        }
    }

    void prepare (double sampleRate, int kind)
    {
        fs = sampleRate;
        ch = character (kind);
        seed = (uint32_t) (kind * 7919 + 13);
        for (auto& l : lines) l.prepare ((int) (0.20 * fs));
        preDelay.prepare ((int) (0.06 * fs));
        erLine.prepare ((int) (0.12 * fs));
        shimBuf.prepare ((int) (0.2 * fs));
        static constexpr int apLen[4] { 142, 107, 379, 277 };      // passe-tout de diffusion (à 29 761 Hz)
        for (int a = 0; a < 4; ++a) inAp[a].prepare ((int) (apLen[a] * fs / 29761.0));
        static constexpr float outApMs[4] { 3.1f, 4.7f, 3.9f, 5.3f };
        for (int a = 0; a < 4; ++a) outAp[a].prepare ((int) (outApMs[a] * 0.001 * fs));
        send.prepare (fs, 0.05, send.target());
        duck.prepare (fs);
        design();
        reset();
    }

    void reset() noexcept
    {
        for (auto& l : lines) l.reset();
        for (auto& a : loopAp) a.reset();
        for (auto& a : inAp) a.reset();
        for (auto& a : outAp) a.reset();
        preDelay.reset(); erLine.reset(); shimBuf.reset();
        for (auto& s : lpState) s = 0.0f;
        for (auto* b : { &inHp, &sibHp, &erLp, &shimLp, &shimHp, &mud }) b->reset();
        for (int c = 0; c < 2; ++c) { lc[c].reset(); hc[c].reset(); }
        sibEnvH = sibEnvF = sibCut = 0.0f;
        duck.reset();
        shimPhase = 0.0f;
        tail = 0.0f;
        for (int k = 0; k < kLines; ++k) lfo[k] = (double) k * 0.73;
    }

    void set (float amount01, bool snap) noexcept { send.set (std::pow (juce::jlimit (0.0f, 1.0f, amount01), 1.3f), snap); }

    float getTail() const noexcept { return tailMeter.load (std::memory_order_relaxed); }
    bool isActive() const noexcept { return ! send.idle() || tail > 2.0e-5f; }

    // la voix (in) entre dans la reverb ; son retour s'ajoute dans wet
    void process (const float* const* in, float* const* wet, int numCh, int n) noexcept
    {
        if (! isActive()) { if (! wasIdle) { reset(); wasIdle = true; } tailMeter.store (0.0f, std::memory_order_relaxed); return; }
        wasIdle = false;
        const float apG[4] { 0.75f, 0.75f, 0.625f, 0.625f };
        const double lfoInc = 2.0 * juce::MathConstants<double>::pi * ch.modRate / fs;
        const float modSamples = (0.0003f + 0.0012f * ch.modDepth) * (float) fs;
        const float pre = std::max (2.0f, ch.preMs * 0.001f * (float) fs);
        float pk = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            const float xl = in[0][i], xr = numCh > 1 ? in[1][i] : xl;
            const float s = send.next();
            float mid = 0.5f * (xl + xr), side = 0.5f * (xl - xr);

            // ---- voix propre : coupe-bas et sifflantes retirées avant d'entrer
            mid = inHp.process (mid, 0);
            side = inHp.process (side, 1);
            const float hi = sibHp.process (mid, 0);
            const float ah = std::abs (hi), af = std::abs (mid);
            sibEnvH += (ah > sibEnvH ? sibAtk : sibRel) * (ah - sibEnvH);
            sibEnvF += (af > sibEnvF ? sibAtk : sibRel) * (af - sibEnvF);
            const float cut = juce::jlimit (0.0f, 1.0f, (sibEnvH / (sibEnvF + 1.0e-6f) - 0.3f) * 3.0f);
            sibCut += gainSmooth * (cut - sibCut);
            mid = (mid - 0.85f * sibCut * hi) * s;
            side *= s * (1.0f - 0.85f * sibCut);

            preDelay.push (mid);
            float x = preDelay.read (pre);

            // premières réflexions (SHORT, SMALL, BIG)
            erLine.push (x);
            float erL = 0.0f, erR = 0.0f;
            if (ch.erGain > 0.0f)
            {
                for (int t = 0; t < kTaps; ++t)
                {
                    const float v = erLine.tap (erTap[t]) * erAmp[t];
                    (t & 1 ? erR : erL) += v;
                }
                erL = erLp.process (erL, 0) * ch.erGain;
                erR = erLp.process (erR, 1) * ch.erGain;
            }

            // diffusion d'entrée
            for (int a = 0; a < 4; ++a) x = inAp[a].process (x, apG[a]);

            // voile d'octave (LUSH)
            float shim = 0.0f;
            if (ch.shimmer > 0.0f)
            {
                shimPhase += 1.0f / shimWin;
                if (shimPhase >= 1.0f) shimPhase -= 1.0f;
                const float pA = shimPhase, pB = std::fmod (shimPhase + 0.5f, 1.0f);
                const float a = shimBuf.read (2.0f + (1.0f - pA) * shimWin), b = shimBuf.read (2.0f + (1.0f - pB) * shimWin);
                const float wa = std::sin (pA * 3.1415927f), wb = std::sin (pB * 3.1415927f);
                shim = shimLp.process (shimHp.process (a * wa * wa + b * wb * wb, 0), 0) * ch.shimmer;
            }

            // réseau : lecture modulée, passe-tout de densité, amortissement, Hadamard, réinjection
            float y[kLines];
            for (int k = 0; k < kLines; ++k)
            {
                lfo[k] += lfoInc * lfoMul[k];
                float v = lines[k].read (lineLen[k] + modSamples * (float) std::sin (lfo[k]));
                v = loopAp[k].process (v, ch.density);
                lpState[k] = absB[k] * v + absA[k] * lpState[k];
                y[k] = lpState[k];
            }
            float h[kLines];
            std::copy (y, y + kLines, h);
            for (int len = 1; len < kLines; len <<= 1)
                for (int st = 0; st < kLines; st += len << 1)
                    for (int j = st; j < st + len; ++j)
                    {
                        const float a = h[j], b = h[j + len];
                        h[j] = a + b; h[j + len] = a - b;
                    }
            const float inj = x + shim;
            for (int k = 0; k < kLines; ++k)
                lines[k].push (0.25f * h[k] + (inj * inSign[k] + side * sideSign[k]) * 0.35f);

            float wl = 0.0f, wr = 0.0f;
            for (int k = 0; k < kLines; ++k) { wl += y[k] * outL[k]; wr += y[k] * outR[k]; }
            wl *= outGain; wr *= outGain;
            if (ch.shimmer > 0.0f) shimBuf.push (0.5f * (wl + wr));

            // décorrélation, premières réflexions, EQ de la queue
            wl = outAp[1].process (outAp[0].process (wl, 0.5f), 0.5f) + erL;
            wr = outAp[3].process (outAp[2].process (wr, 0.5f), 0.5f) + erR;
            wl = hc[0].process (lc[0].process (mud.process (wl, 0), 0), 0);
            wr = hc[1].process (lc[1].process (mud.process (wr, 1), 0), 0);
            const float g = duck.next (0.5f * (xl + xr), ch.duck);
            wl = sanitize (wl * g); wr = sanitize (wr * g);
            if (numCh > 1) { wet[0][i] += wl; wet[1][i] += wr; }
            else wet[0][i] += 0.5f * (wl + wr);
            pk = std::max (pk, std::abs (wl) + std::abs (wr));
        }
        for (auto& l : lfo) l = std::fmod (l, 2.0 * juce::MathConstants<double>::pi);
        tail = pk;
        tailMeter.store (pk, std::memory_order_relaxed);
    }

private:
    static constexpr int kTaps = 12;

    void design()
    {
        juce::Random rnd ((juce::int64) seed);
        static constexpr float baseMs[kLines] { 29.7f, 37.1f, 41.1f, 43.7f, 47.9f, 53.3f, 59.0f, 61.4f,
                                                67.3f, 71.1f, 73.9f, 79.6f, 83.5f, 89.2f, 97.3f, 101.6f };
        static constexpr float apMs[kLines] { 2.3f, 3.1f, 2.7f, 3.7f, 4.3f, 2.9f, 3.3f, 4.1f,
                                              2.1f, 3.9f, 4.7f, 2.5f, 3.5f, 4.9f, 2.2f, 3.6f };
        const float rt = ch.rt, rtHi = std::max (0.08f, ch.rt * ch.hfRatio);
        float gAvg = 0.0f;
        for (int k = 0; k < kLines; ++k)
        {
            lineLen[k] = std::min ((float) (0.19 * fs), baseMs[k] * ch.size * 0.001f * (float) fs);
            const int ap = std::max (8, (int) (apMs[k] * (0.5f + 0.5f * ch.size) * 0.001f * (float) fs));
            loopAp[k].prepare (ap);
            // le passe-tout allonge le trajet de la ligne : il compte dans le temps de décroissance
            const float trip = lineLen[k] + (float) ap;
            const float gdc = std::pow (10.0f, -3.0f * trip / (rt * (float) fs));
            const float gny = std::pow (10.0f, -3.0f * trip / (rtHi * (float) fs));
            const float a = (gdc - gny) / (gdc + gny);
            absA[k] = a;
            absB[k] = gdc * (1.0f - a);
            gAvg += gdc;
            lfoMul[k] = 0.7f + 0.6f * (float) ((k * 7) % 11) / 10.0f;
            inSign[k] = (k * 5 + 3) % 4 < 2 ? 1.0f : -1.0f;
            sideSign[k] = (k & 1) ? 0.6f : -0.6f;
            outL[k] = ((k & 1) ? -1.0f : 1.0f) * 0.25f;
            outR[k] = (((k >> 1) ^ (k >> 2)) & 1 ? -1.0f : 1.0f) * 0.25f;
        }
        gAvg /= (float) kLines;
        outGain = 1.5f * ch.outGain * std::pow (std::max (0.02f, 1.0f - gAvg * gAvg), 0.35f);
        shimWin = (float) (0.06 * fs);
        static constexpr float erMs[kTaps] { 7.1f, 11.3f, 13.7f, 17.2f, 19.9f, 23.4f, 29.1f, 31.3f, 37.7f, 41.2f, 43.9f, 47.8f };
        for (int t = 0; t < kTaps; ++t)
        {
            erTap[t] = juce::jlimit (1, (int) (0.115 * fs), (int) (erMs[t] * std::max (0.2f, ch.erSize) * 0.001f * (float) fs));
            erAmp[t] = 0.55f * std::pow (0.82f, (float) t) * (0.85f + 0.3f * rnd.nextFloat());
        }
        inHp.setCoeffs (BiquadCoeffs::highpass (fs, ch.lowCut * 0.7, 0.707));
        sibHp.setCoeffs (BiquadCoeffs::highpass (fs, std::min (5000.0, 0.4 * fs), 0.707));
        erLp.setCoeffs (BiquadCoeffs::lowpass (fs, std::min (6500.0, 0.45 * fs), 0.707));
        shimLp.setCoeffs (BiquadCoeffs::lowpass (fs, std::min (7000.0, 0.45 * fs), 0.707));
        shimHp.setCoeffs (BiquadCoeffs::highpass (fs, 300.0, 0.707));
        mud.setCoeffs (BiquadCoeffs::peaking (fs, 350.0, 0.9, -3.0));
        for (int c = 0; c < 2; ++c)
        {
            lc[c].setCoeffs (BiquadCoeffs::highpass (fs, ch.lowCut, 0.707));
            hc[c].setCoeffs (BiquadCoeffs::lowpass (fs, std::min ((double) ch.highCut, 0.45 * fs), 0.707));
        }
        sibAtk = coefFor (0.001, fs); sibRel = coefFor (0.060, fs);
        gainSmooth = coefFor (0.015, fs);
    }

    double fs = 48000.0;
    Character ch = character (smallPlate);
    uint32_t seed = 1;
    ModDelay lines[kLines], preDelay, erLine, shimBuf;
    Allpass loopAp[kLines], inAp[4], outAp[4];
    float lineLen[kLines] {}, absA[kLines] {}, absB[kLines] {}, lpState[kLines] {}, lfoMul[kLines] {}, inSign[kLines] {}, sideSign[kLines] {};
    float outL[kLines] {}, outR[kLines] {};
    double lfo[kLines] {};
    float outGain = 1.0f, shimWin = 2880.0f, shimPhase = 0.0f;
    int erTap[kTaps] {};
    float erAmp[kTaps] {};
    Biquad inHp, sibHp, erLp, shimLp, shimHp, mud, lc[2], hc[2];
    float sibAtk = 0, sibRel = 0, gainSmooth = 0, sibEnvH = 0, sibEnvF = 0, sibCut = 0;
    Smooth send;
    Ducker duck;
    float tail = 0.0f;
    bool wasIdle = true;
    std::atomic<float> tailMeter { 0.0f };
};
} // namespace onirique::mini
