// The plug-in, Phase 0 (PLAN.md §6): a VST3 instrument with MIDI out, built to answer the
// feasibility spikes before the real engine exists.
//
//   (a) Source = Generator plays the test pattern (core/engine/TestPattern.h) in time with the
//       host and sends it to the MIDI output, for another track's *MIDI From* to record
//   (b) writeTestPattern() gives the editor a .mid to drag onto a clip slot
//   (c) the editor logs what Live's browser and Finder drop on it
//
// Incoming notes on the pad notes (36 38 39 42 46 45 50 49) play the pads in both Source modes,
// and are not passed to the MIDI output. BlipKit's synthesised sounds stand in for samples.
// Spike results are logged to ~/Library/Logs/DrumChimp/spike.log.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../core/engine/BlipKit.h"
#include "../core/engine/TestPattern.h"
#include <array>
#include <atomic>

namespace drumchimp
{
    class DrumChimpProcessor : public juce::AudioProcessor
    {
    public:
        DrumChimpProcessor();

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
        double getTailLengthSeconds() const override { return 1.0; }

        int getNumPrograms() override { return 1; }
        int getCurrentProgram() override { return 0; }
        void setCurrentProgram (int) override {}
        const juce::String getProgramName (int) override { return {}; }
        void changeProgramName (int, const juce::String&) override {}

        void getStateInformation (juce::MemoryBlock&) override;
        void setStateInformation (const void*, int) override;

        // Message thread.
        void audition (int pad) noexcept { pendingAuditions.fetch_or (1u << pad); }
        juce::File writeTestPattern (int bars, juce::String& error) const;
        static void logSpike (const juce::String& line);

        struct Status
        {
            bool playing = false;
            double bpm = 0.0, ppq = 0.0;
            juce::int64 notesIn = 0, notesOut = 0, jumps = 0;
        };
        Status status() const noexcept;

        enum class Source { generator = 0, midiIn = 1 };

        juce::AudioProcessorValueTreeState state;
        const juce::String hostDescription;

    private:
        static BusesProperties makeBuses();
        static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
        void flushNoteOffs (juce::MidiBuffer& midi, int sample) noexcept;

        struct Trigger { int sample; int pad; float velocity; };
        static constexpr int maxTriggers = 256;

        std::atomic<float>* sourceParam = nullptr;
        BlipKit kit;
        std::array<Trigger, maxTriggers> triggers {};
        std::array<bool, numPads> open {};
        bool wasGenerating = false;
        double expectedPpq = 0.0;

        std::atomic<uint32_t> pendingAuditions { 0 };
        std::atomic<bool> playing { false };
        std::atomic<double> bpm { 120.0 }, ppq { 0.0 };
        std::atomic<juce::int64> notesIn { 0 }, notesOut { 0 }, jumps { 0 };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumChimpProcessor)
    };
}
