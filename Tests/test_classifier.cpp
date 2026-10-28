// Phase 2: the classifier (core/library/Classifier.h).
//
//   Tokens       splitting and joining
//   Other packs  names in the styles third-party packs and users use, with no Ableton folders
//   Fixture      300 hand-labelled paths from this Mac's library (Tests/fixtures/
//                classifier_labels.csv: paths and lengths only, no audio). PLAN.md G3: ≥ 95%.
//                Every miss is listed.
#include "TestUtil.h"
#include "core/library/Classifier.h"
#include <cstdlib>
#include <fstream>
#include <sstream>

using namespace drumchimp;
using testutil::check;

namespace
{
    std::string joined (const std::vector<std::string>& v)
    {
        std::string s;
        for (const auto& t : v)
            s += (s.empty() ? "" : " ") + t;
        return s;
    }

    void testTokens()
    {
        std::cout << "Tokens\n";
        using classifier::tokenize;
        check (joined (tokenize ("Snare-808 ClosedHH_02")) == "snare 808 closed hh 02", "separators, camelCase, letters and digits");
        check (joined (tokenize ("HiHat Open")) == "hihat open", "Hi Hat joins");
        check (joined (tokenize ("E-Perc Strike")) == "eperc strike", "E-Perc joins");
        check (joined (tokenize ("BD01")) == "bd 01", "BD01");
        check (joined (tokenize ("FatDomino Drums 03 73bpm")) == "fat domino drums 03 73 bpm", "73bpm");
        check (joined (tokenize ("Bass Drum Deep")) == "bassdrum deep", "Bass Drum joins (it isn't a bass)");
    }

    struct Case { const char* path; double seconds; Role role; };

    void testOtherPacks()
    {
        std::cout << "Other packs and user folders\n";
        const Case cases[] {
            { "My Kit/BD_909_01.wav", 0.4, Role::kick },
            { "My Kit/SD Tight.wav", 0.2, Role::snare },
            { "My Kit/OH 1.wav", 0.9, Role::openHat },
            { "My Kit/CH_03.wav", 0.1, Role::closedHat },
            { "My Kit/HiHat_Open_2.wav", 0.8, Role::openHat },
            { "My Kit/Rimshot 3.wav", 0.1, Role::rim },
            { "My Kit/Snare Rim 2.wav", 0.1, Role::rim },
            { "My Kit/Ride Bell.wav", 2.0, Role::ride },
            { "My Kit/Cowbell.wav", 0.3, Role::perc },
            { "My Kit/Floor Tom.wav", 0.6, Role::tom },
            { "Pack/Drums/Hats/Hat 07.wav", 0.12, Role::closedHat },
            { "Pack/Drums/Hats/Hat 08.wav", 1.4, Role::openHat },
            { "Pack/Open Hats/Hat 01.wav", 0.3, Role::openHat },
            { "Pack/Bass Drums/Deep 1.wav", 0.5, Role::kick },
            { "Pack/Kicks/Kick Loop 120 bpm.wav", 2.0, Role::unsorted },
            { "Pack/Loops/Snare Top.wav", 1.0, Role::unsorted },
            { "Pack/Bass/Bass Note C2.wav", 1.0, Role::unsorted },
            { "Pack/Synth/Kick Synth Pluck.wav", 0.5, Role::unsorted },
            { "Pack/Kick/Kick 808 Long Tail.wav", 7.0, Role::kick },
            { "Pack/Kick/Kick Rumble Pad.wav", 14.0, Role::unsorted },
            { "Pack/Clap/Clap Reverb Tail.wav", 5.0, Role::unsorted },
            { "Pack/Cymbals/Crash Long.wav", 8.0, Role::crash },
            { "Pack/Percussion/Kick Box.wav", 0.3, Role::perc },
            { "Pack/One Shots/Vox Hey.wav", 0.4, Role::unsorted },
            { "Pack/One Shots/Clap Snap.wav", 0.2, Role::clap },
        };
        for (const auto& c : cases)
        {
            const auto got = classify (c.path, c.seconds);
            check (got.role == c.role, std::string (c.path) + " → " + roleName (c.role) + (got.role == c.role ? "" : std::string (" (got ") + roleName (got.role) + ")"));
        }

        const auto low = classify ("Pack/Toms/Tom Lo 2.wav", 0.5), high = classify ("Drums/Tom/Tom-808-Hi.aif", 0.4), mid = classify ("Drums/Tom/Tom D-ulator Mid.aif", 0.4);
        check (low.tomRange == TomRange::low && high.tomRange == TomRange::high && mid.tomRange == TomRange::mid, "tom ranges: Lo, Hi, Mid");
        const auto tagged = classify ("Samples/Drums/Kick/Kick 808 Tone 1 Long.aif", 1.0);
        check (joined (tagged.tags) == "808 tone", "tags: the name's other words (" + joined (tagged.tags) + ")");
        check (classify ("Samples/One Shots/Drums/Kick/Kick 909 2.wav", 0.5).confidence > classify ("My Kit/Kick 2.wav", 0.5).confidence,
               "a role folder is more certain than a name alone");
    }

