// Sorts a sample into a drum role by its path and length (PLAN.md §3.2). Plain C++, no JUCE.
//
//   tokenize   "Snare-808 ClosedHH_02" → snare 808 closed hh 02: split on anything not a
//              letter or digit, on camelCase and on letter–digit boundaries, lower-cased; then
//              joins the pairs that mean one thing (hi hat, bass drum, side stick, e perc, …)
//   classify   relative path (from its library root) + length → role, confidence, tom range, tags
//
// The rules, in order:
//   1. Not a one-shot drum: a loop (a Loops folder, "loop", "NNN bpm"), a folder of something
//      else (Synth, Stab, Vocal, Bass, Chords, FX, Multisamples other than Drum Machines, …), a
//      drum folder of effects (FX Hit) or utilities, or too long for a one-shot → Unsorted. Kicks,
//      snares, toms, cymbals and open hats may ring for 10 s (an 808, a floor tom); the rest, 4 s.
//   2. A role folder decides (the deepest one): Ableton sorts its one-shots into folders by role.
//      Within it, the name refines: a Snare folder's "Rimshot" is a Rim, a Cymbal folder's "Ride"
//      a Ride, and a hat folder's "Open"/"Closed" decide which hat (if neither, under 0.5 s is
//      closed). A percussion folder (Shaker, Conga, Bell, Misc and Electronic Percussion, …)
//      is always Perc, whatever drum words its names use ("Timbale Rim", "E-Perc Screech Kick").
//   3. No role folder: the name's first role word decides (Drum Machines/808/"Kick 808 Tone1",
//      or a user folder of "BD_01.wav"). A Chops folder of break slices counts only a role word
//      that comes first ("Snare Ghosts", not "Heavy Kicks"), and never a slice of whole drums.
//   4. Nothing → Unsorted: still browsable and assignable by hand, never picked by Randomise.
//
// Tags are the name's other words (808, Acoustified, Vinyl …); the kit builder (Phase 3) matches
// kits by them.
#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace drumchimp
{
    enum class Role { kick, snare, clap, rim, closedHat, openHat, tom, crash, ride, perc, unsorted };
    constexpr int numRoles = 11;
    enum class TomRange { unknown, low, mid, high };

    constexpr std::array<const char*, numRoles> roleIds { "kick", "snare", "clap", "rim", "closedHat", "openHat", "tom", "crash", "ride", "perc", "unsorted" };
    constexpr std::array<const char*, numRoles> roleNames { "Kick", "Snare", "Clap", "Rim", "Closed hat", "Open hat", "Tom", "Crash", "Ride", "Perc", "Unsorted" };

    inline const char* roleId (Role r) { return roleIds[(size_t) r]; }
    inline const char* roleName (Role r) { return roleNames[(size_t) r]; }

    inline Role roleFromId (std::string_view id)
    {
        for (int i = 0; i < numRoles; ++i)
            if (id == roleIds[(size_t) i])
                return (Role) i;
        return Role::unsorted;
    }

    struct Classification
    {
        Role role = Role::unsorted;
        float confidence = 0.0f;          // 0 for Unsorted; 0.95 from a role folder, less from a name alone
        TomRange tomRange = TomRange::unknown;
        std::vector<std::string> tags;
        const char* reason = "";
    };

    namespace classifier
    {
        inline std::vector<std::string> tokenize (std::string_view text)
        {
            std::vector<std::string> raw;
            std::string current;
            const auto flush = [&] { if (! current.empty()) { raw.push_back (current); current.clear(); } };
            char previous = 0;
            for (const char c : text)
            {
                const auto u = (unsigned char) c;
                if (! std::isalnum (u))
                {
                    flush();
                    previous = 0;
                    continue;
                }
                const bool boundary = previous != 0
                    && ((std::islower ((unsigned char) previous) && std::isupper (u))
                        || (std::isalpha ((unsigned char) previous) && std::isdigit (u))
                        || (std::isdigit ((unsigned char) previous) && std::isalpha (u)));
                if (boundary)
                    flush();
                current += (char) std::tolower (u);
                previous = c;
            }
            flush();

            // "HH" after "Closed" splits fine; upper-case runs like "ClosedHH" give closed, hh.
            static const std::array<std::array<const char*, 3>, 11> pairs {{
                { "hi", "hat", "hihat" }, { "hi", "hats", "hihats" }, { "bass", "drum", "bassdrum" }, { "bass", "drums", "bassdrum" },
                { "kick", "drum", "kickdrum" }, { "kick", "drums", "kickdrum" },
                { "side", "stick", "sidestick" }, { "cross", "stick", "sidestick" }, { "rim", "shot", "rimshot" },
                { "e", "perc", "eperc" }, { "floor", "tom", "floortom" },
            }};
            std::vector<std::string> out;
            for (size_t i = 0; i < raw.size(); ++i)
            {
                bool joined = false;
                if (i + 1 < raw.size())
                    for (const auto& p : pairs)
                        if (raw[i] == p[0] && raw[i + 1] == p[1])
                        {
                            out.push_back (p[2]);
                            ++i;
                            joined = true;
                            break;
                        }
                if (! joined)
                    out.push_back (raw[i]);
            }
            return out;
        }

        template <size_t N>
        bool isOneOf (const std::string& token, const std::array<const char*, N>& words)
        {
            return std::any_of (words.begin(), words.end(), [&] (const char* w) { return token == w; });
        }

        inline bool hasAny (const std::vector<std::string>& tokens, std::initializer_list<const char*> words)
        {
            return std::any_of (tokens.begin(), tokens.end(), [&] (const std::string& t)
            {
                return std::any_of (words.begin(), words.end(), [&] (const char* w) { return t == w; });
            });
        }

        enum class Word { none, kick, snare, clap, rim, hat, closedHat, openHat, tom, crash, ride, perc };

        inline Word wordRole (const std::string& t)
        {
            static const std::array<const char*, 7> kick { "kick", "kicks", "kik", "kck", "bd", "bassdrum", "kickdrum" };
            static const std::array<const char*, 4> snare { "snare", "snares", "snr", "sd" };
            static const std::array<const char*, 6> clap { "clap", "claps", "clp", "handclap", "snap", "snaps" };
            static const std::array<const char*, 7> rim { "rim", "rims", "rimshot", "rimshots", "sidestick", "sidesticks", "rimclick" };
            static const std::array<const char*, 6> hat { "hihat", "hihats", "hat", "hats", "hh", "hhat" };
            static const std::array<const char*, 3> closedHat { "ch", "chh", "clhh" };
            static const std::array<const char*, 2> openHat { "oh", "ohh" };
            static const std::array<const char*, 3> tom { "tom", "toms", "floortom" };
            static const std::array<const char*, 7> crash { "crash", "crashes", "cymbal", "cymbals", "cym", "splash", "china" };
            static const std::array<const char*, 2> ride { "ride", "rides" };
            static const std::array<const char*, 33> perc {
                "perc", "percs", "percussion", "eperc", "shaker", "shakers", "shake", "tamb", "tambo", "tambourine", "tamborine",
                "conga", "congas", "bongo", "bongos", "timbale", "timbales", "wood", "woodblock", "block", "cowbell", "bell",
                "bells", "triangle", "clave", "claves", "guiro", "cabasa", "agogo", "maracas", "cuica", "tabla", "timpani" };
            if (isOneOf (t, kick)) return Word::kick;
            if (isOneOf (t, snare)) return Word::snare;
            if (isOneOf (t, clap)) return Word::clap;
            if (isOneOf (t, rim)) return Word::rim;
            if (isOneOf (t, hat)) return Word::hat;
            if (isOneOf (t, closedHat)) return Word::closedHat;
            if (isOneOf (t, openHat)) return Word::openHat;
            if (isOneOf (t, tom)) return Word::tom;
            if (isOneOf (t, crash)) return Word::crash;
            if (isOneOf (t, ride)) return Word::ride;
            if (isOneOf (t, perc)) return Word::perc;
            return Word::none;
        }

        // The first role word in the tokens, or none.
        inline Word firstRole (const std::vector<std::string>& tokens, size_t* at = nullptr)
        {
            for (size_t i = 0; i < tokens.size(); ++i)
                if (const auto w = wordRole (tokens[i]); w != Word::none)
                {
                    if (at != nullptr)
                        *at = i;
                    return w;
                }
            return Word::none;
        }

        // Open or closed from the words, or Unsorted if they don't say. Which word comes first
        // decides "Open Close" (an open hat closed by the pedal).
        inline Role hatWords (const std::vector<std::string>& tokens)
        {
            for (const auto& t : tokens)
            {
                if (t == "open" || t == "opn" || t == "op" || t == "half" || t == "oh" || t == "ohh")
                    return Role::openHat;
                if (t == "closed" || t == "close" || t == "clsd" || t == "cl" || t == "ch" || t == "chh"
                    || t == "tight" || t == "pedal" || t == "pdl" || t == "foot")
                    return Role::closedHat;
            }
            return Role::unsorted;
        }

        // The name's words, then its folder's (a user's "Open Hats" folder), then the length.
        inline Role hatFrom (const std::vector<std::string>& name, const std::vector<std::string>& folder, double seconds, float& confidence)
        {
            if (const auto r = hatWords (name); r != Role::unsorted)
                return r;
            if (const auto r = hatWords (folder); r != Role::unsorted)
                return r;
            confidence *= 0.8f;                   // decided by length
            return seconds < 0.5 ? Role::closedHat : Role::openHat;
        }

        inline Role fromWord (Word w, const std::vector<std::string>& name, double seconds, float& confidence)
        {
            switch (w)
            {
                case Word::kick:      return Role::kick;
                case Word::snare:     return Role::snare;
                case Word::clap:      return Role::clap;
                case Word::rim:       return Role::rim;
                case Word::hat:       return hatFrom (name, {}, seconds, confidence);
                case Word::closedHat: return Role::closedHat;
                case Word::openHat:   return Role::openHat;
                case Word::tom:       return Role::tom;
                case Word::crash:     return Role::crash;
                case Word::ride:      return Role::ride;
                case Word::perc:      return Role::perc;
                case Word::none:      break;
            }
            return Role::unsorted;
        }

        inline bool isNonDrumFolder (const std::vector<std::string>& folder, bool drumMachines)
        {
            if (hasAny (folder, { "multisamples", "multisample" }))
                return ! drumMachines;
            return hasAny (folder, { "synth", "synths", "stab", "stabs", "vocal", "vocals", "vox", "chord", "chords", "piano", "keys",
                                     "guitar", "guitars", "strings", "orchestral", "brass", "winds", "mallets", "instrument",
                                     "instruments", "metronome", "metronomes", "defaults", "bass", "basses", "pad", "pads", "lead",
                                     "leads", "melodic", "texture", "textures", "ambience", "foley" })
                || (folder.size() == 1 && (folder[0] == "fx" || folder[0] == "sfx"));
        }

        inline TomRange tomRangeOf (const std::vector<std::string>& name)
        {
            for (const auto& t : name)
            {
                if (t == "lo" || t == "low" || t == "floor" || t == "floortom" || t == "fl")
                    return TomRange::low;
                if (t == "mid" || t == "med")
                    return TomRange::mid;
                if (t == "hi" || t == "high" || t == "rack")
                    return TomRange::high;
            }
            return TomRange::unknown;
        }

        inline std::vector<std::string> tagsOf (const std::vector<std::string>& name)
        {
            std::vector<std::string> tags;
            for (const auto& t : name)
            {
                const bool number = std::all_of (t.begin(), t.end(), [] (char c) { return std::isdigit ((unsigned char) c) != 0; })
                                    && t.size() < 3;      // 808 and 909 stay; 01 and 2 go
                if (t.size() < 2 || number || wordRole (t) != Word::none
                    || hasAny ({ t }, { "open", "closed", "close", "lo", "low", "mid", "hi", "high", "short", "long", "hard", "soft",
                                        "medium", "the", "and", "of", "one", "shot" }))
                    continue;
                if (std::find (tags.begin(), tags.end(), t) == tags.end())
                    tags.push_back (t);
            }
            return tags;
        }

        inline bool isLoop (const std::vector<std::vector<std::string>>& folders, const std::vector<std::string>& name)
        {
            for (const auto& f : folders)
                if (hasAny (f, { "loop", "loops" }))
                    return true;
            for (size_t i = 0; i < name.size(); ++i)
                if (name[i] == "loop" || name[i] == "loops" || name[i] == "bpm"
                    || (name[i].size() > 3 && name[i].compare (name[i].size() - 3, 3, "bpm") == 0))
                    return true;
            return false;
        }
    }

    inline Classification classify (std::string_view relativePath, double seconds)
    {
        using namespace classifier;
        Classification out;

        // Folders and the name (without its extension).
        std::vector<std::string> parts;
        {
            std::string part;
            for (const char c : relativePath)
            {
                if (c == '/') { if (! part.empty()) parts.push_back (part); part.clear(); }
                else part += c;
            }
            if (const auto dot = part.rfind ('.'); dot != std::string::npos && dot > 0)
                part.erase (dot);
            parts.push_back (part);
        }
        const auto name = tokenize (parts.back());
        std::vector<std::vector<std::string>> folders;
        bool drumMachines = false;
        for (size_t i = 0; i + 1 < parts.size(); ++i)
        {
            folders.push_back (tokenize (parts[i]));
            drumMachines = drumMachines || (hasAny (folders.back(), { "drum", "drums" }) && hasAny (folders.back(), { "machine", "machines" }));
        }
        out.tags = tagsOf (name);

        const auto unsorted = [&] (const char* why) { out.role = Role::unsorted; out.confidence = 0.0f; out.reason = why; return out; };
        if (isLoop (folders, name))
            return unsorted ("loop");
        for (const auto& f : folders)
            if (isNonDrumFolder (f, drumMachines))
                return unsorted ("not a drum folder");

        // The deepest folder with a role (or a special drum folder).
        Word folderRole = Word::none;
        const std::vector<std::string>* roleFolder = nullptr;
        bool chops = false;
        for (auto f = folders.rbegin(); f != folders.rend() && folderRole == Word::none; ++f)
        {
            if (hasAny (*f, { "fx", "sfx", "utility", "utilities" }))
                return unsorted ("effects or utility folder");
            if (hasAny (*f, { "chop", "chops", "breaks", "break", "slices" }))
            {
                chops = true;
                break;
            }
            folderRole = firstRole (*f);
            roleFolder = &*f;
        }

        float confidence = 0.95f;
        Role role = Role::unsorted;
        if (folderRole == Word::perc)
            role = Role::perc;
        else if (folderRole == Word::snare)
        {
            const auto first = firstRole (name);
            role = hasAny (name, { "rim", "rimshot", "sidestick", "rimclick" }) ? Role::rim
                 : first == Word::clap ? Role::clap
                 : Role::snare;
        }
        else if (folderRole == Word::crash)
            role = hasAny (name, { "ride", "rides" }) ? Role::ride : Role::crash;
        else if (folderRole == Word::hat || folderRole == Word::closedHat || folderRole == Word::openHat)
            role = folderRole == Word::hat ? hatFrom (name, *roleFolder, seconds, confidence) : fromWord (folderRole, name, seconds, confidence);
        else if (folderRole != Word::none)
            role = fromWord (folderRole, name, seconds, confidence);
        else
        {
            size_t at = 0;
            const auto first = firstRole (name, &at);
            if (first == Word::none)
                return unsorted ("no drum word in its name or folders");
            if (chops && (at != 0 || hasAny (name, { "drums", "fill", "fills", "break" })))
                return unsorted ("a slice of a break");
            confidence = at == 0 ? 0.8f : 0.6f;
            role = (first == Word::snare && hasAny (name, { "rim", "rimshot", "sidestick" })) ? Role::rim
                 : (first == Word::ride || (first == Word::crash && hasAny (name, { "ride" }))) ? Role::ride
                 : fromWord (first, name, seconds, confidence);
        }

        const bool rings = role == Role::kick || role == Role::snare || role == Role::tom || role == Role::crash
                           || role == Role::ride || role == Role::openHat;
        if (seconds > (rings ? 10.0 : 4.0))
            return unsorted ("too long for a one-shot");
        if (role == Role::tom)
            out.tomRange = tomRangeOf (name);
        out.role = role;
        out.confidence = confidence;
        out.reason = folderRole != Word::none ? "folder" : "name";
        return out;
    }
}
