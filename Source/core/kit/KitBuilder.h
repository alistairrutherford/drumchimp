// Kits (PLAN.md §3.3): choosing a sample for each pad from the library. Plain C++, no JUCE.
// Message thread only.
//
//   PadRole      what a pad is for. The classifier's roles, with toms split by range.
//   buildKit     a sample for every unlocked pad:
//                  1. Character Matched → Wild. At Matched, first choose a family: a tag (808,
//                     909, Acoustified, …) that has samples for as many of the pads' roles as
//                     possible, chosen at random weighted by that coverage, and pulled hard
//                     toward the families of locked pads' samples. Generic words (dry, hit,
//                     room …) aren't families. A pack is the family only when no tag will do:
//                     a pack mixes kits. The style's families (808 for Trap, 909 for House …)
//                     are three times as likely.
//                  2. Each unlocked pad takes, with probability 1 − Character, a sample of its
//                     role from the family; otherwise (or if the family has none) any sample of
//                     its role. Toms prefer their range, then an unknown range. No sample is used
//                     twice, and a re-roll avoids the pad's current sample when it can.
//                  3. A role the library has nothing for keeps the pad's current sample.
//                One pad's dice is the same with every other pad locked.
//   KitHistory   undo and redo over the last 20 kits
//   relocate     a missing sample found again in the library: same file name, then same size,
//                then the most matching folders (a pack moved, Live renamed). Two equally good
//                matches are a guess, and refused, unless both have the saved size: then they're
//                copies of one file (packs share samples), and the first will do.
#pragma once

#include "../engine/NoteMap.h"
#include "../library/LibraryIndex.h"
#include <map>
#include <set>

namespace drumchimp
{
    enum class PadRole { kick, snare, clap, rim, closedHat, openHat, lowTom, midTom, highTom, crash, ride, perc };
    constexpr int numPadRoles = 12;

    constexpr std::array<const char*, numPadRoles> padRoleIds { "kick", "snare", "clap", "rim", "closedHat", "openHat", "lowTom", "midTom", "highTom", "crash", "ride", "perc" };
    constexpr std::array<const char*, numPadRoles> padRoleNames { "Kick", "Snare", "Clap", "Rim", "Closed hat", "Open hat", "Low tom", "Mid tom", "High tom", "Crash", "Ride", "Perc" };
    constexpr std::array<PadRole, numPads> defaultPadRoles {
        PadRole::kick, PadRole::snare, PadRole::clap, PadRole::closedHat, PadRole::openHat, PadRole::lowTom, PadRole::highTom, PadRole::crash,
        PadRole::rim, PadRole::midTom, PadRole::ride, PadRole::perc };

    inline const char* padRoleName (PadRole r) { return padRoleNames[(size_t) r]; }
    inline const char* padRoleId (PadRole r) { return padRoleIds[(size_t) r]; }

    inline PadRole padRoleFromId (std::string_view id, PadRole fallback)
    {
        for (int i = 0; i < numPadRoles; ++i)
            if (id == padRoleIds[(size_t) i])
                return (PadRole) i;
        return fallback;
    }

    inline Role libraryRole (PadRole r)
    {
        switch (r)
        {
            case PadRole::kick:      return Role::kick;
            case PadRole::snare:     return Role::snare;
            case PadRole::clap:      return Role::clap;
            case PadRole::rim:       return Role::rim;
            case PadRole::closedHat: return Role::closedHat;
            case PadRole::openHat:   return Role::openHat;
            case PadRole::lowTom:
            case PadRole::midTom:
            case PadRole::highTom:   return Role::tom;
            case PadRole::crash:     return Role::crash;
            case PadRole::ride:      return Role::ride;
            case PadRole::perc:      return Role::perc;
        }
        return Role::unsorted;
    }

    inline TomRange wantedRange (PadRole r)
    {
        return r == PadRole::lowTom ? TomRange::low : r == PadRole::midTom ? TomRange::mid : r == PadRole::highTom ? TomRange::high : TomRange::unknown;
    }

    using KitPaths = std::array<std::string, numPads>;

    struct KitRequest
    {
        std::array<PadRole, numPads> roles = defaultPadRoles;
        std::array<bool, numPads> locked {};
        KitPaths current;                 // kept for locked pads (and a role with no samples)
        float character = 0.25f;          // 0 Matched … 1 Wild
        std::vector<std::string> preferredFamilies;   // the style's (Style::families)
        uint64_t seed = 1;
    };

