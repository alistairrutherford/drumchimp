// A drum pattern and how it plays (PLAN.md §3.4, §4 Sequencer). Plain C++, no JUCE. A Pattern
// is fixed-size (no allocation), so the audio thread can read one and the message thread can copy
// and edit one freely; it reaches the audio thread whole, through a Handoff.
//
//   Pattern            8 lanes (one per pad) × up to 8 bars of 16 steps. A cell is a hit: velocity
//                      (0 = none), repeats (a roll of 2 or 3 in the step) and a timing offset in ms
//                      (Humanise, baked in so the drag-out has it too).
//   forEachEvent       every note-on and note-off in [fromBeat, toBeat), the pattern looping from
//                      beat 0 (bar 1 of the song): Swing delays the off-beat sixteenths, then each
//                      hit's offset; a hit near the loop's end can land in the next loop
//   forEachEventInBlock  the same for one audio block, each event on the first sample at or after
//                      its time (as the Phase 0 test pattern did; never early)
//   toMidiSong         N loops of it as MIDI (the drag-out)
#pragma once

#include "../engine/NoteMap.h"
#include "../midiio/MidiFile.h"
#include <array>
#include <cmath>

namespace drumchimp
{
    constexpr int maxPatternSteps = 16 * 8;

    struct Cell
    {
        uint8_t velocity = 0;             // 0: no hit
        uint8_t repeats = 1;              // a roll: 2 or 3 evenly in the step
        float offsetMs = 0.0f;            // Humanise

        bool operator== (const Cell& o) const noexcept { return velocity == o.velocity && repeats == o.repeats && offsetMs == o.offsetMs; }
    };

    struct Pattern
    {
        int stepsPerBar = 16;
        int bars = 1;
        std::array<std::array<Cell, maxPatternSteps>, numPads> lanes {};

        int steps() const noexcept { return stepsPerBar * bars; }
        double stepBeats() const noexcept { return 4.0 / stepsPerBar; }
        double lengthBeats() const noexcept { return 4.0 * bars; }

        int hits (int pad) const noexcept
        {
            int n = 0;
            for (int s = 0; s < steps(); ++s)
                n += lanes[(size_t) pad][(size_t) s].velocity > 0 ? 1 : 0;
            return n;
        }

        bool operator== (const Pattern& o) const noexcept
        {
            if (stepsPerBar != o.stepsPerBar || bars != o.bars)
                return false;
            for (int p = 0; p < numPads; ++p)
                for (int s = 0; s < steps(); ++s)
                    if (! (lanes[(size_t) p][(size_t) s] == o.lanes[(size_t) p][(size_t) s]))
                        return false;
            return true;
        }
    };

    namespace pattern
    {
        // A hit's start in beats from the loop's start: Swing delays the second sixteenth of each
        // eighth (50% straight … 66% a triplet feel … 75%), on a 16-step grid only.
        inline double hitBeat (const Pattern& p, int step, float offsetMs, double swing, double bpm) noexcept
        {
            double beat = step * p.stepBeats();
            if (p.stepsPerBar == 16 && (step & 1) != 0)
                beat += (swing - 0.5) * 2.0 * p.stepBeats();
            return beat + offsetMs * bpm / 60000.0;
        }

        // Calls fn (beat, pad, velocity, isNoteOn) for each event in [fromBeat, toBeat).
        template <typename Fn>
        void forEachEvent (const Pattern& p, double fromBeat, double toBeat, double swing, double bpm, Fn&& fn)
        {
            if (! (toBeat > fromBeat) || p.steps() <= 0)
                return;
            const double length = p.lengthBeats();
            // A hit can start up to a step late (swing, humanise) and end later: look one loop each way.
            const auto first = (int64_t) std::floor (fromBeat / length) - 1;
            const auto last = (int64_t) std::floor (toBeat / length) + 1;
            for (int64_t loop = first; loop <= last; ++loop)
                for (int pad = 0; pad < numPads; ++pad)
                    for (int s = 0; s < p.steps(); ++s)
                    {
                        const auto& c = p.lanes[(size_t) pad][(size_t) s];
                        if (c.velocity == 0)
                            continue;
                        const int repeats = c.repeats < 1 ? 1 : c.repeats;
                        const double sub = p.stepBeats() / repeats;
                        const double start = (double) loop * length + hitBeat (p, s, c.offsetMs, swing, bpm);
                        for (int k = 0; k < repeats; ++k)
                        {
                            const double on = start + k * sub;
                            const double off = on + sub * 0.5;
                            const auto velocity = (uint8_t) std::max (1, k == 0 ? (int) c.velocity : (int) c.velocity * (10 - k) / 12);
                            if (on >= fromBeat && on < toBeat)
                                fn (on, pad, velocity, true);
                            if (off >= fromBeat && off < toBeat)
                                fn (off, pad, (uint8_t) 0, false);
                        }
                    }
        }

        // A block of n samples, sample i at fromBeat + i * beatsPerSample: calls fn (sample, pad,
        // velocity, isNoteOn) for each event whose first sample at or after it is in the block.
        // The block is chosen by that rounded sample, so consecutive blocks see each event once.
        template <typename Fn>
        void forEachEventInBlock (const Pattern& p, double fromBeat, double beatsPerSample, int n, double swing, double bpm, Fn&& fn)
        {
            if (n <= 0 || ! (beatsPerSample > 0.0))
                return;
            forEachEvent (p, fromBeat - 2.0 * beatsPerSample, fromBeat + (n + 1) * beatsPerSample, swing, bpm,
                          [&] (double beat, int pad, uint8_t velocity, bool on)
            {
                const auto sample = (int) std::ceil ((beat - fromBeat) / beatsPerSample - 1.0e-6);
                if (sample >= 0 && sample < n)
                    fn (sample, pad, velocity, on);
            });
        }

        // `loops` times through the pattern as MIDI, from beat 0 (an early hit is moved to 0).
        inline MidiSong toMidiSong (const Pattern& p, int loops, double bpm, double swing, NoteMap map, const std::string& name, int ppq = 960)
        {
            MidiSong song;
            song.ppq = ppq;
            song.bpm = bpm;
            song.name = name;
            const auto ticks = [ppq] (double beats) { return (int64_t) std::llround (std::max (0.0, beats) * ppq); };
            for (int loop = 0; loop < loops; ++loop)
                for (int pad = 0; pad < numPads; ++pad)
                    for (int s = 0; s < p.steps(); ++s)
                    {
                        const auto& c = p.lanes[(size_t) pad][(size_t) s];
                        if (c.velocity == 0)
                            continue;
                        const int repeats = c.repeats < 1 ? 1 : c.repeats;
                        const double sub = p.stepBeats() / repeats;
                        const double start = loop * p.lengthBeats() + hitBeat (p, s, c.offsetMs, swing, bpm);
                        for (int k = 0; k < repeats; ++k)
                        {
                            const auto velocity = (uint8_t) std::max (1, k == 0 ? (int) c.velocity : (int) c.velocity * (10 - k) / 12);
                            const auto on = ticks (start + k * sub);
                            song.notes.push_back ({ on, std::max (on + 1, ticks (start + k * sub + sub * 0.5)), noteForPad (map, pad), velocity, 0 });
                        }
                    }
            std::stable_sort (song.notes.begin(), song.notes.end(), [] (const MidiNote& a, const MidiNote& b)
            {
                return a.start != b.start ? a.start < b.start : a.pitch < b.pitch;
            });
            return song;
        }
    }
}
