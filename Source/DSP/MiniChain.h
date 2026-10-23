#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include "MiniDSP.h"
#include "DePlosive.h"
#include "NoiseSuppressor.h"
#include "VoiceGate.h"
#include "VoiceEQ.h"
#include "SmartDeEsser.h"
#include "VoiceComp.h"
#include "VoiceMultiband.h"
#include "VoiceParametric.h"
#include "SpectrumTap.h"
#include "VoiceColor.h"
#include "VoiceWide.h"
#include "VoiceDelay.h"
#include "VoiceReverb.h"

namespace onirique::mini
{
/*  AUTO INPUT : le gain d'entrée se règle tout seul, comme un ingé qui cale son préampli au début de la prise.
    Le niveau de la voix (moyenne en dB des passages chantés, mesurés par fenêtres de 50 ms, graves sous 80 Hz ignorés,
    seuls les passages à moins de 15 dB du plus fort récent comptent) est ramené dans la zone BEST (-18 dBFS RMS).
    - CALAGE RAPIDE : pendant la première seconde et demie de chant, le gain suit directement : la voix est dans la
      zone presque tout de suite.
    - PUIS IMMOBILE : le niveau habituel n'évolue plus que lentement (moyenne sur ~5 s de chant) et le gain ne bouge
      que si la voix a VRAIMENT changé de niveau (plus de 2.5 dB d'écart durable : un autre couplet, un autre
      chanteur, le micro déplacé). Pendant le chant, rien ne pompe : les nuances du chanteur restent les siennes.
    - Un changement, quand il a lieu, glisse en 0.6 s. Bornes +/-18 dB ; tout est gelé dans les silences : le souffle
      entre les phrases ne remonte jamais. Un changement de preset ne touche pas à AUTO INPUT (aucun saut). */
class AutoInput
{
public:
    static constexpr float kTargetDb = -13.5f;   // passages forts (à 15 dB du plus fort) : la voix chantée autour de -18 dBFS RMS

    void prepare (double fs)
    {
        hp.setCoeffs (BiquadCoeffs::highpass (fs, 80.0, 0.707));
        hp.reset();
        powC = coefFor (0.05, fs);
        tick = kTick / fs;                                     // la mesure avance par pas de kTick échantillons
        fastC = (float) (1.0 - std::exp (-tick / 0.25));
        slowC = (float) (1.0 - std::exp (-tick / 5.0));
        maxFall = (float) (6.0 * tick);                         // le niveau le plus fort récent redescend de 6 dB/s
        gainC = coefFor (0.6 / 3.0, fs);                        // un changement glisse en ~0.6 s
        pow = 0.0f; levelDb = kTargetDb; recentMax = -100.0f; gainDb = 0.0f; command = 0.0f; voiced = 0.0; count = 0;
    }

