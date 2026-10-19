#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace onirique;

OniriqueVocalMiniAudioProcessor::OniriqueVocalMiniAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    auto r = [this] (const char* id) { return apvts.getRawParameterValue (id); };
    raw.in = r (ids::inGain);     raw.out = r (ids::outGain);   raw.autoIn = r (ids::autoInput); raw.lowCut = r (ids::lowCut);
    raw.low = r (ids::low);       raw.mid = r (ids::mid);       raw.hi = r (ids::hi);            raw.air = r (ids::air);
    raw.comp = r (ids::comp);     raw.multi = r (ids::multi);   raw.deess = r (ids::deess);      raw.drive = r (ids::drive);
    raw.phone = r (ids::phone);   raw.doubler = r (ids::doubler); raw.chorus = r (ids::chorus);
    raw.delay = r (ids::delay);   raw.feed = r (ids::feed);     raw.filter = r (ids::filter);    raw.time = r (ids::time);
    raw.ping = r (ids::ping);
    raw.rev[0] = r (ids::rShort); raw.rev[1] = r (ids::rSmall); raw.rev[2] = r (ids::rBig);      raw.rev[3] = r (ids::rLush);
    raw.bypass = r (ids::bypass);
    raw.mode = r (ids::mode);
    apvts.addParameterListener (ids::mode, this);

    // l'état de départ est le preset « Lead Clean Vocal » (les valeurs par défaut des paramètres)
    apvts.state.setProperty ("presetName", "Lead Clean Vocal", nullptr);
    presets.takeSnapshot();

   #if ONIRIQUE_UI_TEST
    // essais locaux uniquement : ONIRIQUE_UI_TEST=demo,... charge un preset plus parlant pour le visuel de la boutique
    if (juce::SystemStats::getEnvironmentVariable ("ONIRIQUE_UI_TEST", {}).contains ("hook"))
        for (const auto& e : presets.list())
            if (e.name == "Dream Hook") presets.load (e);
   #endif
}

OniriqueVocalMiniAudioProcessor::~OniriqueVocalMiniAudioProcessor()
{
    apvts.removeParameterListener (ids::mode, this);
    cancelPendingUpdate();
}

mini::MiniChain::Params OniriqueVocalMiniAudioProcessor::readParams() const noexcept
{
    mini::MiniChain::Params p;
    p.inDb = raw.in->load();
    p.outDb = raw.out->load();
    p.autoInput = raw.autoIn->load() > 0.5f;
    p.lowCut = raw.lowCut->load() > 0.5f;
    p.low = raw.low->load() * 0.1f;
    p.mid = raw.mid->load() * 0.1f;
    p.hi = raw.hi->load() * 0.1f;
    p.air = raw.air->load() * 0.1f;
    p.comp = raw.comp->load() * 0.01f;
    p.multi = raw.multi->load() * 0.01f;
    p.deess = raw.deess->load() * 0.01f;
    p.drive = raw.drive->load() * 0.01f;
    p.phone = raw.phone->load() * 0.01f;
    p.doubler = raw.doubler->load() * 0.01f;
    p.chorus = raw.chorus->load() * 0.01f;
    p.delay = raw.delay->load() * 0.01f;
    p.feed = raw.feed->load() * 0.01f;
    p.filter = raw.filter->load() * 0.01f;
    p.division = juce::roundToInt (raw.time->load());
    p.ping = raw.ping->load() > 0.5f;
    for (int k = 0; k < 4; ++k) p.reverb[(size_t) k] = raw.rev[k]->load() * 0.01f;
    p.bypass = raw.bypass->load() > 0.5f;
    p.bpm = bpmNow.load (std::memory_order_relaxed);
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
    const bool live = juce::roundToInt (raw.mode->load()) == 0;
    chain.prepare (currentRate, readParams(), live);
    liveNow.store (live);
    setLatencySamples (chain.getLatencySamples());
}

void OniriqueVocalMiniAudioProcessor::handleAsyncUpdate()
{
    const bool live = juce::roundToInt (raw.mode->load()) == 0;
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
    // les reverbs s'ouvrent en stéréo)
    const int inCh = getTotalNumInputChannels(), outCh = getTotalNumOutputChannels();
    if (inCh == 1 && outCh > 1 && buffer.getNumChannels() > 1)
        buffer.copyFrom (1, 0, buffer, 0, 0, total);
    else
        for (int c = inCh; c < outCh; ++c) buffer.clear (c, 0, total);

    const int numCh = juce::jmin (buffer.getNumChannels(), 2);
    if (numCh < 1 || total < 1) return;

    // tempo du DAW (DELAY au temps)
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm(); b.hasValue() && *b > 1.0)
                bpmNow.store (*b, std::memory_order_relaxed);

    chain.setParams (readParams());
    float* ch[2] { buffer.getWritePointer (0), numCh > 1 ? buffer.getWritePointer (1) : nullptr };
    chain.process (ch, numCh, total);
    outputTap.push (ch[0], ch[1], total);
   #if ! ONIRIQUE_NO_LICENSE
    license.process (buffer, getSampleRate(), isNonRealtime());   // copie personnelle de l'équipe : pas de licence
   #endif
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
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            presets.takeSnapshot();   // l'état rechargé sert de référence pour l'indicateur "modifié"
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new OniriqueVocalMiniAudioProcessor();
}
