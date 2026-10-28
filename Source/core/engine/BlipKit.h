// Phase 0 placeholder sounds: one synthesised one-shot per pad (sine drops for the kick and toms,
// noise for the rest), so the spikes can be heard before the sampler exists (Phase 1 replaces
// this with PadVoice and real samples). Plain C++, no JUCE, no allocation: audio thread only.
//
// The closed hat chokes the open hat, as the real kit will (PLAN.md §3.1).
#pragma once

#include <array>
#include <cmath>
#include <cstdint>

namespace drumchimp
{
    class BlipKit
    {
    public:
        static constexpr int numVoices = 8;

        void prepare (double sampleRate) noexcept
        {
            sr = sampleRate > 0.0 ? sampleRate : 44100.0;
            for (auto& v : voices)
                v = {};
        }

        void trigger (int pad, float velocity) noexcept
        {
            if (pad < 0 || pad >= numVoices)
                return;
            if (pad == 3)
                voices[4].level = 0.0f;               // closed hat chokes open hat
            const auto& s = shapes[(size_t) pad];
            auto& v = voices[(size_t) pad];
            v.level = 0.2f + 0.8f * velocity;
            v.decay = (float) std::exp (-1.0 / (s.decaySeconds * sr));
            v.phase = 0.0;
            v.freq = s.startHz;
            v.freqDecay = std::exp (-1.0 / (0.04 * sr));
        }

        // Adds `n` samples to left and right.
        void render (float* left, float* right, int n) noexcept
        {
            for (int pad = 0; pad < numVoices; ++pad)
            {
                auto& v = voices[(size_t) pad];
                if (v.level < 1.0e-4f)
                    continue;
                const auto& s = shapes[(size_t) pad];
                const float pan = s.pan;
                for (int i = 0; i < n; ++i)
                {
                    float tone = 0.0f;
                    if (s.toneMix > 0.0f)
                    {
                        tone = (float) std::sin (v.phase * 6.283185307179586);
                        v.phase += v.freq / sr;
                        v.freq = s.endHz + (v.freq - s.endHz) * v.freqDecay;
                    }
                    float noise = 0.0f;
                    if (s.toneMix < 1.0f)
                    {
                        const float white = nextNoise();
                        noise = s.highpass ? white - v.lastNoise : white;
                        v.lastNoise = white;
                    }
                    const float x = v.level * 0.25f * (s.toneMix * tone + (1.0f - s.toneMix) * noise);
                    left[i] += x * (1.0f - pan);
                    right[i] += x * (1.0f + pan);
                    v.level *= v.decay;
                }
            }
        }

    private:
        struct Shape { double startHz, endHz, decaySeconds; float toneMix; bool highpass; float pan; };
        static constexpr std::array<Shape, numVoices> shapes {{
            { 160.0, 48.0, 0.35, 1.0f, false, 0.0f },    // kick
            { 220.0, 180.0, 0.14, 0.35f, false, 0.0f },  // snare
            { 0.0, 0.0, 0.12, 0.0f, false, 0.1f },       // clap
            { 0.0, 0.0, 0.035, 0.0f, true, -0.3f },      // closed hat
            { 0.0, 0.0, 0.30, 0.0f, true, -0.3f },       // open hat
            { 140.0, 95.0, 0.28, 1.0f, false, 0.3f },    // low tom
            { 240.0, 170.0, 0.22, 1.0f, false, -0.2f },  // high tom
            { 0.0, 0.0, 0.9, 0.0f, true, 0.25f },        // crash
        }};

        struct Voice { float level = 0.0f, decay = 0.0f, lastNoise = 0.0f; double phase = 0.0, freq = 0.0, freqDecay = 0.0; };

        float nextNoise() noexcept
        {
            seed ^= seed << 13;
            seed ^= seed >> 17;
            seed ^= seed << 5;
            return (float) (int32_t) seed * (1.0f / 2147483648.0f);
        }

        double sr = 44100.0;
        uint32_t seed = 0x9e3779b9u;
        std::array<Voice, numVoices> voices {};
    };
}