    // mesure du signal d'entrée (après le gain IN) ; renvoie le gain à appliquer (linéaire)
    float next (float l, float r, bool on) noexcept
    {
        const float x = hp.process (0.5f * (l + r), 0);
        pow += powC * (x * x - pow);
        if (++count >= kTick)
        {
            count = 0;
            const float lvl = 10.0f * std::log10 (pow + 1.0e-12f);   // RMS de 50 ms, en dBFS
            recentMax = std::max (lvl, recentMax - maxFall);
            if (lvl > -65.0f && lvl > recentMax - 15.0f)             // un passage chanté
            {
                if (voiced <= 0.0) levelDb = lvl;
                const bool settling = voiced < 1.5;
                levelDb += (settling ? fastC : slowC) * (lvl - levelDb);
                voiced += tick;
                const float want = juce::jlimit (-18.0f, 18.0f, kTargetDb - levelDb);
                if (settling || std::abs (want - command) > 2.5f) command = want;
            }
        }
        gainDb += gainC * ((on ? command : 0.0f) - gainDb);
        return dbToGain (gainDb);
    }
    float getGainDb() const noexcept { return gainDb; }

private:
    static constexpr int kTick = 32;
    Biquad hp;
    float powC = 0, fastC = 0, slowC = 0, maxFall = 0, gainC = 0;
    float pow = 0, levelDb = kTargetDb, recentMax = -100.0f, gainDb = 0, command = 0;
    double tick = 0.0, voiced = 0.0;
    int count = 0;
};

/*  La chaîne complète de VOCAL MINI PRO (chemin du signal) :

      IN + AUTO INPUT
      -> DE-PLOSIVE         les chocs de grave (P, B) retirés d'abord : ils n'ouvrent pas le gate, ne font pas pomper le COMP
      -> NOISE SUPPRESSOR   bruit stationnaire retiré tôt, tant qu'il est encore stable (avant que la compression
                            ne le fasse bouger et ne rende ses artefacts audibles)
      -> NOISE GATE         silences nettoyés AVANT la compression : le rattrapage du COMP ne peut plus remonter le souffle
      -> [x2 en MIX] LOW CUT, LOW, MID   le corps de la voix se règle avant la compression, qui l'égalise ensuite
      -> COMP               niveleur (LA-2A) puis crêtes (1176) puis garde, un seul bouton
      -> MULTIBAND          ce que la compression large laisse passer, bande par bande
      -> PARAMETRIC         l'EQ paramétrique (8 bandes) : après les compresseurs, la courbe dessinée est celle qu'on
                            entend (le MULTIBAND ne reprend pas ce qu'on pousse ou creuse) ; son mode dynamique trouve
                            une voix déjà tenue
      -> DRIVE              harmoniques sur une voix déjà tenue (saturation régulière, sans écarts de niveau)
      -> HI, AIR            les aigus APRÈS la compression : un AIR poussé ne fait jamais comprimer la voix
      -> DE-ESS             après tout ce qui ajoute des aigus (compression, HI, AIR, DRIVE) : il voit les S finaux
      -> PHONE
      -> DOUBLER, CHORUS    la largeur sur la voix finie
      -> DELAY, REVERB      pistes auxiliaires : la voix reste entière, les retours s'ajoutent (la reverb reçoit aussi
                            un peu des répétitions) ; le gate est en amont, il ne coupe jamais une queue d'effet
      -> OUT, garde-fou (transparent jusqu'à -1 dBFS, n'agit que sur les dépassements), BYPASS en fondu de 20 ms.

    Deux modes :
      LIVE (par défaut) : latence nulle, pour enregistrer en s'écoutant. Tout tourne à la cadence du projet ; le DRIVE
        sature à bande divisée, le PHONE écrête une voix déjà bornée, le grain de l'AIR est préfiltré ; le NOISE
        SUPPRESSOR agit par 24 cloches IIR, le gate et le de-plosive sans anticipation.
      MIX : la qualité maximale une fois l'enregistrement fini. Le coeur de la voix tourne suréchantillonné x2 (filtres
        demi-bande à phase linéaire) ; le de-esser anticipe les S de 0.6 ms, le de-plosive les P de 2 ms, le gate
        s'ouvre 1.5 ms avant la voix, le NOISE SUPPRESSOR travaille bin par bin (STFT de ~21 ms). Latence
        ~27 ms, annoncée au DAW (compensée automatiquement), constante : allumer ou couper un module ne la change
        jamais.
    Garanties temps réel : aucune allocation dans process(), blocs découpés en tranches de 512 échantillons, NaN/Inf
    neutralisés à l'entrée et à la sortie. BYPASS en fondu de 20 ms, aligné sur la latence. */
class MiniChain
{
public:
    static constexpr int kChunk = 512;
    static constexpr int kHistory = 256;   // énergie de la voix pour l'interface (une valeur par tranche de 256)

