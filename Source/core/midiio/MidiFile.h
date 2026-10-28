// Standard MIDI files (copied from ChordChimp, which has it from Chordism): the drag-out's file, and a file's notes in (the
// tests read exports back with it). Plain C++, no JUCE. Uses std::vector: message thread
// only, never the audio thread.
//
//   readMidiFile   type 0 or 1, ticks per quarter note (SMPTE time isn't supported); every
//                  track's notes, paired on (channel, pitch) in order, with running status,
//                  note-on velocity 0 as a note-off, and the first tempo and time signature
//   writeMidiFile  type 1, `ppq` ticks per quarter (Export: 960): track 1 has the name, tempo
//                  and time signature, track 2 the notes (at one tick, note-offs first)
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace drumchimp
{
    struct MidiNote
    {
        int64_t start = 0, end = 0;       // ticks
        uint8_t pitch = 60, velocity = 100, channel = 0;
    };

    struct MidiSong
    {
        int ppq = 960;                    // ticks per quarter note
        double bpm = 120.0;
        int numerator = 4, denominator = 4;
        std::string name;
        std::vector<MidiNote> notes;      // sorted by start, then pitch
    };

    namespace smf
    {
        inline void put16 (std::vector<uint8_t>& b, int v) { b.push_back ((uint8_t) (v >> 8)); b.push_back ((uint8_t) v); }
        inline void put32 (std::vector<uint8_t>& b, uint32_t v)
        {
            for (int s = 24; s >= 0; s -= 8)
                b.push_back ((uint8_t) (v >> s));
        }

        inline void putVarLen (std::vector<uint8_t>& b, uint32_t v)
        {
            uint8_t bytes[5];
            int n = 0;
            bytes[n++] = (uint8_t) (v & 0x7f);
            while ((v >>= 7) != 0)
                bytes[n++] = (uint8_t) (0x80 | (v & 0x7f));
            while (n > 0)
                b.push_back (bytes[--n]);
        }

        inline void putTrack (std::vector<uint8_t>& out, const std::vector<uint8_t>& events)
        {
            out.insert (out.end(), { 'M', 'T', 'r', 'k' });
            put32 (out, (uint32_t) events.size());
            out.insert (out.end(), events.begin(), events.end());
        }

        struct Reader
        {
            const uint8_t* p;
            const uint8_t* end;
            bool ok = true;

            bool has (size_t n) const noexcept { return (size_t) (end - p) >= n; }
            uint32_t u8() noexcept { if (! has (1)) { ok = false; return 0; } return *p++; }
            uint32_t u16() noexcept { const uint32_t a = u8(); return (a << 8) | u8(); }
            uint32_t u32() noexcept { const uint32_t a = u16(); return (a << 16) | u16(); }
            uint32_t varLen() noexcept
            {
                uint32_t v = 0;
                for (int i = 0; i < 4; ++i)
                {
                    const uint32_t c = u8();
                    v = (v << 7) | (c & 0x7f);
                    if ((c & 0x80) == 0)
                        return v;
                }
                ok = false;
                return v;
            }
            void skip (size_t n) noexcept
            {
                if (! has (n)) { ok = false; p = end; return; }
                p += n;
            }
        };
    }

    inline std::vector<uint8_t> writeMidiFile (const MidiSong& song)
    {
        using namespace smf;
        std::vector<uint8_t> out;
        out.insert (out.end(), { 'M', 'T', 'h', 'd' });
        put32 (out, 6);
        put16 (out, 1);                   // type 1
        put16 (out, 2);                   // two tracks
        put16 (out, song.ppq);

        // Track 1: name, tempo, time signature.
        std::vector<uint8_t> t;
        if (! song.name.empty())
        {
            putVarLen (t, 0);
            t.insert (t.end(), { 0xff, 0x03 });
            putVarLen (t, (uint32_t) song.name.size());
            t.insert (t.end(), song.name.begin(), song.name.end());
        }
        const auto usPerQuarter = (uint32_t) std::lround (60.0e6 / (song.bpm > 0.0 ? song.bpm : 120.0));
        putVarLen (t, 0);
        t.insert (t.end(), { 0xff, 0x51, 0x03, (uint8_t) (usPerQuarter >> 16), (uint8_t) (usPerQuarter >> 8), (uint8_t) usPerQuarter });
        int denPower = 0;
        while ((1 << denPower) < song.denominator && denPower < 7)
            ++denPower;
        putVarLen (t, 0);
        t.insert (t.end(), { 0xff, 0x58, 0x04, (uint8_t) song.numerator, (uint8_t) denPower, 24, 8 });
        putVarLen (t, 0);
        t.insert (t.end(), { 0xff, 0x2f, 0x00 });
        putTrack (out, t);

        // Track 2: the notes.
        struct Ev { int64_t tick; bool on; uint8_t channel, pitch, velocity; };
        std::vector<Ev> events;
        events.reserve (song.notes.size() * 2);
        int64_t last = 0;
        for (const auto& n : song.notes)
        {
            const int64_t start = n.start < 0 ? 0 : n.start;
            const int64_t end = n.end > start ? n.end : start + 1;
            events.push_back ({ start, true, (uint8_t) (n.channel & 15), (uint8_t) (n.pitch & 127), (uint8_t) (n.velocity < 1 ? 1 : (n.velocity & 127)) });
            events.push_back ({ end, false, (uint8_t) (n.channel & 15), (uint8_t) (n.pitch & 127), 0 });
            last = std::max (last, end);
        }
        std::stable_sort (events.begin(), events.end(), [] (const Ev& a, const Ev& b)
        {
            if (a.tick != b.tick)
                return a.tick < b.tick;
            return ! a.on && b.on;        // note-offs first at one tick
        });
        std::vector<uint8_t> n;
        int64_t now = 0;
        for (const auto& e : events)
        {
            putVarLen (n, (uint32_t) (e.tick - now));
            now = e.tick;
            n.push_back ((uint8_t) ((e.on ? 0x90 : 0x80) | e.channel));
            n.push_back (e.pitch);
            n.push_back (e.on ? e.velocity : 0);
        }
        putVarLen (n, (uint32_t) (last - now));
        n.insert (n.end(), { 0xff, 0x2f, 0x00 });
        putTrack (out, n);
        return out;
    }

    // Reads a file's notes. Returns false (with `error`) if it isn't a MIDI file this reads.
    inline bool readMidiFile (const uint8_t* data, size_t size, MidiSong& song, std::string& error)
    {
        song = {};
        smf::Reader r { data, data + size };
        if (size < 14 || std::string ((const char*) data, 4) != "MThd")
        {
            error = "not a MIDI file";
            return false;
        }
        r.skip (4);
        const uint32_t headerLength = r.u32();
        const uint32_t format = r.u16();
        const uint32_t tracks = r.u16();
        const uint32_t division = r.u16();
        r.skip (headerLength > 6 ? headerLength - 6 : 0);
        if (! r.ok || format > 2)
        {
            error = "unsupported MIDI file header";
            return false;
        }
        if ((division & 0x8000) != 0 || division == 0)
        {
            error = "SMPTE-timed MIDI files aren't supported";
            return false;
        }
        song.ppq = (int) division;

        bool haveTempo = false, haveSignature = false;
        for (uint32_t track = 0; track < tracks && r.ok && r.has (8); ++track)
        {
            const std::string id ((const char*) r.p, 4);
            r.skip (4);
            const uint32_t length = r.u32();
            if (! r.has (length))
            {
                error = "truncated track";
                return false;
            }
            if (id != "MTrk")
            {
                r.skip (length);
                continue;
            }

            smf::Reader t { r.p, r.p + length };
            r.skip (length);
            int64_t tick = 0;
            uint8_t status = 0;
            std::vector<std::vector<size_t>> open (16 * 128);   // (channel, pitch) → notes still on, oldest first
            while (t.ok && t.p < t.end)
            {
                tick += t.varLen();
                uint32_t first = t.u8();
                if (first == 0xff)
                {
                    const uint32_t type = t.u8();
                    const uint32_t len = t.varLen();
                    if (! t.has (len))
                        break;
                    if (type == 0x51 && len == 3 && ! haveTempo)
                    {
                        const uint32_t us = ((uint32_t) t.p[0] << 16) | ((uint32_t) t.p[1] << 8) | t.p[2];
                        if (us > 0)
                            song.bpm = 60.0e6 / us;
                        haveTempo = true;
                    }
                    else if (type == 0x58 && len >= 2 && ! haveSignature)
                    {
                        song.numerator = t.p[0] > 0 ? t.p[0] : 4;
                        song.denominator = 1 << (t.p[1] < 7 ? t.p[1] : 2);
                        haveSignature = true;
                    }
                    else if (type == 0x03 && song.name.empty())
                    {
                        song.name.assign ((const char*) t.p, len);
                    }
                    t.skip (len);
                    if (type == 0x2f)
                        break;
                    continue;
                }
                if (first == 0xf0 || first == 0xf7)
                {
                    t.skip (t.varLen());
                    continue;
                }

                uint32_t d1;
                if ((first & 0x80) != 0)
                {
                    status = (uint8_t) first;
                    d1 = t.u8();
                }
                else
                {
                    if (status == 0)
                    {
                        error = "running status without a status byte";
                        return false;
                    }
                    d1 = first;           // running status
                }
                const uint32_t kind = status & 0xf0u;
                const uint32_t d2 = (kind == 0xc0 || kind == 0xd0) ? 0 : t.u8();
                const uint8_t channel = (uint8_t) (status & 0x0f);
                const size_t slot = (size_t) channel * 128 + (d1 & 127);
                if (kind == 0x90 && d2 > 0)
                {
                    open[slot].push_back (song.notes.size());
                    song.notes.push_back ({ tick, -1, (uint8_t) (d1 & 127), (uint8_t) d2, channel });
                }
                else if (kind == 0x80 || kind == 0x90)
                {
                    auto& o = open[slot];
                    if (! o.empty())
                    {
                        song.notes[o.front()].end = tick;
                        o.erase (o.begin());
                    }
                }
            }
            for (auto& note : song.notes)   // notes left on end with their track
                if (note.end < 0)
                    note.end = tick > note.start ? tick : note.start + 1;
        }

        std::stable_sort (song.notes.begin(), song.notes.end(), [] (const MidiNote& a, const MidiNote& b)
        {
            return a.start != b.start ? a.start < b.start : a.pitch < b.pitch;
        });
        return true;
    }
}
