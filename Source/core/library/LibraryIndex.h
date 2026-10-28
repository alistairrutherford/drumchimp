// The sample library (PLAN.md §3.2, §4): finding every sample under the roots, remembering what
// was read, and looking samples up by role. Plain C++ (std::filesystem), no JUCE. Background or
// message thread only.
//
//   SampleInfo    one sample: where it is, its length, and its classification
//   LibraryCache  what an earlier scan read from each file's header, keyed by path and checked
//                 against size and modification time. A plain text file (one line per sample:
//                 path, size, mtime, seconds, tab-separated). Classifications aren't cached: they
//                 take microseconds, so a classifier change needs no rescan.
//   scanLibrary   walks the roots: skips .asd files, other extensions and loops, reuses the cache
//                 for unchanged files, reads the header of the rest (AudioHeader.h), classifies
//   LibraryIndex  the result, with the samples of each role
#pragma once

#include "AudioHeader.h"
#include "Classifier.h"
#include "LibraryRoots.h"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <unordered_map>

namespace drumchimp
{
    struct SampleInfo
    {
        std::string root;                 // the root's name ("Core Library", a pack)
        std::string path;                 // absolute
        std::string relative;             // below the root
        double seconds = 0.0;
        int64_t size = 0;
        Classification classification;
    };

    struct LibraryCache
    {
        struct Entry { int64_t size = 0, modified = 0; double seconds = 0.0; };
        std::unordered_map<std::string, Entry> entries;

        static constexpr const char* header = "DrumChimp library cache 1";

        bool read (const std::string& file)
        {
            entries.clear();
            std::ifstream in (file);
            std::string line;
            if (! std::getline (in, line) || line != header)
                return false;
            while (std::getline (in, line))
            {
                const auto a = line.find ('\t'), b = line.find ('\t', a + 1), c = line.find ('\t', b + 1);
                if (a == std::string::npos || b == std::string::npos || c == std::string::npos)
                    continue;
                try
                {
                    entries[line.substr (0, a)] = { std::stoll (line.substr (a + 1, b - a - 1)), std::stoll (line.substr (b + 1, c - b - 1)),
                                                     std::stod (line.substr (c + 1)) };
                }
                catch (...) {}            // a damaged line is just read again
            }
            return true;
        }

        bool write (const std::string& file) const
        {
            std::error_code ec;
            std::filesystem::create_directories (std::filesystem::path (file).parent_path(), ec);
            const auto temp = file + ".tmp";
            {
                std::ofstream out (temp);
                if (! out)
                    return false;
                out << header << "\n";
                for (const auto& [path, e] : entries)
                    if (path.find_first_of ("\t\n\r") == std::string::npos)
                        out << path << '\t' << e.size << '\t' << e.modified << '\t' << e.seconds << '\n';
                if (! out)
                    return false;
            }
            std::filesystem::rename (temp, file, ec);   // whole or not at all
            return ! ec;
        }
    };

    struct ScanStats
    {
        size_t files = 0, headersRead = 0, fromCache = 0, skippedLoops = 0, unreadable = 0;
        double milliseconds = 0.0;
        bool cancelled = false;
    };

    // Samples longer than this can't go on a pad (SampleLoader), so they aren't listed.
    constexpr double maxIndexedSeconds = 30.0;

    inline std::vector<SampleInfo> scanLibrary (const std::vector<LibraryRoot>& roots, LibraryCache& cache, ScanStats& stats,
                                                const std::atomic<bool>* cancel = nullptr)
    {
        namespace fs = std::filesystem;
        const auto start = std::chrono::steady_clock::now();
        std::vector<SampleInfo> samples;
        std::unordered_map<std::string, LibraryCache::Entry> seen;

        for (const auto& root : roots)
        {
            std::error_code ec;
            for (fs::recursive_directory_iterator it (root.path, fs::directory_options::skip_permission_denied, ec), end; it != end; it.increment (ec))
            {
                if (ec)
                    break;
                if (cancel != nullptr && cancel->load (std::memory_order_relaxed))
                {
                    stats.cancelled = true;
                    return {};
                }
                if (! it->is_regular_file (ec))
                    continue;
                const auto& p = it->path();
                std::string ext = p.extension().string();
                for (auto& c : ext)
                    c = (char) std::tolower ((unsigned char) c);
                if (ext != ".wav" && ext != ".aif" && ext != ".aiff" && ext != ".flac")
                    continue;

                SampleInfo s;
                s.root = root.name;
                s.path = p.string();
                s.relative = fs::relative (p, root.path, ec).generic_string();
                ++stats.files;
                if (classifier::isLoop ({ classifier::tokenize (fs::path (s.relative).parent_path().string()) },
                                        classifier::tokenize (p.stem().string())))
                {
                    ++stats.skippedLoops;
                    continue;
                }

                s.size = (int64_t) it->file_size (ec);
                const auto modified = (int64_t) it->last_write_time (ec).time_since_epoch().count();
                if (const auto c = cache.entries.find (s.path);
                    c != cache.entries.end() && c->second.size == s.size && c->second.modified == modified)
                {
                    s.seconds = c->second.seconds;
                    ++stats.fromCache;
                }
                else
                {
                    const auto h = readAudioHeader (s.path);
                    ++stats.headersRead;
                    if (! h.ok)
                    {
                        ++stats.unreadable;
                        continue;
                    }
                    s.seconds = h.seconds();
                }
                seen[s.path] = { s.size, modified, s.seconds };
                if (s.seconds > maxIndexedSeconds)
                    continue;
                s.classification = classify (s.relative, s.seconds);
                samples.push_back (std::move (s));
            }
        }
        cache.entries = std::move (seen);         // files that went away drop out
        stats.milliseconds = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - start).count();
        return samples;
    }

    class LibraryIndex
    {
    public:
        LibraryIndex() = default;

        explicit LibraryIndex (std::vector<SampleInfo> all, std::vector<LibraryRoot> rootsScanned = {})
            : samples (std::move (all)), roots (std::move (rootsScanned))
        {
            std::sort (samples.begin(), samples.end(), [] (const SampleInfo& a, const SampleInfo& b)
            {
                return a.root != b.root ? a.root < b.root : a.relative < b.relative;
            });
            for (size_t i = 0; i < samples.size(); ++i)
                byRole[(size_t) samples[i].classification.role].push_back (i);
        }

        const std::vector<SampleInfo>& all() const noexcept { return samples; }
        const std::vector<size_t>& withRole (Role r) const noexcept { return byRole[(size_t) r]; }
        const std::vector<LibraryRoot>& scannedRoots() const noexcept { return roots; }
        size_t size() const noexcept { return samples.size(); }
        size_t drums() const noexcept { return samples.size() - byRole[(size_t) Role::unsorted].size(); }

        const SampleInfo* find (const std::string& path) const noexcept
        {
            const auto it = std::find_if (samples.begin(), samples.end(), [&] (const SampleInfo& s) { return s.path == path; });
            return it != samples.end() ? &*it : nullptr;
        }

    private:
        std::vector<SampleInfo> samples;
        std::vector<LibraryRoot> roots;
        std::array<std::vector<size_t>, numRoles> byRole;
    };
}
