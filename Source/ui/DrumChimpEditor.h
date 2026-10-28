// Phase 1's editor (PLAN.md §5, a first cut).
//
//   Header      Source (Generator / MIDI In), Note map (GM / Chromatic), and the drag handle:
//               4 bars of the test pattern as a .mid to drop on a clip slot
//   Pads        eight pads with their role and sample. Click to select and play one; drop a
//               sample from Live's browser or Finder on one to load it. A pad lights when it plays.
//   Pad panel   the selected pad: its sample (Load…, Clear), Level, Pan, Tune, Decay, Velocity,
//               Choke, Mute and Solo
//   Status      transport, notes in and out; the library scan
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

    class PadButton : public juce::Component,
                      public juce::SettableTooltipClient,
                      public juce::FileDragAndDropTarget
    {
    public:
        PadButton (DrumChimpProcessor& p, int index, std::function<void (int)> onSelect);

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
        void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
        void fileDragExit (const juce::StringArray&) override { dragOver = false; repaint(); }
        void filesDropped (const juce::StringArray& files, int x, int y) override;

        void refresh (bool selected, NoteMap map);
        void hit() { flash = 1.0f; repaint(); }
        void fade();

    private:
        DrumChimpProcessor& processor;
        const int index;
        std::function<void (int)> onSelect;
        DrumChimpProcessor::PadSource source;
        bool selected = false, dragOver = false;
        NoteMap noteMap = NoteMap::gm;
        float flash = 0.0f;
    };

    class DrumChimpEditor : public juce::AudioProcessorEditor,
                            private juce::Timer
    {
    public:
        explicit DrumChimpEditor (DrumChimpProcessor&);
        ~DrumChimpEditor() override;

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        struct Knob
        {
            juce::Slider slider;
            juce::Label label;
            const char* id;
        };

        void timerCallback() override;
        void select (int pad);
        void refreshPads();
        void chooseSample();

        DrumChimpProcessor& processor;
        juce::LookAndFeel_V4 lookAndFeel;
        juce::TooltipWindow tooltips { this };

        juce::ComboBox source, noteMap;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> sourceAttachment, noteMapAttachment;
        DragHandle dragHandle;

        std::vector<std::unique_ptr<PadButton>> pads;
        int selected = 0;

        juce::Label padTitle, padSample;
        juce::TextButton loadButton { "Load..." }, clearButton { "Clear" };
        std::array<Knob, 5> knobs {{ { {}, {}, "level" }, { {}, {}, "pan" }, { {}, {}, "tune" }, { {}, {}, "decay" }, { {}, {}, "vel" } }};
        std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 5> knobAttachments;
        juce::ComboBox choke;
        juce::ToggleButton mute { "Mute" }, solo { "Solo" };
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> chokeAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> muteAttachment, soloAttachment;
        std::unique_ptr<juce::FileChooser> chooser;

        juce::Label status, libraryStatus;
        juce::uint32 seenKit = ~0u, seenLibrary = ~0u;
        NoteMap seenMap = NoteMap::gm;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumChimpEditor)
    };
}
