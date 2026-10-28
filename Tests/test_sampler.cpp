// Phase 1: the sampler (core/sampler/DrumEngine.h). Hits land on their exact sample whatever the
// block size; Tune, the host rate, level, pan, velocity, decay, choke, retrigger and mute work;
// kit banks swap without allocation on the audio thread, and old samples are freed only by
// collectGarbage (the message thread). No JUCE.
#include "TestUtil.h"
#include "core/sampler/DrumEngine.h"
#include <atomic>
#include <cstdlib>
#include <new>

using namespace drumchimp;
using testutil::check;

// Every allocation in this process while `counting` is set.
namespace
{
    std::atomic<bool> counting { false };
    std::atomic<int> allocations { 0 };
}

void* operator new (std::size_t size)
{
    if (counting.load (std::memory_order_relaxed))
        allocations.fetch_add (1, std::memory_order_relaxed);
    if (void* p = std::malloc (size == 0 ? 1 : size))
        return p;
    throw std::bad_alloc();
}

void operator delete (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }

namespace
{
    using Trigger = DrumEngine::Trigger;

    std::shared_ptr<SampleData> impulses (std::initializer_list<std::pair<int, float>> at, int length, double rate = 48000.0, bool stereo = false)
    {
        auto s = std::make_shared<SampleData>();
        s->left.assign ((size_t) length, 0.0f);
        for (const auto& [i, v] : at)
            s->left[(size_t) i] = v;
        if (stereo)
            s->right = s->left;
        s->sampleRate = rate;
        return s;
    }

    std::unique_ptr<KitBank> bankWith (int pad, std::shared_ptr<const SampleData> sample)
    {
        auto bank = std::make_unique<KitBank>();
        bank->samples[(size_t) pad] = std::move (sample);
        return bank;
    }

    struct Rendered { std::vector<float> left, right; };

    // Renders `total` samples in blocks of `block`; triggers at global sample positions.
    Rendered render (DrumEngine& engine, int total, int block, std::vector<std::pair<int64_t, Trigger>> hits, const PadSettingsArray& settings)
    {
        Rendered out { std::vector<float> ((size_t) total), std::vector<float> ((size_t) total) };
        std::vector<Trigger> local;
        for (int start = 0; start < total; start += block)
        {
            const int n = std::min (block, total - start);
            local.clear();
            for (const auto& [at, t] : hits)
                if (at >= start && at < start + n)
                    local.push_back ({ (int) (at - start), t.pad, t.velocity });
            engine.process (out.left.data() + start, out.right.data() + start, n, local.data(), (int) local.size(), settings);
        }
        return out;
    }

    bool near (float a, float b, float tolerance = 1.0e-5f) { return std::abs (a - b) <= tolerance; }

    void testTiming()
    {
        std::cout << "Timing\n";
        for (const int block : { 1, 7, 64, 480, 4096 })
        {
            DrumEngine engine;
            engine.prepare (48000.0);
            engine.publish (bankWith (0, impulses ({ { 0, 1.0f }, { 50, 0.5f } }, 100)));
            PadSettingsArray settings {};
            const auto out = render (engine, 6000, block, { { 0, { 0, 0, 1.0f } }, { 1234, { 0, 0, 1.0f } }, { 4800, { 0, 0, 1.0f } } }, settings);
            bool exact = true;
            for (int i = 0; i < 6000; ++i)
            {
                float expected = 0.0f;
                for (const int at : { 0, 1234, 4800 })
                    expected += i == at ? 1.0f : i == at + 50 ? 0.5f : 0.0f;
                // A retrigger fades the previous hit, but each is long over by the next.
                exact = exact && near (out.left[(size_t) i], expected) && near (out.right[(size_t) i], expected);
            }
            check (exact, "block size " + std::to_string (block) + ": each hit is the sample itself, starting on its exact sample");
        }
    }

