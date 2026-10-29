// Which MIDI note plays which pad (PLAN.md §3.1). Plain C++, no JUCE, no allocation.
//
// Twelve pads, in three rows of four (a third row since 2026-10-29: Rim, Mid tom, Ride, Perc).
//
//   GM         Kick 36, Snare 38, Clap 39, Closed hat 42, Open hat 46, Low tom 45, High tom 50,
//              Crash 49, Rim 37, Mid tom 47, Ride 51, Perc 54 (the tambourine): so DrumChimp's
//              MIDI also plays an Ableton Drum Rack (the default)
//   Chromatic  36–47, like a Drum Rack's bottom three rows
#pragma once

#include <array>
#include <cstdint>

namespace drumchimp
{
    constexpr int numPads = 12;

    enum class NoteMap { gm = 0, chromatic = 1 };

    constexpr std::array<uint8_t, numPads> gmNotes { 36, 38, 39, 42, 46, 45, 50, 49, 37, 47, 51, 54 };

    constexpr uint8_t noteForPad (NoteMap map, int pad) noexcept
    {
        return map == NoteMap::chromatic ? (uint8_t) (36 + pad) : gmNotes[(size_t) pad];
    }

    constexpr int padForNote (NoteMap map, int note) noexcept
    {
        for (int i = 0; i < numPads; ++i)
            if (noteForPad (map, i) == note)
                return i;
        return -1;
    }
}
