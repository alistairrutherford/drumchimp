// The pattern generator (PLAN.md §3.4): (style, settings, the pads' roles, seed) → Pattern. A pure
// function, run on the message thread. Plain C++, no JUCE.
//
//   Steps     each style step plays with a probability:
//               X  always                          o  Complexity × (0.15 + 0.85 Density)
//               x, r  min (1, 0.3 + 1.4 Density)   g  with Ghost notes: Complexity × (0.4 + 0.6 Density)
//               .  only in an `extras` lane: Density × Complexity² × 0.15
//             A roll (r) has 2 hits with probability Complexity, 3 with Complexity / 3.
//   Random    the draws for every step are made in the same order whatever the settings, so the
//             same seed gives the same pattern, more Density only ever adds hits, and more
//             Complexity only ever adds o, g and extra hits.
//   Pads      a lane plays on every pad of its role. With no such pad: a ride lane plays on the
//             closed hats, a rim lane on the snares, a tom lane on any tom; otherwise it's silent.
//   Feel      velocity (X 118, x/r 100, o 86, g 42, extras 70; x on the beat +8) and timing vary by
//             up to ±14 velocity and ±12 ms with Humanise (anchors half as much).
//   Fills     in every Nth bar (Fill: every 2, 4 or 8), from the last beat (the last two beats at
//             Complexity > 0.6): everything but the kick stops, and toms run down (high → low) or
//             the snare builds, with rolls at high Complexity. A crash follows on the next bar's
//             one (and on the pattern's first beat, for crash = one styles).
//   Locks     a locked pad's lane is copied from the previous pattern.
#pragma once

#include "../kit/KitBuilder.h"
#include "Pattern.h"
#include "Style.h"

namespace drumchimp
{
    struct GeneratorSettings
    {
        int bars = 4;
        float density = 0.5f, complexity = 0.4f, humanise = 0.2f;
        bool ghosts = true;
        int fillEvery = 4;                // 0: no fills
        uint64_t seed = 1;
    };

    namespace generator
    {
        // The pads a lane plays on.
        inline std::vector<int> padsFor (const std::string& lane, const std::array<PadRole, numPads>& roles)
        {
            const auto with = [&] (auto&& match)
            {
                std::vector<int> pads;
                for (int p = 0; p < numPads; ++p)
                    if (match (roles[(size_t) p]))
                        pads.push_back (p);
                return pads;
            };
            const auto isTom = [] (PadRole r) { return r == PadRole::lowTom || r == PadRole::midTom || r == PadRole::highTom; };
            if (lane == "tom")
                return with (isTom);
            const auto role = padRoleFromId (lane, PadRole::kick);
            auto pads = with ([&] (PadRole r) { return r == role; });
            if (pads.empty() && lane == "ride")
                pads = with ([] (PadRole r) { return r == PadRole::closedHat; });
            if (pads.empty() && lane == "rim")
                pads = with ([] (PadRole r) { return r == PadRole::snare; });
            if (pads.empty() && isTom (role))
                pads = with (isTom);
            return pads;
        }

        inline void place (Cell& cell, int velocity, int repeats, float offsetMs)
        {
            velocity = std::clamp (velocity, 1, 127);
            if (velocity > cell.velocity)
                cell = { (uint8_t) velocity, (uint8_t) repeats, offsetMs };
        }
    }

