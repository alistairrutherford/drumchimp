// Phase 3: kits (core/kit/KitBuilder.h) on a made-up library with two complete families (808
// and Acoustic) and a crowd of unrelated samples: every pad gets its role, Matched kits share a
// family and Wild ones mostly don't, locks hold and pull the family their way, toms take their
// range, nothing repeats, a re-roll changes the pad, the same seed gives the same kit, undo and
// redo, and finding a missing sample again.
#include "TestUtil.h"
#include "core/kit/KitBuilder.h"

using namespace drumchimp;
using testutil::check;

namespace
{
    SampleInfo make (const std::string& root, const std::string& name, Role role, std::vector<std::string> tags, TomRange range = TomRange::unknown, int64_t size = 1000)
    {
        SampleInfo s;
        s.root = root;
        s.relative = "Drums/" + name + ".wav";
        s.path = "/lib/" + root + "/" + s.relative;
        s.seconds = 0.5;
        s.size = size;
        s.classification.role = role;
        s.classification.confidence = 0.95f;
        s.classification.tags = std::move (tags);
        s.classification.tomRange = range;
        return s;
    }

    LibraryIndex library()
    {
        std::vector<SampleInfo> all;
        for (const char* fam : { "808", "acoustic" })
        {
            const std::string f = fam;
            for (int v = 1; v <= 3; ++v)
            {
                const auto n = " " + f + " " + std::to_string (v);
                all.push_back (make ("Pack A", "Kick" + n, Role::kick, { f }));
                all.push_back (make ("Pack A", "Snare" + n, Role::snare, { f }));
                all.push_back (make ("Pack A", "Clap" + n, Role::clap, { f }));
                all.push_back (make ("Pack A", "Hat Closed" + n, Role::closedHat, { f }));
                all.push_back (make ("Pack A", "Hat Open" + n, Role::openHat, { f }));
                all.push_back (make ("Pack A", "Tom Lo" + n, Role::tom, { f }, TomRange::low));
                all.push_back (make ("Pack A", "Tom Hi" + n, Role::tom, { f }, TomRange::high));
                all.push_back (make ("Pack A", "Crash" + n, Role::crash, { f }));
            }
        }
        // A crowd with nothing in common: one-off tags in another pack.
        const std::array<Role, 7> roles { Role::kick, Role::snare, Role::clap, Role::closedHat, Role::openHat, Role::tom, Role::crash };
        for (int i = 0; i < 140; ++i)
            all.push_back (make ("Pack B", "Other " + std::to_string (i), roles[(size_t) i % roles.size()], { "odd" + std::to_string (i) }));
        all.push_back (make ("Pack B", "Shaker 1", Role::perc, { "odd" }));
        return LibraryIndex (std::move (all));
    }

    const SampleInfo* info (const LibraryIndex& index, const std::string& path) { return index.find (path); }

    bool hasTag (const SampleInfo* s, const std::string& t)
    {
        return s != nullptr && std::find (s->classification.tags.begin(), s->classification.tags.end(), t) != s->classification.tags.end();
    }

    void testBuild()
    {
        std::cout << "Building kits\n";
        const auto index = library();
        KitRequest request;

        bool roles = true, unique = true;
        int matchedShared = 0, wildShared = 0;
        const int runs = 200;
        for (int seed = 1; seed <= runs; ++seed)
        {
            request.seed = (uint64_t) seed;
            for (const float character : { 0.0f, 1.0f })
            {
                request.character = character;
                const auto kit = buildKit (index, request);
                std::set<std::string> seen;
                for (int pad = 0; pad < numPads; ++pad)
                {
                    const auto* s = info (index, kit.paths[(size_t) pad]);
                    roles = roles && s != nullptr && s->classification.role == libraryRole (request.roles[(size_t) pad]);
                    unique = unique && seen.insert (kit.paths[(size_t) pad]).second;
                }
                bool shared = false;
                for (const char* fam : { "808", "acoustic" })
                {
                    bool all = true;
                    for (const auto& p : kit.paths)
                        all = all && hasTag (info (index, p), fam);
                    shared = shared || all;
                }
                (character == 0.0f ? matchedShared : wildShared) += shared ? 1 : 0;
            }
        }
        check (roles, "every pad gets a sample of its role (200 seeds, Matched and Wild)");
        check (unique, "no sample twice in a kit");
        check (matchedShared == runs, "Matched: every kit is one family (" + std::to_string (matchedShared) + "/200)");
        check (wildShared < runs / 10, "Wild: almost none are (" + std::to_string (wildShared) + "/200)");

        request.character = 0.0f;
        request.seed = 7;
        const auto a = buildKit (index, request), b = buildKit (index, request);
        check (a.paths == b.paths && ! a.family.empty(), "the same seed gives the same kit (family " + a.family + ")");

        const auto low = info (index, a.paths[5]), high = info (index, a.paths[6]);
        check (low != nullptr && low->classification.tomRange == TomRange::low && high != nullptr && high->classification.tomRange == TomRange::high,
               "Low tom and High tom take their ranges");
    }