    struct KitResult
    {
        KitPaths paths;
        std::string family;               // "" when none was used ("pack:" + name for a pack)
    };

    namespace kitbuilder
    {
        // splitmix64: the same kit for the same seed on every machine.
        struct Random
        {
            uint64_t state;
            uint64_t next() noexcept
            {
                uint64_t z = (state += 0x9e3779b97f4a7c15ull);
                z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
                z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
                return z ^ (z >> 31);
            }
            double uniform() noexcept { return (double) (next() >> 11) * (1.0 / 9007199254740992.0); }
            size_t below (size_t n) noexcept { return n == 0 ? 0 : (size_t) (next() % n); }
        };

        inline bool isGenericWord (const std::string& t)
        {
            static const std::array<const char*, 30> words {
                "dry", "hit", "tone", "chop", "combo", "room", "stick", "crack", "feel", "synth", "layer", "noise", "real",
                "big", "deep", "fat", "tight", "punch", "sub", "mix", "new", "old", "wet", "reverb", "verb", "short",
                "long", "single", "sample", "edge" };
            return std::any_of (words.begin(), words.end(), [&] (const char* w) { return t == w; });
        }

        inline std::vector<std::string> familiesOf (const SampleInfo& s)
        {
            std::vector<std::string> f;
            for (const auto& t : s.classification.tags)
                if (! isGenericWord (t) && t.size() >= 2)
                    f.push_back (t);
            f.push_back ("pack:" + s.root);
            return f;
        }
    }

    inline KitResult buildKit (const LibraryIndex& index, const KitRequest& request)
    {
        using namespace kitbuilder;
        Random random { request.seed * 0x2545f4914f6cdd1dull + 0x632be59bd9b4e019ull };
        KitResult result;
        result.paths = request.current;

        // The roles still to fill, and families' coverage of them.
        std::set<Role> needed;
        for (int pad = 0; pad < numPads; ++pad)
            if (! request.locked[(size_t) pad])
                needed.insert (libraryRole (request.roles[(size_t) pad]));
        if (needed.empty())
            return result;

        std::map<std::string, int> lockedFamilies;    // family → locked pads in it
        std::set<std::string> used;
        for (int pad = 0; pad < numPads; ++pad)
            if (request.locked[(size_t) pad] && ! request.current[(size_t) pad].empty())
            {
                used.insert (request.current[(size_t) pad]);
                if (const auto* s = index.find (request.current[(size_t) pad]))
                    for (auto& f : familiesOf (*s))
                        ++lockedFamilies[f];
            }

        std::map<std::string, std::set<Role>> coverage;
        for (const auto role : needed)
            for (const auto i : index.withRole (role))
                for (auto& f : familiesOf (index.all()[i]))
                    coverage[f].insert (role);

        // Choose a family (always, so a Character between the ends has one to lean on).
        const size_t enough = std::min<size_t> (needed.size(), 3);
        std::vector<std::pair<std::string, double>> weighted;
        double total = 0.0;
        for (const bool packs : { false, true })
        {
            for (const auto& [family, roles] : coverage)
                if (roles.size() >= enough && (family.rfind ("pack:", 0) == 0) == packs)
                {
                    const double score = (double) roles.size();
                    const auto locked = lockedFamilies.find (family);
                    const bool preferred = std::find (request.preferredFamilies.begin(), request.preferredFamilies.end(), family) != request.preferredFamilies.end();
                    const double weight = score * score * score * (1.0 + 3.0 * (locked != lockedFamilies.end() ? locked->second : 0)) * (preferred ? 3.0 : 1.0);
                    weighted.push_back ({ family, weight });
                    total += weight;
                }
            if (total > 0.0)
                break;                    // tags first; packs only when no tag will do
        }
        if (total > 0.0)
        {
            double pick = random.uniform() * total;
            for (const auto& [family, weight] : weighted)
                if ((pick -= weight) <= 0.0)
                {
                    result.family = family;
                    break;
                }
            if (result.family.empty())
                result.family = weighted.back().first;
        }

        const auto inFamily = [&] (const SampleInfo& s)
        {
            if (result.family.rfind ("pack:", 0) == 0)
                return "pack:" + s.root == result.family;
            const auto& tags = s.classification.tags;
            return std::find (tags.begin(), tags.end(), result.family) != tags.end();
        };

        for (int pad = 0; pad < numPads; ++pad)
        {
            if (request.locked[(size_t) pad])
                continue;
            const auto padRole = request.roles[(size_t) pad];
            const auto& ofRole = index.withRole (libraryRole (padRole));
            const auto range = wantedRange (padRole);
            const bool matched = ! result.family.empty() && random.uniform() >= request.character;

            // Candidates, best group first: [family?] × [range, unknown range, any range]. A re-roll
            // avoids the pad's current sample: the first group with anything else wins (found in
            // Phase 6: a family with one low tom kept re-rolling to it); only if nothing else
            // anywhere will do does the pad keep it.
            std::vector<size_t> pool;
            for (const bool allowCurrent : { false, true })
                for (int familyPass = matched ? 0 : 1; familyPass < 2 && pool.empty(); ++familyPass)
                    for (int rangePass = range == TomRange::unknown ? 2 : 0; rangePass < 3 && pool.empty(); ++rangePass)
                        for (const auto i : ofRole)
                        {
                            const auto& s = index.all()[i];
                            if (used.count (s.path) > 0 || (familyPass == 0 && ! inFamily (s)) || (! allowCurrent && s.path == request.current[(size_t) pad]))
                                continue;
                            const auto r = s.classification.tomRange;
                            if ((rangePass == 0 && r != range) || (rangePass == 1 && r != TomRange::unknown))
                                continue;
                            pool.push_back (i);
                        }
            if (pool.empty())
                continue;                         // nothing for this role: the pad keeps its sample
            const auto& chosen = index.all()[pool[random.below (pool.size())]];
            result.paths[(size_t) pad] = chosen.path;
            used.insert (chosen.path);
        }
        return result;
    }

