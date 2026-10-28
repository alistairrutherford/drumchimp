// The sampler (PLAN.md §4, PadVoice and SampleBank): eight pads of one-shot samples, played from
// triggers at exact sample offsets. Plain C++, no JUCE.
//
//   SampleData   a decoded sample at its own rate (the voices resample: Tune needs that anyway,
//                and a host rate change needs no reload). Immutable once made.
//   KitBank      the sample of each pad, and the browser's preview (slot `previewSlot`, played
//                with default settings). Made on the message thread and handed to the audio
//                thread whole.
//   PadSettings  one pad's sound: level, pan, tune, decay, velocity sensitivity, choke group,
//                and whether it's audible (mute and solo, resolved by the caller).
//   DrumEngine   32 voices. process() is the audio thread's; publish() and collectGarbage() are
//                the message thread's.
//
// The bank handoff, without locks or allocation on the audio thread:
//
//   message thread                     audio thread (start of each process())
//   publish (bank) ──▶ incoming ──▶    takes it when nothing is retiring: the old bank retires, and
//                                      voices playing a sample the new bank doesn't have fade out
//                                      over 2 ms (voices on unchanged pads play on)
//   collectGarbage() ◀── outgoing ◀──  the retiring bank, once no voice plays a sample only it has
//
// The audio thread reads samples through raw pointers and never touches a shared_ptr, so every
// sample is freed on the message thread.
//
// Voices: 4-point Hermite interpolation (exact at the sample's own rate and Tune 0), an
// exponential decay (−60 dB at the Decay time; 0 = the whole sample), equal-power pan (unity in
// the centre). A pad retriggered, or choked by another pad in its group, fades out over 2 ms.
// When all 32 voices are busy the oldest is taken.
#pragma once

