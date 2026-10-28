// Ableton's Drum Rack presets as kits (PLAN.md §3.3, Phase 7: curated kits). Plain C++, no JUCE.
// Message or background thread only.
//
// A Drum Rack preset (.adg) is gzipped XML; the plug-in unzips it (Library.cpp). Each pad is a
// <DrumBranchPreset> with a <ReceivingNote Value="N"/> (stored as 128 − the MIDI note: 92 is C1)
// and, somewhere inside, a sample: a <FileRef> whose <RelativePath> is below its library (the
// Core Library's racks use the Core Library's own samples) and whose <Path> was absolute on the
// machine that saved it.
//
//   RackPad, DrumRack are in LibraryIndex.h (the index keeps the racks it can play)
//   parseDrumRack   the XML → each pad's note, relative path and absolute path
//   rackKit         a rack's samples onto the pads' roles: each unlocked pad takes the rack's
//                   sample of its role (as the library sorted it) nearest the role's usual note
//                   (kick 36, snare 38, closed hat 42 …), never the same sample twice. A role the
//                   rack hasn't got keeps the pad's sample.
#pragma once

#include "KitBuilder.h"

namespace drumchimp
{
    namespace drumrack
    {
        inline std::string attribute (const std::string& xml, const std::string& element, size_t from, size_t to)
        {
            const auto at = xml.find ("<" + element + " Value=\"", from);
            if (at == std::string::npos || at >= to)
                return {};
            const auto start = at + element.size() + 9;
            const auto end = xml.find ('"', start);
            return end == std::string::npos ? std::string() : libraryroots::unescapeXml (xml.substr (start, end - start));
        }

        inline bool isAudio (const std::string& path)
        {
            const auto dot = path.rfind ('.');
            if (dot == std::string::npos)
                return false;
            std::string ext = path.substr (dot);
            for (auto& c : ext)
                c = (char) std::tolower ((unsigned char) c);
            return ext == ".wav" || ext == ".aif" || ext == ".aiff" || ext == ".flac";
        }

        inline int preferredNote (PadRole role)
        {
            switch (role)
            {
                case PadRole::kick:      return 36;
                case PadRole::rim:       return 37;
                case PadRole::snare:     return 38;
                case PadRole::clap:      return 39;
                case PadRole::closedHat: return 42;
                case PadRole::lowTom:    return 43;
                case PadRole::openHat:   return 46;
                case PadRole::midTom:    return 47;
                case PadRole::crash:     return 49;
                case PadRole::highTom:   return 50;
                case PadRole::ride:      return 51;
                case PadRole::perc:      return 56;
            }
            return 36;
        }
    }

    inline std::vector<RackPad> parseDrumRack (const std::string& xml)
    {
        using namespace drumrack;
        std::vector<RackPad> pads;
        const std::string open = "<DrumBranchPreset", close = "</DrumBranchPreset>";
        for (size_t at = xml.find (open); at != std::string::npos; at = xml.find (open, at + open.size()))
        {
            const auto end = xml.find (close, at);
            if (end == std::string::npos)
                break;
            const auto noteText = attribute (xml, "ReceivingNote", at, end);
            if (noteText.empty())
                continue;
            RackPad pad;
            pad.note = 128 - std::atoi (noteText.c_str());
            // The first audio file in the pad: its RelativePath, then the Path that follows it.
            for (size_t r = xml.find ("<RelativePath Value=\"", at); r != std::string::npos && r < end; r = xml.find ("<RelativePath Value=\"", r + 1))
            {
                const auto relative = attribute (xml, "RelativePath", r, end);
                if (isAudio (relative))
                {
                    pad.relativePath = relative;
                    pad.path = attribute (xml, "Path", r, end);
                    break;
                }
            }
            if (pad.note >= 0 && pad.note < 128 && ! pad.relativePath.empty())
                pads.push_back (pad);
        }
        return pads;
    }

    // The rack's samples for the unlocked pads (`current` for the rest).
    inline KitPaths rackKit (const DrumRack& rack, const LibraryIndex& index, const std::array<PadRole, numPads>& roles,
                             const KitPaths& current, const std::array<bool, numPads>& locked)
    {
        KitPaths out = current;
        std::set<std::string> used;
        for (int pad = 0; pad < numPads; ++pad)
            if (locked[(size_t) pad])
                used.insert (current[(size_t) pad]);

        for (int pad = 0; pad < numPads; ++pad)
        {
            if (locked[(size_t) pad])
                continue;
            const auto role = roles[(size_t) pad];
            const auto range = wantedRange (role);
            const RackPad* best = nullptr;
            int bestScore = 1 << 30;
            for (const auto& p : rack.pads)
            {
                const auto* s = index.find (p.resolved);
                if (s == nullptr || used.count (p.resolved) > 0 || s->classification.role != libraryRole (role))
                    continue;
                int score = std::abs (p.note - drumrack::preferredNote (role));
                if (range != TomRange::unknown && s->classification.tomRange != range)
                    score += s->classification.tomRange == TomRange::unknown ? 10 : 30;
                if (score < bestScore)
                {
                    best = &p;
                    bestScore = score;
                }
            }
            if (best != nullptr)
            {
                out[(size_t) pad] = best->resolved;
                used.insert (best->resolved);
            }
        }
        return out;
    }
}