    struct Params
    {
        float inDb = 0.0f, outDb = 0.0f;
        bool autoInput = true, lowCut = true, bypass = false;
        bool plosiveOn = false, nsOn = false, gateOn = false;
        float plosiveAmount = 0.5f, nsAmount = 0.5f;                          // 0..1
        float gateThrDb = -50.0f, gateRangeDb = 18.0f, gateReleaseMs = 200.0f;
        float low = 0.0f, mid = 0.0f, hi = 0.0f, air = 0.0f;                  // -1..1
        float comp = 0.0f, multi = 0.0f, deess = 0.0f, drive = 0.0f;          // 0..1
        bool compAutoGain = true;
        std::array<VoiceMultiband::Band, VoiceMultiband::kBands> bands { VoiceMultiband::defaultBand (0), VoiceMultiband::defaultBand (1),
                                                                         VoiceMultiband::defaultBand (2), VoiceMultiband::defaultBand (3) };
        float phone = 0.0f, doubler = 0.0f, chorus = 0.0f;                    // 0..1
        float delay = 0.0f, feed = 0.35f, filter = 0.0f;                      // 0..1, 0..1, -1..1
        int division = 5;
        bool ping = true;
        VoiceReverb::Params reverb { [] { VoiceReverb::Params r; r.send = 0.0f; return r; }() };
        bool peqOn = true;
        std::array<VoiceParametric::Band, VoiceParametric::kBands> peq { []
        {
            std::array<VoiceParametric::Band, VoiceParametric::kBands> a;
            for (int b = 0; b < VoiceParametric::kBands; ++b) a[(size_t) b] = VoiceParametric::defaultBand (b);
            return a;
        }() };
        double bpm = 120.0, ppq = -1.0;                                       // ppq : position en noires (< 0 : arrêt)
    };

    // live : latence nulle (enregistrement) ; sinon MIX, le coeur suréchantillonné x2 et les modules qui anticipent
    void prepare (double sampleRate, const Params& p, bool live)
    {
        fs = sampleRate;
        isLive = live;
        osFactor = live ? 1 : 2;
        os.reset();
        if (! live)
        {
            os = std::make_unique<juce::dsp::Oversampling<float>> (2, 1, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, false, true);
            os->initProcessing ((size_t) kChunk);
        }
        const double fs2 = fs * osFactor;

        deplosive.prepare (fs, live);
        suppressor.prepare (fs, live);
        gate.prepare (fs, live);
        body.prepare (fs2);
        comp.prepare (fs2);
        multi.prepare (fs2);
        peq.prepare (fs2);
        warmth.prepare (fs2);
        shine.prepare (fs2);
        deess.prepare (fs2, live ? 0 : (int) std::lround (0.0006 * fs) * osFactor);   // MIX : 0.6 ms d'anticipation
        phone.prepare (fs2);
        doubler.prepare (fs);
        chorus.prepare (fs);
        delay.prepare (fs);
        reverb.prepare (fs);
        autoIn.prepare (fs);
        for (auto& s : scratch) s.assign ((size_t) kChunk * 2, 0.0f);
        for (auto& w : wetBuf) w.assign ((size_t) kChunk, 0.0f);
        for (auto& w : revIn) w.assign ((size_t) kChunk, 0.0f);

        // latence (cadence de l'hôte) : de-plosive + débruiteur + gate (anticipations) + suréchantillonneur + de-esser
        latency = live ? 0 : deplosive.getLatencySamples() + suppressor.getLatencySamples() + gate.getLatencySamples()
                                 + juce::roundToInt (os->getLatencyInSamples()) + deess.getLatencySamples() / osFactor;
        for (auto& d : dryDelay) d.prepare (latency);
        inGain.reset (fs, 0.03); outGain.reset (fs, 0.03); wetMix.reset (fs, 0.02);
        listenMix.reset (fs, 0.03);
        listenMix.setCurrentAndTargetValue (0.0f);
        listenA.reset(); listenB.reset();
        listenType = -1;
        rmsC = coefFor (0.3, fs);
        inRms = 0.0f;
        setParams (p, true);
    }

