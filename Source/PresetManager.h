#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters.h"

namespace onirique
{
/*  Presets : usine (définis ici) + utilisateur (fichiers .ovmpreset dans
    %APPDATA%/Onirique/VocalMini/Presets). Thread de l'interface uniquement.

    - Un preset contient tous les réglages SAUF Bypass et le mode LIVE / MIX (réglages de session).
    - Un paramètre absent d'un fichier reprend sa valeur par défaut.
    - Le nom du preset courant est rangé dans l'état du plugin : il revient avec le projet.
    - "Modifié" : comparaison des valeurs actuelles avec celles du chargement.
*/
class PresetManager
{
public:
    struct Entry { juce::String name; bool factory = false; juce::File file; int factoryIndex = -1; };

    explicit PresetManager (juce::AudioProcessorValueTreeState& s) : apvts (s)
    {
        folder().createDirectory();
        takeSnapshot();
    }

    static juce::File folder()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Onirique").getChildFile ("VocalMini").getChildFile ("Presets");
    }

    std::vector<Entry> list() const
    {
        std::vector<Entry> v;
        for (int i = 0; i < (int) factory().size(); ++i)
            v.push_back ({ factory()[(size_t) i].name, true, {}, i });
        auto files = folder().findChildFiles (juce::File::findFiles, false, "*.ovmpreset");
        files.sort();
        for (auto& f : files) v.push_back ({ f.getFileNameWithoutExtension(), false, f, -1 });
        return v;
    }

    juce::String getCurrentName() const { return apvts.state.getProperty ("presetName", "Init").toString(); }

    bool isModified() const
    {
        if (snapshot.empty()) return false;
        size_t i = 0;
        for (auto* p : params())
            if (i < snapshot.size() && std::abs (p->getValue() - snapshot[i++]) > 1.0e-4f) return true;
        return false;
    }

    void load (const Entry& e)
    {
        std::map<juce::String, float> values;
        if (e.factory)
            values = factory()[(size_t) e.factoryIndex].values;
        else if (auto xml = juce::XmlDocument::parse (e.file))
            for (auto* p : xml->getChildWithTagNameIterator ("PARAM"))
                values[p->getStringAttribute ("id")] = (float) p->getDoubleAttribute ("value");

        applyValues (values, e.name);
    }

    // tous les réglages d'un coup (valeurs en unités réelles) ; un paramètre absent reprend sa valeur par défaut
    void applyValues (const std::map<juce::String, float>& values, const juce::String& name)
    {
        for (auto* p : params())
        {
            const auto it = values.find (p->getParameterID());
            const float norm = it != values.end() ? p->convertTo0to1 (it->second) : p->getDefaultValue();
            p->beginChangeGesture();
            p->setValueNotifyingHost (norm);
            p->endChangeGesture();
        }
        apvts.state.setProperty ("presetName", name, nullptr);
        takeSnapshot();
    }

    // renvoie false si le nom est vide ; un nom d'usine est suffixé pour ne jamais l'écraser
    bool save (juce::String name)
    {
        name = juce::File::createLegalFileName (name.trim());
        if (name.isEmpty()) return false;
        for (const auto& f : factory()) if (f.name.equalsIgnoreCase (name)) name << " (user)";

        juce::XmlElement xml ("OniriquePreset");
        xml.setAttribute ("name", name);
        xml.setAttribute ("version", 1);
        for (auto* p : params())
        {
            auto* e = xml.createNewChildElement ("PARAM");
            e->setAttribute ("id", p->getParameterID());
            e->setAttribute ("value", (double) p->convertFrom0to1 (p->getValue()));
        }
        if (! xml.writeTo (folder().getChildFile (name + ".ovmpreset"))) return false;
        apvts.state.setProperty ("presetName", name, nullptr);
        takeSnapshot();
        return true;
    }

    void remove (const Entry& e)
    {
        if (! e.factory) e.file.deleteFile();
    }

    // charge le preset précédent / suivant dans la liste (boucle)
    void step (int delta)
    {
        const auto v = list();
        if (v.empty()) return;
        int idx = 0;
        for (int i = 0; i < (int) v.size(); ++i) if (v[(size_t) i].name == getCurrentName()) idx = i;
        load (v[(size_t) ((idx + delta + (int) v.size()) % (int) v.size())]);
    }

    void takeSnapshot()
    {
        snapshot.clear();
        for (auto* p : params()) snapshot.push_back (p->getValue());
    }

private:
    struct Factory { juce::String name; std::map<juce::String, float> values; };

    // tous les paramètres sauf Bypass
    std::vector<juce::RangedAudioParameter*> params() const
    {
        std::vector<juce::RangedAudioParameter*> v;
        for (auto* p : apvts.processor.getParameters())
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p))
                if (const auto id = r->getParameterID(); id != ids::bypass && id != ids::mode)
                    v.push_back (r);   // réglages de session exclus : bypass, LIVE / MIX
        return v;
    }

    // valeurs en unités réelles (dB, %, 0..10, index de choix, 0/1)
    static const std::vector<Factory>& factory()
    {
        static const std::vector<Factory> f = makeFactory();
        return f;
    }

#include "FactoryPresets.inc"

    juce::AudioProcessorValueTreeState& apvts;
    std::vector<float> snapshot;
};
} // namespace onirique
