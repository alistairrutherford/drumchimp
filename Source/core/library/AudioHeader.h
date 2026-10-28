// Reads an audio file's format and length from its header, without decoding any audio (PLAN.md
// §3.2 step 4: the classifier skips long files). WAV, AIFF/AIFC and FLAC. Plain C++, no JUCE.
// File I/O: message or background thread only.
//
// `protectedFile`: an AIFC with an `able` chunk. Every sample in Ableton's installed Packs is one
// (found in Phase 4): its header reads, but its audio is protected, and only Live can play it
// (macOS's own afinfo can't open it either). The Core Library's samples aren't protected.
#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>

namespace drumchimp
{
    struct AudioHeader
    {
        bool ok = false;
        double sampleRate = 0.0;
        int channels = 0;
        int bitsPerSample = 0;
        uint64_t frames = 0;
        bool protectedFile = false;

        double seconds() const noexcept { return sampleRate > 0.0 ? (double) frames / sampleRate : 0.0; }
    };

    namespace audioheader
    {
        inline uint32_t le32 (const unsigned char* p) { return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24); }
        inline uint16_t le16 (const unsigned char* p) { return (uint16_t) (p[0] | (p[1] << 8)); }
        inline uint32_t be32 (const unsigned char* p) { return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | (uint32_t) p[3]; }
        inline uint16_t be16 (const unsigned char* p) { return (uint16_t) ((p[0] << 8) | p[1]); }

        // AIFF's sample rate: an 80-bit IEEE extended float, big-endian.
        inline double extended80 (const unsigned char* p)
        {
            const int exponent = ((p[0] & 0x7f) << 8) | p[1];
            uint64_t mantissa = 0;
            for (int i = 0; i < 8; ++i)
                mantissa = (mantissa << 8) | p[2 + i];
            if (exponent == 0 && mantissa == 0)
                return 0.0;
            const double v = std::ldexp ((double) mantissa, exponent - 16383 - 63);
            return (p[0] & 0x80) ? -v : v;
        }

        inline AudioHeader readWav (std::ifstream& in)
        {
            AudioHeader h;
            unsigned char chunk[8];
            int blockAlign = 0;
            bool haveFormat = false, haveData = false;
            uint64_t dataBytes = 0;
            while (in.read ((char*) chunk, 8))
            {
                const uint32_t size = le32 (chunk + 4);
                const auto next = (std::streamoff) in.tellg() + (std::streamoff) size + (size & 1);
                if (std::memcmp (chunk, "fmt ", 4) == 0 && size >= 16)
                {
                    unsigned char f[16];
                    if (! in.read ((char*) f, 16))
                        break;
                    h.channels = le16 (f + 2);
                    h.sampleRate = le32 (f + 4);
                    blockAlign = le16 (f + 12);
                    h.bitsPerSample = le16 (f + 14);
                    haveFormat = true;
                }
                else if (std::memcmp (chunk, "data", 4) == 0)
                {
                    dataBytes = size;
                    haveData = true;
                }
                if (haveFormat && haveData)
                    break;
                in.seekg (next);
            }
            if (haveFormat && haveData && blockAlign > 0)
            {
                h.frames = dataBytes / (uint64_t) blockAlign;
                h.ok = h.sampleRate > 0.0 && h.channels > 0;
            }
            return h;
        }

        inline AudioHeader readAiff (std::ifstream& in)
        {
            AudioHeader h;
            unsigned char chunk[8];
            while (in.read ((char*) chunk, 8))
            {
                const uint32_t size = be32 (chunk + 4);
                const auto next = (std::streamoff) in.tellg() + (std::streamoff) size + (size & 1);
                if (std::memcmp (chunk, "able", 4) == 0)
                    h.protectedFile = true;       // Ableton's protected pack audio (it comes before COMM)
                if (std::memcmp (chunk, "COMM", 4) == 0 && size >= 18)
                {
                    unsigned char c[18];
                    if (! in.read ((char*) c, 18))
                        break;
                    h.channels = be16 (c);
                    h.frames = be32 (c + 2);
                    h.bitsPerSample = be16 (c + 6);
                    h.sampleRate = extended80 (c + 8);
                    h.ok = h.sampleRate > 0.0 && h.channels > 0;
                    return h;
                }
                in.seekg (next);
            }
            return h;
        }

        inline AudioHeader readFlac (std::ifstream& in)
        {
            AudioHeader h;
            unsigned char b[4 + 34];                  // metadata block header + STREAMINFO
            if (! in.read ((char*) b, sizeof (b)) || (b[0] & 0x7f) != 0)
                return h;
            const unsigned char* s = b + 4 + 10;      // after the block and frame sizes
            h.sampleRate = (double) (((uint32_t) s[0] << 12) | ((uint32_t) s[1] << 4) | (s[2] >> 4));
            h.channels = ((s[2] >> 1) & 7) + 1;
            h.bitsPerSample = (((s[2] & 1) << 4) | (s[3] >> 4)) + 1;
            h.frames = ((uint64_t) (s[3] & 0x0f) << 32) | ((uint64_t) s[4] << 24) | ((uint64_t) s[5] << 16) | ((uint64_t) s[6] << 8) | s[7];
            h.ok = h.sampleRate > 0.0;
            return h;
        }
    }

    inline AudioHeader readAudioHeader (const std::string& path)
    {
        using namespace audioheader;
        std::ifstream in (path, std::ios::binary);
        unsigned char id[12];
        if (! in.read ((char*) id, 12))
            return {};
        if (std::memcmp (id, "RIFF", 4) == 0 && std::memcmp (id + 8, "WAVE", 4) == 0)
            return readWav (in);
        if (std::memcmp (id, "FORM", 4) == 0 && (std::memcmp (id + 8, "AIFF", 4) == 0 || std::memcmp (id + 8, "AIFC", 4) == 0))
            return readAiff (in);
        if (std::memcmp (id, "fLaC", 4) == 0)
        {
            in.seekg (4);
            return readFlac (in);
        }
        return {};
    }
}