    class KitHistory
    {
    public:
        static constexpr size_t maxKits = 20;

        // A change from `before` to `after`: anything that could be redone is dropped.
        void record (const KitPaths& before, const KitPaths& after)
        {
            if (before == after)
                return;
            if (kits.empty() || kits[at] != before)
            {
                kits.erase (kits.begin() + (std::ptrdiff_t) (kits.empty() ? 0 : at + 1), kits.end());
                kits.push_back (before);
            }
            else
                kits.erase (kits.begin() + (std::ptrdiff_t) at + 1, kits.end());
            kits.push_back (after);
            while (kits.size() > maxKits)
                kits.erase (kits.begin());
            at = kits.size() - 1;
        }

        bool canUndo() const noexcept { return ! kits.empty() && at > 0; }
        bool canRedo() const noexcept { return at + 1 < kits.size(); }
        KitPaths undo() { return kits[canUndo() ? --at : at]; }
        KitPaths redo() { return kits[canRedo() ? ++at : at]; }
        void clear() { kits.clear(); at = 0; }

    private:
        std::vector<KitPaths> kits;
        size_t at = 0;
    };

    inline const SampleInfo* relocate (const LibraryIndex& index, const std::string& missingPath, int64_t size)
    {
        const auto components = [] (const std::string& p)
        {
            std::vector<std::string> parts;
            std::string part;
            for (const char c : p)
            {
                if (c == '/') { if (! part.empty()) parts.push_back (part); part.clear(); }
                else part += c;
            }
            if (! part.empty())
                parts.push_back (part);
            return parts;
        };
        const auto wanted = components (missingPath);
        if (wanted.empty())
            return nullptr;

        const SampleInfo* best = nullptr;
        int bestScore = -1;
        bool tie = false, tieAreCopies = true;
        for (const auto& s : index.all())
        {
            const auto parts = components (s.path);
            if (parts.empty() || parts.back() != wanted.back())
                continue;
            int score = size > 0 && s.size == size ? 100 : 0;
            for (size_t k = 2; k <= std::min (parts.size(), wanted.size()) && parts[parts.size() - k] == wanted[wanted.size() - k]; ++k)
                ++score;
            if (score > bestScore)
            {
                best = &s;
                bestScore = score;
                tie = false;
                tieAreCopies = true;
            }
            else if (score == bestScore)
            {
                tie = true;
                tieAreCopies = tieAreCopies && size > 0 && s.size == size && best->size == size;
            }
        }
        return tie && ! tieAreCopies ? nullptr : best;
    }
}