    int getLatencySamples() const noexcept { return latency; }

    void setParams (const Params& p, bool snap = false) noexcept
    {
        const float gi = dbToGain (juce::jlimit (-24.0f, 24.0f, p.inDb)), go = dbToGain (juce::jlimit (-24.0f, 12.0f, p.outDb));
        const float wm = p.bypass ? 0.0f : 1.0f;
        if (snap) { inGain.setCurrentAndTargetValue (gi); outGain.setCurrentAndTargetValue (go); wetMix.setCurrentAndTargetValue (wm); }
        else      { inGain.setTargetValue (gi); outGain.setTargetValue (go); wetMix.setTargetValue (wm); }
        autoOn = p.autoInput;
        deplosive.set (p.plosiveOn, p.plosiveAmount);
        suppressor.setAmount (p.nsOn ? p.nsAmount : 0.0f);
        gate.set (p.gateOn, p.gateThrDb, p.gateRangeDb, p.gateReleaseMs);
        body.set (p.low, p.mid, p.lowCut, snap);
        comp.set (p.comp, p.compAutoGain, snap);
        multi.set (p.multi, p.bands, snap);
        peq.setBands (p.peq, p.peqOn, snap);
        peqBands = p.peq;
        warmth.set (p.drive, snap);
        shine.set (p.hi, p.air, snap);
        deess.set (p.deess, snap);
        phone.set (p.phone, snap);
        doubler.set (p.doubler, snap);
        chorus.set (p.chorus, snap);
        delay.set (p.delay, p.feed, p.filter, p.division, p.ping, p.bpm, snap);
        reverb.set (p.reverb, snap);
        bpm = juce::jlimit (20.0, 400.0, p.bpm);
        ppq = p.ppq;
    }

    void process (float* const* ch, int numCh, int n) noexcept
    {
        const double beatsPerSample = bpm / (60.0 * fs);
        for (int pos = 0; pos < n; pos += kChunk)
        {
            const int m = std::min (kChunk, n - pos);
            float* c[2] { ch[0] + pos, numCh > 1 ? ch[1] + pos : nullptr };
            processChunk (c, numCh, m, ppq >= 0.0 ? ppq + pos * beatsPerSample : -1.0);
        }
    }

    // ---- lus par l'interface
    float consumeInputPeak() noexcept  { return inPeak.exchange (0.0f, std::memory_order_relaxed); }
    float consumeOutputPeak() noexcept { return outPeak.exchange (0.0f, std::memory_order_relaxed); }
    float getInputRmsDb() const noexcept { return inRmsDb.load (std::memory_order_relaxed); }
    float getAutoInputDb() const noexcept { return autoDb.load (std::memory_order_relaxed); }
    float getCompReductionDb() const noexcept { return comp.getReductionDb(); }
    float getCompLevelDb() const noexcept { return comp.getLevelReductionDb(); }
    float getCompPeakDb() const noexcept { return comp.getPeakReductionDb(); }
    float getMultibandReductionDb (int b) const noexcept { return multi.getBandReductionDb (b); }
    float getMultibandLevelDb (int b) const noexcept { return multi.getBandLevelDb (b); }
    float getDeEssReductionDb() const noexcept { return deess.getReductionDb(); }
    float getDeEssFrequency() const noexcept { return deess.getFrequency(); }
    float getAirGuard() const noexcept { return shine.getGuard(); }
    float getDelayTail() const noexcept { return delay.getTail(); }
    float getReverbTail() const noexcept { return reverb.getTail(); }
    const VoiceReverb& getReverb() const noexcept { return reverb; }
    float getGateGainDb() const noexcept { return gate.getGainDb(); }
    float getGateLevelDb() const noexcept { return gate.getLevelDb(); }
    bool isGateOpen() const noexcept { return gate.isGateOpen(); }
    float getPlosiveReductionDb() const noexcept { return deplosive.getReductionDb(); }
    float getNoiseReductionDb() const noexcept { return suppressor.getReductionDb(); }
    float getNoiseFloorDb() const noexcept { return suppressor.getNoiseFloorDb(); }
    float getPeqBandGainDb (int b) const noexcept { return peq.getBandGainDb (b); }

