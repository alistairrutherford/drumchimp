// Decodes samples on a background thread (PLAN.md §4, SampleLoader), so loading never blocks the
// message thread or the audio thread. WAV, AIFF and FLAC, up to 30 s (a pad is a one-shot).
//
//   request()      message thread: queue a file for a pad. `generation` lets the caller drop a
//                  result a later request for the same pad has made stale.
//   takeResults()  message thread: what has finished since the last call.
#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include "../core/sampler/DrumEngine.h"

namespace drumchimp
{
    class SampleLoader : private juce::Thread
    {
    public:
        static constexpr double maxSeconds = 30.0;

        struct Result
        {
            int pad = 0;
            juce::uint32 generation = 0;
            juce::String path;
            std::shared_ptr<const SampleData> sample;   // null if it failed
            juce::String error;
        };

        SampleLoader();
        ~SampleLoader() override;

        void request (int pad, juce::uint32 generation, const juce::String& path);
        std::vector<Result> takeResults();

        // Decodes one file now, on the calling thread (also used by the tests).
        static std::shared_ptr<const SampleData> load (juce::AudioFormatManager&, const juce::File&, juce::String& error);

    private:
        void run() override;

        struct Job { int pad; juce::uint32 generation; juce::String path; };
        juce::CriticalSection lock;
        std::vector<Job> jobs;
        std::vector<Result> results;
        juce::AudioFormatManager formats;
    };
}
