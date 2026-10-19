#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>

#include "License/LicenseGuard.h"
#include "Parameters.h"
#include "DSP/MiniChain.h"
#include "DSP/SpectrumTap.h"
#include "PresetManager.h"

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
    double getTailLengthSeconds() const override { return 8.0; }   // queue de LUSH et répétitions du delay

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    onirique::PresetManager presets { apvts };

    // lus par l'interface
    onirique::mini::MiniChain& getChain() noexcept { return chain; }
    onirique::SpectrumTap outputTap;   // la voix qui sort (mono), pour l'orbe de l'interface
    double getBpm() const noexcept { return bpmNow.load (std::memory_order_relaxed); }
    bool isLive() const noexcept { return liveNow.load (std::memory_order_relaxed); }   // mode qui tourne

    /** Licence ONIRIQUE (acheté, essai, démo) : voir License/LicenseGuard.h. */
    onirique::license::Guard license { "onirique-vocal-mini" };

private:
    onirique::mini::MiniChain::Params readParams() const noexcept;

    struct Raw
    {
        std::atomic<float> *in = nullptr, *out = nullptr, *autoIn = nullptr, *lowCut = nullptr, *low = nullptr, *mid = nullptr,
                           *hi = nullptr, *air = nullptr, *comp = nullptr, *multi = nullptr, *deess = nullptr, *drive = nullptr,
                           *phone = nullptr, *doubler = nullptr, *chorus = nullptr, *delay = nullptr, *feed = nullptr,
                           *filter = nullptr, *time = nullptr, *ping = nullptr, *rev[4] {}, *bypass = nullptr, *mode = nullptr;
    } raw;

    // LIVE / MIX change la latence : la chaîne est reconfigurée sur le thread de l'interface (audio suspendu le temps
    // du changement), puis la nouvelle latence est annoncée au DAW
    void parameterChanged (const juce::String&, float) override { triggerAsyncUpdate(); }
    void handleAsyncUpdate() override;
    void configure();
    double currentRate = 0.0;

    onirique::mini::MiniChain chain;
    std::atomic<double> bpmNow { 120.0 };
    std::atomic<bool> liveNow { true };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OniriqueVocalMiniAudioProcessor)
};
