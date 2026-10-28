#include "Parameters.h"

namespace drumchimp::params
{
    juce::String padID (int pad, const char* name)
    {
        return "p" + juce::String (pad + 1) + "_" + name;
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        using namespace juce;
        AudioProcessorValueTreeState::ParameterLayout layout;

        for (int pad = 0; pad < numPads; ++pad)
        {
            const String n = "Pad " + String (pad + 1) + " ";
            auto group = std::make_unique<AudioProcessorParameterGroup> ("pad" + String (pad + 1), "Pad " + String (pad + 1), "|");

            group->addChild (std::make_unique<AudioParameterFloat> (ParameterID { padID (pad, "level"), 1 }, n + "Level",
                NormalisableRange<float> (minLevelDb, 6.0f, 0.01f, 2.5f), 0.0f,
                AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction ([] (float v, int)
                {
                    return v <= minLevelDb ? String ("-inf dB") : String (std::abs (v) < 0.05f ? 0.0f : v, 1) + " dB";   // no "-0.0"
                })));
            group->addChild (std::make_unique<AudioParameterFloat> (ParameterID { padID (pad, "pan"), 1 }, n + "Pan",
                NormalisableRange<float> (-1.0f, 1.0f, 0.01f), 0.0f,
                AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
                {
                    const int p = roundToInt (v * 100.0f);
                    return p == 0 ? String ("C") : (p < 0 ? "L" : "R") + String (std::abs (p));
                })));
            group->addChild (std::make_unique<AudioParameterFloat> (ParameterID { padID (pad, "tune"), 1 }, n + "Tune",
                NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f,
                AudioParameterFloatAttributes().withLabel ("st").withStringFromValueFunction ([] (float v, int)
                {
                    return std::abs (v) < 0.005f ? String ("0.00 st") : (v > 0.0f ? "+" : "") + String (v, 2) + " st";
                })));
            group->addChild (std::make_unique<AudioParameterFloat> (ParameterID { padID (pad, "decay"), 1 }, n + "Decay",
                NormalisableRange<float> (0.01f, maxDecaySeconds, 0.001f, 0.3f), maxDecaySeconds,
                AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
                {
                    return v >= maxDecaySeconds - 0.001f ? String ("Full") : v < 1.0f ? String (roundToInt (v * 1000.0f)) + " ms" : String (v, 2) + " s";
                })));
            group->addChild (std::make_unique<AudioParameterFloat> (ParameterID { padID (pad, "vel"), 1 }, n + "Velocity",
                NormalisableRange<float> (0.0f, 1.0f, 0.01f), 1.0f,
                AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction ([] (float v, int)
                {
                    return String (roundToInt (v * 100.0f)) + "%";
                })));
            group->addChild (std::make_unique<AudioParameterChoice> (ParameterID { padID (pad, "choke"), 1 }, n + "Choke",
                chokeGroups, pad == 3 || pad == 4 ? 1 : 0));
            group->addChild (std::make_unique<AudioParameterBool> (ParameterID { padID (pad, "mute"), 1 }, n + "Mute", false));
            group->addChild (std::make_unique<AudioParameterBool> (ParameterID { padID (pad, "solo"), 1 }, n + "Solo", false));
            layout.add (std::move (group));
        }

        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "source", 1 }, "Source", StringArray { "Generator", "MIDI In" }, 0));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "notemap", 1 }, "Note map", StringArray { "GM", "Chromatic" }, 0));
        // Automatable only because Live leaves non-automatable VST3 parameters out of the LOM
        // (Phase 0, spike e).
        layout.add (std::make_unique<AudioParameterInt> (ParameterID { "instance", 1 }, "Instance ID", 0, 99999, 0));
        return layout;
    }

    Reader::Reader (juce::AudioProcessorValueTreeState& state)
    {
        for (int pad = 0; pad < numPads; ++pad)
        {
            const auto get = [&] (const char* name) { return state.getRawParameterValue (padID (pad, name)); };
            pads[(size_t) pad] = { get ("level"), get ("pan"), get ("tune"), get ("decay"), get ("vel"), get ("choke"), get ("mute"), get ("solo") };
        }
        source = state.getRawParameterValue ("source");
        noteMapParam = state.getRawParameterValue ("notemap");
    }

    PadSettingsArray Reader::padSettings() const noexcept
    {
        PadSettingsArray s;
        bool anySolo = false;
        for (const auto& p : pads)
            anySolo = anySolo || p.solo->load (std::memory_order_relaxed) >= 0.5f;

        for (size_t i = 0; i < pads.size(); ++i)
        {
            const auto& p = pads[i];
            const float db = p.level->load (std::memory_order_relaxed);
            const float decay = p.decay->load (std::memory_order_relaxed);
            s[i].gain = db <= minLevelDb ? 0.0f : juce::Decibels::decibelsToGain (db);
            s[i].pan = p.pan->load (std::memory_order_relaxed);
            s[i].tune = p.tune->load (std::memory_order_relaxed);
            s[i].decaySeconds = decay >= maxDecaySeconds - 0.001f ? 0.0f : decay;
            s[i].velocitySensitivity = p.vel->load (std::memory_order_relaxed);
            s[i].choke = juce::roundToInt (p.choke->load (std::memory_order_relaxed));
            s[i].audible = p.mute->load (std::memory_order_relaxed) < 0.5f && (! anySolo || p.solo->load (std::memory_order_relaxed) >= 0.5f);
        }
        return s;
    }

    NoteMap Reader::noteMap() const noexcept
    {
        return juce::roundToInt (noteMapParam->load (std::memory_order_relaxed)) == 1 ? NoteMap::chromatic : NoteMap::gm;
    }

    bool Reader::generatorOn() const noexcept
    {
        return juce::roundToInt (source->load (std::memory_order_relaxed)) == 0;
    }
}
