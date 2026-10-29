// The processor tests (PLAN.md §6, Phase 1; Tests/run_host_tests.sh): DrumChimpProcessor as a
// host sees it, with the message loop running so samples load as they do in Live.
//
//   1. Samples   WAV files (written here) load on the loader thread into their pads; a note on a
//                pad's note plays it on its exact sample, at the sample's level; the Chromatic note
//                map moves the pads; Clear empties a pad; an unreadable file is reported
//   2. Generator a style's pattern plays the pads and goes to MIDI out, every note on its sample
//                through a steady run, a loop and a tempo change; settings remake it, Generate
//                renews it, a locked lane stays, a step toggles, the drag-out matches it, and it
//                is saved with the set
//   3. State     sample paths and settings survive getState → setState into a new instance (the
//                Instance ID too); a missing sample shows as missing and leaves its pad silent
//   4. RT safety no allocation in processBlock (a malloc hook counts them on this thread) while
//                notes, the generator, a loop jump and a sample swap all happen
//   5. Default   a new instance loads the Core Library 909 kit (skipped without a Core Library)
//   6. Editor    opens and closes with a kit loading
//   7. Library   the background scan finishes, is shared by every instance, and sorts the default
//                kit's samples into their roles (skipped without a Core Library)
//   8. Kits      on the real library: Randomise fills every pad with its role, undo and redo, a
//                lock holds, a pad's dice, a pad's role, the browser's preview, a kit preset
//                round trip, roles and locks with the set, and a missing sample found again
//   6 runs after 8: the editor with the library and a kit loaded, written to Tests/build/editor.png
//   9. Damaged   NaN, infinite and out-of-range values, a damaged pattern, unknown roles and
//                elements, garbage: each restores to something sane that plays without NaN
//   10. Edges    a zero-length block, a block 8 times the prepared size, a sample-rate change and a
//                reset while playing, 32 instances made and destroyed
//   11. Size     the editor opens at its saved size, keeps a resize, and saves it with the set
//   12. Places   a folder added in Settings is scanned and sorted; removed, it's gone
//   13. CPU      Trap at its busiest, 20 s at 48 kHz / 512: under 2% of a core
//   14. Snapshot the editor in a known state against Tests/fixtures/snapshot/editor.png
//
//   DrumChimpHost [--update-snapshot | --no-library (with DRUMCHIMP_LIBRARY=none) | --screenshot FILE]
//
// --screenshot writes the README's picture: the default kit, a House pattern, at 2×.
//
#include <juce_audio_processors/juce_audio_processors.h>
#include "../../Source/plugin/DrumChimpProcessor.h"
#include <iostream>
#include <set>
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

    // Note-ons the pattern should send over [from, to) beats, each with the first sample at or after it.
    std::multiset<std::pair<juce::int64, int>> expectedOns (const Pattern& pattern, double from, double to, double swing, double bpm, double rate)
    {
        std::multiset<std::pair<juce::int64, int>> out;
        const double bps = bpm / 60.0 / rate;
        pattern::forEachEvent (pattern, from, to, swing, bpm, [&] (double beat, int pad, uint8_t, bool on)
        {
            if (on)
                out.insert ({ (juce::int64) std::ceil ((beat - from) / bps - 1.0e-6), pad });
        });
        return out;
    }

    void testGenerator (const Files& f)
    {
        std::cout << "2. Generator\n";
        DrumChimpProcessor p;
        prepare (p);
        for (int pad = 0; pad < numPads; ++pad)
            p.loadSample (pad, f.click);
        check (waitFor ([&] { return allSettled (p); }), "a click on every pad");
        setParam (p, "style", 5.0f);                   // House
        setParam (p, "humanise", 0.5f);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (100);   // the timer remakes the pattern
        const auto pattern = p.currentPattern();
        const double swing = p.state.getRawParameterValue ("swing")->load();
        check (std::abs (swing - 0.54) < 0.001, "choosing House brings its swing (54%)");
        for (const int beat : { 0, 4, 8, 12 })
            check (pattern.lanes[0][(size_t) beat].velocity > 0, "House: kick on step " + juce::String (beat));

        FakePlayHead head;
        head.info.setIsPlaying (true);
        p.setPlayHead (&head);
        Block b;

        // Three runs through the transport: steady, a loop back to bar 1 halfway, and a tempo change.
        // Every note-on must land where the pattern says, sample for sample.
        struct Run { const char* what; double bpm1, bpm2; double loopAt; };
        for (const Run scenario : { Run { "steady at 120 BPM", 120.0, 120.0, -1.0 }, Run { "looping from beat 6 back to 0", 120.0, 120.0, 6.0 },
                               Run { "120 BPM, then 97 BPM from beat 4", 120.0, 97.0, -1.0 } })
        {
            std::multiset<std::pair<juce::int64, int>> want, got;
            double ppq = 0.0;
            juce::int64 sample = 0, segmentStart = 0;
            double segmentBeat = 0.0, tempo = scenario.bpm1;
            bool switched = false;
            for (int i = 0; i < 400; ++i)
            {
                if (scenario.loopAt > 0.0 && ppq >= scenario.loopAt && ! switched)
                {
                    for (const auto& e : expectedOns (pattern, segmentBeat, ppq, swing, tempo, 48000.0))
                        want.insert ({ segmentStart + e.first, e.second });
                    ppq = 0.0;
                    segmentStart = sample;
                    segmentBeat = 0.0;
                    switched = true;
                }
                if (scenario.bpm2 != scenario.bpm1 && ppq >= 4.0 && ! switched)
                {
                    for (const auto& e : expectedOns (pattern, segmentBeat, ppq, swing, tempo, 48000.0))
                        want.insert ({ segmentStart + e.first, e.second });
                    tempo = scenario.bpm2;
                    segmentStart = sample;
                    segmentBeat = ppq;
                    switched = true;
                }
                head.info.setBpm (tempo);
                head.info.setPpqPosition (ppq);
                run (p, b);
                for (const auto meta : b.midi)
                    if (meta.getMessage().isNoteOn())
                        got.insert ({ sample + meta.samplePosition, padForNote (NoteMap::gm, meta.getMessage().getNoteNumber()) });
                sample += 512;
                ppq += 512 * tempo / 60.0 / 48000.0;
            }
            for (const auto& e : expectedOns (pattern, segmentBeat, ppq, swing, tempo, 48000.0))
                want.insert ({ segmentStart + e.first, e.second });
            check (got == want && ! got.empty(), juce::String (scenario.what) + ": every note on its sample (" + juce::String ((int) got.size()) + " of "
                                                    + juce::String ((int) want.size()) + ")");
        }

        // The pads sound; MIDI out stops cleanly when the transport does.
        float peak = 0.0f;
        head.info.setPpqPosition (0.0);
        for (int i = 0; i < 20; ++i)
        {
            run (p, b);
            peak = std::max (peak, b.audio.getMagnitude (0, 512));
        }
        head.info.setIsPlaying (false);
        run (p, b);
        bool offs = true;
        for (const auto meta : b.midi)
            offs = offs && meta.getMessage().isNoteOff();
        check (peak > 0.5f && offs, "the pads sound, and stopping sends only note-offs");

        // A clip on this track and the generator at once: the editor warns.
        p.takeDoubled();
        head.info.setIsPlaying (true);
        run (p, b, { { 0, 36, 100 } });
        check (p.takeDoubled() && ! p.takeDoubled(), "notes coming in while the generator plays are noticed (once)");
        setParam (p, "source", 1.0f);
        run (p, b, { { 0, 36, 100 } });
        check (! p.takeDoubled(), "but not with Source on MIDI In");
        setParam (p, "source", 0.0f);
        head.info.setIsPlaying (false);
        run (p, b);
        p.setPlayHead (nullptr);

        // Settings remake the pattern with the same seed; Generate makes a new one.
        setParam (p, "density", 1.0f);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
        const auto denser = p.currentPattern();
        bool superset = true;
        for (int pad = 0; pad < numPads; ++pad)
            for (int s = 0; s < pattern.steps(); ++s)
                superset = superset && (pattern.lanes[(size_t) pad][(size_t) s].velocity == 0 || denser.lanes[(size_t) pad][(size_t) s].velocity > 0);
        int before = 0, after = 0;
        for (int pad = 0; pad < numPads; ++pad)
        {
            before += pattern.hits (pad);
            after += denser.hits (pad);
        }
        check (superset && after > before, "more Density remakes the pattern with more hits (" + juce::String (before) + " → " + juce::String (after) + ")");

        p.setLaneLocked (1, true);
        const auto beforeGenerate = p.currentPattern();
        p.newPattern();
        const auto generated = p.currentPattern();
        check (! (generated == beforeGenerate) && generated.lanes[1] == beforeGenerate.lanes[1], "Generate: a new pattern, the locked lane kept");
        const auto kickBefore = generated.lanes[0][3].velocity;
        p.toggleStep (0, 3);
        check ((p.currentPattern().lanes[0][3].velocity > 0) != (kickBefore > 0), "a step toggles by hand");

        juce::String error;
        const auto file = p.writePatternMidi (error);
        MidiSong song;
        std::string readError;
        juce::MemoryBlock data;
        file.loadFileAsData (data);
        const bool read = readMidiFile ((const uint8_t*) data.getData(), data.getSize(), song, readError);
        int hits = 0;
        const auto now = p.currentPattern();
        for (int pad = 0; pad < numPads; ++pad)
            for (int s = 0; s < now.steps(); ++s)
                hits += now.lanes[(size_t) pad][(size_t) s].velocity > 0 ? now.lanes[(size_t) pad][(size_t) s].repeats : 0;
        check (read && (int) song.notes.size() == hits && file.getFileName().startsWith ("DrumChimp House 4 bars"),
               "the drag-out is the pattern: " + file.getFileName() + ", " + juce::String ((int) song.notes.size()) + " notes");

        // The pattern (with the hand edit) and lane locks go with the set, as they are.
        juce::MemoryBlock saved;
        p.getStateInformation (saved);
        DrumChimpProcessor q;
        q.setStateInformation (saved.getData(), (int) saved.getSize());
        juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
        check (q.currentPattern() == now && q.isLaneLocked (1) && ! q.isLaneLocked (0), "pattern, hand edits and lane locks restored");
        check (std::abs (q.state.getRawParameterValue ("swing")->load() - (float) swing) < 0.001f, "and restoring doesn't reset the swing");

        // A 3/4 style: the pattern, its playback and its state are in 3/4.
        setParam (p, "style", 20.0f);                  // Waltz
        setParam (p, "humanise", 0.0f);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
        const auto waltz = p.currentPattern();
        check (waltz.numerator == 3 && waltz.denominator == 4 && waltz.lengthBeats() == 12.0 && p.patternTimeSignature() == 304,
               "Waltz: a 4-bar pattern of 3/4, 12 beats long");
        FakePlayHead head34;
        head34.info.setIsPlaying (true);
        head34.info.setBpm (150.0);
        head34.info.setTimeSignature (juce::AudioPlayHead::TimeSignature { 3, 4 });
        p.setPlayHead (&head34);
        std::multiset<std::pair<juce::int64, int>> want34, got34;
        double ppq34 = 0.0;
        juce::int64 at34 = 0;
        for (int i = 0; i < 600; ++i)                  // 16 beats: 5⅓ bars of 3/4
        {
            head34.info.setPpqPosition (ppq34);
            run (p, b);
            for (const auto meta : b.midi)
                if (meta.getMessage().isNoteOn())
                    got34.insert ({ at34 + meta.samplePosition, padForNote (NoteMap::gm, meta.getMessage().getNoteNumber()) });
            at34 += 512;
            ppq34 += 512 * 150.0 / 60.0 / 48000.0;
        }
        p.setPlayHead (nullptr);
        for (const auto& e : expectedOns (waltz, 0.0, ppq34, p.state.getRawParameterValue ("swing")->load(), 150.0, 48000.0))
            if (e.first < at34)                        // only what the blocks played
                want34.insert (e);
        int downbeats = 0;
        for (const auto& [sample, pad] : got34)
            if (pad == 0 && std::abs (std::fmod ((double) sample * 150.0 / 60.0 / 48000.0, 3.0)) < 1.0e-3)
                ++downbeats;
        check (got34 == want34 && downbeats >= 5, "it plays in 3/4: every note on its sample, the kick on every bar's 1 ("
                                                      + juce::String (downbeats) + " bars)");
        check (p.hostTimeSignature() == 304, "and it reads the host's time signature (3/4)");
        juce::MemoryBlock saved34;
        p.getStateInformation (saved34);
        DrumChimpProcessor r34;
        r34.setStateInformation (saved34.getData(), (int) saved34.getSize());
        check (r34.currentPattern() == waltz, "a 3/4 pattern restores as 3/4");
        juce::String error34;
        const auto file34 = p.writePatternMidi (error34);
        juce::MemoryBlock data34;
        file34.loadFileAsData (data34);
        MidiSong song34;
        std::string readError34;
        check (readMidiFile ((const uint8_t*) data34.getData(), data34.getSize(), song34, readError34) && song34.numerator == 3,
               "and its drag-out is a 3/4 MIDI file");
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
            if (i == 120)
                p.randomiseKit();                      // a new kit while playing (if the library has one)
            if (i == 140)
                p.previewSample (f.click);             // the browser's preview
            if (i == 160)
                p.toggleStep (2, 5);                   // a hand edit
            if (i == 180)
                setParam (p, "density", 0.9f);         // a new pattern, made by the timer
            if (i == 250)
                p.newPattern();
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
        check (total == 0, "no allocation in 400 blocks of notes, generator, sample swap, Randomise, preview, step edit, new patterns, "
                           "loop jump and note-map change ("
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
            all909 = all909 && p.padSource (pad).status == Status::loaded
                     && p.padSource (pad).path.contains (pad == numPads - 1 ? "Tamb 707" : "909");
        check (all909, "twelve samples from the Core Library: 909s, and a 707 tambourine on Perc");
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
        const std::array<Role, numPads> expected { Role::kick, Role::snare, Role::clap, Role::closedHat, Role::openHat, Role::tom, Role::tom, Role::crash,
                                                   Role::rim, Role::tom, Role::ride, Role::perc };
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

    bool kitMatchesRoles (const DrumChimpProcessor& p, const LibraryIndex& index)
    {
        for (int pad = 0; pad < numPads; ++pad)
        {
            const auto* info = index.find (p.padSource (pad).path.toStdString());
            if (info == nullptr || info->classification.role != libraryRole (p.padRole (pad)))
                return false;
        }
        return true;
    }

    void testKits (const Files& f)
    {
        std::cout << "8. Kits\n";
        DrumChimpProcessor p;
        prepare (p);
        waitFor ([&] { return p.library->index() != nullptr; }, 20000);
        const auto index = p.library->index();
        if (index == nullptr || index->drums() == 0)
        {
            std::cout << "  SKIP  no Ableton library on this Mac\n";
            return;
        }
        waitFor ([&] { return p.padSource (0).status != Status::empty && allSettled (p); });
        const auto first = p.kitPaths();
        check (! p.canUndo(), "the default kit isn't an undo step");

        p.randomiseKit();
        waitFor ([&] { return allSettled (p); });
        const auto second = p.kitPaths();
        int changed = 0;
        for (int pad = 0; pad < numPads; ++pad)
            changed += second[(size_t) pad] != first[(size_t) pad] ? 1 : 0;
        check (changed == numPads, "Randomise changes all " + juce::String (numPads) + " pads (" + juce::String (changed) + ")");
        check (kitMatchesRoles (p, *index), "each with a sample of its role (family: " + p.lastFamily() + ")");
        bool allLoad = true;
        for (int round = 0; round < 10 && allLoad; ++round)
        {
            if (round > 0)
                p.randomiseKit();
            waitFor ([&] { return allSettled (p); });
            for (int pad = 0; pad < numPads; ++pad)
                allLoad = allLoad && p.padSource (pad).status == Status::loaded;
        }
        check (allLoad, "and every sample Randomise picks loads (10 kits): none from protected Packs");
        for (int round = 1; round < 10; ++round)
            p.undoKit();                       // back to the first Randomise's kit
        check (p.canUndo(), "and can be undone");
        p.undoKit();
        check (p.kitPaths() == first && p.canRedo(), "Undo brings back the kit before");
        p.redoKit();
        check (p.kitPaths() == second, "Redo the one after");

        p.setLocked (0, true);
        p.setLocked (3, true);
        p.randomiseKit();
        const auto third = p.kitPaths();
        check (third[0] == second[0] && third[3] == second[3] && third[1] != second[1], "locked pads stay; the rest change");

        p.randomisePad (5);
        const auto fourth = p.kitPaths();
        bool onlyFive = fourth[5] != third[5];
        for (int pad = 0; pad < numPads; ++pad)
            onlyFive = onlyFive && (pad == 5 || fourth[(size_t) pad] == third[(size_t) pad]);
        check (onlyFive, "a pad's dice changes only that pad");

        p.setPadRole (6, PadRole::perc);
        p.randomisePad (6);
        waitFor ([&] { return allSettled (p); });
        const auto* perc = index->find (p.padSource (6).path.toStdString());
        check (perc != nullptr && perc->classification.role == Role::perc, "a pad turned to Perc gets a perc on its dice");

        // The browser's preview plays without touching the pads.
        Block b;
        setParam (p, "source", 1.0f);
        const auto before = p.kitPaths();
        p.previewSample (f.click);
        float heard = 0.0f;
        for (int i = 0; i < 50 && heard < 0.5f; ++i)
        {
            juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
            run (p, b);
            heard = std::max (heard, b.audio.getMagnitude (0, 512));
        }
        check (heard > 0.5f && p.kitPaths() == before, "a preview plays (" + juce::String (heard, 2) + ") and changes no pad");

        // Kit presets.
        setParam (p, "p2_tune", 3.0f);
        juce::String error;
        const juce::String name = "DrumChimpHost test kit";
        check (p.saveKitPreset (name, error), "a kit saves as a preset");
        const auto presetFile = DrumChimpProcessor::kitsFolder().getChildFile (name + ".json");
        const auto saved = p.kitPaths();
        p.randomiseKit();
        p.setPadRole (6, PadRole::highTom);
        setParam (p, "p2_tune", 0.0f);
        check (p.loadKitPreset (presetFile, error), "and loads back");
        check (p.kitPaths() == saved && p.padRole (6) == PadRole::perc && std::abs (p.state.getRawParameterValue ("p2_tune")->load() - 3.0f) < 0.01f,
               "samples, roles and sounds as saved");
        const auto other = presetFile.getSiblingFile ("not a kit.json");
        other.replaceWithText ("{}");
        check (! p.loadKitPreset (other, error) && error.contains ("isn't a DrumChimp kit"), "a JSON that isn't a kit is refused");
        presetFile.deleteFile();
        other.deleteFile();

        // Roles and locks go with the set; a sample moved since is found again in the library.
        juce::MemoryBlock state;
        p.getStateInformation (state);
        auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
        const auto kick = p.padSource (0);
        for (auto* pad : xml->getChildByName ("Kit")->getChildIterator())
            if (pad->getIntAttribute ("index") == 0)
                pad->setAttribute ("path", "/Volumes/Old Drive/Somewhere/" + juce::File (kick.path).getFileName());
        juce::MemoryBlock moved;
        juce::AudioProcessor::copyXmlToBinary (*xml, moved);
        DrumChimpProcessor q;
        q.setStateInformation (moved.getData(), (int) moved.getSize());
        check (q.padRole (6) == PadRole::perc && q.isLocked (0) && q.isLocked (3) && ! q.isLocked (1), "roles and locks restored");
        check (waitFor ([&] { return q.padSource (0).status == Status::loaded; }), "the moved kick loads");
        const auto found = q.padSource (0);
        check (found.path == kick.path && found.message.startsWith ("Found again"), "from where the library has it, saying so");

        // Ableton's Drum Rack kits.
        const auto& racks = index->racks();
        check (racks.size() >= 50, juce::String ((int) racks.size()) + " of Ableton's Drum Rack kits can be played (the Core Library's)");
        bool resolvedAll = true;
        for (const auto& rack : racks)
            for (const auto& pad : rack.pads)
                resolvedAll = resolvedAll && index->find (pad.resolved) != nullptr && pad.resolved.find ("Factory Packs") == std::string::npos;
        check (resolvedAll, "every pad of every one is a sample in the library, none from protected Packs");
        DrumChimpProcessor k;
        prepare (k);
        waitFor ([&] { return k.padSource (0).status != Status::empty && allSettled (k); });
        k.setKitsFromRacks (true);
        bool racksLoad = true;
        juce::StringArray names;
        for (int round = 0; round < 5; ++round)
        {
            k.randomiseKit();
            waitFor ([&] { return allSettled (k); });
            names.addIfNotAlreadyThere (k.lastRack());
            for (int pad = 0; pad < numPads; ++pad)
            {
                const auto st = k.padSource (pad).status;
                const auto* info = index->find (k.padSource (pad).path.toStdString());
                racksLoad = racksLoad && st == Status::loaded && info != nullptr;
            }
        }
        check (racksLoad && names.size() >= 3 && ! names.contains (""), "Randomise from Ableton kits: 5 kits (" + names.joinIntoString (", ")
                                                                              + "), every pad loads");
        int selectorKit = -1;
        for (int i = 0; i < (int) racks.size(); ++i)
            if (racks[(size_t) i].name == "Selector Kit Warm")
                selectorKit = i;
        if (selectorKit >= 0)
        {
            k.setLocked (2, true);
            const auto clapBefore = k.kitPaths()[2];
            k.loadRack (selectorKit);
            waitFor ([&] { return allSettled (k); });
            check (k.lastRack() == "Selector Kit Warm" && k.kitPaths()[0].find ("Kick Remo") != std::string::npos
                       && k.kitPaths()[1].find ("Snare Cust") != std::string::npos && k.kitPaths()[2] == clapBefore,
                   "loading Selector Kit Warm by name: its kick on C1, its snare on D1, the locked pad kept");
        }
        juce::MemoryBlock rackState;
        k.getStateInformation (rackState);
        DrumChimpProcessor kr;
        kr.setStateInformation (rackState.getData(), (int) rackState.getSize());
        check (kr.kitsFromRacks(), "the kit source is saved with the set");
    }

    void testEditor (const Files&)
    {
        std::cout << "6. Editor\n";
        DrumChimpProcessor p;
        waitFor ([&] { return p.library->index() != nullptr && p.padSource (0).status != Status::empty && allSettled (p); }, 20000);
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
        check (editor != nullptr && editor->getWidth() > 0, "opens");
        juce::MessageManager::getInstance()->runDispatchLoopUntil (200);

        // A picture for a look (not compared): Tests/build/editor.png.
        const auto png = juce::File (__FILE__).getParentDirectory().getSiblingFile ("build").getChildFile ("editor.png");
        juce::FileOutputStream out (png);
        if (out.openedOk())
        {
            out.setPosition (0);
            out.truncate();
            juce::PNGImageFormat().writeImageToStream (editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f), out);
        }
        // A triplet style in the grid: Tests/build/editor-jazz.png.
        setParam (p, "style", 12.0f);                  // Jazz, a 12-step grid
        juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
        {
            const auto jazzPng = png.getSiblingFile ("editor-jazz.png");
            jazzPng.deleteFile();
            juce::FileOutputStream jazzOut (jazzPng);
            juce::PNGImageFormat().writeImageToStream (editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f), jazzOut);
        }
        // And with Settings open: Tests/build/settings.png.
        for (auto* child : editor->getChildren())
            if (auto* button = dynamic_cast<juce::Button*> (child->findChildWithID ("settingsButton")))
            {
                button->triggerClick();
                juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
                const auto settingsPng = png.getSiblingFile ("settings.png");
                settingsPng.deleteFile();
                juce::FileOutputStream settingsOut (settingsPng);
                juce::PNGImageFormat().writeImageToStream (editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f), settingsOut);
            }
        editor.reset();
        check (true, "and closes");
    }
}

