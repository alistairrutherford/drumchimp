// The plug-in (PLAN.md §4): a VST3 instrument with MIDI out. Phase 1: eight sampler pads.
//
//   Pads       notes on the pad notes (Note map: GM or Chromatic) play the pads, in both Source
//              modes; they are not passed to the MIDI output
//   Generator  Source = Generator plays the test pattern (core/engine/TestPattern.h) in time with
//              the host and sends it to the MIDI output, for another track's *MIDI From* (until
//              the pattern generator, Phase 4)
//   Samples    loadSample() decodes on a background thread (SampleLoader); the finished kit goes
//              to the audio thread as a whole bank (DrumEngine::publish). A new instance loads the
//              Core Library 909 kit (DefaultKit.h) unless the host restores a state first.
//   State      the parameters, plus each pad's sample path and file size. A sample that is gone
//              when the set opens shows as missing (Phase 3 will look for it by name).
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../core/engine/TestPattern.h"
#include "../core/sampler/DrumEngine.h"
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

        void loadSample (int pad, const juce::File& file, juce::int64 savedSize = 0);
        void clearPad (int pad);
        void loadDefaultKit();
        PadSource padSource (int pad) const;
        juce::uint32 kitVersion() const noexcept { return kitChanges.load(); }   // changes with every pad change

        void audition (int pad) noexcept { pendingAuditions.fetch_or (1u << pad); }
        juce::uint32 takeHits() noexcept { return hits.exchange (0); }   // pads played since the last call
        juce::File writeTestPattern (int bars, juce::String& error) const;

        struct Status
        {
            bool playing = false;
            double bpm = 0.0, ppq = 0.0;
            juce::int64 notesIn = 0, notesOut = 0;
        };
        Status status() const noexcept;

        juce::AudioProcessorValueTreeState state;

    private:
        static BusesProperties makeBuses();
        void timerCallback() override;
        void takeLoadedSamples();
        void publishKit();
        void flushNoteOffs (juce::MidiBuffer& midi, int sample, NoteMap map) noexcept;

        const params::Reader reader;
        DrumEngine engine;
        SampleLoader loader;

        // Message thread (getStateInformation may come from another thread: `kitLock`).
        juce::CriticalSection kitLock;
        std::array<std::shared_ptr<const SampleData>, numPads> kit;
        std::array<PadSource, numPads> sources;
        std::array<juce::uint32, numPads> generations {};
        bool stateRestored = false, defaultKitTried = false;
        std::atomic<juce::uint32> kitChanges { 0 };

        // Audio thread.
        static constexpr int maxTriggers = 512;
        std::array<DrumEngine::Trigger, maxTriggers> triggers {};
        std::array<bool, numPads> open {};
        double preparedRate = 44100.0;
        NoteMap openMap = NoteMap::gm;
        bool wasGenerating = false;
        double expectedPpq = 0.0;

        std::atomic<juce::uint32> pendingAuditions { 0 }, hits { 0 };
        std::atomic<bool> playing { false };
        std::atomic<double> bpm { 120.0 }, ppq { 0.0 };
        std::atomic<juce::int64> notesIn { 0 }, notesOut { 0 };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumChimpProcessor)
    };
}
