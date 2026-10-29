// The kit a new DrumChimp starts with (PLAN.md §6, Phase 1), until the kit builder (Phase 3)
// picks one: the Core Library's 909 one-shots, which every Live 12 Standard and Suite has (and
// a 707 tambourine for the Perc pad: there's no 909 one).
// Paths are relative to the Core Library root (LibraryRoots.h).
#pragma once

#include "../engine/NoteMap.h"
#include <array>

namespace drumchimp
{
    constexpr std::array<const char*, numPads> defaultKitPaths {
        "Samples/One Shots/Drums/Kick/Kick 909 2.wav",
        "Samples/One Shots/Drums/Snare/Snare 909 Hard 1.wav",
        "Samples/One Shots/Drums/Clap/Clap 909.aif",
        "Samples/One Shots/Drums/Hihat/Hihat Closed 909.aif",
        "Samples/One Shots/Drums/Hihat/Hihat Open 909.aif",
        "Samples/One Shots/Drums/Tom/Tom 909 Lo 3.wav",
        "Samples/One Shots/Drums/Tom/Tom 909 Hi 2.wav",
        "Samples/One Shots/Drums/Cymbal/Crash 909.aif",
        "Samples/One Shots/Drums/Rim/Rim 909.aif",
        "Samples/One Shots/Drums/Tom/Tom 909 Mid.aif",
        "Samples/One Shots/Drums/Ride/Ride 909.aif",
        "Samples/One Shots/Drums/Tambourine/Tamb 707.aif",
    };
}