namespace
{
    bool finite (const juce::AudioBuffer<float>& b)
    {
        for (int c = 0; c < b.getNumChannels(); ++c)
            for (int i = 0; i < b.getNumSamples(); ++i)
                if (! std::isfinite (b.getSample (c, i)))
                    return false;
        return true;
    }

    // Plays a second with the generator and incoming notes; true if the output stays finite.
    bool playsCleanly (DrumChimpProcessor& p)
    {
        FakePlayHead head;
        head.info.setIsPlaying (true);
        head.info.setBpm (120.0);
        p.setPlayHead (&head);
        Block b;
        bool ok = true;
        double ppq = 0.0;
        for (int i = 0; i < 94; ++i)
        {
            head.info.setPpqPosition (ppq);
            run (p, b, { { 0, 36, 127 }, { 100, 38, 64 } });
            ok = ok && finite (b.audio);
            ppq += 512 * 2.0 / 48000.0;
        }
        p.setPlayHead (nullptr);
        return ok;
    }

    void testDamagedState (const Files& f)
    {
        std::cout << "9. Damaged saved states\n";
        DrumChimpProcessor good;
        good.loadSample (0, f.click);
        juce::MemoryBlock saved;
        good.getStateInformation (saved);
        const auto xml = juce::AudioProcessor::getXmlFromBinary (saved.getData(), (int) saved.getSize());

        const auto restoreFrom = [&] (std::function<void (juce::XmlElement&)> damage, const juce::String& what, auto&& expect)
        {
            auto copy = std::make_unique<juce::XmlElement> (*xml);
            damage (*copy);
            juce::MemoryBlock data;
            juce::AudioProcessor::copyXmlToBinary (*copy, data);
            DrumChimpProcessor p;
            prepare (p);
            p.setStateInformation (data.getData(), (int) data.getSize());
            waitFor ([&] { return allSettled (p); });
            check (playsCleanly (p) && expect (p), what);
        };
        const auto paramValue = [] (juce::XmlElement& x, const char* id, const juce::String& value)
        {
            for (auto* e : x.getChildIterator())
                if (e->getStringAttribute ("id") == id)
                    e->setAttribute ("value", value);
        };

        restoreFrom ([&] (juce::XmlElement& x) { paramValue (x, "p1_level", "nan"); paramValue (x, "p2_tune", "inf"); }, "NaN and infinity: those parameters keep their values",
                     [] (DrumChimpProcessor& p) { return std::abs (p.state.getRawParameterValue ("p1_level")->load()) < 0.01f && std::abs (p.state.getRawParameterValue ("p2_tune")->load()) < 0.01f; });
        restoreFrom ([&] (juce::XmlElement& x) { paramValue (x, "p1_level", "1e9"); paramValue (x, "density", "-5"); }, "out-of-range values are clamped",
                     [] (DrumChimpProcessor& p) { return p.state.getRawParameterValue ("p1_level")->load() <= 6.0f && p.state.getRawParameterValue ("density")->load() == 0.0f; });
        restoreFrom ([&] (juce::XmlElement& x)
                     {
                         auto* pattern = x.getChildByName ("Pattern");
                         pattern->setAttribute ("bars", 9999);
                         for (auto* lane : pattern->getChildIterator())
                             lane->setAttribute ("cells", "junk;5:200:9:1e30;-3:100:1:0;999999:1:1:0;:::;");
                     }, "a damaged pattern: sizes and cells kept in range",
                     [] (DrumChimpProcessor& p) { const auto pt = p.currentPattern(); return pt.steps() <= maxPatternSteps && pt.lanes[0][5].velocity == 127 && pt.lanes[0][5].repeats == 3; });
        restoreFrom ([&] (juce::XmlElement& x)
                     {
                         for (auto* pad : x.getChildByName ("Kit")->getChildIterator())
                         {
                             pad->setAttribute ("role", "trombone");
                             pad->setAttribute ("index", pad->getIntAttribute ("index") + 100);
                         }
                         x.addChildElement (new juce::XmlElement ("Unknown"));
                     }, "unknown roles, pad numbers out of range and unknown elements are ignored",
                     [] (DrumChimpProcessor& p) { return p.padRole (0) == PadRole::kick && p.padSource (0).status == Status::empty; });

        DrumChimpProcessor p;
        const char garbage[] = "not a state at all, just some bytes \x01\x02\x03";
        p.setStateInformation (garbage, (int) sizeof (garbage));
        p.setStateInformation (nullptr, 0);
        check (playsCleanly (p), "garbage and empty states are ignored");
    }

