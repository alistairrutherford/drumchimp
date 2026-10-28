// Spike (d), PLAN.md §6: how long it takes to find and read every sample in the Ableton library,
// and what is there. Uses the same core code the plug-in will (LibraryRoots, AudioHeader).
//
//   Tools/scan_library.sh [--csv out.csv]
//
// Prints the roots found, the time to walk them and to read every header, the counts that the
// classifier will skip (loops, long files, unreadable), and the drum folders by size. --csv writes
// one row per one-shot (root, relative path, seconds, rate, channels): the raw list Phase 2's
// hand-labelled fixture is picked from.
#include "core/library/AudioHeader.h"
#include "core/library/LibraryRoots.h"
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <map>

using namespace drumchimp;
namespace fs = std::filesystem;

namespace
{
    std::string lower (std::string s)
    {
        for (auto& c : s)
            c = (char) std::tolower ((unsigned char) c);
        return s;
    }

    bool isAudio (const fs::path& p)
    {
        const auto ext = lower (p.extension().string());
        return ext == ".wav" || ext == ".aif" || ext == ".aiff" || ext == ".flac";
    }

    bool isLoop (const std::string& relative)
    {
        const auto l = lower (relative);
        return l.find ("loops/") != std::string::npos || l.find ("loop") != std::string::npos || l.find (" bpm") != std::string::npos;
    }

    bool isDrum (const std::string& relative)
    {
        const auto l = lower (relative);
        return l.find ("drums/") != std::string::npos || l.find ("drum machines/") != std::string::npos;
    }

    std::string csvField (const std::string& s)
    {
        std::string out = "\"";
        for (char c : s)
            out += c == '"' ? std::string ("\"\"") : std::string (1, c);
        return out + "\"";
    }

    double msSince (std::chrono::steady_clock::time_point t)
    {
        return std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t).count();
    }
}

int main (int argc, char** argv)
{
    std::string csvPath;
    for (int i = 1; i < argc; ++i)
        if (std::string (argv[i]) == "--csv" && i + 1 < argc)
            csvPath = argv[++i];

    const char* home = std::getenv ("HOME");
    const auto t0 = std::chrono::steady_clock::now();
    const auto roots = detectRoots (home != nullptr ? home : "");
    const double detectMs = msSince (t0);

    std::cout << "Roots (" << roots.size() << ", found in " << std::fixed << std::setprecision (1) << detectMs << " ms):\n";
    for (const auto& r : roots)
        std::cout << "  " << std::left << std::setw (13) << kindName (r.kind) << r.name << "  " << r.path << "\n";

    struct File { const LibraryRoot* root; std::string relative, path; };
    std::vector<File> files;
    size_t asd = 0;
    const auto t1 = std::chrono::steady_clock::now();
    for (const auto& r : roots)
    {
        std::error_code ec;
        for (fs::recursive_directory_iterator it (r.path, fs::directory_options::skip_permission_denied, ec), end; it != end; it.increment (ec))
        {
            if (ec)
                break;
            if (! it->is_regular_file (ec))
                continue;
            const auto& p = it->path();
            if (p.extension() == ".asd")
                ++asd;
            else if (isAudio (p))
                files.push_back ({ &r, fs::relative (p, r.path, ec).string(), p.string() });
        }
    }
    const double walkMs = msSince (t1);

    size_t loops = 0, unreadable = 0, longOnes = 0, oneShots = 0, drums = 0;
    std::map<std::string, size_t> drumFolders;
    std::ofstream csv;
    if (! csvPath.empty())
    {
        csv.open (csvPath);
        csv << "root,relative_path,seconds,sample_rate,channels\n";
    }
    const auto t2 = std::chrono::steady_clock::now();
    for (const auto& f : files)
    {
        if (isLoop (f.relative))
        {
            ++loops;
            continue;
        }
        const auto h = readAudioHeader (f.path);
        if (! h.ok)
        {
            ++unreadable;
            continue;
        }
        const auto folder = fs::path (f.relative).parent_path().filename().string();
        const bool cymbal = lower (folder).find ("cymbal") != std::string::npos || lower (folder) == "ride";
        if (h.seconds() > (cymbal ? 10.0 : 4.0))
        {
            ++longOnes;
            continue;
        }
        ++oneShots;
        if (isDrum (f.relative))
        {
            ++drums;
            ++drumFolders[folder];
        }
        if (csv.is_open())
            csv << csvField (f.root->name) << "," << csvField (f.relative) << "," << std::setprecision (3) << h.seconds() << ","
                << (int) h.sampleRate << "," << h.channels << "\n";
    }
    const double headerMs = msSince (t2);

    std::cout << std::setprecision (1)
              << "\nWalk:    " << walkMs << " ms, " << files.size() << " audio files (" << asd << " .asd skipped)\n"
              << "Headers: " << headerMs << " ms for " << (files.size() - loops) << " files ("
              << (files.size() > loops ? headerMs * 1000.0 / (double) (files.size() - loops) : 0.0) << " µs each)\n"
              << "Total:   " << detectMs + walkMs + headerMs << " ms\n\n"
              << "Loops skipped:            " << loops << "\n"
              << "Too long (>4 s, >10 s cymbals): " << longOnes << "\n"
              << "Unreadable:               " << unreadable << "\n"
              << "One-shots kept:           " << oneShots << "\n"
              << "  in a drum folder:       " << drums << "\n\nDrum folders:\n";
    std::vector<std::pair<std::string, size_t>> sorted (drumFolders.begin(), drumFolders.end());
    std::sort (sorted.begin(), sorted.end(), [] (const auto& a, const auto& b) { return a.second > b.second; });
    for (const auto& [name, n] : sorted)
        std::cout << "  " << std::setw (6) << std::right << n << "  " << name << "\n";
    if (csv.is_open())
        std::cout << "\nCSV: " << csvPath << "\n";
    return 0;
}
