// Phase 4: styles, the pattern generator and playback (core/pattern). PLAN.md G7: at least 10
// styles, each keeping its invariants; the same seed gives the same pattern.
//
//   Styles       the factory styles parse; bad style text is refused with a reason
//   Invariants   over 200 seeds and a spread of Density and Complexity, every anchor (X) plays on
//                its lane's pads in every bar; and by name: House's kick on every beat, Rock's snare
//                on 2 and 4, Reggae's one drop (the kick on 3, never on 1)
//   Generator    same seed, same pattern; Density only adds hits; Complexity only adds them (fills
//                off); fills only where asked, with a crash after; locked lanes kept; lanes with no
//                pad of their role fall back (rim → snare)
//   Playback     every hit once, on the first sample at or after its time, at any block size, with
//                swing and humanise; swing's offset; hits across the loop point; rolls
//   MIDI         the drag-out's notes; Handoff
#include "TestUtil.h"
#include "core/pattern/FactoryStyles.h"
#include "core/pattern/PatternGenerator.h"
#include "core/util/Handoff.h"
#include <set>

using namespace drumchimp;
using testutil::check;

namespace
{
    std::vector<Style> styles()
    {
        std::vector<Style> out;
        for (const char* text : factoryStyles)
        {
            Style s;
            std::string error;
            if (parseStyle (text, s, error))
                out.push_back (s);
            else
                std::cout << "        " << error << "\n";
        }
        return out;
    }

    const Style& named (const std::vector<Style>& all, const std::string& name)
    {
        for (const auto& s : all)
            if (s.name == name)
                return s;
        return all.front();
    }

    bool hitAt (const Pattern& p, const std::vector<int>& pads, int step)
    {
        for (const int pad : pads)
            if (p.lanes[(size_t) pad][(size_t) step].velocity > 0)
                return true;
        return false;
    }

    void testStyles (const std::vector<Style>& all)
    {
        std::cout << "Styles\n";
        check (all.size() == factoryStyles.size() && all.size() == 24, std::to_string (all.size()) + " factory styles parse");
        std::set<std::string> names;
        for (const auto& s : all)
            names.insert (s.name);
        check (names.size() == all.size(), "with different names");

        Style s;
        std::string error;
        check (! parseStyle ("name = A\nkick = X..q", s, error) && error.find ("isn't a step") != std::string::npos, "a bad step is refused: " + error);
        check (! parseStyle ("name = A\nkick = X...X...X...", s, error) && error.find ("whole bars") != std::string::npos, "a part bar is refused: " + error);
        check (! parseStyle ("name = A\ncowbell = X...X...X...X...", s, error) && error.find ("unknown key") != std::string::npos, "an unknown lane is refused: " + error);
        check (! parseStyle ("kick = X...X...X...X...", s, error) && error.find ("no name") != std::string::npos, "a style needs a name");
        check (parseStyle ("name = Two bars\nkick = X... .... X... .... | X... X... X... X...\n", s, error) && s.lanes[0].steps.size() == 32,
               "a lane may be several bars");
        check (parseStyle ("name = W\nmeter = 3/4\ngrid = 12\nkick = X... .... ....", s, error) && s.numerator == 3 && s.beatsPerBar() == 3.0
                   && s.stepsPerBeat() == 4, "3/4: 3 beats of 4 sixteenths");
        check (parseStyle ("name = S\nmeter = 6/8\ngrid = 12\nkick = X..... X.....", s, error) && s.denominator == 8 && s.beatsPerBar() == 3.0
                   && s.stepsPerBeat() == 6, "6/8: felt in 2 dotted beats of 6 sixteenths");
        check (! parseStyle ("name = W\nmeter = 3/4\nkick = X... .... .... ....", s, error) && error.find ("grid of 12") != std::string::npos,
               "3/4 on a 16 grid is refused: " + error);
        check (! parseStyle ("name = W\nmeter = 5/4\ngrid = 12\nkick = X... .... ....", s, error) && error.find ("meter is") != std::string::npos,
               "an unknown meter is refused");
    }

