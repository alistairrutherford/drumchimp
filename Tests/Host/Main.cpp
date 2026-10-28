// The processor tests (PLAN.md §6, Phase 1; Tests/run_host_tests.sh): DrumChimpProcessor as a
// host sees it, with the message loop running so samples load as they do in Live.
//
//   1. Samples   WAV files (written here) load on the loader thread into their pads; a note on a
//                pad's note plays it on its exact sample, at the sample's level; the Chromatic note
//                map moves the pads; Clear empties a pad; an unreadable file is reported
//   2. Generator with a fake transport, the test pattern plays the pads and goes to MIDI out
//   3. State     sample paths and settings survive getState → setState into a new instance (the
//                Instance ID too); a missing sample shows as missing and leaves its pad silent
//   4. RT safety no allocation in processBlock (a malloc hook counts them on this thread) while
//                notes, the generator, a loop jump and a sample swap all happen
//   5. Default   a new instance loads the Core Library 909 kit (skipped without a Core Library)
//   6. Editor    opens and closes with a kit loading
//   7. Library   the background scan finishes, is shared by every instance, and sorts the default
//                kit's samples into their roles (skipped without a Core Library)
//
//   DrumChimpHost
#include <juce_audio_processors/juce_audio_processors.h>
#include "../../Source/plugin/DrumChimpProcessor.h"
#include <iostream>
#include <pthread.h>

using namespace drumchimp;

// Every allocation the watched thread makes (malloc, realloc, new: libmalloc's logger hook sees
// them all). From ChordChimp's snapshot test.
extern "C" void (*malloc_logger) (uint32_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uint32_t);

namespace
{
    std::atomic<pthread_t> allocWatched { nullptr };
    std::atomic<int> allocCount { 0 };

    void allocLogger (uint32_t type, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uint32_t)
    {
        if ((type & 2) != 0 && allocWatched.load (std::memory_order_relaxed) == pthread_self())   // 2: an allocation
            allocCount.fetch_add (1, std::memory_order_relaxed);
    }

    struct AllocWatch
    {
        AllocWatch() { malloc_logger = allocLogger; allocWatched = pthread_self(); }
        ~AllocWatch() { allocWatched = nullptr; }
    };

    int failures = 0;

    void check (bool ok, const juce::String& what)
    {
        std::cout << (ok ? "  PASS  " : "  FAIL  ") << what << std::endl;
        failures += ok ? 0 : 1;
    }

    bool near (float a, float b, float tolerance = 1.0e-4f) { return std::abs (a - b) <= tolerance; }

    // Runs the message loop until `done` or the timeout.
    template <typename Fn>
    bool waitFor (Fn&& done, int timeoutMs = 5000)
    {
        const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
        while (! done())
        {
            if (juce::Time::getMillisecondCounter() > end)
                return false;
            juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
        }
        return true;
    }

    bool allSettled (const DrumChimpProcessor& p)
    {
        for (int i = 0; i < numPads; ++i)
            if (p.padSource (i).status == DrumChimpProcessor::PadSource::Status::loading)
                return false;
        return true;
    }

    // A WAV of silence with impulses, 32-bit float so the values are exact.
    juce::File writeWav (const juce::File& file, double rate, int channels, int length, std::initializer_list<std::pair<int, float>> impulses)
    {
        juce::AudioBuffer<float> buffer (channels, length);
        buffer.clear();
        for (const auto& [at, v] : impulses)
            for (int c = 0; c < channels; ++c)
                buffer.setSample (c, at, v);
        file.deleteFile();
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
        const auto writer = juce::WavAudioFormat().createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (rate)
                                                                         .withNumChannels (channels).withBitsPerSample (32)
                                                                         .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
        writer->writeFromAudioSampleBuffer (buffer, 0, length);
        return file;
    }

    struct FakePlayHead : juce::AudioPlayHead
    {
        PositionInfo info;
        juce::Optional<PositionInfo> getPosition() const override { return info; }
    };

    struct Block
    {
        juce::AudioBuffer<float> audio { 2, 512 };
        juce::MidiBuffer midi;
        Block() { midi.ensureSize (8192); }
    };

