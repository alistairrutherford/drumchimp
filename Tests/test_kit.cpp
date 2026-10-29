// Phase 3: kits (core/kit/KitBuilder.h) on a made-up library with two complete families (808
// and Acoustic: every role of the 12 default pads) and a crowd of unrelated samples: every pad gets its role, Matched kits share a
// family and Wild ones mostly don't, locks hold and pull the family their way, toms take their
// range, nothing repeats, a re-roll changes the pad, the same seed gives the same kit, undo and
// redo, and finding a missing sample again.
#include "TestUtil.h"
#include "core/kit/DrumRack.h"

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
                all.push_back (make ("Pack A", "Rim" + n, Role::rim, { f }));
                all.push_back (make ("Pack A", "Tom Mid" + n, Role::tom, { f }, TomRange::mid));
                all.push_back (make ("Pack A", "Ride" + n, Role::ride, { f }));
                all.push_back (make ("Pack A", "Perc" + n, Role::perc, { f }));
            }
        }
        // A crowd with nothing in common: one-off tags in another pack.
        const std::array<Role, 10> roles { Role::kick, Role::snare, Role::clap, Role::closedHat, Role::openHat, Role::tom, Role::crash,
                                           Role::rim, Role::ride, Role::perc };
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

        KitRequest ride;                            // the small library has no rides
        ride.current[10] = "/somewhere/Ride.wav";
        check (buildKit (small, ride).paths[10] == "/somewhere/Ride.wav", "a role with no samples keeps the pad's sample");
        KitRequest perc;
        perc.roles[7] = PadRole::perc;
        const auto* percInfo = index.find (buildKit (index, perc).paths[7]);
        check (percInfo != nullptr && percInfo->classification.role == Role::perc, "a pad turned to Perc gets a perc");

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

namespace
{
    void testDrumRacks()
    {
        std::cout << "Drum Rack presets\n";
        // Three pads as Live writes them (trimmed): notes stored as 128 − note, the sample's
        // RelativePath then Path, other file refs (the preset itself) that aren't audio.
        const std::string xml = R"(<Ableton><GroupDevicePreset><Device><DrumGroupDevice>
            <FileRef><RelativePath Value="Racks/Drum Racks/X.adg" /><Path Value="/build/X.adg" /></FileRef>
            <DrumBranchPreset Id="0"><ZoneSettings><ReceivingNote Value="92" /></ZoneSettings>
              <FileRef><RelativePath Value="Drums/Kick Warm.adg" /></FileRef>
              <SampleRef><FileRef><RelativePath Value="Samples/One Shots/Drums/Kick/Kick Remo.aif" /><Path Value="/build/Core Library/Samples/One Shots/Drums/Kick/Kick Remo.aif" /></FileRef></SampleRef>
            </DrumBranchPreset>
            <DrumBranchPreset Id="1"><ZoneSettings><ReceivingNote Value="90" /></ZoneSettings>
              <SampleRef><FileRef><RelativePath Value="Samples/One Shots/Drums/Snare/Snare R&amp;B.wav" /><Path Value="" /></FileRef></SampleRef>
            </DrumBranchPreset>
            <DrumBranchPreset Id="2"><ZoneSettings><ReceivingNote Value="86" /></ZoneSettings>
              <SampleRef><FileRef><RelativePath Value="" /></FileRef></SampleRef>
            </DrumBranchPreset>
        </DrumGroupDevice></Device></GroupDevicePreset></Ableton>)";
        const auto pads = parseDrumRack (xml);
        check (pads.size() == 2, "two pads with samples (the third has none): " + std::to_string (pads.size()));
        if (pads.size() == 2)
        {
            check (pads[0].note == 36 && pads[0].relativePath == "Samples/One Shots/Drums/Kick/Kick Remo.aif"
                       && pads[0].path == "/build/Core Library/Samples/One Shots/Drums/Kick/Kick Remo.aif",
                   "the kick on C1 (stored as 92), its relative and absolute paths, not the preset's own .adg");
            check (pads[1].note == 38 && pads[1].relativePath == "Samples/One Shots/Drums/Snare/Snare R&B.wav", "the snare on D1, entities unescaped");
        }
        check (parseDrumRack ("<Ableton/>").empty() && parseDrumRack ("<DrumBranchPreset><ReceivingNote Value=\"92\" />").empty(), "nothing, or a cut-off file: no pads");

        // A rack onto the pads: roles, notes, tom ranges, locks.
        std::vector<SampleInfo> lib {
            make ("Core", "Kick A", Role::kick, {}), make ("Core", "Kick B", Role::kick, {}), make ("Core", "Snare A", Role::snare, {}),
            make ("Core", "Hat Closed", Role::closedHat, {}), make ("Core", "Hat Open", Role::openHat, {}),
            make ("Core", "Tom Lo", Role::tom, {}, TomRange::low), make ("Core", "Tom Hi", Role::tom, {}, TomRange::high),
            make ("Core", "Crash", Role::crash, {}),
        };
        const LibraryIndex index (std::move (lib));
        const auto path = [] (const char* name) { return "/lib/Core/Drums/" + std::string (name) + ".wav"; };
        DrumRack rack;
        rack.name = "Test Kit";
        for (const auto& [note, name] : std::vector<std::pair<int, const char*>> { { 35, "Kick B" }, { 36, "Kick A" }, { 38, "Snare A" }, { 42, "Hat Closed" },
                                                                                 { 46, "Hat Open" }, { 45, "Tom Hi" }, { 41, "Tom Lo" }, { 49, "Crash" } })
            rack.pads.push_back ({ note, "", "", path (name) });
        KitPaths current;
        current[2] = "/old/Clap.wav";
        std::array<bool, numPads> locked {};
        const auto kit = rackKit (rack, index, defaultPadRoles, current, locked);
        check (kit[0] == path ("Kick A"), "the kick nearest C1 (36, not 35)");
        check (kit[1] == path ("Snare A") && kit[3] == path ("Hat Closed") && kit[4] == path ("Hat Open") && kit[7] == path ("Crash"), "snare, hats and crash by role");
        check (kit[5] == path ("Tom Lo") && kit[6] == path ("Tom Hi"), "low and high toms by their range, not their notes");
        check (kit[2] == "/old/Clap.wav", "a role the rack hasn't got (clap) keeps the pad's sample");
        locked[0] = true;
        current[0] = path ("Kick A");
        const auto lockedKit = rackKit (rack, index, defaultPadRoles, current, locked);
        check (lockedKit[0] == path ("Kick A"), "a locked pad stays");
    }
}

int main()
{
    testBuild();
    testLocksAndDice();
    testHistory();
    testRelocate();
    testDrumRacks();
    return testutil::finish ("test_kit");
}
