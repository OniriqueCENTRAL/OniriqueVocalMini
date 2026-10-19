#pragma once

#include "MiniDSP.h"

namespace onirique::mini
{
// valeurs de note du DELAY, en noires
inline constexpr int kNumDivisions = 13;
inline const char* const kDivisionNames[kNumDivisions] = { "1/32", "1/16T", "1/16", "1/16D", "1/8T", "1/8", "1/8D",
                                                          "1/4T", "1/4", "1/4D", "1/2", "1/2D", "1/1" };
inline constexpr float kDivisionBeats[kNumDivisions] = { 0.125f, 1.0f / 6.0f, 0.25f, 0.375f, 1.0f / 3.0f, 0.5f, 0.75f,
                                                         2.0f / 3.0f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f };

/*  DELAY : un écho de voix prêt à l'emploi, au tempo du projet. C'est une piste auxiliaire : la voix reste entière,
    le bouton règle ce qui part dans l'écho (comme un envoi), les répétitions déjà parties finissent leur course.

    - FEED : réinjection (0 à 92 %).
    - FILTER : -100 = répétitions sombres (coupe-haut jusqu'à 1.5 kHz), 0 = écho chaud de studio (150 Hz - 9 kHz),
      +100 = répétitions fines et lointaines (coupe-bas jusqu'à 1.5 kHz). Les filtres sont dans la boucle : chaque
      répétition passe une fois de plus dedans et s'éloigne un peu plus.
    - Boucle façon bande : chaque passage arrondit légèrement les crêtes, et la vitesse flotte à peine (± 0.15 ms) :
      les répétitions vivent au lieu de se répéter à l'identique.
    - PING-PONG : les répétitions passent d'un côté à l'autre.
    - DUCKING intégré : l'écho se retire sous la voix et revient dans les silences.
    - Changement de temps glissé (250 ms) : aucun clic quand le tempo ou la note change. */
class VoiceDelay
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        for (auto& d : line) d.prepare ((int) (4.2 * fs));
        timeS.reset (fs, 0.25);
        timeS.setCurrentAndTargetValue (timeSamples());
        send.prepare (fs, 0.05, send.target());
        wow.prepare (fs, 0.6f, 0xa5a5a5u);
        duck.prepare (fs);
        lastTone = 99.0f;
        reset();
    }

    void reset() noexcept
    {
        for (auto& d : line) d.reset();
        for (int c = 0; c < 2; ++c) { hp[c].reset(); lp[c].reset(); }
        duck.reset();
        tail = 0.0f;
    }

    void set (float amount01, float feed01, float tone, int division, bool pingPong, double bpm, bool snap) noexcept
    {
        send.set (std::pow (juce::jlimit (0.0f, 1.0f, amount01), 1.4f) * 0.9f, snap);
        fb = 0.92f * std::pow (juce::jlimit (0.0f, 1.0f, feed01), 0.9f);
        ping = pingPong;
        div = juce::jlimit (0, kNumDivisions - 1, division);
        tempo = juce::jlimit (30.0, 300.0, bpm);
        if (snap) timeS.setCurrentAndTargetValue (timeSamples()); else timeS.setTargetValue (timeSamples());
        tone = juce::jlimit (-1.0f, 1.0f, tone);
        if (tone != lastTone) updateFilters (tone);
    }

    // niveau des répétitions (pour l'interface)
    float getTail() const noexcept { return tailMeter.load (std::memory_order_relaxed); }
    bool isActive() const noexcept { return ! send.idle() || tail > 1.0e-5f; }

    // la voix (in) entre dans l'écho ; son retour s'ajoute dans wet
    void process (const float* const* in, float* const* wet, int numCh, int n) noexcept
    {
        if (! isActive()) { if (! wasIdle) { reset(); wasIdle = true; } tailMeter.store (0.0f, std::memory_order_relaxed); return; }
        wasIdle = false;
        float pk = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float xl = in[0][i], xr = numCh > 1 ? in[1][i] : xl;
            const float s = send.next();
            const float d = timeS.getNextValue() + 0.00015f * (float) fs * wow.next();
            float r[2] { line[0].read (d), line[1].read (d) };
            for (int c = 0; c < 2; ++c) r[c] = lp[c].process (hp[c].process (r[c], 0), 0);
            auto tape = [] (float v) { return v / (1.0f + 0.25f * std::abs (v)); };
            if (ping)
            {
                line[0].push (0.5f * (xl + xr) * s + fb * tape (r[1]));
                line[1].push (fb * tape (r[0]));
            }
            else
            {
                line[0].push (xl * s + fb * tape (r[0]));
                line[1].push (xr * s + fb * tape (r[1]));
            }
            const float g = duck.next (0.5f * (xl + xr), 0.35f);
            if (numCh > 1) { wet[0][i] += r[0] * g; wet[1][i] += r[1] * g; }
            else wet[0][i] += 0.5f * (r[0] + r[1]) * g;
            pk = std::max (pk, std::abs (r[0]) + std::abs (r[1]));
        }
        tail = pk;
        tailMeter.store (pk, std::memory_order_relaxed);
    }

private:
    float timeSamples() const noexcept
    {
        const double sec = kDivisionBeats[div] * 60.0 / tempo;
        return (float) juce::jlimit (0.005 * fs, 4.0 * fs, sec * fs);
    }

    void updateFilters (float tone) noexcept
    {
        lastTone = tone;
        const double lpHz = tone < 0.0f ? 9000.0 * std::pow (1500.0 / 9000.0, (double) -tone) : 9000.0;
        const double hpHz = tone > 0.0f ? 150.0 * std::pow (10.0, (double) tone) : 150.0 * std::pow (0.5, (double) -tone);
        for (int c = 0; c < 2; ++c)
        {
            hp[c].setCoeffs (BiquadCoeffs::highpass (fs, hpHz, 0.707));
            lp[c].setCoeffs (BiquadCoeffs::lowpass (fs, std::min (lpHz, 0.45 * fs), 0.707));
        }
    }

    double fs = 48000.0, tempo = 120.0;
    int div = 5;
    float fb = 0.3f, lastTone = 99.0f, tail = 0.0f;
    bool ping = true, wasIdle = true;
    Smooth send;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> timeS { 24000.0f };
    ModDelay line[2];
    Biquad hp[2], lp[2];
    Drift wow;
    Ducker duck;
    std::atomic<float> tailMeter { 0.0f };
};
} // namespace onirique::mini
