// The plug-in's parameters (PLAN.md §3.1): eight per pad, then four global ones.
//
//   pN_level   dB, −60 (off) … +6           pN_decay   10 ms … 10 s, the top is "Full"
//   pN_pan     L100 … C … R100              pN_vel     velocity sensitivity, 0 … 100%
//   pN_tune    −24 … +24 semitones          pN_choke   None, A, B, C, D (hats share A)
//   pN_mute, pN_solo
//   character  Randomise: Matched (one family) … Wild
//   source     Generator (the test pattern plays and goes to MIDI out) or MIDI In
//   notemap    GM or Chromatic
//   instance   Instance ID, random per instance (PLAN.md §3.6, route C)
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../core/sampler/DrumEngine.h"

namespace drumchimp::params
{
    constexpr float minLevelDb = -60.0f, maxDecaySeconds = 10.0f;
    const juce::StringArray chokeGroups { "None", "A", "B", "C", "D" };

    juce::String padID (int pad, const char* name);
    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // The raw values the audio thread reads each block.
    class Reader
    {
    public:
        explicit Reader (juce::AudioProcessorValueTreeState&);
        PadSettingsArray padSettings() const noexcept;
        NoteMap noteMap() const noexcept;
        bool generatorOn() const noexcept;

    private:
        struct Pad { std::atomic<float>* level, *pan, *tune, *decay, *vel, *choke, *mute, *solo; };
        std::array<Pad, numPads> pads {};
        std::atomic<float>* source = nullptr;
        std::atomic<float>* noteMapParam = nullptr;
    };
}