    void testInvariants (const std::vector<Style>& all)
    {
        std::cout << "Invariants (200 seeds × Density 0, 0.5, 1 × Complexity 0, 0.5, 1, fills off)\n";
        for (const auto& style : all)
        {
            bool ok = true;
            for (int seed = 1; seed <= 200 && ok; ++seed)
                for (const float d : { 0.0f, 0.5f, 1.0f })
                    for (const float c : { 0.0f, 0.5f, 1.0f })
                    {
                        GeneratorSettings g;
                        g.seed = (uint64_t) seed;
                        g.density = d;
                        g.complexity = c;
                        g.fillEvery = 0;
                        const auto p = generatePattern (style, g, defaultPadRoles);
                        for (const auto& lane : style.lanes)
                        {
                            const auto pads = generator::padsFor (lane.role, defaultPadRoles);
                            if (pads.empty())
                                continue;
                            const int laneBars = (int) lane.steps.size() / style.grid;
                            for (int bar = 0; bar < p.bars; ++bar)
                                for (int s = 0; s < style.grid; ++s)
                                    if (lane.steps[(size_t) ((bar % laneBars) * style.grid + s)] == 'X')
                                        ok = ok && hitAt (p, pads, bar * style.grid + s);
                        }
                    }
            check (ok, style.name + ": every anchor plays");
        }

        GeneratorSettings g;
        g.fillEvery = 0;
        g.density = 0.0f;
        bool house = true, rock = true, reggae = true;
        for (int seed = 1; seed <= 200; ++seed)
        {
            g.seed = (uint64_t) seed;
            g.complexity = (float) (seed % 3) * 0.5f;
            const auto h = generatePattern (named (all, "House"), g, defaultPadRoles);
            const auto r = generatePattern (named (all, "Rock"), g, defaultPadRoles);
            const auto one = generatePattern (named (all, "Reggae"), g, defaultPadRoles);
            for (int bar = 0; bar < 4; ++bar)
            {
                for (const int beat : { 0, 4, 8, 12 })
                    house = house && h.lanes[0][(size_t) (bar * 16 + beat)].velocity > 0;
                rock = rock && r.lanes[1][(size_t) (bar * 16 + 4)].velocity > 0 && r.lanes[1][(size_t) (bar * 16 + 12)].velocity > 0;
                reggae = reggae && one.lanes[0][(size_t) (bar * 16 + 8)].velocity > 0 && one.lanes[0][(size_t) (bar * 16)].velocity == 0;
            }
        }
        check (house, "House: the kick on every beat");

        // The written groove, exactly: at Complexity 0, Density 50%, no humanise and no fills,
        // each pad plays exactly the X, x and r steps of the lanes that land on it (and, for a
        // crash = one style, the crash on the first beat).
        for (const auto& style : all)
        {
            GeneratorSettings exact;
            exact.complexity = 0.0f;
            exact.density = 0.5f;
            exact.humanise = 0.0f;
            exact.fillEvery = 0;
            exact.bars = 2;
            bool same = true;
            for (int seed = 1; seed <= 20; ++seed)
            {
                exact.seed = (uint64_t) seed;
                const auto p = generatePattern (style, exact, defaultPadRoles);
                std::array<std::array<bool, maxPatternSteps>, numPads> want {};
                for (const auto& lane : style.lanes)
                {
                    const int laneBars = (int) lane.steps.size() / style.grid;
                    for (const int pad : generator::padsFor (lane.role, defaultPadRoles))
                        for (int s = 0; s < p.steps(); ++s)
                        {
                            const char c = lane.steps[(size_t) (((s / style.grid) % laneBars) * style.grid + s % style.grid)];
                            want[(size_t) pad][(size_t) s] = want[(size_t) pad][(size_t) s] || c == 'X' || c == 'x' || c == 'r';
                        }
                }
                if (style.crashOnOne)                  // and the crash on the pattern's first beat
                    for (const int pad : generator::padsFor ("crash", defaultPadRoles))
                        want[(size_t) pad][0] = true;
                for (int pad = 0; pad < numPads; ++pad)
                    for (int s = 0; s < p.steps(); ++s)
                        same = same && (p.lanes[(size_t) pad][(size_t) s].velocity > 0) == want[(size_t) pad][(size_t) s];
            }
            check (same, style.name + ": at Complexity 0 it plays its groove exactly as written");
        }

        GeneratorSettings g2;
        g2.density = 0.5f;
        g2.fillEvery = 0;
        bool disco = true, dembow = true, jazz = true, bossa = true;
        for (int seed = 1; seed <= 100; ++seed)
        {
            g2.seed = (uint64_t) seed;
            g2.complexity = (float) (seed % 3) * 0.5f;
            const auto d = generatePattern (named (all, "Disco"), g2, defaultPadRoles);
            const auto dem = generatePattern (named (all, "Dembow"), g2, defaultPadRoles);
            const auto j = generatePattern (named (all, "Jazz"), g2, defaultPadRoles);
            const auto b = generatePattern (named (all, "Bossa Nova"), g2, defaultPadRoles);
            for (int bar = 0; bar < 4; ++bar)
            {
                for (const int s : { 2, 6, 10, 14 })
                    disco = disco && d.lanes[4][(size_t) (bar * 16 + s)].velocity > 0;
                for (const int s : { 3, 6, 11, 14 })
                    dembow = dembow && dem.lanes[1][(size_t) (bar * 16 + s)].velocity > 0;
                for (const int s : { 0, 3, 6, 9 })
                    jazz = jazz && j.lanes[10][(size_t) (bar * 12 + s)].velocity > 0;    // the ride, on the Ride pad
                const std::vector<int> clave = bar % 2 == 0 ? std::vector<int> { 0, 3, 6, 10, 13 } : std::vector<int> { 2, 5, 10 };
                for (const int s : clave)
                    bossa = bossa && b.lanes[8][(size_t) (bar * 16 + s)].velocity > 0;   // the rim, on the Rim pad
            }
            jazz = jazz && j.stepsPerBar == 12 && j.steps() == 48;
        }
        check (disco, "Disco: open hats on every off-beat");
        check (dembow, "Dembow: the snare on 3, 6, 11 and 14");
        check (jazz, "Jazz: a 12-step (triplet) grid, the ride on every beat");
        check (bossa, "Bossa Nova: the two-bar clave on the rim");

        // Meters.
        GeneratorSettings m;
        m.density = 0.5f;
        m.fillEvery = 0;
        m.bars = 4;
        bool waltz = true, ballad = true, afro = true;
        for (int seed = 1; seed <= 50; ++seed)
        {
            m.seed = (uint64_t) seed;
            const auto w = generatePattern (named (all, "Waltz"), m, defaultPadRoles);
            const auto b6 = generatePattern (named (all, "6/8 Ballad"), m, defaultPadRoles);
            const auto a6 = generatePattern (named (all, "Afro 6/8"), m, defaultPadRoles);
            waltz = waltz && w.numerator == 3 && w.steps() == 48 && w.lengthBeats() == 12.0 && w.stepsPerBeat == 4;
            ballad = ballad && b6.numerator == 6 && b6.denominator == 8 && b6.stepsPerBeat == 6 && b6.lengthBeats() == 12.0;
            for (int bar = 0; bar < 4; ++bar)
            {
                waltz = waltz && w.lanes[0][(size_t) (bar * 12)].velocity > 0 && w.lanes[0][(size_t) (bar * 12 + 4)].velocity == 0
                        && w.lanes[1][(size_t) (bar * 12 + 4)].velocity > 0 && w.lanes[1][(size_t) (bar * 12 + 8)].velocity > 0;
                ballad = ballad && b6.lanes[0][(size_t) (bar * 12)].velocity > 0 && b6.lanes[1][(size_t) (bar * 12 + 6)].velocity > 0;
                afro = afro && a6.lanes[0][(size_t) (bar * 12)].velocity > 0 && a6.lanes[0][(size_t) (bar * 12 + 6)].velocity > 0;
            }
        }
        check (waltz, "Waltz: 3/4, 12 sixteenths a bar, oom on 1, pah-pah on 2 and 3");
        check (ballad, "6/8 Ballad: 6/8 in two dotted beats, the kick on 1, the snare on 4");
        check (afro, "Afro 6/8: the kick on both dotted beats");
        GeneratorSettings wf = m;
        wf.fillEvery = 4;
        wf.complexity = 0.3f;
        const auto waltzFill = generatePattern (named (all, "Waltz"), wf, defaultPadRoles), waltzPlain = generatePattern (named (all, "Waltz"), m, defaultPadRoles);
        bool lastBeat = true;
        for (int pad = 1; pad < numPads; ++pad)
            for (int s = 0; s < 48; ++s)
                if (waltzFill.lanes[(size_t) pad][(size_t) s].velocity != waltzPlain.lanes[(size_t) pad][(size_t) s].velocity)
                    lastBeat = lastBeat && (s >= 36 + 8 || (s == 0 && pad == 7));
        check (lastBeat, "a 3/4 fill takes the last beat of the bar (4 sixteenths)");
        check (rock, "Rock: the snare on 2 and 4");
        check (reggae, "Reggae: one drop, the kick on 3 and never on 1");
    }