    void testEdges (const Files& f)
    {
        std::cout << "10. Edges\n";
        DrumChimpProcessor p;
        prepare (p);
        p.loadSample (0, f.click);
        waitFor ([&] { return allSettled (p); });
        juce::AudioBuffer<float> empty (2, 0);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 100), 0);
        p.processBlock (empty, midi);
        check (true, "a zero-length block");
        juce::AudioBuffer<float> big (2, 4096);
        midi.clear();
        midi.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 127), 4000);
        p.processBlock (big, midi);
        check (std::abs (big.getSample (0, 4000) - 1.0f) < 1.0e-4f && finite (big), "a block 8 times bigger than prepared, a hit at 4000 on its sample");
        p.setRateAndBufferSizeDetails (96000.0, 64);
        p.prepareToPlay (96000.0, 64);
        check (playsCleanly (p), "a sample-rate change (96 kHz) while notes sound");
        p.reset();
        check (playsCleanly (p), "a reset");

        bool made = true;
        for (int i = 0; i < 32; ++i)
        {
            auto q = std::make_unique<DrumChimpProcessor>();
            made = made && &*q->library == &*p.library;
        }
        check (made, "32 instances made and destroyed, sharing one library");
    }

    void testEditorScale()
    {
        std::cout << "11. Editor size\n";
        DrumChimpProcessor p;
        p.uiScale = 1.25f;
        {
            std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
            check (editor->getWidth() == 1200 && editor->getHeight() == 1025, "opens at its saved size (125%: 1200 × 1025)");
            editor->setSize (768, 656);
            check (std::abs (p.uiScale - 0.8f) < 0.001f, "a resize is kept (80%)");
        }
        juce::MemoryBlock state;
        p.getStateInformation (state);
        DrumChimpProcessor q;
        q.setStateInformation (state.getData(), (int) state.getSize());
        check (std::abs (q.uiScale - 0.8f) < 0.001f, "and saved with the set");
    }

    void testPlaces (const Files& f)
    {
        std::cout << "12. Library places\n";
        DrumChimpProcessor p;
        waitFor ([&] { return p.library->index() != nullptr; }, 20000);
        const auto settingsBefore = Library::settingsFile().existsAsFile() ? Library::settingsFile().loadFileAsString() : juce::String();
        const bool hadSettings = Library::settingsFile().existsAsFile();
        const auto folder = f.dir.getChildFile ("My Drums");
        folder.getChildFile ("Kicks").createDirectory();
        writeWav (folder.getChildFile ("Kicks/Kick Mine.wav"), 48000.0, 1, 4800, { { 0, 1.0f } });
        const auto path = folder.getChildFile ("Kicks/Kick Mine.wav").getFullPathName().toStdString();

        auto version = p.library->version();
        p.library->addFolder (folder);
        check (waitFor ([&] { return p.library->version() != version; }, 20000) && p.library->extraFolders().contains (folder.getFullPathName()),
               "a folder added in Settings is scanned");
        const auto* mine = p.library->index()->find (path);
        check (mine != nullptr && mine->classification.role == Role::kick && mine->root == "My Drums", "its kick is in the library, sorted as a Kick");
        version = p.library->version();
        p.library->removeFolder (folder.getFullPathName());
        check (waitFor ([&] { return p.library->version() != version; }, 20000) && p.library->index()->find (path) == nullptr, "removed again, it's gone");

        if (hadSettings)
            Library::settingsFile().replaceWithText (settingsBefore);
        else
            Library::settingsFile().deleteFile();
    }

    void testCpu()
    {
        std::cout << "13. CPU\n";
        DrumChimpProcessor p;
        prepare (p);
        waitFor ([&] { return p.padSource (0).status != Status::empty && allSettled (p); }, 20000);
        setParam (p, "style", 4.0f);                   // Trap, as busy as it gets: rolls on the hats
        setParam (p, "density", 1.0f);
        setParam (p, "complexity", 1.0f);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
        FakePlayHead head;
        head.info.setIsPlaying (true);
        head.info.setBpm (140.0);
        p.setPlayHead (&head);
        Block b;
        const int blocks = 48000 * 20 / 512;          // 20 s of audio
        double ppq = 0.0;
        const auto start = juce::Time::getMillisecondCounterHiRes();
        for (int i = 0; i < blocks; ++i)
        {
            head.info.setPpqPosition (ppq);
            run (p, b);
            ppq += 512 * 140.0 / 60.0 / 48000.0;
        }
        const double ms = juce::Time::getMillisecondCounterHiRes() - start;
        const double percent = 100.0 * ms / (blocks * 512.0 / 48.0);
        check (percent < 2.0, "Trap at full Density and Complexity, 48 kHz, 512: " + juce::String (percent, 3) + "% of one core (target under 2%)");
        p.setPlayHead (nullptr);
    }

    // The editor in a known state, compared with Tests/fixtures/snapshot/editor.png (this Mac's
    // fonts; --update writes it). The library status (this Mac's library) is masked.
    void testSnapshot (bool update)
    {
        std::cout << "14. Editor snapshot\n";
        DrumChimpProcessor p;
        waitFor ([&] { return p.library->index() != nullptr && p.padSource (0).status != Status::empty && allSettled (p); }, 20000);
        juce::MemoryBlock state;
        p.getStateInformation (state);
        auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
        auto* pattern = xml->getChildByName ("Pattern");
        pattern->setAttribute ("seed", "12345");
        pattern->deleteAllChildElements();         // restored empty, then made from the seed below
        juce::AudioProcessor::copyXmlToBinary (*xml, state);
        p.setStateInformation (state.getData(), (int) state.getSize());
        waitFor ([&] { return allSettled (p); });
        setParam (p, "density", 0.6f);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (200);

        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
        editor->setSize (960, 820);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
        auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        for (auto* child : editor->getChildren())     // the editor's one child is its content, at scale 1 here
            if (auto* found = child->findChildWithID ("libraryStatus"))
            {
                juce::Graphics g (image);
                g.setColour (juce::Colours::black);
                g.fillRect (found->getBounds());
            }

        const auto reference = juce::File (__FILE__).getParentDirectory().getSiblingFile ("fixtures").getChildFile ("snapshot/editor.png");
        if (update)
        {
            reference.getParentDirectory().createDirectory();
            reference.deleteFile();
            juce::FileOutputStream out (reference);
            juce::PNGImageFormat().writeImageToStream (image, out);
            std::cout << "  wrote " << reference.getFullPathName() << "\n";
            return;
        }
        const auto expected = juce::ImageFileFormat::loadFrom (reference);
        if (! expected.isValid())
        {
            check (false, "no reference image: run DrumChimpHost --update-snapshot once");
            return;
        }
        int differing = 0;
        for (int y = 0; y < std::min (image.getHeight(), expected.getHeight()); ++y)
            for (int x = 0; x < std::min (image.getWidth(), expected.getWidth()); ++x)
            {
                const auto a = image.getPixelAt (x, y), e = expected.getPixelAt (x, y);
                if (std::abs (a.getRed() - e.getRed()) > 8 || std::abs (a.getGreen() - e.getGreen()) > 8 || std::abs (a.getBlue() - e.getBlue()) > 8)
                    ++differing;
            }
        const double share = 100.0 * differing / (image.getWidth() * image.getHeight());
        check (image.getBounds() == expected.getBounds() && share < 0.2, "matches the reference (" + juce::String (share, 3) + "% of pixels differ)");
        if (share >= 0.2)
        {
            const auto now = juce::File (__FILE__).getParentDirectory().getSiblingFile ("build").getChildFile ("snapshot-now.png");
            now.deleteFile();
            juce::FileOutputStream out (now);
            juce::PNGImageFormat().writeImageToStream (image, out);
            std::cout << "        this run's image: " << now.getFullPathName() << "\n";
        }
    }

        void screenshot (const juce::File& file)
    {
        DrumChimpProcessor p;
        waitFor ([&] { return p.library->index() != nullptr && p.padSource (0).status != Status::empty && allSettled (p); }, 20000);
        setParam (p, "style", 5.0f);                   // House
        setParam (p, "density", 0.65f);
        setParam (p, "complexity", 0.55f);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
        editor->setSize (960, 820);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
        file.getParentDirectory().createDirectory();
        file.deleteFile();
        juce::FileOutputStream out (file);
        juce::PNGImageFormat().writeImageToStream (editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f), out);
        std::cout << "wrote " << file.getFullPathName() << "\n";
    }

    // With DRUMCHIMP_LIBRARY=none: a Mac without Ableton's library.
    void testNoLibrary (const Files& f)
    {
        std::cout << "No library (DRUMCHIMP_LIBRARY=none)\n";
        DrumChimpProcessor p;
        prepare (p);
        check (waitFor ([&] { return p.library->index() != nullptr; }, 20000) && p.library->index()->size() == 0, "the library is empty");
        juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
        bool empty = true;
        for (int pad = 0; pad < numPads; ++pad)
            empty = empty && p.padSource (pad).status == Status::empty;
        check (empty, "no default kit: the pads are empty");
        p.randomiseKit();
        check (! p.canUndo() && p.kitPaths()[0].empty(), "Randomise does nothing (and leaves nothing to undo)");
        check (playsCleanly (p), "the generator runs silent");
        p.loadSample (0, f.click);
        waitFor ([&] { return allSettled (p); });
        check (p.padSource (0).status == Status::loaded, "a sample still loads by hand");
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
        juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
        check (editor != nullptr, "the editor opens");
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::StringArray args (argv + 1, argc - 1);
    {
        Files files;
        if (args.contains ("--no-library"))
            testNoLibrary (files);
        else if (const int at = args.indexOf ("--screenshot"); at >= 0 && at + 1 < args.size())
            screenshot (juce::File::getCurrentWorkingDirectory().getChildFile (args[at + 1]));
        else if (args.contains ("--update-snapshot"))
            testSnapshot (true);
        else
        {
            testSamples (files);
            testGenerator (files);
            testState (files);
            testRealtime (files);
            testDefaultKit();
            testLibrary();
            testKits (files);
            testEditor (files);
            testDamagedState (files);
            testEdges (files);
            testEditorScale();
            testPlaces (files);
            testCpu();
            testSnapshot (false);
        }
    }
    std::cout << (failures == 0 ? "\nDrumChimpHost: all passed\n" : "\n*** DrumChimpHost: " + std::to_string (failures) + " failure(s)\n");
    return failures == 0 ? 0 : 1;
}
