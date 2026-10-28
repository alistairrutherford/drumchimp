// The whole library through the scanner and the classifier (PLAN.md §3.2), as the plug-in sees
// it: the roots found, how long a scan takes (cold, then warm from the cache), the count of each
// role, why samples went Unsorted, and how many hats only their length decided. With --csv, one
// row per sample (root, path, seconds, role, confidence, tom range, tags, reason) for reading the
// misses, and for picking fixture paths (Tests/fixtures/classifier_labels.csv).
//
//   Tools/classify.sh [--csv out.csv]
#include "core/library/LibraryIndex.h"
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <map>

using namespace drumchimp;

namespace
{
    std::string csvField (const std::string& s)
    {
        std::string out = "\"";
        for (char c : s)
            out += c == '"' ? std::string ("\"\"") : std::string (1, c);
        return out + "\"";
    }

    const char* rangeName (TomRange r)
    {
        switch (r)
        {
            case TomRange::low:     return "low";
            case TomRange::mid:     return "mid";
            case TomRange::high:    return "high";
            case TomRange::unknown: break;
        }
        return "";
    }
}

int main (int argc, char** argv)
{
    std::string csvPath;
    for (int i = 1; i < argc; ++i)
        if (std::string (argv[i]) == "--csv" && i + 1 < argc)
            csvPath = argv[++i];

    const char* home = std::getenv ("HOME");
    const auto roots = detectRoots (home != nullptr ? home : "");
    std::cout << "Roots (" << roots.size() << "):\n";
    for (const auto& r : roots)
        std::cout << "  " << std::left << std::setw (13) << kindName (r.kind) << r.name << "  " << r.path << "\n";

    LibraryCache cache;
    ScanStats cold, warm;
    scanLibrary (roots, cache, cold);
    const LibraryIndex index (scanLibrary (roots, cache, warm), roots);

    std::cout << std::fixed << std::setprecision (0)
              << "\nScan, cold: " << cold.milliseconds << " ms (" << cold.files << " audio files, " << cold.headersRead << " headers read, "
              << cold.skippedLoops << " loops skipped, " << cold.unreadable << " unreadable)\n"
              << "Scan, warm: " << warm.milliseconds << " ms (" << warm.headersRead << " headers read, " << warm.fromCache << " from the cache)\n"
              << "\n" << index.size() << " samples, " << index.drums() << " drums:\n";
    for (int r = 0; r < numRoles; ++r)
        std::cout << "  " << std::right << std::setw (6) << index.withRole ((Role) r).size() << "  " << roleNames[(size_t) r] << "\n";

    std::map<std::string, size_t> reasons;
    size_t hatsByLength = 0, tomsWithRange = 0;
    for (const auto& s : index.all())
    {
        const auto& c = s.classification;
        if (c.role == Role::unsorted)
            ++reasons[c.reason];
        if ((c.role == Role::openHat || c.role == Role::closedHat) && c.confidence < 0.9f && std::string (c.reason) == "folder")
            ++hatsByLength;
        if (c.role == Role::tom && c.tomRange != TomRange::unknown)
            ++tomsWithRange;
    }
    std::cout << "\nUnsorted, why:\n";
    for (const auto& [why, n] : reasons)
        std::cout << "  " << std::setw (6) << n << "  " << why << "\n";
    std::cout << "\nHats decided by length: " << hatsByLength << "\nToms with a range in their name: " << tomsWithRange << " of "
              << index.withRole (Role::tom).size() << "\n";

    if (! csvPath.empty())
    {
        std::ofstream csv (csvPath);
        csv << "root,relative_path,seconds,role,confidence,tom_range,tags,reason\n";
        for (const auto& s : index.all())
        {
            std::string tags;
            for (const auto& t : s.classification.tags)
                tags += (tags.empty() ? "" : " ") + t;
            csv << csvField (s.root) << "," << csvField (s.relative) << "," << std::setprecision (3) << s.seconds << ","
                << roleId (s.classification.role) << "," << std::setprecision (2) << s.classification.confidence << ","
                << rangeName (s.classification.tomRange) << "," << csvField (tags) << "," << s.classification.reason << "\n";
        }
        std::cout << "\nCSV: " << csvPath << "\n";
    }
    return 0;
}
