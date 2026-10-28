// Core tests: the note maps, the audio header reader and Library.cfg parsing. No JUCE. (The
// Phase 0 test pattern's tests went with it in Phase 4; test_pattern.cpp covers playback.)
#include "TestUtil.h"
#include "core/engine/NoteMap.h"
#include "core/library/AudioHeader.h"
#include "core/library/LibraryRoots.h"
#include <cstdio>
#include <filesystem>
#include <fstream>

using namespace drumchimp;
using testutil::check;

namespace
{
    void testNoteMap()
    {
        std::cout << "Note map\n";
        check (padForNote (NoteMap::gm, 36) == 0 && padForNote (NoteMap::gm, 49) == 7 && padForNote (NoteMap::gm, 60) == -1, "GM: note → pad");
        check (padForNote (NoteMap::chromatic, 43) == 7 && noteForPad (NoteMap::chromatic, 2) == 38 && padForNote (NoteMap::chromatic, 49) == -1, "Chromatic: note ↔ pad");
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
        check (a.ok && a.sampleRate == 44100.0 && a.channels == 1 && a.frames == 88200 && ! a.protectedFile, "AIFF header");

        // An Ableton Pack sample: AIFC, FVER, then an `able` chunk before COMM.
        const auto pack = (dir / "pack.aif").string();
        {
            std::ofstream out (pack, std::ios::binary);
            out << "FORM";
            putBE (out, 0, 4);
            out << "AIFC" << "FVER";
            putBE (out, 4, 4);
            putBE (out, 0xa2805140, 4);
            out << "able";
            putBE (out, 6, 4);
            put (out, { 1, 2, 3, 4, 5, 6 });
            out << "COMM";
            putBE (out, 18, 4);
            putBE (out, 1, 2);
            putBE (out, 44100, 4);
            putBE (out, 16, 2);
            put (out, { 0x40, 0x0e, 0xac, 0x44, 0, 0, 0, 0, 0, 0 });
        }
        const auto protectedHeader = readAudioHeader (pack);
        check (protectedHeader.ok && protectedHeader.protectedFile && protectedHeader.frames == 44100, "an Ableton Pack sample reads, and is marked protected");
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
    testNoteMap();
    testAudioHeader();
    testLibraryCfg();
    return testutil::finish ("test_core");
}
