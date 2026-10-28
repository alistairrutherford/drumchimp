// The plug-in (PLAN.md §4): a VST3 instrument with MIDI out. Phase 1: eight sampler pads.
//
//   Pads       notes on the pad notes (Note map: GM or Chromatic) play the pads, in both Source
//              modes; they are not passed to the MIDI output
//   Generator  Source = Generator plays the pattern in time with the host and sends it to the
//              MIDI output, for another track's *MIDI From*. The pattern comes from the generator
//              (core/pattern): a new one with the same seed whenever Style, Bars, Density,
//              Complexity, Humanise, Ghost notes, Fill or a pad's role changes, and with a new seed
//              on Generate. Steps can be toggled by hand, and a pad's lane locked. It reaches the
//              audio thread through a Handoff, and is saved with the set.
//   Samples    loadSample() decodes on a background thread (SampleLoader); the finished kit goes
//              to the audio thread as a whole bank (DrumEngine::publish). A new instance loads the
//              Core Library 909 kit (DefaultKit.h) unless the host restores a state first.
//   Library    the sample library (Library.h), scanned in the background, shared by every instance
//   Kits       each pad has a role and a lock. Randomise and a pad's dice build kits from the
//              library (core/kit/KitBuilder.h, Character Matched → Wild); every kit change can be
//              undone (the last 20, for the session). Kit presets (samples, roles and sound
//              settings) are JSON files in ~/Library/Application Support/DrumChimp/Kits. With
//              "Ableton kits" as the source, Randomise picks one of Ableton's Drum Rack presets
//              instead (core/kit/DrumRack.h); the Kits menu loads one by name. A
//              missing sample is looked for in the library by name and size, and loaded from
//              where it is now. previewSample() plays a file from the browser without a pad.
//   State      the parameters, plus each pad's sample path and file size. A sample that is gone
//              when the set opens shows as missing (Phase 3 will look for it by name).
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../core/kit/DrumRack.h"
#include "../core/pattern/PatternGenerator.h"
#include "../core/util/Handoff.h"
#include "../core/sampler/DrumEngine.h"
#include "Library.h"
#include "Parameters.h"
#include "SampleLoader.h"
#include <array>
#include <atomic>