    // One block, with optional note-ons (sample, note, velocity 0–127).
    void run (DrumChimpProcessor& p, Block& b, std::initializer_list<std::tuple<int, int, int>> notes = {})
    {
        b.midi.clear();
        for (const auto& [at, note, velocity] : notes)
            b.midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) velocity), at);
        p.processBlock (b.audio, b.midi);
    }

    void setParam (DrumChimpProcessor& p, const juce::String& id, float plain)
    {
        auto* param = p.state.getParameter (id);
        param->setValueNotifyingHost (param->convertTo0to1 (plain));
    }

    using Status = DrumChimpProcessor::PadSource::Status;

    // As a host does: the rate first, then prepareToPlay.
    void prepare (DrumChimpProcessor& p)
    {
        p.setRateAndBufferSizeDetails (48000.0, 512);
        p.prepareToPlay (48000.0, 512);
    }

    struct Files
    {
        juce::File dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("DrumChimpHostTest");
        juce::File click, stereo, notAudio;
        Files()
        {
            dir.deleteRecursively();
            dir.createDirectory();
            click = writeWav (dir.getChildFile ("click.wav"), 48000.0, 1, 200, { { 0, 1.0f }, { 100, 0.5f } });
            stereo = writeWav (dir.getChildFile ("stereo 44k.wav"), 44100.0, 2, 4410, { { 0, 0.25f } });
            notAudio = dir.getChildFile ("notes.wav");
            notAudio.replaceWithText ("not audio");
        }
        ~Files() { dir.deleteRecursively(); }
    };

    void testSamples (const Files& f)
    {
        std::cout << "1. Samples\n";
        DrumChimpProcessor p;
        prepare (p);
        p.loadSample (0, f.click);
        p.loadSample (1, f.stereo);
        p.loadSample (2, f.notAudio);
        for (int pad = 3; pad < numPads; ++pad)
            p.clearPad (pad);
        check (waitFor ([&] { return allSettled (p); }), "the samples finish loading");
        check (p.padSource (0).status == Status::loaded && p.padSource (1).status == Status::loaded, "WAVs (48 kHz mono, 44.1 kHz stereo) load");
        check (p.padSource (2).status == Status::failed && p.padSource (2).message.contains ("Not an audio file"), "a non-audio file is reported");
        check (p.padSource (3).status == Status::empty, "a cleared pad is empty");

        Block b;
        setParam (p, "source", 1.0f);                  // MIDI In
        run (p, b);                                    // takes the kit
        run (p, b, { { 100, 36, 127 } });
        const auto* l = b.audio.getReadPointer (0);
        check (near (l[99], 0.0f) && near (l[100], 1.0f) && near (l[200], 0.5f), "note 36 plays pad 1 on its exact sample (100), at full level");
        check (b.midi.isEmpty(), "and incoming notes don't go to the MIDI output");

        run (p, b);
        run (p, b);
        run (p, b, { { 0, 38, 64 } });
        const float v = 64.0f / 127.0f;
        check (near (b.audio.getSample (0, 0), 0.25f * v * v) && near (b.audio.getSample (1, 0), 0.25f * v * v),
               "note 38 plays pad 2 (stereo), velocity 64 at full sensitivity: " + juce::String (b.audio.getSample (0, 0), 4));

        setParam (p, "notemap", 1.0f);                 // Chromatic: pad 2 is note 37
        run (p, b);
        run (p, b);
        run (p, b, { { 0, 37, 127 } });
        check (near (b.audio.getSample (0, 0), 0.25f), "Chromatic: note 37 plays pad 2");
        run (p, b);
        run (p, b, { { 0, 38, 127 } });
        check (near (b.audio.getSample (0, 0), 0.0f), "and 38 plays nothing (pad 3 failed to load)");

        p.clearPad (0);
        run (p, b);
        run (p, b, { { 0, 36, 127 } });
        check (b.audio.getMagnitude (0, 512) < 1.0e-6f, "a cleared pad is silent");
    }

    void testGenerator (const Files& f)
    {
        std::cout << "2. Generator\n";
        DrumChimpProcessor p;
        prepare (p);
        for (int pad = 0; pad < numPads; ++pad)
            p.loadSample (pad, f.click);
        check (waitFor ([&] { return allSettled (p); }), "a click on every pad");
        FakePlayHead head;
        head.info.setIsPlaying (true);
        head.info.setBpm (120.0);
        p.setPlayHead (&head);

        Block b;
        int notesOut = 0, firstNote = -1;
        float peak = 0.0f;
        double ppq = 0.0;
        for (int i = 0; i < 200; ++i)                  // 200 blocks of 512 at 48 kHz: 2.1 s, a bar at 120 BPM
        {
            head.info.setPpqPosition (ppq);
            run (p, b);
            for (const auto meta : b.midi)
                if (meta.getMessage().isNoteOn())
                {
                    if (notesOut++ == 0)
                        firstNote = meta.getMessage().getNoteNumber();
                }
            peak = std::max (peak, b.audio.getMagnitude (0, 512));
            ppq += 512 * 2.0 / 48000.0;
        }
        check (notesOut == 15 + 3, "a bar and the next downbeat of the test pattern go to MIDI out (" + juce::String (notesOut) + " notes)");
        check (firstNote == 36, "starting with the kick on 36");
        check (peak > 0.5f, "and the pads sound");
        p.setPlayHead (nullptr);
    }

    void testState (const Files& f)
    {
        std::cout << "3. State\n";
        juce::MemoryBlock saved;
        int instanceId = 0;
        {
            DrumChimpProcessor p;
            p.loadSample (0, f.click);
            p.loadSample (4, f.stereo);
            for (int pad : { 1, 2, 3, 5, 6, 7 })
                p.clearPad (pad);
            waitFor ([&] { return allSettled (p); });
            setParam (p, "p5_tune", 5.0f);
            setParam (p, "p5_choke", 3.0f);
            instanceId = juce::roundToInt (p.state.getRawParameterValue ("instance")->load());
            p.getStateInformation (saved);
        }
        DrumChimpProcessor q;
        q.setStateInformation (saved.getData(), (int) saved.getSize());
        check (waitFor ([&] { return allSettled (q); }), "the restored kit loads");
        check (q.padSource (0).path == f.click.getFullPathName() && q.padSource (4).path == f.stereo.getFullPathName(), "sample paths restored");
        check (q.padSource (1).status == Status::empty, "empty pads stay empty (no default kit over a restored state)");
        check (near (q.state.getRawParameterValue ("p5_tune")->load(), 5.0f) && near (q.state.getRawParameterValue ("p5_choke")->load(), 3.0f),
               "pad settings restored");
        check (juce::roundToInt (q.state.getRawParameterValue ("instance")->load()) == instanceId, "the Instance ID too (" + juce::String (instanceId) + ")");

        // The click's file goes away before the set opens.
        const auto gone = f.dir.getChildFile ("gone.wav");
        writeWav (gone, 48000.0, 1, 10, { { 0, 1.0f } });
        q.loadSample (0, gone);
        waitFor ([&] { return allSettled (q); });
        q.getStateInformation (saved);
        gone.deleteFile();
        DrumChimpProcessor r;
        prepare (r);
        r.setStateInformation (saved.getData(), (int) saved.getSize());
        waitFor ([&] { return allSettled (r); });
        check (r.padSource (0).status == Status::missing && r.padSource (0).message.startsWith ("Missing"), "a sample gone missing shows as missing");
        check (r.padSource (0).fileSize > 0, "and keeps its saved size, for finding it again");
        Block b;
        setParam (r, "source", 1.0f);
        run (r, b);
        run (r, b, { { 0, 36, 127 } });
        check (b.audio.getMagnitude (0, 512) < 1.0e-6f, "its pad is silent");
        juce::MemoryBlock again;
        r.getStateInformation (again);
        check (juce::String::fromUTF8 ((const char*) again.getData(), (int) again.getSize()).isNotEmpty()
                   && r.padSource (0).path == gone.getFullPathName(), "and saving again keeps its path");
    }

    void testRealtime (const Files& f)
    {
        std::cout << "4. Real-time safety\n";
        DrumChimpProcessor p;
        prepare (p);
        for (int pad = 0; pad < numPads; ++pad)
            p.loadSample (pad, pad % 2 == 0 ? f.click : f.stereo);
        waitFor ([&] { return allSettled (p); });
        FakePlayHead head;
        head.info.setIsPlaying (true);
        head.info.setBpm (128.0);
        p.setPlayHead (&head);
        Block b;
        run (p, b);                                    // takes the kit

        int total = 0;
        double ppq = 0.0;
        for (int i = 0; i < 400; ++i)
        {
            if (i == 100)
                p.loadSample (3, f.click);             // a sample swap while playing
            if (i % 50 == 0)
                juce::MessageManager::getInstance()->runDispatchLoopUntil (20);   // lets it load and publish
            if (i == 200)
                ppq = 0.0;                             // a loop jump
            if (i == 300)
                setParam (p, "notemap", 1.0f);
            head.info.setPpqPosition (ppq);
            b.midi.clear();
            for (int k = 0; k < 8; ++k)
                b.midi.addEvent (juce::MidiMessage::noteOn (1, 36 + k * 2, (juce::uint8) 100), k * 60);
            allocCount = 0;
            {
                AllocWatch watch;
                p.processBlock (b.audio, b.midi);
            }
            total += allocCount.load();
            ppq += 512 * 128.0 / 60.0 / 48000.0;
        }
        check (p.padSource (3).status == Status::loaded, "the swapped sample loaded mid-play");
        check (total == 0, "no allocation in 400 blocks of notes, generator, sample swap, loop jump and note-map change ("
                               + juce::String (total) + ")");
        p.setPlayHead (nullptr);
    }

    void testDefaultKit()
    {
        std::cout << "5. Default kit\n";
        DrumChimpProcessor p;
        waitFor ([&] { return p.padSource (0).status != Status::empty; }, 2000);
        if (p.padSource (0).status == Status::empty)
        {
            std::cout << "  SKIP  no Core Library on this Mac\n";
            return;
        }
        check (waitFor ([&] { return allSettled (p); }), "the default kit loads");
        bool all909 = true;
        for (int pad = 0; pad < numPads; ++pad)
            all909 = all909 && p.padSource (pad).status == Status::loaded && p.padSource (pad).path.contains ("909");
        check (all909, "eight 909 samples from the Core Library");
    }

    void testLibrary()
    {
        std::cout << "7. Library\n";
        DrumChimpProcessor p, q;
        check (&*p.library == &*q.library, "one library for every instance");
        check (waitFor ([&] { return p.library->index() != nullptr; }, 20000), "the scan finishes (" + juce::String (juce::roundToInt (p.library->status().milliseconds)) + " ms)");
        const auto index = p.library->index();
        if (index == nullptr || index->size() == 0)
        {
            std::cout << "  SKIP  no Ableton library on this Mac\n";
            return;
        }
        check (index->drums() > 0 && index->drums() < index->size(), juce::String ((int) index->drums()) + " drums among " + juce::String ((int) index->size()) + " samples");
        check (Library::cacheFile().existsAsFile(), "the cache is written");
        waitFor ([&] { return p.padSource (0).status == Status::loaded; }, 5000);
        const std::array<Role, numPads> expected { Role::kick, Role::snare, Role::clap, Role::closedHat, Role::openHat, Role::tom, Role::tom, Role::crash };
        bool sorted = true;
        for (int pad = 0; pad < numPads; ++pad)
        {
            const auto* info = index->find (p.padSource (pad).path.toStdString());
            sorted = sorted && info != nullptr && info->classification.role == expected[(size_t) pad];
        }
        check (sorted, "the default kit's samples are found, each in its pad's role");
        const auto before = p.library->version();
        p.library->rescan();
        check (waitFor ([&] { return p.library->version() != before; }, 20000), "a rescan finishes (" + juce::String (juce::roundToInt (p.library->status().milliseconds)) + " ms, from the cache)");
    }

    void testEditor (const Files& f)
    {
        std::cout << "6. Editor\n";
        DrumChimpProcessor p;
        p.loadSample (0, f.click);
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
        check (editor != nullptr && editor->getWidth() > 0, "opens");
        waitFor ([&] { return allSettled (p); });
        juce::MessageManager::getInstance()->runDispatchLoopUntil (100);

        // A picture for a look (not compared): Tests/build/editor.png.
        const auto png = juce::File (__FILE__).getParentDirectory().getSiblingFile ("build").getChildFile ("editor.png");
        juce::FileOutputStream out (png);
        if (out.openedOk())
        {
            out.setPosition (0);
            out.truncate();
            juce::PNGImageFormat().writeImageToStream (editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f), out);
        }
        editor.reset();
        check (true, "and closes");
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    {
        Files files;
        testSamples (files);
        testGenerator (files);
        testState (files);
        testRealtime (files);
        testDefaultKit();
        testEditor (files);
        testLibrary();
    }
    std::cout << (failures == 0 ? "\nDrumChimpHost: all passed\n" : "\n*** DrumChimpHost: " + std::to_string (failures) + " failure(s)\n");
    return failures == 0 ? 0 : 1;
}
