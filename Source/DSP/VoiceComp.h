#pragma once

#include <atomic>
#include "MiniDSP.h"
#include "Comp1176.h"
#include "CompLA2A.h"

namespace onirique::mini
{
/*  COMP : la compression de voix en un seul bouton, deux étages en série comme sur une console de studio.

    1) FET (modèle de 1176, VOCALBOX) : attrape les crêtes, attaque moyenne-rapide, relâchement vif, 4:1. Le bouton
       « pousse » la voix dans l'étage (INPUT) et la sortie compense exactement la poussée (OUTPUT) : à faible
       réglage il n'attrape que les pics, à fond il tient toute la phrase avec l'attitude du FET.
    2) OPTO (modèle de LA-2A, VOCALBOX) : nivelle le corps de la voix en douceur, relâchement en deux temps (rapide sur
       un mot isolé, plus long sur une phrase tenue).
    3) Volume tenu : la sortie est ramenée au niveau d'entrée (moyenne lente, gelée dans les silences), avec un léger
       gain de densité quand on monte le bouton. On entend la voix se tenir et devenir dense, pas un saut de volume.
    À 0 le module est contourné (fondu de 30 ms) : rien ne colore la voix.
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
        match.prepare (fs, 1.2, 0.8);
        lastA = -1.0f;
        apply (amt.target(), true);
    }

    void reset() noexcept { fet.reset(); opto.reset(); match.reset(); }

    void set (float amount01, bool snap) noexcept
    {
        amt.set (juce::jlimit (0.0f, 1.0f, amount01), snap);
        if (snap) apply (amount01, true);
    }

    float getReductionDb() const noexcept { return grMeter.load (std::memory_order_relaxed); }

    void process (float* const* ch, int numCh, int n, float* const* scratch) noexcept
    {
        if (amt.idle()) { if (! wasIdle) { reset(); wasIdle = true; } grMeter.store (0.0f, std::memory_order_relaxed); return; }
        wasIdle = false;

        // réglages suivis par tranches de 32 échantillons (les étages lissent eux-mêmes leurs gains)
        for (int pos = 0; pos < n; pos += 32)
        {
            const int m = std::min (32, n - pos);
            const float a = amt.current();
            if (std::abs (a - lastA) > 1.0e-4f) apply (a, false);
            float* x[2] { ch[0] + pos, numCh > 1 ? ch[1] + pos : nullptr };
            float* d[2] { scratch[0], scratch[1] };
            for (int c = 0; c < numCh; ++c) std::copy (x[c], x[c] + m, d[c]);

            fet.process (x, numCh, m);
            opto.process (x, numCh, m);

            const float inv = 1.0f / (float) numCh;
            for (int i = 0; i < m; ++i)
            {
                const float w = amt.next();
                float pin = 0.0f, pout = 0.0f;
                for (int c = 0; c < numCh; ++c) { pin += d[c][i] * d[c][i]; pout += x[c][i] * x[c][i]; }
                const float g = match.next (pin * inv, pout * inv, 0.5f, 8.0f) * (1.0f + 0.16f * w);
                const float mix = std::min (1.0f, w * 20.0f);   // de 0 à 5 % : on entre en fondu dans le module
                for (int c = 0; c < numCh; ++c) x[c][i] = d[c][i] + mix * (x[c][i] * g - d[c][i]);
            }
        }
        grMeter.store (fet.getGainReductionDb() + opto.getGainReductionDb(), std::memory_order_relaxed);
    }

private:
    void apply (float a, bool snap) noexcept
    {
        lastA = a;
        const float push = -14.0f + 22.0f * std::pow (a, 1.1f);
        fet.setParams (push, -push, 3.6f, 5.4f, 0, snap);
        opto.setParams (0.0f, 52.0f * std::pow (a, 0.95f), false, snap);
    }

    double fs = 48000.0;
    FetCompressor fet;
    OptoCompressor opto;
    Smooth amt;
    LevelMatch match;
    float lastA = -1.0f;
    bool wasIdle = true;
    std::atomic<float> grMeter { 0.0f };
};
} // namespace onirique::mini