    void testLocksAndDice()
    {
        std::cout << "Locks, dice and missing roles\n";
        const auto index = library();
        KitRequest request;
        request.character = 0.0f;
        request.seed = 3;
        request.current = buildKit (index, request).paths;

        // Lock the kick and snare to an 808 pair, re-roll the rest many times.
        request.current[0] = "/lib/Pack A/Drums/Kick 808 1.wav";
        request.current[1] = "/lib/Pack A/Drums/Snare 808 2.wav";
        request.locked[0] = request.locked[1] = true;
        bool kept = true;
        int towardLocked = 0;
        for (int seed = 1; seed <= 100; ++seed)
        {
            request.seed = (uint64_t) seed;
            const auto kit = buildKit (index, request);
            kept = kept && kit.paths[0] == request.current[0] && kit.paths[1] == request.current[1];
            towardLocked += kit.family == "808" ? 1 : 0;
        }
        check (kept, "locked pads never change");
        check (towardLocked > 60, "locked 808s pull Matched kits to 808 (" + std::to_string (towardLocked) + "/100)");

        KitRequest styled;
        styled.character = 0.0f;
        styled.preferredFamilies = { "acoustic" };
        int towardStyle = 0;
        for (int seed = 1; seed <= 100; ++seed)
        {
            styled.seed = (uint64_t) seed;
            towardStyle += buildKit (index, styled).family == "acoustic" ? 1 : 0;
        }
        check (towardStyle > 60, "a style's families pull Matched kits to them (" + std::to_string (towardStyle) + "/100)");

        // One pad's dice: everything else locked.
        KitRequest dice = request;
        dice.locked.fill (true);
        dice.locked[3] = false;
        bool onlyThat = true, changed = true;
        for (int seed = 1; seed <= 50; ++seed)
        {
            dice.seed = (uint64_t) seed;
            const auto kit = buildKit (index, dice);
            for (int pad = 0; pad < numPads; ++pad)
                if (pad != 3)
                    onlyThat = onlyThat && kit.paths[(size_t) pad] == dice.current[(size_t) pad];
            changed = changed && kit.paths[3] != dice.current[3];
        }
        check (onlyThat, "a pad's dice changes only that pad");
        check (changed, "and always to a different sample");

        // A family with a single low tom: re-rolling that pad still changes it (to another family's).
        std::vector<SampleInfo> few {
            make ("P", "Kick lone 1", Role::kick, { "lone" }), make ("P", "Snare lone 1", Role::snare, { "lone" }),
            make ("P", "Tom Lo lone", Role::tom, { "lone" }, TomRange::low), make ("P", "Tom Lo other", Role::tom, { "other" }, TomRange::low),
            make ("P", "Tom Lo third", Role::tom, { "third" }, TomRange::low),
        };
        const LibraryIndex small (std::move (few));
        KitRequest lone;
        lone.character = 0.0f;
        lone.roles = { PadRole::kick, PadRole::snare, PadRole::lowTom, PadRole::perc, PadRole::perc, PadRole::perc, PadRole::perc, PadRole::perc };
        lone.locked.fill (true);
        lone.locked[2] = false;
        lone.current[0] = "/lib/P/Drums/Kick lone 1.wav";
        lone.current[1] = "/lib/P/Drums/Snare lone 1.wav";
        lone.current[2] = "/lib/P/Drums/Tom Lo lone.wav";
        bool moves = true;
        for (int seed = 1; seed <= 50; ++seed)
        {
            lone.seed = (uint64_t) seed;
            moves = moves && buildKit (small, lone).paths[2] != lone.current[2];
        }
        check (moves, "a dice on a family's only low tom still changes the pad");

        KitRequest ride;
        ride.roles[7] = PadRole::ride;              // the library has no rides
        ride.current[7] = "/somewhere/Ride.wav";
        const auto kit = buildKit (index, ride);
        check (kit.paths[7] == "/somewhere/Ride.wav", "a role with no samples keeps the pad's sample");
        ride.roles[7] = PadRole::perc;
        check (buildKit (index, ride).paths[7] == "/lib/Pack B/Drums/Shaker 1.wav", "a pad turned to Perc gets the perc");

        KitRequest allLocked;
        allLocked.locked.fill (true);
        allLocked.current[0] = "x";
        check (buildKit (index, allLocked).paths == allLocked.current, "everything locked: nothing changes");
    }

