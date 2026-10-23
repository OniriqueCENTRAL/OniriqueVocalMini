#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace onirique;

OniriqueVocalMiniAudioProcessor::OniriqueVocalMiniAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", prm::createLayout())
{
    for (int i = 0; i < prm::count; ++i) raw[(size_t) i] = apvts.getRawParameterValue (prm::spec (i).id);
    apvts.addParameterListener (ids::mode, this);

    // l'état de départ est le preset « Lead Clean Vocal » (les valeurs par défaut des paramètres)
    apvts.state.setProperty ("presetName", "Lead Clean Vocal", nullptr);
    presets.takeSnapshot();

   #if ONIRIQUE_UI_TEST
    // essais locaux uniquement : ONIRIQUE_UI_TEST=hook,... charge un preset plus parlant pour le visuel de la boutique
    if (juce::SystemStats::getEnvironmentVariable ("ONIRIQUE_UI_TEST", {}).contains ("hook"))
        for (const auto& e : presets.list())
            if (e.name == "Wide Atmospheric Vocal") presets.load (e);
   #endif
}

OniriqueVocalMiniAudioProcessor::~OniriqueVocalMiniAudioProcessor()
{
    apvts.removeParameterListener (ids::mode, this);
    cancelPendingUpdate();
}

mini::MiniChain::Params OniriqueVocalMiniAudioProcessor::readParams() const noexcept
{
    prm::Values v;
    for (int i = 0; i < prm::count; ++i) v[(size_t) i] = raw[(size_t) i]->load (std::memory_order_relaxed);
    auto p = prm::toChain (v);
    p.bpm = bpmNow.load (std::memory_order_relaxed);
    p.ppq = ppqNow;
    return p;
}

bool OniriqueVocalMiniAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    const auto& in = layouts.getMainInputChannelSet();
    return in == out || (in == juce::AudioChannelSet::mono() && out == juce::AudioChannelSet::stereo());
}

void OniriqueVocalMiniAudioProcessor::configure()
{
    const bool live = juce::roundToInt (raw[prm::mode]->load()) == 0;
    chain.prepare (currentRate, readParams(), live);
    liveNow.store (live);
    setLatencySamples (chain.getLatencySamples());
}

void OniriqueVocalMiniAudioProcessor::handleAsyncUpdate()
{
    const bool live = juce::roundToInt (raw[prm::mode]->load()) == 0;
    if (currentRate <= 0.0 || live == liveNow.load()) { if (currentRate <= 0.0) liveNow.store (live); return; }
    suspendProcessing (true);    // le thread audio attend la fin de la reconfiguration (allocation hors temps réel)
    configure();
    suspendProcessing (false);
}

void OniriqueVocalMiniAudioProcessor::prepareToPlay (double sr, int)
{
    license.refresh();   // beaucoup de DAW rappellent prepareToPlay juste avant un export

    // Toute l'allocation se fait ici ; processBlock n'alloue rien.
    currentRate = sr;
    configure();
}

void OniriqueVocalMiniAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int total = buffer.getNumSamples();

    // une piste mono dans un plugin stéréo : la voix est recopiée à droite (le doubler, le chorus, le ping-pong et
    // la reverb s'ouvrent en stéréo)
    const int inCh = getTotalNumInputChannels(), outCh = getTotalNumOutputChannels();
    if (inCh == 1 && outCh > 1 && buffer.getNumChannels() > 1)
        buffer.copyFrom (1, 0, buffer, 0, 0, total);
    else
        for (int c = inCh; c < outCh; ++c) buffer.clear (c, 0, total);

    const int numCh = juce::jmin (buffer.getNumChannels(), 2);
    if (numCh < 1 || total < 1) return;

    // tempo du DAW (DELAY au temps) et position de lecture (PULSE de la reverb calé sur la mesure)
    ppqNow = -1.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm(); b.hasValue() && *b > 1.0)
                bpmNow.store (*b, std::memory_order_relaxed);
            if (pos->getIsPlaying())
                if (auto q = pos->getPpqPosition(); q.hasValue()) ppqNow = *q;
        }

    chain.setParams (readParams());
    float* ch[2] { buffer.getWritePointer (0), numCh > 1 ? buffer.getWritePointer (1) : nullptr };
    chain.process (ch, numCh, total);
    outputTap.push (ch[0], ch[1], total);
    license.process (buffer, getSampleRate(), isNonRealtime());
}

juce::AudioProcessorEditor* OniriqueVocalMiniAudioProcessor::createEditor()
{
    return new OniriqueVocalMiniAudioProcessorEditor (*this);
}

void OniriqueVocalMiniAudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void OniriqueVocalMiniAudioProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            // un projet enregistré avant VOCAL MINI PRO : les quatre envois de reverb deviennent un seul espace
            std::map<juce::String, float> values;
            for (auto* e : xml->getChildWithTagNameIterator ("PARAM"))
                values[e->getStringAttribute ("id")] = (float) e->getDoubleAttribute ("value");
            if (values.count (ids::revMode) == 0)
            {
                prm::migrateLegacy (values);
                for (auto* id : { "revShort", "revSmall", "revBig", "revLush" })
                    if (auto* old = xml->getChildByAttribute ("id", id)) xml->removeChildElement (old, true);
                for (auto* id : { ids::revMode, ids::revMix, ids::revDecay, ids::revPre })
                    if (const auto it = values.find (id); it != values.end())
                    {
                        auto* e = xml->getChildByAttribute ("id", id);
                        if (e == nullptr) { e = xml->createNewChildElement ("PARAM"); e->setAttribute ("id", id); }
                        e->setAttribute ("value", (double) it->second);
                    }
            }
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            presets.takeSnapshot();   // l'état rechargé sert de référence pour l'indicateur "modifié"
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new OniriqueVocalMiniAudioProcessor();
}