#include "../engine/NoteMap.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace drumchimp
{
    struct SampleData
    {
        std::vector<float> left, right;   // right is empty for a mono sample
        double sampleRate = 44100.0;
        std::string path;

        int length() const noexcept { return (int) left.size(); }
    };

    struct KitBank
    {
        std::array<std::shared_ptr<const SampleData>, numPads + 1> samples;
    };

    struct PadSettings
    {
        float gain = 1.0f;                // linear
        float pan = 0.0f;                 // −1 (left) … 1 (right)
        float tune = 0.0f;                // semitones
        float decaySeconds = 0.0f;        // 0: play the whole sample
        float velocitySensitivity = 1.0f; // 0: every hit at full level
        int choke = 0;                    // 0: none; pads sharing a group cut each other off
        bool audible = true;
    };

    using PadSettingsArray = std::array<PadSettings, numPads>;
    constexpr int previewSlot = numPads;

    class DrumEngine
    {
    public:
        static constexpr int maxVoices = 32;
        static constexpr double fadeSeconds = 0.002;

        struct Trigger { int sample; int pad; float velocity; };

        DrumEngine() = default;
        DrumEngine (const DrumEngine&) = delete;
        DrumEngine& operator= (const DrumEngine&) = delete;

        ~DrumEngine()
        {
            delete incoming.exchange (nullptr);
            delete outgoing.exchange (nullptr);
            delete active;
            delete retiring;
        }

        //==================================================================== message thread
        // Hands a bank to the audio thread. A bank published earlier and not yet taken is dropped.
        void publish (std::unique_ptr<KitBank> bank)
        {
            delete incoming.exchange (bank.release());
        }

        // Frees a bank the audio thread has finished with. Call it regularly (a timer).
        void collectGarbage()
        {
            delete outgoing.exchange (nullptr);
        }

        //==================================================================== audio thread
        void prepare (double sampleRate) noexcept
        {
            hostRate = sampleRate > 0.0 ? sampleRate : 44100.0;
            fadeLength = std::max (1, (int) std::lround (fadeSeconds * hostRate));
            reset();
        }

        void reset() noexcept
        {
            for (auto& v : voices)
                v.sample = nullptr;
        }

        // Adds n samples to left and right. Triggers are sorted by sample, each in [0, n).
        void process (float* left, float* right, int n, const Trigger* triggers, int count, const PadSettingsArray& settings) noexcept
        {
            takeBank();
            if (n <= 0)
                return;

            for (auto& v : voices)
                if (v.sample != nullptr)
                {
                    const auto& s = settingsFor (v.pad, settings);
                    if (! s.audible)
                        v.startFade (fadeLength);
                    aim (v, s, n);
                }

            int done = 0, next = 0;
            while (done < n)
            {
                while (next < count && triggers[next].sample <= done)
                    start (triggers[next++], settings);
                const int until = next < count ? std::min (n, std::max (done + 1, triggers[next].sample)) : n;
                for (auto& v : voices)
                    if (v.sample != nullptr)
                        render (v, left, right, done, until);
                done = until;
            }
        }

        int activeVoices() const noexcept
        {
            return (int) std::count_if (voices.begin(), voices.end(), [] (const Voice& v) { return v.sample != nullptr; });
        }

    private:
        struct Voice
        {
            const SampleData* sample = nullptr;
            int pad = -1;
            uint64_t started = 0;
            double position = 0.0, step = 1.0;
            float velocityGain = 1.0f;
            float gainL = 0.0f, gainR = 0.0f, deltaL = 0.0f, deltaR = 0.0f;
            float envelope = 1.0f, envelopeMul = 1.0f;
            int fadeLeft = 0, fadeLength = 0;    // fadeLeft > 0: fading out

            void startFade (int length) noexcept
            {
                if (fadeLeft == 0)
                {
                    fadeLeft = length;
                    fadeLength = length;
                }
            }
        };

        static void targetGains (const PadSettings& s, float velocityGain, float& l, float& r) noexcept
        {
            const double angle = (std::clamp (s.pan, -1.0f, 1.0f) + 1.0) * 0.7853981633974483;   // 0 … π/2
            const float g = s.gain * velocityGain * 1.4142135623730951f;
            l = g * (float) std::cos (angle);
            r = g * (float) std::sin (angle);
        }

        // Settings are read once a block; level and pan glide to them across the block.
        void aim (Voice& v, const PadSettings& s, int n) noexcept
        {
            float l, r;
            targetGains (s, v.velocityGain, l, r);
            v.deltaL = (l - v.gainL) / (float) n;
            v.deltaR = (r - v.gainR) / (float) n;
            v.step = v.sample->sampleRate / hostRate * std::exp2 (s.tune / 12.0);
            v.envelopeMul = s.decaySeconds > 0.0f ? (float) std::exp (std::log (0.001) / (s.decaySeconds * hostRate)) : 1.0f;
        }

        void start (const Trigger& t, const PadSettingsArray& settings) noexcept
        {
            if (t.pad < 0 || t.pad > previewSlot || active == nullptr)
                return;
            const auto& s = settingsFor (t.pad, settings);
            const SampleData* sample = active->samples[(size_t) t.pad].get();
            if (! s.audible || sample == nullptr || sample->length() == 0)
                return;

            for (auto& v : voices)
                if (v.sample != nullptr && (v.pad == t.pad || (s.choke != 0 && settingsFor (v.pad, settings).choke == s.choke)))
                    v.startFade (fadeLength);

            Voice* free = nullptr;
            for (auto& v : voices)
                if (v.sample == nullptr) { free = &v; break; }
            if (free == nullptr)
                free = &*std::min_element (voices.begin(), voices.end(), [] (const Voice& a, const Voice& b) { return a.started < b.started; });

            auto& v = *free;
            v = {};
            v.sample = sample;
            v.pad = t.pad;
            v.started = ++voiceCounter;
            const float velocity = std::clamp (t.velocity, 0.0f, 1.0f);
            v.velocityGain = 1.0f - s.velocitySensitivity + s.velocitySensitivity * velocity * velocity;
            aim (v, s, 1);
            v.gainL += v.deltaL;               // start at the target, with no glide
            v.gainR += v.deltaR;
            v.deltaL = v.deltaR = 0.0f;
        }

        static const PadSettings& settingsFor (int pad, const PadSettingsArray& settings) noexcept
        {
            static constexpr PadSettings preview {};          // constant-initialised: no guard on the audio thread
            return pad < numPads ? settings[(size_t) pad] : preview;
        }

        static float at (const std::vector<float>& x, int i) noexcept
        {
            return i >= 0 && i < (int) x.size() ? x[(size_t) i] : 0.0f;
        }

        static float hermite (const std::vector<float>& x, int i, float f) noexcept
        {
            const float xm1 = at (x, i - 1), x0 = at (x, i), x1 = at (x, i + 1), x2 = at (x, i + 2);
            const float c1 = 0.5f * (x1 - xm1);
            const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
            const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
            return ((c3 * f + c2) * f + c1) * f + x0;
        }

        void render (Voice& v, float* left, float* right, int from, int until) noexcept
        {
            const auto& s = *v.sample;
            const bool stereo = ! s.right.empty();
            const double end = s.length() + 2.0;      // the interpolation tail
            for (int i = from; i < until; ++i)
            {
                if (v.position >= end || v.envelope < 1.0e-4f)
                {
                    v.sample = nullptr;
                    return;
                }
                const int index = (int) v.position;
                const float frac = (float) (v.position - index);
                float a = v.envelope;
                if (v.fadeLeft > 0)
                {
                    a *= (float) v.fadeLeft / (float) v.fadeLength;
                    if (--v.fadeLeft == 0)
                    {
                        v.sample = nullptr;
                        return;
                    }
                }
                const float l = hermite (s.left, index, frac);
                const float r = stereo ? hermite (s.right, index, frac) : l;
                left[i] += l * a * v.gainL;
                right[i] += r * a * v.gainR;
                v.gainL += v.deltaL;
                v.gainR += v.deltaR;
                v.envelope *= v.envelopeMul;
                v.position += v.step;
            }
        }

        bool inBank (const KitBank* bank, const SampleData* sample) const noexcept
        {
            if (bank != nullptr)
                for (const auto& s : bank->samples)
                    if (s.get() == sample)
                        return true;
            return false;
        }

        void takeBank() noexcept
        {
            if (retiring != nullptr)
            {
                const bool inUse = std::any_of (voices.begin(), voices.end(), [this] (const Voice& v)
                {
                    return v.sample != nullptr && ! inBank (active, v.sample);
                });
                KitBank* expected = nullptr;
                if (! inUse && outgoing.compare_exchange_strong (expected, retiring))
                    retiring = nullptr;
            }
            if (retiring == nullptr)
                if (auto* bank = incoming.exchange (nullptr))
                {
                    retiring = active;
                    active = bank;
                    for (auto& v : voices)
                        if (v.sample != nullptr && ! inBank (active, v.sample))
                            v.startFade (fadeLength);
                }
        }

        std::array<Voice, maxVoices> voices {};
        uint64_t voiceCounter = 0;
        double hostRate = 44100.0;
        int fadeLength = 88;

        std::atomic<KitBank*> incoming { nullptr }, outgoing { nullptr };
        KitBank* active = nullptr;        // audio thread only
        KitBank* retiring = nullptr;      // audio thread only
    };
}
