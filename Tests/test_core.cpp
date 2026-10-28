// Phase 0 core tests: the test pattern, its MIDI file, the audio header reader and Library.cfg
// parsing. No JUCE.
#include "TestUtil.h"
#include "core/engine/TestPattern.h"
#include "core/library/AudioHeader.h"
#include "core/library/LibraryRoots.h"
#include <cstdio>
#include <filesystem>
#include <fstream>

using namespace drumchimp;
using testutil::check;

namespace
{
    struct Event { double beat; int pad; bool on; };

    std::vector<Event> collect (double from, double to)
    {
        std::vector<Event> out;
        testpattern::forEachEvent (from, to, [&] (double beat, int pad, uint8_t, bool on) { out.push_back ({ beat, pad, on }); });
        std::sort (out.begin(), out.end(), [] (const Event& a, const Event& b)
        {
            return a.beat != b.beat ? a.beat < b.beat : (a.pad != b.pad ? a.pad < b.pad : a.on < b.on);
        });
        return out;
    }

    void testPattern()
    {
        std::cout << "Test pattern\n";
        const auto bar = collect (0.0, 4.0);
        check (std::count_if (bar.begin(), bar.end(), [] (const Event& e) { return e.on; }) == (long) testpattern::hits.size(),
               "one bar has every hit once");
        check (std::count_if (bar.begin(), bar.end(), [] (const Event& e) { return ! e.on; }) == (long) testpattern::hits.size(),
               "and every note-off (the last ends before the bar line)");

        // The same events whatever the block size, including blocks that straddle bar lines.
        for (const double block : { 0.0123, 0.25, 1.0 / 3.0, 3.7 })
        {
            std::vector<Event> pieces;
            for (double from = 0.0; from < 8.0; from += block)
            {
                const auto part = collect (from, std::min (from + block, 8.0));
                pieces.insert (pieces.end(), part.begin(), part.end());
            }
            std::sort (pieces.begin(), pieces.end(), [] (const Event& a, const Event& b)
            {
                return a.beat != b.beat ? a.beat < b.beat : (a.pad != b.pad ? a.pad < b.pad : a.on < b.on);
            });
            const auto whole = collect (0.0, 8.0);
            bool same = pieces.size() == whole.size();
            for (size_t i = 0; same && i < whole.size(); ++i)
                same = std::abs (pieces[i].beat - whole[i].beat) < 1.0e-9 && pieces[i].pad == whole[i].pad && pieces[i].on == whole[i].on;
            check (same, "block size " + std::to_string (block) + " gives the same events as one block");
        }

        // Blocks: 120 BPM at 44.1 kHz puts every other sixteenth between two samples (5512.5
        // samples apart). Each note-on must land on the first sample at or after its beat, once.
        for (const int blockSize : { 1, 64, 441, 512, 4096 })
        {
            const double bps = 120.0 / 60.0 / 44100.0;
            std::vector<std::pair<int64_t, int>> ons;                // (global sample, pad)
            const int64_t total = 2 * 4 * 22050;                    // two bars
            for (int64_t start = 0; start < total; start += blockSize)
                testpattern::forEachEventInBlock ((double) start * bps, bps, blockSize, [&] (int sample, int pad, uint8_t, bool on)
                {
                    if (on && start + sample < total)               // the last block may run into bar 3
                        ons.push_back ({ start + sample, pad });
                });
            bool exact = ons.size() == 2 * testpattern::hits.size();
            for (const auto& [sample, pad] : ons)
            {
                const auto it = std::find_if (testpattern::hits.begin(), testpattern::hits.end(), [&, s = sample, p = pad] (const auto& h)
                {
                    const double beat = std::fmod ((double) s * bps, 4.0);
                    const double hitBeat = h.step * testpattern::stepBeats;
                    return h.pad == p && beat >= hitBeat - 1.0e-9 && beat < hitBeat + bps;   // at or within one sample after
                });
                exact = exact && it != testpattern::hits.end();
            }
            check (exact, "block size " + std::to_string (blockSize) + ": every note-on once, on the first sample at or after its beat");
        }

        check (collect (-4.0, 0.0).size() == 2 * testpattern::hits.size(), "negative positions (pre-roll) loop too");
        check (collect (2.0, 2.0).empty(), "an empty window has no events");
        check (padForNote (NoteMap::gm, 36) == 0 && padForNote (NoteMap::gm, 49) == 7 && padForNote (NoteMap::gm, 60) == -1, "GM: note → pad");
        check (padForNote (NoteMap::chromatic, 43) == 7 && noteForPad (NoteMap::chromatic, 2) == 38 && padForNote (NoteMap::chromatic, 49) == -1, "Chromatic: note ↔ pad");

        const auto song = testpattern::testPatternSong (4, 97.0);
        const auto bytes = writeMidiFile (song);
        MidiSong back;
        std::string error;
        check (readMidiFile (bytes.data(), bytes.size(), back, error), "the 4-bar MIDI file reads back");
        check (back.notes.size() == 4 * testpattern::hits.size(), "with every note");
        check (std::abs (back.bpm - 97.0) < 0.01, "and its tempo");
        check (back.notes.back().start == (int64_t) ((3 * 4.0 + 15 * 0.25) * 960), "the last note is bar 4's sixteenth 16");
    }

