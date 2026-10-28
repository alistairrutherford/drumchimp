#include "SampleLoader.h"
#include "../core/library/AudioHeader.h"

namespace drumchimp
{
    SampleLoader::SampleLoader() : juce::Thread ("DrumChimp sample loader")
    {
        formats.registerBasicFormats();
    }

    SampleLoader::~SampleLoader()
    {
        stopThread (4000);
    }

    void SampleLoader::request (int pad, juce::uint32 generation, const juce::String& path)
    {
        {
            const juce::ScopedLock sl (lock);
            jobs.push_back ({ pad, generation, path });
        }
        if (! isThreadRunning())
            startThread (juce::Thread::Priority::low);
        notify();
    }

    std::vector<SampleLoader::Result> SampleLoader::takeResults()
    {
        const juce::ScopedLock sl (lock);
        return std::exchange (results, {});
    }

    std::shared_ptr<const SampleData> SampleLoader::load (juce::AudioFormatManager& formats, const juce::File& file, juce::String& error)
    {
        if (! file.existsAsFile())
        {
            error = "Missing: " + file.getFullPathName();
            return {};
        }
        if (readAudioHeader (file.getFullPathName().toStdString()).protectedFile)
        {
            error = file.getFileName() + " is from an Ableton Pack. Packs' samples are protected: only Live can play them";
            return {};
        }
        const std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        if (reader == nullptr)
        {
            error = "Not an audio file DrumChimp can read: " + file.getFileName();
            return {};
        }
        if (reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
        {
            error = "Empty or damaged: " + file.getFileName();
            return {};
        }
        if ((double) reader->lengthInSamples / reader->sampleRate > maxSeconds)
        {
            error = file.getFileName() + " is longer than " + juce::String ((int) maxSeconds) + " s: too long for a pad";
            return {};
        }

        const int length = (int) reader->lengthInSamples;
        const int channels = reader->numChannels >= 2 ? 2 : 1;
        juce::AudioBuffer<float> buffer (channels, length);
        if (! reader->read (&buffer, 0, length, 0, true, channels > 1))
        {
            error = "Couldn't read " + file.getFileName();
            return {};
        }

        auto sample = std::make_shared<SampleData>();
        sample->left.assign (buffer.getReadPointer (0), buffer.getReadPointer (0) + length);
        if (channels > 1)
            sample->right.assign (buffer.getReadPointer (1), buffer.getReadPointer (1) + length);
        sample->sampleRate = reader->sampleRate;
        sample->path = file.getFullPathName().toStdString();
        return sample;
    }

    void SampleLoader::run()
    {
        while (! threadShouldExit())
        {
            Job job;
            {
                const juce::ScopedLock sl (lock);
                if (jobs.empty())
                    job.pad = -1;
                else
                {
                    job = jobs.front();
                    jobs.erase (jobs.begin());
                }
            }
            if (job.pad < 0)
            {
                wait (500);
                continue;
            }

            Result result { job.pad, job.generation, job.path, {}, {} };
            result.sample = load (formats, juce::File (job.path), result.error);
            const juce::ScopedLock sl (lock);
            results.push_back (std::move (result));
        }
    }
}