    // EQ PARAMÉTRIQUE, pour son écran : la voix qui entre dans le coeur (après le nettoyage) et celle qui en sort
    // (prises remplies seulement quand l'écran est ouvert), et l'écoute d'une bande tenue à la souris (-1 : aucune)
    onirique::SpectrumTap& getEqInputTap() noexcept { return tapIn; }
    onirique::SpectrumTap& getEqOutputTap() noexcept { return tapOut; }
    void setAnalyzerActive (bool a) noexcept { tapsOn.store (a, std::memory_order_relaxed); }
    void setListenBand (int b) noexcept { listenBand.store (b, std::memory_order_relaxed); }
    int getListenBand() const noexcept { return listenBand.load (std::memory_order_relaxed); }
    int getHistoryPos() const noexcept { return histPos.load (std::memory_order_acquire); }
    float getHistory (int i) const noexcept { return history[(size_t) ((i % kHistory + kHistory) % kHistory)].load (std::memory_order_relaxed); }

private:
    void processChunk (float* const* ch, int numCh, int n, double chunkPpq) noexcept
    {
        // 1) entrée saine, copie sèche retardée de la latence (BYPASS aligné), IN + AUTO INPUT
        float pk = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            for (int c = 0; c < numCh; ++c)
            {
                const float x = sanitize (ch[c][i]);
                ch[c][i] = x;
                dry[c][i] = dryDelay[c].process (x);
            }
            const float gi = inGain.getNextValue();
            for (int c = 0; c < numCh; ++c) ch[c][i] *= gi;
            const float l = ch[0][i], r = numCh > 1 ? ch[1][i] : l;
            const float ga = autoIn.next (l, r, autoOn);
            inGainNow = gi * ga;
            for (int c = 0; c < numCh; ++c) { ch[c][i] *= ga; pk = std::max (pk, std::abs (ch[c][i])); }
            const float mono = numCh > 1 ? 0.5f * (ch[0][i] + ch[1][i]) : ch[0][i];
            inRms += rmsC * (mono * mono - inRms);
        }
        if (pk > inPeak.load (std::memory_order_relaxed)) inPeak.store (pk, std::memory_order_relaxed);
        inRmsDb.store (10.0f * std::log10 (2.0f * inRms + 1.0e-12f), std::memory_order_relaxed);
        autoDb.store (autoIn.getGainDb(), std::memory_order_relaxed);

        // 2) nettoyage : DE-PLOSIVE, NOISE SUPPRESSOR puis NOISE GATE (cadence de l'hôte)
        deplosive.process (ch, numCh, n);
        suppressor.process (ch, numCh, n);
        gate.process (ch, numCh, n);
        const bool taps = tapsOn.load (std::memory_order_relaxed);
        if (taps) tapIn.push (ch[0], numCh > 1 ? ch[1] : nullptr, n);

        // 3) le cœur de la voix (suréchantillonné x2 en MIX)
        float* io[2] { ch[0], numCh > 1 ? ch[1] : ch[0] };
        juce::dsp::AudioBlock<float> block (io, (size_t) numCh, (size_t) n);
        juce::dsp::AudioBlock<float> up = os != nullptr ? os->processSamplesUp (block) : block;
        const int n2 = n * osFactor;
        float* x[2] { up.getChannelPointer (0), numCh > 1 ? up.getChannelPointer (1) : nullptr };
        float* sc[2] { scratch[0].data(), scratch[1].data() };
        body.process (x, numCh, n2);
        comp.process (x, numCh, n2, sc);
        multi.process (x, numCh, n2);
        peq.process (x, numCh, n2);
        warmth.process (x, numCh, n2);
        shine.process (x, numCh, n2);
        deess.process (x, numCh, n2);
        phone.process (x, numCh, n2);
        if (os != nullptr) os->processSamplesDown (block);
        if (taps) tapOut.push (ch[0], numCh > 1 ? ch[1] : nullptr, n);

