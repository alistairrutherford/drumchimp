// Phase 0's test pattern (PLAN.md §6): one bar of a rock beat on the default pads, looped. It
// stands in for the pattern generator (Phase 4) so the spikes can check MIDI out, *MIDI From*
// recording and the drag-out. Plain C++, no JUCE, no allocation: safe on the audio thread.
//
//   gmNotes          the default note of each pad (PLAN.md §3.1)
//   padForNote       note → pad, or -1
//   forEachEvent     every note-on and note-off whose time is in [fromBeat, toBeat)
//   forEachEventInBlock
//                    the same for one audio block, with each event on the first sample at or after
//                    its time (never early: found in Live, 2026-10-28)
//   testPatternSong  N bars of the pattern as a MidiSong (message thread: it allocates)
#pragma once

#include "../midiio/MidiFile.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace drumchimp
{
    constexpr int numPads = 8;

    // Kick, Snare, Clap, Closed hat, Open hat, Low tom, High tom, Crash.
    constexpr std::array<uint8_t, numPads> gmNotes { 36, 38, 39, 42, 46, 45, 50, 49 };

    constexpr int padForNote (int note) noexcept
    {
        for (int i = 0; i < numPads; ++i)
            if (gmNotes[(size_t) i] == note)
                return i;
        return -1;
    }

    namespace testpattern
    {
        constexpr int stepsPerBar = 16;
        constexpr double beatsPerBar = 4.0;
        constexpr double stepBeats = beatsPerBar / stepsPerBar;
        constexpr double noteBeats = stepBeats / 2.0;     // each hit lasts a 32nd

        struct Hit { int step; int pad; uint8_t velocity; };

        constexpr std::array<Hit, 15> hits {{
            { 0, 0, 115 }, { 8, 0, 105 }, { 10, 0, 90 },                       // kick
            { 4, 1, 112 }, { 12, 1, 112 },                                      // snare
            { 0, 3, 92 }, { 2, 3, 68 }, { 4, 3, 88 }, { 6, 3, 68 },             // closed hat
            { 8, 3, 92 }, { 10, 3, 68 }, { 12, 3, 88 },
            { 14, 4, 84 },                                                      // open hat
            { 15, 6, 76 },                                                      // high tom pickup
            { 0, 7, 100 },                                                      // crash on the one
        }};

        // Calls fn (beat, pad, note, velocity, isNoteOn) for each event in [fromBeat, toBeat), bar
        // by bar and in pattern order (not sorted by time: the caller's MidiBuffer sorts them).
        template <typename Fn>
        void forEachEvent (double fromBeat, double toBeat, Fn&& fn)
        {
            if (! (toBeat > fromBeat))
                return;
            // A note-off belongs to the bar its note-on is in, so start one bar early.
            const auto firstBar = (int64_t) std::floor (fromBeat / beatsPerBar) - 1;
            const auto lastBar = (int64_t) std::floor (toBeat / beatsPerBar);
            for (int64_t bar = firstBar; bar <= lastBar; ++bar)
                for (const auto& h : hits)
                {
                    const double on = (double) bar * beatsPerBar + h.step * stepBeats;
                    const double off = on + noteBeats;
                    const auto note = gmNotes[(size_t) h.pad];
                    if (on >= fromBeat && on < toBeat)
                        fn (on, h.pad, note, h.velocity, true);
                    if (off >= fromBeat && off < toBeat)
                        fn (off, h.pad, note, (uint8_t) 0, false);
                }
        }

        // A block of n samples, sample i at fromBeat + i * beatsPerSample: calls fn (sample, pad,
        // note, velocity, isNoteOn) for each event whose first sample at or after it is in the
        // block. Consecutive blocks see each event once: the block is chosen by that rounded
        // sample, not by comparing beats, so a beat exactly on a block's edge isn't lost or
        // doubled by rounding (an event within 1e-6 samples after a sample counts as on it).
        template <typename Fn>
        void forEachEventInBlock (double fromBeat, double beatsPerSample, int n, Fn&& fn)
        {
            if (n <= 0 || ! (beatsPerSample > 0.0))
                return;
            forEachEvent (fromBeat - 2.0 * beatsPerSample, fromBeat + (n + 1) * beatsPerSample,
                          [&] (double beat, int pad, uint8_t note, uint8_t velocity, bool on)
            {
                const auto sample = (int) std::ceil ((beat - fromBeat) / beatsPerSample - 1.0e-6);
                if (sample >= 0 && sample < n)
                    fn (sample, pad, note, velocity, on);
            });
        }

        inline MidiSong testPatternSong (int bars, double bpm, int ppq = 960)
        {
            MidiSong song;
            song.ppq = ppq;
            song.bpm = bpm;
            song.name = "DrumChimp Test Pattern";
            const auto ticks = [ppq] (double beats) { return (int64_t) std::llround (beats * ppq); };
            for (int bar = 0; bar < bars; ++bar)
                for (const auto& h : hits)
                {
                    const double on = bar * beatsPerBar + h.step * stepBeats;
                    song.notes.push_back ({ ticks (on), ticks (on + noteBeats), gmNotes[(size_t) h.pad], h.velocity, 0 });
                }
            std::stable_sort (song.notes.begin(), song.notes.end(), [] (const MidiNote& a, const MidiNote& b)
            {
                return a.start != b.start ? a.start < b.start : a.pitch < b.pitch;
            });
            return song;
        }
    }
}
