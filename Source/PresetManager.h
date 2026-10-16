#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace ids
{
    inline const char* const drive = "drive";       inline const char* const tone = "tone";
    inline const char* const wow = "wowflutter";    inline const char* const character = "character";
    inline const char* const input = "input";       inline const char* const output = "output";
    inline const char* const mix = "mix";           inline const char* const autogain = "autogain";
    inline const char* const model = "model";
    // order used by the MIDI map and the UI
    inline const char* const all[] = { drive, tone, wow, character, input, output, mix, autogain, model };
    constexpr int numParams = 9;
}

struct Preset
{
    juce::String name; int model = 0;
    float drive = 35, tone = 0, wow = 0, character = 50, input = 0, output = 0, mix = 100; bool autoGain = true;
    bool factory = true;
};

// Message-thread only (touches files and notifies the host).
class PresetManager : public juce::ChangeBroadcaster
{
public:
    explicit PresetManager (juce::AudioProcessorValueTreeState& s) : apvts (s)
    {
        auto U = [] (const char* t) { return juce::String::fromUTF8 (t); };
        //                 name                          model drive tone  wow  char  in   out  mix auto
        presets.push_back ({ U("TUBE \xe2\x80\x94 Warm Glue"),            0, 28,  1.0f,  8, 55,  0.f,  0.f, 100, true,  true });
        presets.push_back ({ U("TUBE \xe2\x80\x94 Soft Tape"),            0, 22, -3.5f, 14, 40,  0.f,  0.f, 100, true,  true });
        presets.push_back ({ U("TRANSISTOR \xe2\x80\x94 Punch & Presence"), 1, 40,  2.5f,  0, 65,  1.f, -1.f, 100, true,  true });
        presets.push_back ({ U("TRANSISTOR \xe2\x80\x94 Drum Density"),    1, 55,  1.0f,  0, 50,  4.f, -3.f, 100, true,  true });
        presets.push_back ({ U("OPTO \xe2\x80\x94 Smooth Glue"),           2, 30,  0.0f,  6, 60,  2.f,  0.f, 100, true,  true });
        presets.push_back ({ U("DIODE \xe2\x80\x94 Dirty Texture"),        3, 68,  2.0f, 12, 70,  3.f, -3.f, 100, true,  true });
        scanUserPresets();
    }

    int size() const { return (int) presets.size(); }
    const Preset& get (int i) const { return presets[(size_t) juce::jlimit (0, size() - 1, i)]; }
    int getCurrent() const { return current; }
    juce::String getCurrentName() const
    {
        auto n = apvts.state.getProperty ("presetName", "INIT").toString();
        return n.isEmpty() ? juce::String ("INIT") : n;
    }

    void apply (int index)
    {
        if (size() == 0) return;
        index = (index % size() + size()) % size();
        const auto& p = get (index);
        set (ids::drive, p.drive); set (ids::tone, p.tone); set (ids::wow, p.wow); set (ids::character, p.character);
        set (ids::input, p.input); set (ids::output, p.output); set (ids::mix, p.mix);
        set (ids::autogain, p.autoGain ? 1.f : 0.f); set (ids::model, (float) p.model);
        current = index;
        apvts.state.setProperty ("presetName", p.name, nullptr);
        sendChangeMessage();
    }
    void next() { apply (current < 0 ? 0 : current + 1); }
    void previous() { apply (current < 0 ? size() - 1 : current - 1); }

    juce::File getUserDir() const
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("PTZR").getChildFile ("FORGE").getChildFile ("Presets");
    }

    bool saveUser (const juce::String& rawName)
    {
        auto name = rawName.trim().substring (0, 48);
        if (name.isEmpty()) return false;
        auto dir = getUserDir();
        if (! dir.createDirectory().wasOk()) return false;
        juce::XmlElement x ("FORGEPRESET");
        x.setAttribute ("name", name);
        for (auto* id : ids::all) x.setAttribute (id, (double) apvts.getRawParameterValue (id)->load());
        auto f = dir.getChildFile (juce::File::createLegalFileName (name) + ".forgepreset");
        if (! x.writeTo (f)) return false;
        scanUserPresets();
        for (int i = 0; i < size(); ++i) if (presets[(size_t) i].name == name) current = i;
        apvts.state.setProperty ("presetName", name, nullptr);
        sendChangeMessage();
        return true;
    }

    void scanUserPresets()
    {
        presets.erase (std::remove_if (presets.begin(), presets.end(), [] (const Preset& p) { return ! p.factory; }), presets.end());
        auto files = getUserDir().findChildFiles (juce::File::findFiles, false, "*.forgepreset");
        files.sort();
        for (auto& f : files)
            if (auto x = juce::XmlDocument::parse (f))
            {
                Preset p; p.factory = false; p.name = x->getStringAttribute ("name", f.getFileNameWithoutExtension());
                p.drive = (float) x->getDoubleAttribute (ids::drive, 35);   p.tone = (float) x->getDoubleAttribute (ids::tone, 0);
                p.wow = (float) x->getDoubleAttribute (ids::wow, 0);        p.character = (float) x->getDoubleAttribute (ids::character, 50);
                p.input = (float) x->getDoubleAttribute (ids::input, 0);    p.output = (float) x->getDoubleAttribute (ids::output, 0);
                p.mix = (float) x->getDoubleAttribute (ids::mix, 100);      p.autoGain = x->getDoubleAttribute (ids::autogain, 1) > 0.5;
                p.model = juce::jlimit (0, 3, (int) x->getDoubleAttribute (ids::model, 0));
                presets.push_back (p);
            }
    }

private:
    void set (const char* id, float v)
    {
        if (auto* p = apvts.getParameter (id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (v));
            p->endChangeGesture();
        }
    }
    juce::AudioProcessorValueTreeState& apvts;
    std::vector<Preset> presets;
    int current = -1;
};