    void put (std::ofstream& out, std::initializer_list<int> bytes)
    {
        for (int b : bytes)
            out.put ((char) b);
    }

    void putLE (std::ofstream& out, uint32_t v, int n)
    {
        for (int i = 0; i < n; ++i)
            out.put ((char) ((v >> (8 * i)) & 0xff));
    }

    void putBE (std::ofstream& out, uint32_t v, int n)
    {
        for (int i = n - 1; i >= 0; --i)
            out.put ((char) ((v >> (8 * i)) & 0xff));
    }

    void testAudioHeader()
    {
        std::cout << "Audio header\n";
        const auto dir = std::filesystem::temp_directory_path() / "drumchimp_test_core";
        std::filesystem::create_directories (dir);

        // WAV: 48 kHz stereo 24-bit, a LIST chunk before fmt and an odd-sized chunk, 12,000 frames.
        const auto wav = (dir / "a.wav").string();
        {
            std::ofstream out (wav, std::ios::binary);
            out << "RIFF";
            putLE (out, 0, 4);
            out << "WAVE" << "LIST";
            putLE (out, 3, 4);
            put (out, { 1, 2, 3, 0 });                      // 3 bytes + pad
            out << "fmt ";
            putLE (out, 16, 4);
            putLE (out, 1, 2);
            putLE (out, 2, 2);
            putLE (out, 48000, 4);
            putLE (out, 48000 * 6, 4);
            putLE (out, 6, 2);
            putLE (out, 24, 2);
            out << "data";
            putLE (out, 12000 * 6, 4);
        }
        const auto w = readAudioHeader (wav);
        check (w.ok && w.sampleRate == 48000.0 && w.channels == 2 && w.bitsPerSample == 24 && w.frames == 12000, "WAV header");
        check (std::abs (w.seconds() - 0.25) < 1.0e-9, "WAV length 0.25 s");

        // AIFF: 44.1 kHz mono 16-bit, 88,200 frames (2 s).
        const auto aif = (dir / "a.aif").string();
        {
            std::ofstream out (aif, std::ios::binary);
            out << "FORM";
            putBE (out, 0, 4);
            out << "AIFF" << "COMM";
            putBE (out, 18, 4);
            putBE (out, 1, 2);
            putBE (out, 88200, 4);
            putBE (out, 16, 2);
            put (out, { 0x40, 0x0e, 0xac, 0x44, 0, 0, 0, 0, 0, 0 });   // 44100 as 80-bit extended
        }
        const auto a = readAudioHeader (aif);
        check (a.ok && a.sampleRate == 44100.0 && a.channels == 1 && a.frames == 88200, "AIFF header");
        check (std::abs (a.seconds() - 2.0) < 1.0e-9, "AIFF length 2 s");

        // FLAC: STREAMINFO for 96 kHz, 2 channels, 24-bit, 48,000 samples.
        const auto flac = (dir / "a.flac").string();
        {
            std::ofstream out (flac, std::ios::binary);
            out << "fLaC";
            put (out, { 0x80, 0, 0, 34 });
            put (out, { 0x10, 0, 0x10, 0, 0, 0, 0, 0, 0, 0 });       // block and frame sizes
            const uint32_t rate = 96000, ch = 1, bps = 23;
            const uint64_t total = 48000;
            put (out, { (int) (rate >> 12) & 0xff, (int) (rate >> 4) & 0xff, (int) (((rate & 0xf) << 4) | (ch << 1) | (bps >> 4)),
                        (int) (((bps & 0xf) << 4) | (uint32_t) (total >> 32)), (int) (total >> 24) & 0xff, (int) (total >> 16) & 0xff,
                        (int) (total >> 8) & 0xff, (int) total & 0xff });
            for (int i = 0; i < 16; ++i)
                out.put (0);                                         // MD5
        }
        const auto f = readAudioHeader (flac);
        check (f.ok && f.sampleRate == 96000.0 && f.channels == 2 && f.bitsPerSample == 24 && f.frames == 48000, "FLAC header");

        const auto bad = (dir / "bad.wav").string();
        {
            std::ofstream out (bad, std::ios::binary);
            out << "not audio at all";
        }
        check (! readAudioHeader (bad).ok, "not audio: not ok");
        check (! readAudioHeader ((dir / "missing.wav").string()).ok, "a missing file: not ok");
        std::filesystem::remove_all (dir);
    }

