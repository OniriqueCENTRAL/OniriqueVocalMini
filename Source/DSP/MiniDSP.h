#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <vector>
#include "Biquad.h"
#include "Common.h"

namespace onirique::mini
{
// coefficient d'un lissage exponentiel (une constante de temps en secondes)
inline float coefFor (double seconds, double fs) noexcept { return (float) (1.0 - std::exp (-1.0 / (std::max (1.0e-6, seconds) * fs))); }

// tampon circulaire, lecture fractionnaire (Hermite 4 points)
class ModDelay
{
public:
    void prepare (int maxSamples)
    {
        int s = 1;
        while (s < maxSamples + 8) s <<= 1;
        buf.assign ((size_t) s, 0.0f);
        mask = s - 1;
        w = 0;
    }
    void reset() noexcept { std::fill (buf.begin(), buf.end(), 0.0f); w = 0; }
    void push (float x) noexcept { buf[(size_t) w] = x; w = (w + 1) & mask; }
    // valeur écrite il y a d échantillons (d >= 2)
    float read (float d) const noexcept
    {
        d = juce::jlimit (2.0f, (float) (mask - 4), d);
        const float pos = (float) (w - 1) - d;
        const float fl = std::floor (pos);
        const int i = (int) fl;
        const float f = pos - fl;
        const float y0 = at (i - 1), y1 = at (i), y2 = at (i + 1), y3 = at (i + 2);
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + y1;
    }
    float tap (int d) const noexcept { return at (w - 1 - d); }

private:
    float at (int i) const noexcept { return buf[(size_t) (i & mask)]; }
    std::vector<float> buf;
    int mask = 0, w = 0;
};

// passe-tout de Schroeder à retard entier (diffusion)
class Allpass
{
public:
    void prepare (int lengthSamples) { len = std::max (1, lengthSamples); buf.assign ((size_t) len, 0.0f); pos = 0; }
    void reset() noexcept { std::fill (buf.begin(), buf.end(), 0.0f); pos = 0; }
    float process (float x, float g) noexcept
    {
        const float d = buf[(size_t) pos];
        const float v = x + g * d;
        buf[(size_t) pos] = v;
        if (++pos >= len) pos = 0;
        return d - g * v;
    }
    int length() const noexcept { return len; }

private:
    std::vector<float> buf;
    int len = 1, pos = 0;
};

/*  Niveau de sortie ramené au niveau d'entrée (puissance moyenne lente), pour qu'un effet s'entende sans changer le
    volume. Le niveau mesuré est celui de la sortie déjà corrigée : la correction s'ajuste jusqu'à ce que les deux
    moyennes soient égales. Gelée dans les silences (sous -70 dBFS) : rien ne remonte le souffle entre les phrases. */
class LevelMatch
{
public:
    void prepare (double fs, double seconds = 1.0, double glide = 0.6)
    {
        c = coefFor (seconds, fs);
        step = (float) (32.0 / (glide * fs));
        pin = pout = 0.0f; g = 1.0f; count = 0;
    }
    void reset() noexcept { pin = pout = 0.0f; g = 1.0f; count = 0; }
    // inPow / outPow : puissances instantanées (moyenne des canaux) ; renvoie le gain à appliquer à la sortie
    float next (float inPow, float outPow, float lo = 0.1f, float hi = 4.0f) noexcept
    {
        if (inPow > 1.0e-7f || pin > 1.0e-7f)
        {
            pin += c * (inPow - pin);
            pout += c * (outPow * g * g - pout);
        }
        if (++count >= 32)
        {
            count = 0;
            if (pin > 1.0e-7f && pout > 1.0e-10f)
                g = juce::jlimit (lo, hi, g * std::pow (pin / pout, 0.5f * step));
        }
        return g;
    }
    float gain() const noexcept { return g; }

private:
    float c = 0.001f, step = 0.001f, pin = 0.0f, pout = 0.0f, g = 1.0f;
    int count = 0;
};

// l'effet s'efface sous la voix : réduction selon le niveau de la voix au-dessus de -40 dBFS (jusqu'à -24 dB)
class Ducker
{
public:
    void prepare (double fs) { atk = coefFor (0.010, fs); rel = coefFor (0.300, fs); gs = coefFor (0.020, fs); env = 0.0f; g = 1.0f; }
    void reset() noexcept { env = 0.0f; g = 1.0f; }
    float next (float key, float depth) noexcept
    {
        const float a = std::abs (key);
        env += (a > env ? atk : rel) * (a - env);
        float target = 1.0f;
        if (depth > 0.001f)
        {
            const float over = gainToDb (env + 1.0e-9f) + 40.0f;
            target = dbToGain (-std::min (24.0f * depth, 0.6f * std::max (0.0f, over)));
        }
        g += gs * (target - g);
        return g;
    }

private:
    float atk = 0.01f, rel = 0.001f, gs = 0.01f, env = 0.0f, g = 1.0f;
};

// valeur lissée linéairement, prête à l'emploi (0..1 en général)
struct Smooth
{
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> v;
    void prepare (double fs, double seconds, float initial) { v.reset (fs, seconds); v.setCurrentAndTargetValue (initial); }
    void set (float x, bool snap) noexcept { if (snap) v.setCurrentAndTargetValue (x); else v.setTargetValue (x); }
    float next() noexcept { return v.getNextValue(); }
    float current() const noexcept { return v.getCurrentValue(); }
    float target() const noexcept { return v.getTargetValue(); }
    bool moving() const noexcept { return v.isSmoothing(); }
    // à zéro et immobile : l'effet ne coûte rien
    bool idle() const noexcept { return ! v.isSmoothing() && v.getTargetValue() <= 0.0f; }
};

// petit générateur pseudo-aléatoire (temps réel, sans allocation) et marche aléatoire lissée
struct Noise
{
    uint32_t s = 0x9e3779b9u;
    float next() noexcept { s = s * 1664525u + 1013904223u; return (float) (int32_t) s / 2147483648.0f; }   // -1..1
};

// mouvement lent et irrégulier (-1..1) : bruit filtré deux fois, recalé en amplitude
class Drift
{
public:
    void prepare (double fs, float hz, uint32_t seed)
    {
        noise.s = seed;
        k = coefFor (1.0 / (6.2831853 * std::max (0.01f, hz)), fs);
        a = b = 0.0f;
    }
    float next() noexcept
    {
        a += k * (noise.next() * 3.2f - a);
        b += k * (a - b);
        return juce::jlimit (-1.0f, 1.0f, b * 2.2f);
    }

private:
    Noise noise;
    float k = 0.001f, a = 0.0f, b = 0.0f;
};

// sigmoïde algébrique (coude progressif, harmoniques douces)
inline float asig (float z) noexcept { return z / std::sqrt (1.0f + z * z); }

// courbe d'un bouton : progressive au début, pleine à la fin
inline float taper (float x, float p) noexcept { return std::pow (std::max (x, 0.0f), p); }
} // namespace onirique::mini