    // Every hit in `less` is in `more`.
    bool within (const Pattern& less, const Pattern& more)
    {
        for (int pad = 0; pad < numPads; ++pad)
            for (int s = 0; s < less.steps(); ++s)
                if (less.lanes[(size_t) pad][(size_t) s].velocity > 0 && more.lanes[(size_t) pad][(size_t) s].velocity == 0)
                    return false;
        return true;
    }

    int totalHits (const Pattern& p)
    {
        int n = 0;
        for (int pad = 0; pad < numPads; ++pad)
            n += p.hits (pad);
        return n;
    }

    void testGenerator (const std::vector<Style>& all)
    {
        std::cout << "Generator\n";
        const auto& rock = named (all, "Rock");
        GeneratorSettings g;
        g.seed = 42;
        check (generatePattern (rock, g, defaultPadRoles) == generatePattern (rock, g, defaultPadRoles), "the same seed gives the same pattern");
        GeneratorSettings other = g;
        other.seed = 43;
        other.complexity = 0.8f;
        GeneratorSettings first = g;
        first.complexity = 0.8f;
        check (! (generatePattern (rock, first, defaultPadRoles) == generatePattern (rock, other, defaultPadRoles)), "another seed, another pattern");

        bool densityAdds = true, complexityAdds = true, grows = true;
        for (const auto& style : all)
            for (int seed = 1; seed <= 100; ++seed)
            {
                GeneratorSettings a;
                a.seed = (uint64_t) seed;
                a.complexity = 0.5f;
                Pattern previous;
                for (int step = 0; step <= 10; ++step)
                {
                    a.density = (float) step / 10.0f;
                    const auto p = generatePattern (style, a, defaultPadRoles);
                    if (step > 0)
                        densityAdds = densityAdds && within (previous, p);
                    previous = p;
                }
                a.fillEvery = 0;
                a.density = 0.5f;
                for (int step = 0; step <= 10; ++step)
                {
                    a.complexity = (float) step / 10.0f;
                    const auto p = generatePattern (style, a, defaultPadRoles);
                    if (step > 0)
                        complexityAdds = complexityAdds && within (previous, p);
                    previous = p;
                }
                GeneratorSettings sparse = a, busy = a;
                sparse.density = 0.0f;
                busy.density = 1.0f;
                busy.complexity = sparse.complexity = 1.0f;
                grows = grows && totalHits (generatePattern (style, busy, defaultPadRoles)) > totalHits (generatePattern (style, sparse, defaultPadRoles));
            }
        check (densityAdds, "more Density only adds hits (10 styles × 100 seeds × 11 steps)");
        check (complexityAdds, "more Complexity only adds hits (fills off)");
        check (grows, "and Density 1 has more than Density 0");

        // Fills: only in the 4th bar, from the last beat; a crash on the next one.
        GeneratorSettings plain;
        plain.seed = 9;
        plain.fillEvery = 0;
        plain.complexity = 0.3f;
        GeneratorSettings filled = plain;
        filled.fillEvery = 4;
        const auto noFill = generatePattern (rock, plain, defaultPadRoles), fill = generatePattern (rock, filled, defaultPadRoles);
        bool onlyThere = true, tomsThere = false;
        for (int pad = 0; pad < numPads; ++pad)
            for (int s = 0; s < 64; ++s)
            {
                const bool differs = noFill.lanes[(size_t) pad][(size_t) s].velocity != fill.lanes[(size_t) pad][(size_t) s].velocity;
                const bool allowed = s >= 48 + 12 || (s == 0 && pad == 7);
                onlyThere = onlyThere && (! differs || allowed);
                tomsThere = tomsThere || ((pad == 5 || pad == 6) && s >= 60 && fill.lanes[(size_t) pad][(size_t) s].velocity > 0);
            }
        check (onlyThere, "Fill every 4 bars: only the last beat of bar 4 changes (and the crash after it)");
        check (tomsThere, "Rock's fill is on the toms");
        check (fill.lanes[7][0].velocity > 0, "and the crash lands on the loop's one");
        GeneratorSettings twoBars = filled;
        twoBars.bars = 2;
        bool noTomsIn2 = true;
        const auto two = generatePattern (rock, twoBars, defaultPadRoles);
        for (int s = 0; s < two.steps(); ++s)
            noTomsIn2 = noTomsIn2 && two.lanes[5][(size_t) s].velocity == 0 && two.lanes[6][(size_t) s].velocity == 0;
        check (noTomsIn2, "a 2-bar pattern has no 4-bar fill");

        // Lane locks.
        std::array<bool, numPads> locks {};
        locks[1] = true;
        GeneratorSettings next = g;
        next.seed = 777;
        next.complexity = 0.9f;
        const auto kept = generatePattern (rock, g, defaultPadRoles);
        const auto locked = generatePattern (rock, next, defaultPadRoles, &kept, &locks);
        const auto free = generatePattern (rock, next, defaultPadRoles);
        check (locked.lanes[1] == kept.lanes[1] && locked.lanes[0] == free.lanes[0], "a locked lane is kept; the others are new");

        // Fallbacks: the default kit has no rim; Reggae's rim plays on the snare.
        const auto reggae = generatePattern (named (all, "Reggae"), g, defaultPadRoles);
        check (reggae.lanes[8][8].velocity > 0 && reggae.lanes[1][8].velocity == 0, "a rim lane plays the Rim pad (pad 9)");
        auto noRim = defaultPadRoles;
        noRim[8] = PadRole::perc;
        const auto withoutRim = generatePattern (named (all, "Reggae"), g, noRim);
        check (withoutRim.lanes[1][8].velocity > 0, "with no Rim pad, the snare plays it");
        auto noRide = defaultPadRoles;
        noRide[10] = PadRole::perc;
        const auto jazzNoRide = generatePattern (named (all, "Jazz"), g, noRide);
        check (jazzNoRide.lanes[3][0].velocity > 0, "with no Ride pad, the closed hats play the ride");

        GeneratorSettings eight = g;
        eight.bars = 8;
        check (generatePattern (rock, eight, defaultPadRoles).steps() == 128, "8 bars: 128 steps");
    }

