// Phase 2: scanning and the index (core/library/LibraryIndex.h), on a small library made here:
// what's found and skipped, the cache (a second scan reads no headers; a changed file is read
// again; a deleted one drops out), cancelling, and lookups by role.
#include "TestUtil.h"
#include "core/library/LibraryIndex.h"
#include <thread>

using namespace drumchimp;
using testutil::check;
namespace fs = std::filesystem;

namespace
{
    void putLE (std::ofstream& out, uint32_t v, int n)
    {
        for (int i = 0; i < n; ++i)
            out.put ((char) ((v >> (8 * i)) & 0xff));
    }

    // A 16-bit mono 48 kHz WAV header (and no audio) for `seconds`.
    void writeWav (const fs::path& file, double seconds)
    {
        fs::create_directories (file.parent_path());
        std::ofstream out (file, std::ios::binary);
        out << "RIFF";
        putLE (out, 36, 4);
        out << "WAVE" << "fmt ";
        putLE (out, 16, 4);
        putLE (out, 1, 2);
        putLE (out, 1, 2);
        putLE (out, 48000, 4);
        putLE (out, 96000, 4);
        putLE (out, 2, 2);
        putLE (out, 16, 2);
        out << "data";
        putLE (out, (uint32_t) (seconds * 48000) * 2, 4);
    }

    void test()
    {
        const auto dir = fs::temp_directory_path() / "drumchimp_test_library";
        fs::remove_all (dir);
        const auto pack = dir / "My Pack";
        writeWav (pack / "Samples/Drums/Kick/Kick One.wav", 0.4);
        writeWav (pack / "Samples/Drums/Snare/Snare Rimshot.wav", 0.2);
        writeWav (pack / "Samples/Drums/Hihat/Hihat Open.wav", 0.8);
        writeWav (pack / "Samples/Drums/Hihat/Hihat 2.wav", 0.1);
        writeWav (pack / "Samples/Synth/Pluck C3.wav", 1.0);
        writeWav (pack / "Samples/Loops/Beat 120 bpm.wav", 2.0);
        writeWav (pack / "Samples/Drums/Kick/Kick Long Tail.wav", 45.0);   // too long for a pad
        { std::ofstream (pack / "Samples/Drums/Kick/Kick One.wav.asd") << "analysis"; }
        { std::ofstream (pack / "Samples/Drums/Kick/readme.txt") << "text"; }
        { std::ofstream (pack / "Samples/Drums/Kick/Broken.wav") << "not audio"; }
        const std::vector<LibraryRoot> roots { { LibraryRoot::Kind::pack, "My Pack", pack.string() } };

        std::cout << "Scan\n";
        LibraryCache cache;
        ScanStats stats;
        auto samples = scanLibrary (roots, cache, stats);
        check (stats.files == 8, "8 audio files found (.asd and .txt skipped): " + std::to_string (stats.files));
        check (stats.skippedLoops == 1, "the loop skipped");
        check (stats.unreadable == 1, "the broken file reported");
        check (samples.size() == 5, "5 samples listed (the 45 s one isn't): " + std::to_string (samples.size()));

        const LibraryIndex index (samples, roots);
        check (index.withRole (Role::kick).size() == 1 && index.withRole (Role::rim).size() == 1, "a kick and a rim (the snare folder's Rimshot)");
        check (index.withRole (Role::openHat).size() == 1 && index.withRole (Role::closedHat).size() == 1, "an open hat, and a short hat taken as closed");
        check (index.withRole (Role::unsorted).size() == 1 && index.drums() == 4, "the synth pluck is Unsorted; 4 drums");
        const auto* kick = index.find ((pack / "Samples/Drums/Kick/Kick One.wav").string());
        check (kick != nullptr && kick->root == "My Pack" && kick->relative == "Samples/Drums/Kick/Kick One.wav"
                   && std::abs (kick->seconds - 0.4) < 1.0e-3, "a sample's root, relative path and length");

        std::cout << "Cache\n";
        const auto cacheFile = (dir / "cache/library.tsv").string();
        check (cache.write (cacheFile), "written");
        LibraryCache reread;
        check (reread.read (cacheFile) && reread.entries.size() == cache.entries.size(), "read back: " + std::to_string (reread.entries.size()) + " entries");
        ScanStats again;
        samples = scanLibrary (roots, reread, again);
        check (again.headersRead == 1 && again.fromCache == 6, "a second scan reads no headers but the broken file's ("
                                                                   + std::to_string (again.headersRead) + " read, " + std::to_string (again.fromCache) + " cached)");

        std::this_thread::sleep_for (std::chrono::milliseconds (20));
        writeWav (pack / "Samples/Drums/Kick/Kick One.wav", 0.9);   // changed
        fs::remove (pack / "Samples/Drums/Hihat/Hihat 2.wav");      // gone
        ScanStats changed;
        samples = scanLibrary (roots, reread, changed);
        const LibraryIndex after (samples, roots);
        const auto* k = after.find ((pack / "Samples/Drums/Kick/Kick One.wav").string());
        check (changed.headersRead == 2 && k != nullptr && std::abs (k->seconds - 0.9) < 1.0e-3, "a changed file is read again");
        check (after.withRole (Role::closedHat).empty() && reread.entries.count ((pack / "Samples/Drums/Hihat/Hihat 2.wav").string()) == 0,
               "a deleted one drops out of the index and the cache");
        LibraryCache bad;
        { std::ofstream (dir / "bad.tsv") << "something else\n"; }
        check (! bad.read ((dir / "bad.tsv").string()) && bad.entries.empty(), "a cache file of another kind is ignored");

        std::cout << "Cancel\n";
        std::atomic<bool> cancel { true };
        ScanStats cancelled;
        LibraryCache none;
        check (scanLibrary (roots, none, cancelled, &cancel).empty() && cancelled.cancelled, "a cancelled scan returns nothing");
        fs::remove_all (dir);
    }
}

int main()
{
    test();
    return testutil::finish ("test_library");
}