    void testHistory()
    {
        std::cout << "Kit history\n";
        KitHistory h;
        const auto kit = [] (const char* name) { KitPaths k; k[0] = name; return k; };
        check (! h.canUndo() && ! h.canRedo(), "empty: no undo or redo");
        h.record (kit ("a"), kit ("b"));
        h.record (kit ("b"), kit ("c"));
        check (h.canUndo() && h.undo()[0] == "b" && h.undo()[0] == "a" && ! h.canUndo(), "undo walks back to the first kit");
        check (h.redo()[0] == "b" && h.canRedo(), "redo forward");
        h.record (kit ("b"), kit ("d"));
        check (! h.canRedo() && h.undo()[0] == "b" && h.redo()[0] == "d", "a new change drops what could be redone");
        h.record (kit ("x"), kit ("y"));            // changed outside the history in between
        check (h.undo()[0] == "x" && h.undo()[0] == "d", "a change from a kit it didn't see records that kit too");
        for (int i = 0; i < 40; ++i)
            h.record (kit (std::to_string (i).c_str()), kit (std::to_string (i + 1).c_str()));
        int steps = 0;
        while (h.canUndo()) { h.undo(); ++steps; }
        check (steps == (int) KitHistory::maxKits - 1, "at most 20 kits (" + std::to_string (steps) + " undos)");
    }

    void testRelocate()
    {
        std::cout << "Finding missing samples\n";
        std::vector<SampleInfo> all {
            make ("Core Library", "Kick 909 2", Role::kick, {}, TomRange::unknown, 5000),
            make ("Pack C", "Snare Dup", Role::snare, {}, TomRange::unknown, 10),
            make ("Pack D", "Snare Dup", Role::snare, {}, TomRange::unknown, 20),
            make ("Pack E", "Clap Shared", Role::clap, {}, TomRange::unknown, 30),
            make ("Pack F", "Clap Shared", Role::clap, {}, TomRange::unknown, 30),
        };
        const LibraryIndex index (std::move (all));
        const auto* moved = relocate (index, "/Applications/Ableton Live 12 Suite.app/Core Library/Drums/Kick 909 2.wav", 5000);
        check (moved != nullptr && moved->root == "Core Library", "a Core Library sample after Live's app was renamed");
        const auto* bySize = relocate (index, "/old/place/Drums/Snare Dup.wav", 20);
        check (bySize != nullptr && bySize->root == "Pack D", "two with the same name: the one with the saved size");
        check (relocate (index, "/old/place/Drums/Snare Dup.wav", 0) == nullptr, "two equally good: no guess");
        check (relocate (index, "/old/Other.wav", 5000) == nullptr, "a name the library hasn't got: not found");
        check (relocate (index, "/old/Drums/Clap Shared.wav", 30) != nullptr, "two copies of the same file (same name and saved size): either will do");
        check (relocate (index, "/old/Drums/Clap Shared.wav", 0) == nullptr, "but with no saved size, still no guess");
    }
}

int main()
{
    testBuild();
    testLocksAndDice();
    testHistory();
    testRelocate();
    return testutil::finish ("test_kit");
}