    void testPlayback (const std::vector<Style>& all)
    {
        std::cout << "Playback\n";
        GeneratorSettings g;
        g.seed = 5;
        g.humanise = 1.0f;
        g.complexity = 0.8f;
        const auto p = generatePattern (named (all, "Trap"), g, defaultPadRoles);
        const double bpm = 140.0, rate = 44100.0, bps = bpm / 60.0 / rate, swing = 0.6;

        // Every note-on in two loops, from one call, as (beat, pad).
        std::vector<std::pair<double, int>> expected;
        pattern::forEachEvent (p, 0.0, 2 * p.lengthBeats(), swing, bpm, [&] (double beat, int pad, uint8_t, bool on)
        {
            if (on)
                expected.push_back ({ beat, pad });
        });
        int rolls = 0;
        for (int pad = 0; pad < numPads; ++pad)
            for (int s = 0; s < p.steps(); ++s)
                rolls += p.lanes[(size_t) pad][(size_t) s].repeats > 1 ? 1 : 0;
        check (rolls > 0 && expected.size() > (size_t) totalHits (p), "Trap at Complexity 0.8 rolls its hats (" + std::to_string (rolls) + " rolls)");

        const auto total = (int64_t) std::ceil (2 * p.lengthBeats() / bps);
        for (const int block : { 1, 64, 441, 512, 4096 })
        {
            std::vector<std::pair<int64_t, int>> got;
            for (int64_t start = 0; start < total; start += block)
                pattern::forEachEventInBlock (p, (double) start * bps, bps, block, swing, bpm, [&] (int sample, int pad, uint8_t, bool on)
                {
                    if (on && start + sample < total)
                        got.push_back ({ start + sample, pad });
                });
            bool exact = true;
            std::multiset<std::pair<int64_t, int>> want;
            for (const auto& [beat, pad] : expected)
                if (beat >= 0.0)
                    want.insert ({ (int64_t) std::ceil (beat / bps - 1.0e-6), pad });
            std::multiset<std::pair<int64_t, int>> have (got.begin(), got.end());
            exact = want == have;
            check (exact, "block size " + std::to_string (block) + ": every hit once, on the first sample at or after it ("
                              + std::to_string (have.size()) + " of " + std::to_string (want.size()) + ")");
        }

        Pattern one;
        one.stepsPerBar = 16;
        one.lanes[0][1].velocity = 100;
        double at = -1.0;
        pattern::forEachEvent (one, 0.0, 4.0, 0.66, 120.0, [&] (double beat, int, uint8_t, bool on) { if (on) at = beat; });
        check (std::abs (at - (0.25 + 0.32 * 0.25)) < 1.0e-9, "Swing 66%: the second sixteenth moves a third of a step later");

        Pattern triplets;
        triplets.stepsPerBar = 12;
        triplets.lanes[0][1].velocity = 100;
        triplets.lanes[0][5].velocity = 100;
        std::vector<double> tripletBeats;
        pattern::forEachEvent (triplets, 0.0, 4.0, 0.66, 120.0, [&] (double beat, int, uint8_t, bool on) { if (on) tripletBeats.push_back (beat); });
        check (tripletBeats.size() == 2 && std::abs (tripletBeats[0] - 1.0 / 3.0) < 1.0e-9 && std::abs (tripletBeats[1] - 5.0 / 3.0) < 1.0e-9,
               "a 12-step grid plays triplet eighths, and Swing leaves it alone");

        Pattern three;
        three.stepsPerBar = 12;
        three.numerator = 3;
        three.stepsPerBeat = 4;
        three.bars = 1;
        three.lanes[0][0].velocity = 100;
        three.lanes[0][1].velocity = 100;
        std::vector<double> threeBeats;
        pattern::forEachEvent (three, 0.0, 6.0, 0.66, 120.0, [&] (double beat, int, uint8_t, bool on) { if (on) threeBeats.push_back (beat); });
        std::sort (threeBeats.begin(), threeBeats.end());
        check (threeBeats.size() == 4 && std::abs (threeBeats[2] - 3.0) < 1.0e-9 && std::abs (threeBeats[1] - (0.25 + 0.32 * 0.25)) < 1.0e-9,
               "3/4 loops every 3 beats, and its sixteenths swing");
        const auto song34 = pattern::toMidiSong (three, 2, 120.0, 0.5, NoteMap::gm, "waltz");
        const auto bytes34 = writeMidiFile (song34);
        MidiSong back34;
        std::string err34;
        check (readMidiFile (bytes34.data(), bytes34.size(), back34, err34) && back34.numerator == 3 && back34.denominator == 4
                   && back34.notes.size() == 4 && back34.notes[2].start == 3 * 960,
               "MIDI: a 3/4 pattern's file says 3/4, its second loop starts on beat 3");

        one.lanes[0][1] = {};
        one.lanes[0][0] = { 100, 1, -10.0f };     // 10 ms early, at 120 BPM: 0.02 beats before the loop
        std::vector<double> early;
        pattern::forEachEvent (one, 3.9, 4.1, 0.5, 120.0, [&] (double beat, int, uint8_t, bool on) { if (on) early.push_back (beat); });
        check (early.size() == 1 && std::abs (early[0] - 3.98) < 1.0e-9, "a hit pushed early plays before the loop point, not lost");

        const auto song = pattern::toMidiSong (one, 2, 120.0, 0.5, NoteMap::gm, "test");
        check (song.notes.size() == 2 && song.notes[0].start == 0 && song.notes[1].start == (int64_t) std::llround (3.98 * 960),
               "MIDI: an early first hit starts at 0; the next loop's is early");

        Pattern roll;
        roll.lanes[3][0] = { 100, 3, 0.0f };
        const auto rolled = pattern::toMidiSong (roll, 1, 120.0, 0.5, NoteMap::gm, "roll");
        check (rolled.notes.size() == 3 && rolled.notes[1].start == 80 && rolled.notes[2].start == 160 && rolled.notes[0].pitch == 42,
               "MIDI: a roll of 3 is three notes in the step, on the closed hat's note");
    }

    void testHandoff()
    {
        std::cout << "Handoff\n";
        Handoff<Pattern> h;
        check (h.current() == nullptr, "nothing until something is published");
        auto first = std::make_unique<Pattern>();
        first->bars = 2;
        h.publish (std::move (first));
        check (h.current() != nullptr && h.current()->bars == 2, "the audio side takes it");
        auto second = std::make_unique<Pattern>();
        second->bars = 3;
        h.publish (std::move (second));
        auto third = std::make_unique<Pattern>();
        third->bars = 4;
        h.publish (std::move (third));             // replaces the second before it's taken
        check (h.current()->bars == 4, "the latest wins");
        auto fourth = std::make_unique<Pattern>();
        fourth->bars = 5;
        h.publish (std::move (fourth));
        check (h.current()->bars == 4, "until the old one is collected, the next waits");
        h.collectGarbage();
        check (h.current()->bars == 5, "then it's taken");
    }
}

int main()
{
    const auto all = styles();
    testStyles (all);
    testInvariants (all);
    testGenerator (all);
    testPlayback (all);
    testHandoff();
    return testutil::finish ("test_pattern");
}