        // 4) largeur : DOUBLER puis CHORUS
        doubler.process (ch, numCh, n);
        chorus.process (ch, numCh, n);

        // 5) DELAY et REVERB en pistes auxiliaires : la voix reste entière, les retours s'ajoutent. La reverb reçoit
        //    aussi un peu des répétitions du delay : écho et espace se fondent dans la même pièce.
        float* wet[2] { wetBuf[0].data(), wetBuf[1].data() };
        float* rin[2] { revIn[0].data(), revIn[1].data() };
        const float* voice[2] { ch[0], numCh > 1 ? ch[1] : ch[0] };
        for (int c = 0; c < 2; ++c) std::fill (wet[c], wet[c] + n, 0.0f);
        delay.process (voice, wet, numCh, n);
        for (int c = 0; c < numCh; ++c)
            for (int i = 0; i < n; ++i) rin[c][i] = voice[c][i] + 0.4f * wet[c][i];
        const float* rv[2] { rin[0], numCh > 1 ? rin[1] : rin[0] };
        reverb.process (rv, wet, numCh, n, bpm, chunkPpq);

        // 6) OUT, garde-fou, BYPASS ; écoute d'une bande de l'EQ paramétrique (tenue à la souris) : on n'entend plus
        //    que la zone de cette bande, prise sur la voix d'entrée alignée (au niveau d'IN + AUTO INPUT), en fondu
        const int lb = listenBand.load (std::memory_order_relaxed);
        listenMix.setTargetValue (lb >= 0 && lb < VoiceParametric::kBands ? 1.0f : 0.0f);
        if (lb >= 0 && lb < VoiceParametric::kBands) updateListen (peqBands[(size_t) lb]);
        const bool listening = listenMix.isSmoothing() || listenMix.getTargetValue() > 0.0f;
        const float lg = listenGain * inGainNow;
        float opk = 0.0f, hist = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float go = outGain.getNextValue();
            const float wm = wetMix.getNextValue();
            float lm = listening ? listenMix.getNextValue() : 0.0f;
            lm = lm * lm * lm * (lm * (6.0f * lm - 15.0f) + 10.0f);   // fondu en S quintique
            if (listening) { listenA.tick(); listenB.tick(); }
            for (int c = 0; c < numCh; ++c)
            {
                const float y = safety ((ch[c][i] + wet[c][i]) * go);
                float o = dry[c][i] + wm * (y - dry[c][i]);
                if (listening)
                {
                    const float band = safety (lg * listenB.process (listenA.process (dry[c][i], c), c));
                    o += lm * (band - o);
                }
                ch[c][i] = sanitize (o);
                opk = std::max (opk, std::abs (ch[c][i]));
                hist += ch[c][i] * ch[c][i];
            }
        }
        if (! listening && listenType >= 0) { listenA.reset(); listenB.reset(); listenType = -1; }
        if (opk > outPeak.load (std::memory_order_relaxed)) outPeak.store (opk, std::memory_order_relaxed);