    std::vector<std::string> csvFields (const std::string& line)
    {
        std::vector<std::string> fields;
        std::string field;
        bool quoted = false;
        for (size_t i = 0; i < line.size(); ++i)
        {
            const char c = line[i];
            if (quoted)
            {
                if (c == '"' && i + 1 < line.size() && line[i + 1] == '"') { field += '"'; ++i; }
                else if (c == '"') quoted = false;
                else field += c;
            }
            else if (c == '"') quoted = true;
            else if (c == ',') { fields.push_back (field); field.clear(); }
            else if (c != '\r') field += c;
        }
        fields.push_back (field);
        return fields;
    }

    void testFixture()
    {
        std::cout << "Fixture: hand-labelled library paths\n";
        const char* dir = std::getenv ("DRUMCHIMP_FIXTURES");
        std::ifstream in (std::string (dir != nullptr ? dir : "Tests/fixtures") + "/classifier_labels.csv");
        check (in.good(), "classifier_labels.csv opens");
        std::string line;
        std::getline (in, line);                  // header
        int total = 0, right = 0;
        std::array<int, numRoles> perRole {}, perRoleRight {};
        while (std::getline (in, line))
        {
            if (line.empty())
                continue;
            const auto f = csvFields (line);
            if (f.size() < 3)
                continue;
            // The path starts with its root's name (a pack, "Core Library"): classify from below it.
            const auto slash = f[0].find ('/');
            const auto got = classify (std::string_view (f[0]).substr (slash + 1), std::stod (f[1]));
            bool ok = false;
            std::stringstream expected (f[2]);
            std::string alternative;
            while (std::getline (expected, alternative, '|'))
                ok = ok || roleFromId (alternative) == got.role;
            const auto first = roleFromId (f[2].substr (0, f[2].find ('|')));
            ++total;
            ++perRole[(size_t) first];
            if (ok)
            {
                ++right;
                ++perRoleRight[(size_t) first];
            }
            else
                std::cout << "        miss  " << f[0] << "  expected " << f[2] << ", got " << roleId (got.role) << " (" << got.reason << ")\n";
        }
        const double accuracy = total > 0 ? 100.0 * right / total : 0.0;
        for (int r = 0; r < numRoles; ++r)
            if (perRole[(size_t) r] > 0)
                std::cout << "        " << roleNames[(size_t) r] << ": " << perRoleRight[(size_t) r] << "/" << perRole[(size_t) r] << "\n";
        check (total == 300, "300 labelled paths (" + std::to_string (total) + ")");
        check (accuracy >= 95.0, "at least 95% right: " + std::to_string (right) + "/" + std::to_string (total));
    }
}

int main()
{
    testTokens();
    testOtherPacks();
    testFixture();
    return testutil::finish ("test_classifier");
}
