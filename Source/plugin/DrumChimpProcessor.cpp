#include "DrumChimpProcessor.h"
#include "../core/library/LibraryRoots.h"
#include "../core/pattern/FactoryStyles.h"
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
        patternSeed = (uint64_t) juce::Random::getSystemRandom().nextInt64();
        regenerateIfChanged();
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
        int incoming = 0;
        for (const auto meta : midi)
        {
            const auto m = meta.getMessage();
            if (m.isNoteOn())
            {
                ++incoming;
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

        // The generator: the pattern, looping from the song's bar 1, at the host's position.
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
        const Pattern* pattern = patternHandoff.current();
        const bool generate = reader.generatorOn() && pattern != nullptr && isPlaying && position.hasValue() && tempo.hasValue() && *tempo > 0.0;
        if (generate && incoming > 0)
            doubled.store (true, std::memory_order_relaxed);   // a clip (or a keyboard) and the generator at once
        if (wasGenerating && (! generate || map != openMap || pattern != lastPattern))
            flushNoteOffs (midi, 0, openMap);     // a new pattern may not end what the old one started
        if (generate)
        {
            const double beatsPerSample = *tempo / 60.0 / sr;
            const double from = *position;
            if (wasGenerating && std::abs (from - expectedPpq) > 2.0 * beatsPerSample)
                flushNoteOffs (midi, 0, map);              // loop or relocate: end what's sounding
            pattern::forEachEventInBlock (*pattern, from, beatsPerSample, n, reader.swingAmount(), *tempo, [&] (int sample, int pad, uint8_t velocity, bool on)
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
            const double inLoop = from - std::floor (from / pattern->lengthBeats()) * pattern->lengthBeats();
            playStep.store (std::min (pattern->steps() - 1, (int) (inLoop / pattern->stepBeats())), std::memory_order_relaxed);
        }
        else
            playStep.store (-1, std::memory_order_relaxed);
        wasGenerating = generate;
        lastPattern = pattern;

        std::sort (triggers.begin(), triggers.begin() + count, [] (const auto& a, const auto& b) { return a.sample < b.sample; });
        engine.process (buffer.getWritePointer (0), buffer.getWritePointer (1), n, triggers.data(), count, reader.padSettings());
    }

    //==========================================================================
    void DrumChimpProcessor::loadSample (int pad, const juce::File& file, juce::int64 savedSize, bool record)
    {
        if (pad < 0 || pad >= numPads)
            return;
        const auto before = kitPaths();
        // While a sample loads the pad keeps playing its last one; a missing file empties it.
        defaultKitTried = true;           // a kit chosen before the first timer tick wins
        bool missing;
        {
            const juce::ScopedLock sl (kitLock);
            auto& s = sources[(size_t) pad];
            missing = ! file.existsAsFile();
            s.path = file.getFullPathName();
            s.fileSize = missing ? savedSize : file.getSize();   // kept for finding it again
            s.status = missing ? PadSource::Status::missing : PadSource::Status::loading;
            s.message = missing ? "Missing: " + s.path : juce::String();
            ++generations[(size_t) pad];
            if (missing)
                kit[(size_t) pad].reset();
            else
                loader.request (pad, generations[(size_t) pad], s.path);
        }
        if (missing)
        {
            publishKit();
            lookForMissing = true;
        }
        if (record)
            history.record (before, kitPaths());
        ++kitChanges;
    }

    void DrumChimpProcessor::clearPad (int pad, bool record)
    {
        if (pad < 0 || pad >= numPads)
            return;
        const auto before = kitPaths();
        defaultKitTried = true;
        {
            const juce::ScopedLock sl (kitLock);
            sources[(size_t) pad] = {};
            kit[(size_t) pad].reset();
            ++generations[(size_t) pad];
        }
        publishKit();
        if (record)
            history.record (before, kitPaths());
        ++kitChanges;
    }

    void DrumChimpProcessor::loadDefaultKit()
    {
        defaultKitTried = true;
        for (const auto& root : Library::liveRoots())
            if (root.kind == LibraryRoot::Kind::coreLibrary)
            {
                for (int pad = 0; pad < numPads; ++pad)
                    loadSample (pad, juce::File (root.path).getChildFile (defaultKitPaths[(size_t) pad]), 0, false);
                return;
            }
    }

    DrumChimpProcessor::PadSource DrumChimpProcessor::padSource (int pad) const
    {
        const juce::ScopedLock sl (kitLock);
        return sources[(size_t) juce::jlimit (0, numPads - 1, pad)];
    }

    KitPaths DrumChimpProcessor::kitPaths() const
    {
        KitPaths paths;
        const juce::ScopedLock sl (kitLock);
        for (int pad = 0; pad < numPads; ++pad)
            paths[(size_t) pad] = sources[(size_t) pad].path.toStdString();
        return paths;
    }

    void DrumChimpProcessor::applyKit (const KitPaths& paths, bool record)
    {
        const auto before = kitPaths();
        for (int pad = 0; pad < numPads; ++pad)
        {
            if (paths[(size_t) pad] == before[(size_t) pad])
                continue;
            if (paths[(size_t) pad].empty())
                clearPad (pad, false);
            else
                loadSample (pad, juce::File (paths[(size_t) pad]), 0, false);
        }
        if (record)
            history.record (before, kitPaths());
    }

    KitRequest DrumChimpProcessor::kitRequest() const
    {
        KitRequest request;
        request.roles = roles;
        request.locked = locked;
        request.current = kitPaths();
        request.character = state.getRawParameterValue ("character")->load();
        request.seed = (uint64_t) juce::Random::getSystemRandom().nextInt64();
        request.preferredFamilies = currentStyle().families;
        return request;
    }

    void DrumChimpProcessor::randomiseKit()
    {
        const auto index = library->index();
        if (index == nullptr)
            return;                       // the first scan hasn't finished
        const auto result = buildKit (*index, kitRequest());
        family = result.family.rfind ("pack:", 0) == 0 ? juce::String (result.family.substr (5)) : juce::String (result.family);
        applyKit (result.paths, true);
    }

    void DrumChimpProcessor::randomisePad (int pad)
    {
        const auto index = library->index();
        if (index == nullptr || pad < 0 || pad >= numPads)
            return;
        auto request = kitRequest();
        request.locked.fill (true);
        request.locked[(size_t) pad] = false;
        applyKit (buildKit (*index, request).paths, true);
    }

    void DrumChimpProcessor::undoKit()
    {
        if (history.canUndo())
            applyKit (history.undo(), false);
    }

    void DrumChimpProcessor::redoKit()
    {
        if (history.canRedo())
            applyKit (history.redo(), false);
    }

    void DrumChimpProcessor::setPadRole (int pad, PadRole role)
    {
        if (pad >= 0 && pad < numPads && roles[(size_t) pad] != role)
        {
            roles[(size_t) pad] = role;
            updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
            ++kitChanges;
        }
    }

    void DrumChimpProcessor::setLocked (int pad, bool shouldLock)
    {
        if (pad >= 0 && pad < numPads && locked[(size_t) pad] != shouldLock)
        {
            locked[(size_t) pad] = shouldLock;
            updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
            ++kitChanges;
        }
    }

    void DrumChimpProcessor::previewSample (const juce::File& file)
    {
        const juce::ScopedLock sl (kitLock);
        loader.request (previewSlot, ++generations[(size_t) previewSlot], file.getFullPathName());
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
        bool changed = false, playPreview = false;
        {
            const juce::ScopedLock sl (kitLock);
            for (auto& r : results)
            {
                if (r.generation != generations[(size_t) r.pad])
                    continue;                     // a later request replaced it
                if (r.pad == previewSlot)
                {
                    kit[(size_t) previewSlot] = std::move (r.sample);
                    playPreview = kit[(size_t) previewSlot] != nullptr;
                    changed = true;
                    continue;
                }
                auto& s = sources[(size_t) r.pad];
                if (r.sample != nullptr)
                {
                    kit[(size_t) r.pad] = std::move (r.sample);
                    s.status = PadSource::Status::loaded;
                    if (! s.message.startsWith ("Found again"))
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
        if (playPreview)
            audition (previewSlot);           // the bank is published first, so the preview is in it
    }

    // A sample saved in a set but gone from its place (a pack moved, Live renamed): if the library
    // has it, load it from there. Tried when a pad goes missing and when a scan finishes.
    void DrumChimpProcessor::relocateMissing()
    {
        const auto index = library->index();
        if (index == nullptr)
            return;
        for (int pad = 0; pad < numPads; ++pad)
        {
            const auto s = padSource (pad);
            if (s.status != PadSource::Status::missing)
                continue;
            if (const auto* found = relocate (*index, s.path.toStdString(), s.fileSize))
            {
                const juce::String was = s.path;
                loadSample (pad, juce::File (found->path), 0, false);
                const juce::ScopedLock sl (kitLock);
                sources[(size_t) pad].message = "Found again: it was at " + was;
            }
        }
    }

    void DrumChimpProcessor::timerCallback()
    {
        if (! stateRestored && ! defaultKitTried)
            loadDefaultKit();
        takeLoadedSamples();
        if (const auto v = library->version(); v != libraryVersionSeen || lookForMissing)
        {
            libraryVersionSeen = v;
            lookForMissing = false;
            relocateMissing();
        }
        regenerateIfChanged();
        engine.collectGarbage();
        patternHandoff.collectGarbage();
    }

    //==========================================================================
    juce::File DrumChimpProcessor::kitsFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Application Support/DrumChimp/Kits");
    }

    juce::Array<juce::File> DrumChimpProcessor::kitPresets() const
    {
        auto files = kitsFolder().findChildFiles (juce::File::findFiles, false, "*.json");
        files.sort();
        return files;
    }

    bool DrumChimpProcessor::saveKitPreset (const juce::String& name, juce::String& error)
    {
        const auto clean = juce::File::createLegalFileName (name.trim());
        if (clean.isEmpty())
        {
            error = "The kit needs a name";
            return false;
        }
        juce::Array<juce::var> pads;
        for (int pad = 0; pad < numPads; ++pad)
        {
            auto* p = new juce::DynamicObject();
            const auto s = padSource (pad);
            p->setProperty ("role", padRoleId (roles[(size_t) pad]));
            p->setProperty ("path", s.path);
            p->setProperty ("size", s.fileSize);
            for (const char* setting : { "level", "pan", "tune", "decay", "vel", "choke" })
                p->setProperty (setting, state.getRawParameterValue (params::padID (pad, setting))->load());
            pads.add (juce::var (p));
        }
        auto* root = new juce::DynamicObject();
        root->setProperty ("drumchimpKit", 1);
        root->setProperty ("name", name.trim());
        root->setProperty ("pads", pads);

        const auto file = kitsFolder().getChildFile (clean + ".json");
        if (! kitsFolder().createDirectory() || ! file.replaceWithText (juce::JSON::toString (juce::var (root))))
        {
            error = "Couldn't write " + file.getFullPathName();
            return false;
        }
        return true;
    }

    bool DrumChimpProcessor::loadKitPreset (const juce::File& file, juce::String& error)
    {
        const auto json = juce::JSON::parse (file.loadFileAsString());
        const auto* pads = json["pads"].getArray();
        if ((int) json["drumchimpKit"] != 1 || pads == nullptr || pads->size() != numPads)
        {
            error = file.getFileName() + " isn't a DrumChimp kit";
            return false;
        }
        KitPaths paths;
        for (int pad = 0; pad < numPads; ++pad)
        {
            const auto& p = pads->getReference (pad);
            roles[(size_t) pad] = padRoleFromId (p["role"].toString().toStdString(), defaultPadRoles[(size_t) pad]);
            for (const char* setting : { "level", "pan", "tune", "decay", "vel", "choke" })
                if (auto* param = state.getParameter (params::padID (pad, setting)); param != nullptr && p.hasProperty (setting)
                    && std::isfinite ((double) p[setting]))
                    param->setValueNotifyingHost (param->convertTo0to1 ((float) (double) p[setting]));
            paths[(size_t) pad] = p["path"].toString().toStdString();
        }
        const auto before = kitPaths();
        applyKit (paths, false);
        // Saved sizes, so a sample gone missing can be found again.
        {
            const juce::ScopedLock sl (kitLock);
            for (int pad = 0; pad < numPads; ++pad)
                if (sources[(size_t) pad].status == PadSource::Status::missing)
                    sources[(size_t) pad].fileSize = (juce::int64) pads->getReference (pad)["size"];
        }
        history.record (before, kitPaths());
        family = {};
        ++kitChanges;
        return true;
    }

    //==========================================================================
    DrumChimpProcessor::Status DrumChimpProcessor::status() const noexcept
    {
        return { playing.load(), bpm.load(), ppq.load(), notesIn.load(), notesOut.load() };
    }

    //==========================================================================
    const std::vector<Style>& DrumChimpProcessor::styles()
    {
        static const std::vector<Style> all = []
        {
            std::vector<Style> out;
            for (const char* text : factoryStyles)
            {
                Style s;
                std::string error;
                if (parseStyle (text, s, error))
                    out.push_back (s);
                else
                    jassertfalse;                 // test_pattern checks every factory style parses
            }
            return out;
        }();
        return all;
    }

    const Style& DrumChimpProcessor::currentStyle() const
    {
        const int index = juce::roundToInt (state.getRawParameterValue ("style")->load());
        return styles()[(size_t) juce::jlimit (0, (int) styles().size() - 1, index)];
    }

    GeneratorSettings DrumChimpProcessor::generatorSettings() const
    {
        const auto value = [this] (const char* id) { return state.getRawParameterValue (id)->load(); };
        GeneratorSettings g;
        g.bars = 1 << juce::jlimit (0, 3, juce::roundToInt (value ("bars")));
        g.density = value ("density");
        g.complexity = value ("complexity");
        g.humanise = value ("humanise");
        g.ghosts = value ("ghosts") >= 0.5f;
        const int fill = juce::roundToInt (value ("fill"));
        g.fillEvery = fill == 0 ? 0 : 1 << fill;            // Off, 2, 4, 8
        g.seed = patternSeed;
        return g;
    }

    void DrumChimpProcessor::regenerate()
    {
        const int style = juce::roundToInt (state.getRawParameterValue ("style")->load());
        const auto g = generatorSettings();
        pattern = generatePattern (currentStyle(), g, roles, &pattern, &laneLocks);
        made = { style, g, roles };
        publishPattern();
    }

    // From the timer: a setting or a pad's role changed since the pattern was made (by the editor,
    // automation or the host). A new style also brings its swing.
    void DrumChimpProcessor::regenerateIfChanged()
    {
        const int style = juce::roundToInt (state.getRawParameterValue ("style")->load());
        if (lastStyle >= 0 && style != lastStyle)
            if (auto* swing = state.getParameter ("swing"))
                swing->setValueNotifyingHost (swing->convertTo0to1 ((float) currentStyle().swing));
        lastStyle = style;

        const auto g = generatorSettings();
        const auto& m = made.settings;
        if (made.style != style || made.roles != roles || m.bars != g.bars || m.density != g.density || m.complexity != g.complexity
            || m.humanise != g.humanise || m.ghosts != g.ghosts || m.fillEvery != g.fillEvery || m.seed != g.seed)
            regenerate();
    }

    void DrumChimpProcessor::publishPattern()
    {
        patternHandoff.publish (std::make_unique<Pattern> (pattern));
        ++patternChanges;
        updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
    }

    void DrumChimpProcessor::newPattern()
    {
        patternSeed = (uint64_t) juce::Random::getSystemRandom().nextInt64();
        regenerate();
    }

    void DrumChimpProcessor::toggleStep (int pad, int step)
    {
        if (pad < 0 || pad >= numPads || step < 0 || step >= pattern.steps())
            return;
        auto& cell = pattern.lanes[(size_t) pad][(size_t) step];
        cell = cell.velocity > 0 ? Cell {} : Cell { 100, 1, 0.0f };
        publishPattern();                 // a setting change makes a new pattern: hand edits last until then
    }

    void DrumChimpProcessor::setStepVelocity (int pad, int step, int velocity)
    {
        if (pad < 0 || pad >= numPads || step < 0 || step >= pattern.steps())
            return;
        auto& cell = pattern.lanes[(size_t) pad][(size_t) step];
        const auto v = (uint8_t) juce::jlimit (1, 127, velocity);
        if (cell.velocity == v)
            return;
        cell.velocity = v;
        if (cell.repeats < 1)
            cell.repeats = 1;
        publishPattern();
    }

    void DrumChimpProcessor::setLaneLocked (int pad, bool shouldLock)
    {
        if (pad >= 0 && pad < numPads && laneLocks[(size_t) pad] != shouldLock)
        {
            laneLocks[(size_t) pad] = shouldLock;
            ++patternChanges;
            updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
        }
    }

    juce::File DrumChimpProcessor::writePatternMidi (juce::String& error) const
    {
        const double tempo = bpm.load() > 0.0 ? bpm.load() : 120.0;
        const juce::String name = "DrumChimp " + juce::String (currentStyle().name) + " " + juce::String (pattern.bars) + (pattern.bars == 1 ? " bar" : " bars");
        const auto data = writeMidiFile (pattern::toMidiSong (pattern, 1, tempo, reader.swingAmount(), reader.noteMap(), name.toStdString()));
        const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("DrumChimp Export")
                              .getChildFile (juce::File::createLegalFileName (name) + ".mid");
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
        tree.setProperty ("uiScale", uiScale, nullptr);
        juce::ValueTree kitTree ("Kit");
        {
            const juce::ScopedLock sl (kitLock);
            for (int pad = 0; pad < numPads; ++pad)
            {
                const auto& s = sources[(size_t) pad];
                kitTree.appendChild (juce::ValueTree ("Pad", { { "index", pad }, { "path", s.path }, { "size", s.fileSize },
                                                               { "role", padRoleId (roles[(size_t) pad]) }, { "locked", locked[(size_t) pad] } }),
                                     nullptr);
            }
        }
        tree.appendChild (kitTree, nullptr);

        // The pattern as it is (hand edits included), one lane per pad: "step:velocity:repeats:offset;…".
        juce::ValueTree patternTree ("Pattern", { { "seed", juce::String ((juce::int64) patternSeed) }, { "stepsPerBar", pattern.stepsPerBar },
                                                  { "bars", pattern.bars } });
        for (int pad = 0; pad < numPads; ++pad)
        {
            juce::String cells;
            for (int s = 0; s < pattern.steps(); ++s)
                if (const auto& c = pattern.lanes[(size_t) pad][(size_t) s]; c.velocity > 0)
                    cells << s << ':' << (int) c.velocity << ':' << (int) c.repeats << ':' << juce::String (c.offsetMs, 2) << ';';
            patternTree.appendChild (juce::ValueTree ("Lane", { { "index", pad }, { "locked", laneLocks[(size_t) pad] }, { "cells", cells } }), nullptr);
        }
        tree.appendChild (patternTree, nullptr);
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
        const auto patternTree = tree.getChildWithName ("Pattern");
        tree.removeChild (kitTree, nullptr);
        tree.removeChild (patternTree, nullptr);
        // A damaged set: a value that isn't a finite number keeps the parameter's value instead
        // (NaN would reach the audio as NaN). Out-of-range values are clamped by the parameters.
        for (auto child : tree)
            if (child.hasType ("PARAM") && child.hasProperty ("value") && ! std::isfinite ((double) child.getProperty ("value")))
                child.removeProperty ("value", nullptr);
        uiScale = juce::jlimit (0.7f, 1.5f, (float) (double) tree.getProperty ("uiScale", 1.0));
        state.replaceState (tree);

        stateRestored = true;
        std::array<juce::String, numPads> paths;
        std::array<juce::int64, numPads> sizes {};
        roles = defaultPadRoles;
        locked.fill (false);
        for (const auto& pad : kitTree)
            if (const int index = pad.getProperty ("index", -1); index >= 0 && index < numPads)
            {
                paths[(size_t) index] = pad.getProperty ("path").toString();
                sizes[(size_t) index] = (juce::int64) pad.getProperty ("size", 0);
                roles[(size_t) index] = padRoleFromId (pad.getProperty ("role").toString().toStdString(), defaultPadRoles[(size_t) index]);
                locked[(size_t) index] = (bool) pad.getProperty ("locked", false);
            }
        for (int pad = 0; pad < numPads; ++pad)
        {
            if (paths[(size_t) pad].isEmpty())
                clearPad (pad, false);
            else
                loadSample (pad, juce::File (paths[(size_t) pad]), sizes[(size_t) pad], false);
        }
        history.clear();
        family = {};

        // The saved pattern, as it was: not made again from its settings.
        lastStyle = juce::roundToInt (state.getRawParameterValue ("style")->load());
        if (patternTree.isValid())
        {
            Pattern restored;
            restored.stepsPerBar = (int) patternTree.getProperty ("stepsPerBar", 16) == 12 ? 12 : 16;
            restored.bars = juce::jlimit (1, maxPatternSteps / restored.stepsPerBar, (int) patternTree.getProperty ("bars", 4));
            laneLocks.fill (false);
            for (const auto& lane : patternTree)
            {
                const int pad = lane.getProperty ("index", -1);
                if (pad < 0 || pad >= numPads)
                    continue;
                laneLocks[(size_t) pad] = (bool) lane.getProperty ("locked", false);
                for (const auto& cell : juce::StringArray::fromTokens (lane.getProperty ("cells").toString(), ";", ""))
                {
                    const auto f = juce::StringArray::fromTokens (cell, ":", "");
                    const int step = f[0].getIntValue();
                    if (f.size() == 4 && step >= 0 && step < restored.steps())
                        restored.lanes[(size_t) pad][(size_t) step] = { (uint8_t) juce::jlimit (1, 127, f[1].getIntValue()),
                                                                        (uint8_t) juce::jlimit (1, 3, f[2].getIntValue()),
                                                                        juce::jlimit (-50.0f, 50.0f, f[3].getFloatValue()) };
                }
            }
            pattern = restored;
            patternSeed = (uint64_t) patternTree.getProperty ("seed").toString().getLargeIntValue();
            made = { lastStyle, generatorSettings(), roles };
            publishPattern();
        }
        else
            regenerateIfChanged();
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
