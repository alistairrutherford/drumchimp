// Phase 0's editor (PLAN.md §6): just what the spikes need.
//
//   Source         Generator (the test pattern plays and goes to MIDI out) or MIDI In
//   Drag handle    drags the test pattern out as a 4-bar .mid (spike b)
//   Pads           eight pads; click to audition. Drop a sample from Live's browser or Finder
//                  on one: the editor shows and logs what arrived (spike c)
//   Status         host, transport, notes in and out, transport jumps
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../plugin/DrumChimpProcessor.h"

namespace drumchimp
{
    class DragHandle : public juce::Component,
                       public juce::SettableTooltipClient
    {
    public:
        explicit DragHandle (DrumChimpProcessor& p);
        void paint (juce::Graphics&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

    private:
        DrumChimpProcessor& processor;
        bool dragging = false;
    };

    class DrumChimpEditor : public juce::AudioProcessorEditor,
                            public juce::FileDragAndDropTarget,
                            public juce::TextDragAndDropTarget,
                            private juce::Timer
    {
    public:
        explicit DrumChimpEditor (DrumChimpProcessor&);
        ~DrumChimpEditor() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
        void filesDropped (const juce::StringArray& files, int x, int y) override;
        bool isInterestedInTextDrag (const juce::String&) override { return true; }
        void textDropped (const juce::String& text, int x, int y) override;

    private:
        void timerCallback() override;
        int padAt (int x, int y) const;
        void report (const juce::String& line);

        DrumChimpProcessor& processor;
        juce::TooltipWindow tooltips { this };
        juce::ComboBox source;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> sourceAttachment;
        DragHandle dragHandle;
        std::array<juce::TextButton, numPads> pads;
        juce::Label status;
        juce::TextEditor log;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumChimpEditor)
    };
}