namespace drumchimp
{
    class DrumChimpProcessor : public juce::AudioProcessor,
                               private juce::Timer
    {
    public:
        DrumChimpProcessor();
        ~DrumChimpProcessor() override;

        void prepareToPlay (double sampleRate, int samplesPerBlock) override;
        void releaseResources() override {}
        void reset() override;
        bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
        void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
        using AudioProcessor::processBlock;

        juce::AudioProcessorEditor* createEditor() override;
        bool hasEditor() const override { return true; }

        const juce::String getName() const override { return JucePlugin_Name; }
        bool acceptsMidi() const override { return true; }
        bool producesMidi() const override { return true; }
        bool isMidiEffect() const override { return false; }
        double getTailLengthSeconds() const override { return 2.0; }

        int getNumPrograms() override { return 1; }
        int getCurrentProgram() override { return 0; }
        void setCurrentProgram (int) override {}
        const juce::String getProgramName (int) override { return {}; }
        void changeProgramName (int, const juce::String&) override {}

        void getStateInformation (juce::MemoryBlock&) override;
        void setStateInformation (const void*, int) override;

        //==================================================================== message thread
        struct PadSource
        {
            enum class Status { empty, loading, loaded, missing, failed };
            Status status = Status::empty;
            juce::String path;
            juce::int64 fileSize = 0;
            juce::String message;                 // why it's missing or failed
        };

        // `record`: the change can be undone (Randomise, a drop, Load…, Clear).
        void loadSample (int pad, const juce::File& file, juce::int64 savedSize = 0, bool record = true);
        void clearPad (int pad, bool record = true);
        void loadDefaultKit();
        PadSource padSource (int pad) const;
        juce::uint32 kitVersion() const noexcept { return kitChanges.load(); }   // changes with every pad change

        KitPaths kitPaths() const;
        void applyKit (const KitPaths& paths, bool record);
        void randomiseKit();
        void randomisePad (int pad);
        juce::String lastFamily() const { return family; }   // the family Randomise last chose, or ""
        juce::String lastRack() const { return rackName; }   // the Ableton kit last loaded, or ""
        bool kitsFromRacks() const { return fromRacks; }
        void setKitsFromRacks (bool racks);
        void loadRack (int index);        // index->racks()[index]
        bool canUndo() const { return history.canUndo(); }
        bool canRedo() const { return history.canRedo(); }
        void undoKit();
        void redoKit();

        PadRole padRole (int pad) const { return roles[(size_t) juce::jlimit (0, numPads - 1, pad)]; }
        void setPadRole (int pad, PadRole role);
        bool isLocked (int pad) const { return locked[(size_t) juce::jlimit (0, numPads - 1, pad)]; }
        void setLocked (int pad, bool shouldLock);

        void previewSample (const juce::File& file);

        static juce::File kitsFolder();
        juce::Array<juce::File> kitPresets() const;
        bool saveKitPreset (const juce::String& name, juce::String& error);
        bool loadKitPreset (const juce::File& file, juce::String& error);

        void audition (int pad) noexcept { pendingAuditions.fetch_or (1u << pad); }
        juce::uint32 takeHits() noexcept { return hits.exchange (0); }   // pads played since the last call
        juce::File writePatternMidi (juce::String& error) const;   // the drag-out

        // The pattern (message thread).
        static const std::vector<Style>& styles();
        const Style& currentStyle() const;
        Pattern currentPattern() const { return pattern; }
        void newPattern();                // Generate: a new seed
        void toggleStep (int pad, int step);
        void setStepVelocity (int pad, int step, int velocity);   // 1–127; a hit is made if there's none
        bool isLaneLocked (int pad) const { return laneLocks[(size_t) juce::jlimit (0, numPads - 1, pad)]; }
        void setLaneLocked (int pad, bool shouldLock);
        int playingStep() const noexcept { return playStep.load(); }   // −1 when the generator isn't playing
        // Whether notes came in while the generator played, since the last call: a dragged-out clip
        // on this track with Source still on Generator plays every hit twice.
        bool takeDoubled() noexcept { return doubled.exchange (false); }
        // The host's time signature as numerator × 100 + denominator (404 for 4/4), or 0 if unknown.
        int hostTimeSignature() const noexcept { return hostMeter.load(); }
        int patternTimeSignature() const noexcept { return pattern.numerator * 100 + pattern.denominator; }   // message thread
        juce::uint32 patternVersion() const noexcept { return patternChanges.load(); }

        struct Status
        {
            bool playing = false;
            double bpm = 0.0, ppq = 0.0;
            juce::int64 notesIn = 0, notesOut = 0;
        };
        Status status() const noexcept;

        juce::AudioProcessorValueTreeState state;
        juce::SharedResourcePointer<Library> library;
        float uiScale = 1.0f;             // the editor's size (0.7–1.5), saved with the set

    private:
        static BusesProperties makeBuses();
        void timerCallback() override;
        void takeLoadedSamples();
        void relocateMissing();
        GeneratorSettings generatorSettings() const;
        void regenerate();
        void regenerateIfChanged();
        void publishPattern();
        void publishKit();
        KitRequest kitRequest() const;
        void flushNoteOffs (juce::MidiBuffer& midi, int sample, NoteMap map) noexcept;

        const params::Reader reader;
        DrumEngine engine;
        SampleLoader loader;

        // Message thread (getStateInformation may come from another thread: `kitLock`).
        juce::CriticalSection kitLock;
        std::array<std::shared_ptr<const SampleData>, numPads + 1> kit;   // + the preview
        std::array<PadSource, numPads> sources;
        std::array<juce::uint32, numPads + 1> generations {};
        std::array<PadRole, numPads> roles = defaultPadRoles;
        std::array<bool, numPads> locked {};
        KitHistory history;
        juce::String family, rackName;
        bool fromRacks = false;
        void applyRack (const DrumRack& rack);
        bool stateRestored = false, defaultKitTried = false, lookForMissing = false;
        juce::uint32 libraryVersionSeen = 0;

        // The pattern (message thread).
        Pattern pattern;
        uint64_t patternSeed = 1;
        std::array<bool, numPads> laneLocks {};
        struct Made { int style = -1; GeneratorSettings settings; std::array<PadRole, numPads> roles {}; };
        Made made;                        // what the current pattern was generated from
        int lastStyle = -1;
        Handoff<Pattern> patternHandoff;
        std::atomic<juce::uint32> patternChanges { 0 };
        std::atomic<juce::uint32> kitChanges { 0 };

        // Audio thread.
        static constexpr int maxTriggers = 512;
        std::array<DrumEngine::Trigger, maxTriggers> triggers {};
        std::array<bool, numPads> open {};
        double preparedRate = 44100.0;
        NoteMap openMap = NoteMap::gm;
        bool wasGenerating = false;
        double expectedPpq = 0.0;
        const Pattern* lastPattern = nullptr;
        std::atomic<int> playStep { -1 };
        std::atomic<bool> doubled { false };
        std::atomic<int> hostMeter { 0 };

        std::atomic<juce::uint32> pendingAuditions { 0 }, hits { 0 };
        std::atomic<bool> playing { false };
        std::atomic<double> bpm { 120.0 }, ppq { 0.0 };
        std::atomic<juce::int64> notesIn { 0 }, notesOut { 0 };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumChimpProcessor)
    };
}