    void testPitch()
    {
        std::cout << "Rate and Tune\n";
        PadSettingsArray settings {};
        {
            DrumEngine engine;
            engine.prepare (48000.0);
            engine.publish (bankWith (0, impulses ({ { 50, 1.0f } }, 100)));
            settings[0].tune = 12.0f;
            const auto out = render (engine, 200, 64, { { 0, { 0, 0, 1.0f } } }, settings);
            check (near (out.left[25], 1.0f) && near (out.left[50], 0.0f), "Tune +12 plays an octave up: sample 50 at output 25");
        }
        {
            DrumEngine engine;
            engine.prepare (48000.0);
            engine.publish (bankWith (0, impulses ({ { 0, 1.0f } }, 24000, 24000.0)));
            settings[0].tune = 0.0f;
            const auto out = render (engine, 50000, 512, { { 0, { 0, 0, 1.0f } } }, settings);
            check (engine.activeVoices() == 0, "a 24 kHz sample at 48 kHz plays for twice its length, then ends");
            check (near (out.left[0], 1.0f) && near (out.left[2], 0.0f, 0.01f) && std::abs (out.left[1]) > 0.1f,
                   "and is interpolated between its samples");
        }
    }

    void testLevels()
    {
        std::cout << "Level, pan, velocity, decay\n";
        DrumEngine engine;
        engine.prepare (48000.0);
        auto sample = impulses ({ { 0, 1.0f } }, 10, 48000.0, true);
        engine.publish (bankWith (0, sample));
        PadSettingsArray settings {};
        settings[0].gain = 0.5f;
        settings[0].pan = 1.0f;
        auto out = render (engine, 16, 16, { { 0, { 0, 0, 1.0f } } }, settings);
        check (near (out.left[0], 0.0f) && near (out.right[0], 0.5f * 1.4142135f), "pan hard right: +3 dB right, silent left");

        settings[0] = {};
        settings[0].velocitySensitivity = 1.0f;
        out = render (engine, 16, 16, { { 0, { 0, 0, 0.5f } } }, settings);
        check (near (out.left[0], 0.25f), "velocity 0.5 at full sensitivity: −12 dB");
        settings[0].velocitySensitivity = 0.0f;
        out = render (engine, 16, 16, { { 0, { 0, 0, 0.1f } } }, settings);
        check (near (out.left[0], 1.0f), "no sensitivity: full level");

        // Decay: a constant sample falls to −60 dB at the decay time.
        DrumEngine tail;
        tail.prepare (48000.0);
        auto dc = std::make_shared<SampleData>();
        dc->left.assign (48000, 1.0f);
        tail.publish (bankWith (0, dc));
        settings[0] = {};
        settings[0].decaySeconds = 0.1f;
        out = render (tail, 48000, 256, { { 0, { 0, 0, 1.0f } } }, settings);
        check (near (out.left[4800], 0.001f, 0.0001f), "Decay 100 ms: −60 dB after 100 ms");
        check (tail.activeVoices() == 0, "and the voice ends at −80 dB");
    }

