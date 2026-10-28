#include "DrumChimpProcessor.h"
#include "../ui/DrumChimpEditor.h"
#include <cmath>

namespace drumchimp
{
    juce::AudioProcessor::BusesProperties DrumChimpProcessor::makeBuses()
    {
        return BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true);
    }

    juce::AudioProcessorValueTreeState::ParameterLayout DrumChimpProcessor::createLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "source", 1 }, "Source",
                                                                  juce::StringArray { "Generator", "MIDI In" }, 0));
        // Spike (e): a Remote Script finds this instance's track by reading this through the
        // LOM (PLAN.md §3.6, route C). Random per instance, kept with the set. Automatable only
        // because Live leaves non-automatable VST3 parameters out of the LOM (found 2026-10-28).
        layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "instance", 1 }, "Instance ID", 0, 99999, 0));
        return layout;
    }

    DrumChimpProcessor::DrumChimpProcessor()
        : AudioProcessor (makeBuses()),
          state (*this, nullptr, "DrumChimp", createLayout()),
          hostDescription (juce::PluginHostType().getHostDescription())
    {
        sourceParam = state.getRawParameterValue ("source");
        if (auto* id = state.getParameter ("instance"))
            id->setValue (id->convertTo0to1 ((float) juce::Random::getSystemRandom().nextInt ({ 1, 100000 })));
        logSpike ("loaded: host " + hostDescription + ", wrapper " + juce::AudioProcessor::getWrapperTypeDescription (wrapperType));
    }

    void DrumChimpProcessor::prepareToPlay (double sampleRate, int)
    {
        kit.prepare (sampleRate);
        open.fill (false);
        wasGenerating = false;
    }

    void DrumChimpProcessor::reset()
    {
        kit.prepare (getSampleRate());
        open.fill (false);
        wasGenerating = false;
    }

    bool DrumChimpProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
    {
        const auto out = layouts.getMainOutputChannelSet();
        return layouts.getMainInputChannelSet().isDisabled()
            && (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo());
    }

    void DrumChimpProcessor::flushNoteOffs (juce::MidiBuffer& midi, int sample) noexcept
    {
        for (int pad = 0; pad < numPads; ++pad)
            if (open[(size_t) pad])
            {
                midi.addEvent (juce::MidiMessage::noteOff (1, gmNotes[(size_t) pad]), sample);
                open[(size_t) pad] = false;
            }
    }

    void DrumChimpProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
    {
        juce::ScopedNoDenormals noDenormals;
        buffer.clear();
        const int n = buffer.getNumSamples();
        if (n <= 0)
        {
            midi.clear();
            return;
        }
        const double sr = getSampleRate() > 0.0 ? getSampleRate() : 44100.0;
        int count = 0;
        const auto addTrigger = [&] (int sample, int pad, float velocity)
        {
            if (count < maxTriggers)
                triggers[(size_t) count++] = { juce::jlimit (0, juce::jmax (0, n - 1), sample), pad, velocity };
        };

        // Incoming notes play the pads; they don't go out.
        for (const auto meta : midi)
        {
            const auto m = meta.getMessage();
            if (m.isNoteOn())
            {
                notesIn.fetch_add (1, std::memory_order_relaxed);
                if (const int pad = padForNote (m.getNoteNumber()); pad >= 0)
                    addTrigger (meta.samplePosition, pad, m.getFloatVelocity());
            }
        }
        midi.clear();

        for (uint32_t mask = pendingAuditions.exchange (0); mask != 0; mask &= mask - 1)
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

        const bool generate = (Source) juce::roundToInt (sourceParam->load()) == Source::generator
                              && isPlaying && position.hasValue() && tempo.hasValue() && *tempo > 0.0;
        if (generate)
        {
            const double beatsPerSample = *tempo / 60.0 / sr;
            const double from = *position;
            if (wasGenerating && std::abs (from - expectedPpq) > 2.0 * beatsPerSample)
            {
                jumps.fetch_add (1, std::memory_order_relaxed);   // loop or relocate: end what's sounding
                flushNoteOffs (midi, 0);
            }
            testpattern::forEachEventInBlock (from, beatsPerSample, n, [&] (int sample, int pad, uint8_t note, uint8_t velocity, bool on)
            {
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
        }
        else if (wasGenerating)
        {
            flushNoteOffs (midi, 0);
        }
        wasGenerating = generate;

        // Render, split at each trigger.
        std::sort (triggers.begin(), triggers.begin() + count, [] (const Trigger& a, const Trigger& b) { return a.sample < b.sample; });
        float* left = buffer.getWritePointer (0);
        float* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
        std::array<float, 64> scratch {};
        int done = 0, next = 0;
        while (done < n)
        {
            while (next < count && triggers[(size_t) next].sample <= done)
            {
                kit.trigger (triggers[(size_t) next].pad, triggers[(size_t) next].velocity);
                ++next;
            }
            const int until = juce::jmin (n, done + (int) scratch.size(), next < count ? triggers[(size_t) next].sample : n);
            const int len = until - done;
            if (right != nullptr)
                kit.render (left + done, right + done, len);
            else
            {
                std::fill (scratch.begin(), scratch.end(), 0.0f);
                kit.render (left + done, scratch.data(), len);
                for (int i = 0; i < len; ++i)
                    left[done + i] = 0.5f * (left[done + i] + scratch[(size_t) i]);
            }
            done = until;
        }
    }

    DrumChimpProcessor::Status DrumChimpProcessor::status() const noexcept
    {
        return { playing.load(), bpm.load(), ppq.load(), notesIn.load(), notesOut.load(), jumps.load() };
    }

    juce::File DrumChimpProcessor::writeTestPattern (int bars, juce::String& error) const
    {
        const double tempo = bpm.load() > 0.0 ? bpm.load() : 120.0;
        const auto data = writeMidiFile (testpattern::testPatternSong (bars, tempo));
        const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("DrumChimp Export")
                              .getChildFile ("DrumChimp Test Pattern " + juce::String (bars) + " bars.mid");
        if (! file.getParentDirectory().createDirectory() || ! file.replaceWithData (data.data(), data.size()))
        {
            error = "Couldn't write " + file.getFullPathName();
            return {};
        }
        return file;
    }

    void DrumChimpProcessor::logSpike (const juce::String& line)
    {
        const auto log = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Library/Logs/DrumChimp/spike.log");
        log.getParentDirectory().createDirectory();
        log.appendText (juce::Time::getCurrentTime().toISO8601 (true) + "  " + line + "\n");
    }

    void DrumChimpProcessor::getStateInformation (juce::MemoryBlock& dest)
    {
        if (const auto xml = state.copyState().createXml())
            copyXmlToBinary (*xml, dest);
    }

    void DrumChimpProcessor::setStateInformation (const void* data, int size)
    {
        if (const auto xml = getXmlFromBinary (data, size); xml != nullptr && xml->hasTagName (state.state.getType()))
            state.replaceState (juce::ValueTree::fromXml (*xml));
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
