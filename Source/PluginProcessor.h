#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>

#include "License/LicenseGuard.h"
#include "Parameters.h"
#include "DSP/MiniChain.h"
#include "DSP/SpectrumTap.h"
#include "StockPresets.h"

class OniriqueVocalMiniAudioProcessor : public juce::AudioProcessor,
                                        private juce::AudioProcessorValueTreeState::Listener,
                                        private juce::AsyncUpdater
{
public:
    OniriqueVocalMiniAudioProcessor();
    ~OniriqueVocalMiniAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    // Le bypass du DAW pilote directement le paramètre "Bypass" (fondu propre + latence alignée)
    juce::AudioProcessorParameter* getBypassParameter() const override { return apvts.getParameter (onirique::ids::bypass); }

    const juce::String getName() const override { return "ONIRIQUE VOCAL MINI"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 12.0; }   // DECAY de la reverb (jusqu'à 12 s) et répétitions du delay

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    /** Les presets d'usine et ceux de l'utilisateur, la comparaison A/B : ce que montre la barre du haut. */
    onirique::PresetBank presets { apvts, "VocalMini", onirique::vocalmini::stockPresets(), onirique::vocalmini::tagNames(),
                                   onirique::vocalmini::notStored() };

    // lus par l'interface
    onirique::mini::MiniChain& getChain() noexcept { return chain; }
    onirique::SpectrumTap outputTap;   // la voix qui sort (mono), pour l'orbe de l'interface
    double getBpm() const noexcept { return bpmNow.load (std::memory_order_relaxed); }
    bool isLive() const noexcept { return liveNow.load (std::memory_order_relaxed); }   // mode qui tourne

    /** Licence ONIRIQUE (acheté, essai, démo) : voir License/LicenseGuard.h. */
    onirique::license::Guard license { "onirique-vocal-mini" };

private:
    onirique::mini::MiniChain::Params readParams() const noexcept;

    std::array<std::atomic<float>*, onirique::prm::count> raw {};   // valeurs réelles de tous les paramètres (table de Parameters.h)

    // LIVE / MIX change la latence : la chaîne est reconfigurée sur le thread de l'interface (audio suspendu le temps
    // du changement), puis la nouvelle latence est annoncée au DAW
    void parameterChanged (const juce::String&, float) override { triggerAsyncUpdate(); }
    void handleAsyncUpdate() override;
    void configure();
    double currentRate = 0.0;

    onirique::mini::MiniChain chain;
    std::atomic<double> bpmNow { 120.0 };
    double ppqNow = -1.0;   // thread audio seulement
    std::atomic<bool> liveNow { true };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OniriqueVocalMiniAudioProcessor)
};