    inline Pattern generatePattern (const Style& style, const GeneratorSettings& settings, const std::array<PadRole, numPads>& roles,
                                    const Pattern* previous = nullptr, const std::array<bool, numPads>* lockedLanes = nullptr)
    {
        using namespace generator;
        Pattern out;
        out.stepsPerBar = style.grid;
        out.bars = std::clamp (settings.bars, 1, maxPatternSteps / style.grid);
        const float d = std::clamp (settings.density, 0.0f, 1.0f), c = std::clamp (settings.complexity, 0.0f, 1.0f);
        const float h = std::clamp (settings.humanise, 0.0f, 1.0f);
        kitbuilder::Random random { settings.seed * 0x9e3779b97f4a7c15ull + 0x2545f4914f6cdd1dull };
        const int beat = style.grid / 4;

        for (int bar = 0; bar < out.bars; ++bar)
            for (const auto& lane : style.lanes)
            {
                const auto pads = padsFor (lane.role, roles);
                const bool extras = std::find (style.extras.begin(), style.extras.end(), lane.role) != style.extras.end();
                const int laneBars = (int) lane.steps.size() / style.grid;
                for (int s = 0; s < style.grid; ++s)
                {
                    // Always four draws, in this order, so settings never shift the random stream.
                    const double uHit = random.uniform(), uVelocity = random.uniform(), uTime = random.uniform(), uRoll = random.uniform();
                    const char symbol = lane.steps[(size_t) ((bar % laneBars) * style.grid + s)];
                    double p = 0.0;
                    int velocity = 0;
                    switch (symbol)
                    {
                        case 'X': p = 1.0; velocity = 118; break;
                        case 'x':
                        case 'r': p = std::min (1.0, 0.3 + 1.4 * d); velocity = 100 + (s % beat == 0 ? 8 : 0); break;
                        case 'o': p = c * (0.15 + 0.85 * d); velocity = 86; break;
                        case 'g': p = settings.ghosts ? c * (0.4 + 0.6 * d) : 0.0; velocity = 42; break;
                        default:  p = extras ? d * c * c * 0.15 : 0.0; velocity = 70; break;
                    }
                    if (uHit >= p || pads.empty())
                        continue;
                    const float spread = symbol == 'X' ? 0.5f : 1.0f;
                    velocity += (int) std::lround ((uVelocity - 0.5) * 2.0 * h * 14.0 * spread);
                    const float offset = (float) std::round ((uTime - 0.5) * 2.0 * h * 12.0 * spread * 10.0) / 10.0f;   // to 0.1 ms: saved exactly
                    const int repeats = symbol == 'r' ? (uRoll < c / 3.0 ? 3 : uRoll < c ? 2 : 1) : 1;
                    for (const int pad : pads)
                        place (out.lanes[(size_t) pad][(size_t) (bar * style.grid + s)], velocity, repeats, offset);
                }
            }

        // Fills, then the crash that follows them, from a stream of their own.
        kitbuilder::Random fillRandom { settings.seed * 0xbf58476d1ce4e5b9ull + 0x94d049bb133111ebull };
        const auto crashes = padsFor ("crash", roles);
        const auto crashOn = [&] (int bar)
        {
            for (const int pad : crashes)
                place (out.lanes[(size_t) pad][(size_t) (bar * style.grid)], 112, 1, 0.0f);
        };
        if (style.crashOnOne)
            crashOn (0);

        if (settings.fillEvery > 0 && style.fill != Style::Fill::none)
        {
            const auto kicks = padsFor ("kick", roles);
            std::vector<int> toms;
            for (const auto role : { PadRole::highTom, PadRole::midTom, PadRole::lowTom })
                for (int p = 0; p < numPads; ++p)
                    if (roles[(size_t) p] == role)
                        toms.push_back (p);
            const auto snares = padsFor ("snare", roles);
            const bool useToms = style.fill == Style::Fill::toms && ! toms.empty();

            for (int bar = settings.fillEvery - 1; bar < out.bars; bar += settings.fillEvery)
            {
                const int from = c > 0.6f ? style.grid / 2 : style.grid - beat;
                for (int pad = 0; pad < numPads; ++pad)
                    if (std::find (kicks.begin(), kicks.end(), pad) == kicks.end())
                        for (int s = from; s < style.grid; ++s)
                            out.lanes[(size_t) pad][(size_t) (bar * style.grid + s)] = {};
                const int length = style.grid - from;
                for (int s = from; s < style.grid; ++s)
                {
                    const double u = fillRandom.uniform(), uRoll = fillRandom.uniform();
                    const bool edge = s == from || s == style.grid - 1;
                    if (! edge && u >= 0.45 + 0.55 * d)
                        continue;
                    const int i = s - from;
                    const int velocity = 80 + 35 * i / std::max (1, length - 1);
                    const int repeats = ! useToms && c > 0.5f && i >= length - 2 && uRoll < c ? 2 : 1;
                    if (useToms)
                    {
                        const int pad = toms[(size_t) std::min ((int) toms.size() - 1, i * (int) toms.size() / length)];
                        place (out.lanes[(size_t) pad][(size_t) (bar * style.grid + s)], velocity, 1, 0.0f);
                        if (i == 0)
                            for (const int sn : snares)
                                place (out.lanes[(size_t) sn][(size_t) (bar * style.grid + s)], velocity, 1, 0.0f);
                    }
                    else
                        for (const int sn : snares)
                            place (out.lanes[(size_t) sn][(size_t) (bar * style.grid + s)], velocity, repeats, 0.0f);
                }
                crashOn ((bar + 1) % out.bars);
            }
        }

        if (previous != nullptr && lockedLanes != nullptr && previous->stepsPerBar == out.stepsPerBar && previous->bars == out.bars)
            for (int pad = 0; pad < numPads; ++pad)
                if ((*lockedLanes)[(size_t) pad])
                    out.lanes[(size_t) pad] = previous->lanes[(size_t) pad];
        return out;
    }
}