        // énergie de la voix pour l'interface (une valeur par tranche de 256 échantillons)
        histAcc += hist / (float) numCh;
        histCount += n;
        if (histCount >= 256)
        {
            const int w = histPos.load (std::memory_order_relaxed);
            history[(size_t) w].store (std::sqrt (histAcc / (float) histCount), std::memory_order_relaxed);
            histPos.store ((w + 1) % kHistory, std::memory_order_release);
            histAcc = 0.0f; histCount = 0;
        }
    }

    // garde-fou : transparent jusqu'à -1 dBFS, puis les crêtes s'arrondissent sans jamais dépasser 0 dBFS. Il ne sert
    // qu'aux dépassements accidentels (retours de DELAY et de REVERB qui s'additionnent à la voix, OUT poussé) : il n'est
    // pas là pour faire paraître la voix plus forte, une voix au niveau BEST ne l'atteint jamais.
    // écoute d'une bande : la zone qu'elle touche (cloche / notch : autour de sa fréquence ; shelf ou coupe de grave :
    // sous sa fréquence ; d'aigu : au-dessus). Les filtres glissent quand on tire la bande en l'écoutant.
    void updateListen (const VoiceParametric::Band& b) noexcept
    {
        if (b.freq == listenF && b.q == listenQ && b.type == listenType) return;
        const bool fresh = listenType < 0;
        listenF = b.freq; listenQ = b.q; listenType = b.type;
        using PE = VoiceParametric;
        const double f = juce::jlimit (25.0, 0.45 * fs, (double) b.freq);
        BiquadCoeffs c;
        if (b.type == PE::lowShelf || b.type == PE::lowCut)        { c = BiquadCoeffs::lowpass (fs, f, 0.707); listenGain = 1.0f; }
        else if (b.type == PE::highShelf || b.type == PE::highCut) { c = BiquadCoeffs::highpass (fs, f, 0.707); listenGain = 1.0f; }
        else { c = BiquadCoeffs::bandpass (fs, f, juce::jlimit (0.5, 12.0, (double) b.q)); listenGain = 1.0f; }   // à son vrai niveau
        if (fresh) { listenA.reset(); listenB.reset(); listenA.setNow (c); listenB.setNow (c); }
        else       { listenA.glideTo (c, 64); listenB.glideTo (c, 64); }
    }

    static float safety (float x) noexcept
    {
        constexpr float t = 0.891f;
        const float a = std::abs (x);
        if (a <= t) return x;
        const float y = std::min (0.9999f, t + (1.0f - t) * std::tanh ((a - t) / (1.0f - t)));
        return x < 0.0f ? -y : y;
    }

    double fs = 48000.0, bpm = 120.0, ppq = -1.0;
    int latency = 0, osFactor = 2;
    bool autoOn = true, isLive = true;
    std::unique_ptr<juce::dsp::Oversampling<float>> os;
    DePlosive deplosive;
    onirique::NoiseSuppressor suppressor;
    VoiceGate gate;
    BodyEQ body;
    VoiceComp comp;
    VoiceMultiband multi;
    VoiceParametric peq;
    std::array<VoiceParametric::Band, VoiceParametric::kBands> peqBands {};
    Warmth warmth;
    ShineEQ shine;
    SmartDeEsser deess;
    Phone phone;
    Doubler doubler;
    Chorus chorus;
    VoiceDelay delay;
    VoiceReverb reverb;
    AutoInput autoIn;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> inGain { 1.0f }, outGain { 1.0f };
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> wetMix;
    DelayLine dryDelay[2];
    float dry[2][kChunk] {};
    std::vector<float> scratch[2], wetBuf[2], revIn[2];
    float rmsC = 0.0f, inRms = 0.0f, histAcc = 0.0f;
    int histCount = 0;
    std::atomic<float> inPeak { 0.0f }, outPeak { 0.0f }, inRmsDb { -100.0f }, autoDb { 0.0f };
    std::array<std::atomic<float>, kHistory> history {};
    std::atomic<int> histPos { 0 };
    SpectrumTap tapIn, tapOut;
    std::atomic<bool> tapsOn { false };
    std::atomic<int> listenBand { -1 };
    SmoothBiquad listenA, listenB;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> listenMix;
    float listenF = -1.0f, listenQ = -1.0f, listenGain = 1.0f, inGainNow = 1.0f;
    int listenType = -1;
};
} // namespace onirique::mini