    void testChokeAndRetrigger()
    {
        std::cout << "Choke and retrigger\n";
        auto dc = std::make_shared<SampleData>();
        dc->left.assign (48000, 1.0f);
        PadSettingsArray settings {};
        settings[3].choke = settings[4].choke = 1;

        DrumEngine engine;
        engine.prepare (48000.0);
        auto bank = bankWith (3, dc);
        bank->samples[4] = dc;
        engine.publish (std::move (bank));
        const int fade = (int) std::lround (DrumEngine::fadeSeconds * 48000.0);
        auto out = render (engine, 2000, 128, { { 0, { 0, 4, 1.0f } }, { 1000, { 0, 3, 1.0f } } }, settings);
        check (near (out.left[999], 1.0f) && near (out.left[1000], 2.0f), "the closed hat starts while the open hat still sounds");
        check (near (out.left[1000 + fade], 1.0f), "and the open hat is gone " + std::to_string (fade) + " samples (2 ms) later");
        bool smooth = true;
        for (int i = 1001; i < 1000 + fade; ++i)
            smooth = smooth && out.left[(size_t) i] <= out.left[(size_t) i - 1] + 1.0e-6f;
        check (smooth, "fading, not cut");

        DrumEngine again;
        again.prepare (48000.0);
        again.publish (bankWith (0, dc));
        out = render (again, 2000, 128, { { 0, { 0, 0, 1.0f } }, { 500, { 0, 0, 1.0f } } }, settings);
        check (near (out.left[500], 2.0f) && near (out.left[500 + fade], 1.0f), "a retriggered pad fades its last hit");

        settings[0].audible = false;
        out = render (again, 2000, 128, { { 0, { 0, 0, 1.0f } } }, settings);
        check (near (out.left[fade + 10], 0.0f) && again.activeVoices() == 0, "muted: no new hits, and the sounding ones fade");

        // The browser's preview slot plays at full level whatever the pads' settings.
        DrumEngine preview;
        preview.prepare (48000.0);
        auto previewBank = std::make_unique<KitBank>();
        previewBank->samples[(size_t) previewSlot] = impulses ({ { 0, 1.0f } }, 10);
        preview.publish (std::move (previewBank));
        PadSettingsArray quiet {};
        for (auto& p : quiet)
            p.gain = 0.0f;
        out = render (preview, 64, 64, { { 3, { 0, previewSlot, 0.8f } } }, quiet);
        check (near (out.left[3], 0.8f * 0.8f), "the preview slot plays (velocity 0.8), whatever the pads' settings");
    }

    void testBankSwap()
    {
        std::cout << "Kit bank swap\n";
        auto dc = std::make_shared<SampleData>();
        dc->left.assign (48000, 1.0f);
        std::weak_ptr<SampleData> watch = dc;
        auto kick = impulses ({ { 0, 1.0f } }, 10);
        PadSettingsArray settings {};

        DrumEngine engine;
        engine.prepare (48000.0);
        auto first = bankWith (0, dc);
        first->samples[1] = kick;
        engine.publish (std::move (first));
        dc.reset();

        std::vector<float> l (512), r (512);
        Trigger t { 0, 0, 1.0f };
        engine.process (l.data(), r.data(), 512, &t, 1, settings);
        check (engine.activeVoices() == 1, "pad 1 sounds");

        // Replace pad 1's sample, keep pad 2's.
        auto second = bankWith (0, impulses ({ { 0, 1.0f } }, 10));
        second->samples[1] = kick;
        engine.publish (std::move (second));
        allocations = 0;
        counting = true;
        for (int i = 0; i < 4; ++i)
        {
            std::fill (l.begin(), l.end(), 0.0f);
            Trigger hit { 100, 1, 1.0f };
            engine.process (l.data(), r.data(), 512, &hit, 1, settings);
        }
        counting = false;
        check (allocations == 0, "no allocation while swapping banks and playing (" + std::to_string (allocations.load()) + ")");
        check (! watch.expired(), "the old sample is still alive: the audio thread never frees one");
        engine.collectGarbage();
        check (watch.expired(), "collectGarbage frees it once its voice has faded");

        // Publishing twice before the audio thread looks: the first is dropped on the message thread.
        engine.publish (bankWith (2, kick));
        engine.publish (bankWith (3, kick));
        engine.process (l.data(), r.data(), 64, nullptr, 0, settings);
        Trigger onFour { 0, 3, 1.0f }, onThree { 1, 2, 1.0f };
        std::fill (l.begin(), l.end(), 0.0f);
        const Trigger both[] { onFour, onThree };
        engine.process (l.data(), r.data(), 64, both, 2, settings);
        check (near (l[0], 1.0f) && near (l[1], 0.0f), "only the last bank published is used");
    }
}

int main()
{
    testTiming();
    testPitch();
    testLevels();
    testChokeAndRetrigger();
    testBankSwap();
    return testutil::finish ("test_sampler");
}
