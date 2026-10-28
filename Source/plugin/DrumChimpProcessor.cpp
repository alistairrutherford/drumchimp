#include "DrumChimpProcessor.h"
#include "../core/library/LibraryRoots.h"
#include "../core/sampler/DefaultKit.h"
#include "../ui/DrumChimpEditor.h"
#include <cmath>

namespace drumchimp
{
    juce::AudioProcessor::BusesProperties DrumChimpProcessor::makeBuses()
    {
        return BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true);
    }

    DrumChimpProcessor::DrumChimpProcessor()
        : AudioProcessor (makeBuses()),
          state (*this, nullptr, "DrumChimp", params::createLayout()),
          reader (state)
    {
        if (auto* id = state.getParameter ("instance"))
            id->setValueNotifyingHost (id->convertTo0to1 ((float) juce::Random::getSystemRandom().nextInt ({ 1, 100000 })));   // notifying: the state tree keeps it
        startTimerHz (20);
    }

    DrumChimpProcessor::~DrumChimpProcessor()
    {
        stopTimer();
    }

    void DrumChimpProcessor::prepareToPlay (double sampleRate, int)
    {
        preparedRate = sampleRate > 0.0 ? sampleRate : 44100.0;
        engine.prepare (preparedRate);
        open.fill (false);
        wasGenerating = false;
    }

    // JUCE's VST3 wrapper calls this when the host stops processing: every voice and note ends.
    void DrumChimpProcessor::reset()
    {
        engine.reset();
        open.fill (false);
        wasGenerating = false;
    }

    bool DrumChimpProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
    {
        return layouts.getMainInputChannelSet().isDisabled() && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
    }

    void DrumChimpProcessor::flushNoteOffs (juce::MidiBuffer& midi, int sample, NoteMap map) noexcept
    {
        for (int pad = 0; pad < numPads; ++pad)
            if (open[(size_t) pad])
            {
                midi.addEvent (juce::MidiMessage::noteOff (1, noteForPad (map, pad)), sample);
                open[(size_t) pad] = false;
            }
    }

    void DrumChimpProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
    {
        juce::ScopedNoDenormals noDenormals;
        buffer.clear();
        const int n = buffer.getNumSamples();
        const auto map = reader.noteMap();
        int count = 0;
        const auto addTrigger = [&] (int sample, int pad, float velocity)
        {
            if (count < maxTriggers && n > 0)
            {
                triggers[(size_t) count++] = { juce::jlimit (0, n - 1, sample), pad, velocity };
                hits.fetch_or (1u << pad, std::memory_order_relaxed);
            }
        };

        // Incoming notes play the pads; they don't go out.
        for (const auto meta : midi)
        {
            const auto m = meta.getMessage();
            if (m.isNoteOn())
            {
                notesIn.fetch_add (1, std::memory_order_relaxed);
                if (const int pad = padForNote (map, m.getNoteNumber()); pad >= 0)
                    addTrigger (meta.samplePosition, pad, m.getFloatVelocity());
            }
        }
        midi.clear();
        if (n <= 0)
            return;

        for (juce::uint32 mask = pendingAuditions.exchange (0); mask != 0; mask &= mask - 1)
            addTrigger (0, juce::findHighestSetBit (mask & (~mask + 1)), 0.8f);

        // The generator: the test pattern, at the host's position.
        bool isPlaying = false;
        juce::Optional<double> position, tempo;
        if (auto* ph = getPlayHead())
            if (const auto pos = ph->getPosition())
            {
                isPlaying = pos->getIsPlaying();
                position = pos->getPpqPosition();
                tempo = pos->getBpm();
            }
        playing.store (isPlaying, std::memory_order_relaxed);
        if (tempo)
            bpm.store (*tempo, std::memory_order_relaxed);
        if (position)
            ppq.store (*position, std::memory_order_relaxed);

        const double sr = preparedRate;       // the engine's rate too
        const bool generate = reader.generatorOn() && isPlaying && position.hasValue() && tempo.hasValue() && *tempo > 0.0;
        if (wasGenerating && (! generate || map != openMap))
            flushNoteOffs (midi, 0, openMap);
        if (generate)
        {
            const double beatsPerSample = *tempo / 60.0 / sr;
            const double from = *position;
            if (wasGenerating && std::abs (from - expectedPpq) > 2.0 * beatsPerSample)
                flushNoteOffs (midi, 0, map);              // loop or relocate: end what's sounding
            testpattern::forEachEventInBlock (from, beatsPerSample, n, [&] (int sample, int pad, uint8_t velocity, bool on)
            {
                const auto note = noteForPad (map, pad);
                if (on)
                {
                    if (open[(size_t) pad])
                        midi.addEvent (juce::MidiMessage::noteOff (1, note), sample);
                    midi.addEvent (juce::MidiMessage::noteOn (1, note, velocity), sample);
                    open[(size_t) pad] = true;
                    notesOut.fetch_add (1, std::memory_order_relaxed);
                    addTrigger (sample, pad, velocity / 127.0f);
                }
                else if (open[(size_t) pad])
                {
                    midi.addEvent (juce::MidiMessage::noteOff (1, note), sample);
                    open[(size_t) pad] = false;
                }
            });
            expectedPpq = from + n * beatsPerSample;
            openMap = map;
        }
        wasGenerating = generate;

        std::sort (triggers.begin(), triggers.begin() + count, [] (const auto& a, const auto& b) { return a.sample < b.sample; });
        engine.process (buffer.getWritePointer (0), buffer.getWritePointer (1), n, triggers.data(), count, reader.padSettings());
    }

    //==========================================================================
    void DrumChimpProcessor::loadSample (int pad, const juce::File& file, juce::int64 savedSize)
    {
        if (pad < 0 || pad >= numPads)
            return;
        // While a sample loads the pad keeps playing its last one; a missing file empties it.
        defaultKitTried = true;           // a kit chosen before the first timer tick wins
        bool missing;
        {
            const juce::ScopedLock sl (kitLock);
            auto& s = sources[(size_t) pad];
            missing = ! file.existsAsFile();
            s.path = file.getFullPathName();
            s.fileSize = missing ? savedSize : file.getSize();   // kept for finding it again (Phase 3)
            s.status = missing ? PadSource::Status::missing : PadSource::Status::loading;
            s.message = missing ? "Missing: " + s.path : juce::String();
            ++generations[(size_t) pad];
            if (missing)
                kit[(size_t) pad].reset();
            else
                loader.request (pad, generations[(size_t) pad], s.path);
        }
        if (missing)
            publishKit();
        ++kitChanges;
    }

    void DrumChimpProcessor::clearPad (int pad)
    {
        if (pad < 0 || pad >= numPads)
            return;
        defaultKitTried = true;
        {
            const juce::ScopedLock sl (kitLock);
            sources[(size_t) pad] = {};
            kit[(size_t) pad].reset();
            ++generations[(size_t) pad];
        }
        publishKit();
        ++kitChanges;
    }

    void DrumChimpProcessor::loadDefaultKit()
    {
        defaultKitTried = true;
        const char* home = std::getenv ("HOME");
        for (const auto& root : detectRoots (home != nullptr ? home : ""))
            if (root.kind == LibraryRoot::Kind::coreLibrary)
            {
                for (int pad = 0; pad < numPads; ++pad)
                    loadSample (pad, juce::File (root.path).getChildFile (defaultKitPaths[(size_t) pad]));
                return;
            }
    }

    DrumChimpProcessor::PadSource DrumChimpProcessor::padSource (int pad) const
    {
        const juce::ScopedLock sl (kitLock);
        return sources[(size_t) juce::jlimit (0, numPads - 1, pad)];
    }

    void DrumChimpProcessor::publishKit()
    {
        auto bank = std::make_unique<KitBank>();
        {
            const juce::ScopedLock sl (kitLock);
            bank->samples = kit;
        }
        engine.publish (std::move (bank));
    }

    void DrumChimpProcessor::takeLoadedSamples()
    {
        auto results = loader.takeResults();
        if (results.empty())
            return;
        bool changed = false;
        {
            const juce::ScopedLock sl (kitLock);
            for (auto& r : results)
            {
                if (r.generation != generations[(size_t) r.pad])
                    continue;                     // a later request replaced it
                auto& s = sources[(size_t) r.pad];
                if (r.sample != nullptr)
                {
                    kit[(size_t) r.pad] = std::move (r.sample);
                    s.status = PadSource::Status::loaded;
                    s.message = {};
                }
                else
                {
                    kit[(size_t) r.pad].reset();
                    s.status = juce::File (r.path).existsAsFile() ? PadSource::Status::failed : PadSource::Status::missing;
                    s.message = r.error;
                }
                changed = true;
            }
        }
        if (changed)
        {
            publishKit();
            ++kitChanges;
        }
    }

    void DrumChimpProcessor::timerCallback()
    {
        if (! stateRestored && ! defaultKitTried)
            loadDefaultKit();
        takeLoadedSamples();
        engine.collectGarbage();
    }

    //==========================================================================
    DrumChimpProcessor::Status DrumChimpProcessor::status() const noexcept
    {
        return { playing.load(), bpm.load(), ppq.load(), notesIn.load(), notesOut.load() };
    }

    juce::File DrumChimpProcessor::writeTestPattern (int bars, juce::String& error) const
    {
        const double tempo = bpm.load() > 0.0 ? bpm.load() : 120.0;
        const auto data = writeMidiFile (testpattern::testPatternSong (bars, tempo, reader.noteMap()));
        const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("DrumChimp Export")
                              .getChildFile ("DrumChimp Test Pattern " + juce::String (bars) + " bars.mid");
        if (! file.getParentDirectory().createDirectory() || ! file.replaceWithData (data.data(), data.size()))
        {
            error = "Couldn't write " + file.getFullPathName();
            return {};
        }
        return file;
    }

    void DrumChimpProcessor::getStateInformation (juce::MemoryBlock& dest)
    {
        auto tree = state.copyState();
        juce::ValueTree kitTree ("Kit");
        {
            const juce::ScopedLock sl (kitLock);
            for (int pad = 0; pad < numPads; ++pad)
                if (const auto& s = sources[(size_t) pad]; s.path.isNotEmpty())
                    kitTree.appendChild (juce::ValueTree ("Pad", { { "index", pad }, { "path", s.path }, { "size", s.fileSize } }), nullptr);
        }
        tree.appendChild (kitTree, nullptr);
        if (const auto xml = tree.createXml())
            copyXmlToBinary (*xml, dest);
    }

    void DrumChimpProcessor::setStateInformation (const void* data, int size)
    {
        const auto xml = getXmlFromBinary (data, size);
        if (xml == nullptr || ! xml->hasTagName (state.state.getType()))
            return;
        auto tree = juce::ValueTree::fromXml (*xml);
        const auto kitTree = tree.getChildWithName ("Kit");
        tree.removeChild (kitTree, nullptr);
        state.replaceState (tree);

        stateRestored = true;
        std::array<juce::String, numPads> paths;
        std::array<juce::int64, numPads> sizes {};
        for (const auto& pad : kitTree)
            if (const int index = pad.getProperty ("index", -1); index >= 0 && index < numPads)
            {
                paths[(size_t) index] = pad.getProperty ("path").toString();
                sizes[(size_t) index] = (juce::int64) pad.getProperty ("size", 0);
            }
        for (int pad = 0; pad < numPads; ++pad)
        {
            if (paths[(size_t) pad].isEmpty())
                clearPad (pad);
            else
                loadSample (pad, juce::File (paths[(size_t) pad]), sizes[(size_t) pad]);
        }
    }

    juce::AudioProcessorEditor* DrumChimpProcessor::createEditor()
    {
        return new DrumChimpEditor (*this);
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new drumchimp::DrumChimpProcessor();
}