    void testLibraryCfg()
    {
        std::cout << "Library.cfg\n";
        const std::string cfg = R"(<?xml version="1.0" encoding="UTF-8"?>
<Ableton MajorVersion="5">
	<ContentLibrary>
		<UserLibrary>
			<LibraryProject Id="0">
				<ProjectLocation />
				<ProjectName Value="User Library" />
				<ProjectPath Value="/Users/me/Music/Ableton" />
			</LibraryProject>
		</UserLibrary>
		<SliceInfoList>
			<LibrarySliceInfo Id="808" Path="/Users/me/Music/Ableton/Factory Packs/Drum Essentials" DisplayName="Drum Essentials" UniqueId="www.ableton.com/249" />
			<LibrarySliceInfo Id="809" Path="/Volumes/Big Disk/Packs/Beat Tools" DisplayName="Beat Tools &amp; More" UniqueId="www.ableton.com/1" />
		</SliceInfoList>
		<UserFolderInfoList>
			<UserFolderInfo Id="17" Path="/Users/me/Music/counterpoint machine/samples/" DisplayName="samples" IconName="" />
		</UserFolderInfoList>
	</ContentLibrary>
</Ableton>)";
        const auto roots = parseLibraryCfg (cfg);
        check (roots.size() == 4, "four roots");
        if (roots.size() != 4)
            return;
        check (roots[0].kind == LibraryRoot::Kind::userLibrary && roots[0].path == "/Users/me/Music/Ableton/User Library", "User Library");
        check (roots[1].kind == LibraryRoot::Kind::pack && roots[1].name == "Drum Essentials", "a pack");
        check (roots[2].path == "/Volumes/Big Disk/Packs/Beat Tools" && roots[2].name == "Beat Tools & More", "a pack elsewhere, entities unescaped");
        check (roots[3].kind == LibraryRoot::Kind::userFolder && roots[3].path == "/Users/me/Music/counterpoint machine/samples",
               "a user folder, trailing slash dropped");
        check (parseLibraryCfg ("").empty(), "empty text: no roots");
        check (libraryroots::versionParts ("Live 12.4.10") > libraryroots::versionParts ("Live 12.4.7"), "12.4.10 is newer than 12.4.7");
    }
}

int main()
{
    testPattern();
    testAudioHeader();
    testLibraryCfg();
    return testutil::finish ("test_core");
}
