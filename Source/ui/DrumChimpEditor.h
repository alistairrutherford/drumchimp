// The editor (PLAN.md §5), Phase 3.
//
//   Header      Source (Generator / MIDI In), Note map (GM / Chromatic), and the drag handle:
//               4 bars of the test pattern as a .mid to drop on a clip slot
//   Kit bar     Randomise kit, Undo, Redo, Character (Matched … Wild), the family Randomise chose,
//               and Kits (save the kit as a preset, load one)
//   Pads        eight pads with their role and sample, a lock and a dice. Click to select and play
//               one; drop a sample on one (from Live's browser, Finder or DrumChimp's browser) to
//               load it. A pad lights when it plays.
//   Browser     the library, filtered by role (the selected pad's, by default) and by text. Click
//               a sample to hear it; double-click it, or drag it onto a pad, to load it.
//   Pad panel   the selected pad: its role, its sample (Load…, Clear), Level, Pan, Tune, Decay,
//               Velocity, Choke, Mute and Solo
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

    // A small drawn button: a padlock (toggles) or a die.
    class IconButton : public juce::Button
    {
    public:
        enum class Icon { lock, dice };
        IconButton (Icon i, const juce::String& tip);
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;

    private:
        const Icon icon;
    };

    class PadButton : public juce::Component,
                      public juce::SettableTooltipClient,
                      public juce::FileDragAndDropTarget,
                      public juce::DragAndDropTarget
    {
    public:
        PadButton (DrumChimpProcessor& p, int index, std::function<void (int)> onSelect);

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;

        bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
        void fileDragEnter (const juce::StringArray&, int, int) override { setDragOver (true); }
        void fileDragExit (const juce::StringArray&) override { setDragOver (false); }
        void filesDropped (const juce::StringArray& files, int x, int y) override;

        bool isInterestedInDragSource (const SourceDetails& d) override { return d.description.isString(); }
        void itemDragEnter (const SourceDetails&) override { setDragOver (true); }
        void itemDragExit (const SourceDetails&) override { setDragOver (false); }
        void itemDropped (const SourceDetails& d) override;

        void refresh (bool selected, NoteMap map);
        void hit() { flash = 1.0f; repaint(); }
        void fade();

    private:
        void setDragOver (bool over) { dragOver = over; repaint(); }

        DrumChimpProcessor& processor;
        const int index;
        std::function<void (int)> onSelect;
        IconButton lock { IconButton::Icon::lock, "Lock: Randomise leaves this pad alone" };
        IconButton dice { IconButton::Icon::dice, "A new sample for this pad, of its role" };
        DrumChimpProcessor::PadSource source;
        bool selected = false, dragOver = false;
        NoteMap noteMap = NoteMap::gm;
        float flash = 0.0f;
    };

    // The library, filtered, as a list. Click: hear it. Double-click: load it into the selected pad.
    // Drag: onto any pad.
    class Browser : public juce::Component,
                    private juce::ListBoxModel
    {
    public:
        Browser (DrumChimpProcessor& p, std::function<int()> selectedPad);
        void resized() override;
        void refresh();                    // the library or the selected pad changed

    private:
        int getNumRows() override { return (int) rows.size(); }
        void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
        void listBoxItemClicked (int row, const juce::MouseEvent&) override;
        void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
        juce::var getDragSourceDescription (const juce::SparseSet<int>& rows) override;
        void filter();

        DrumChimpProcessor& processor;
        std::function<int()> selectedPad;
        juce::ComboBox role;
        juce::TextEditor search;
        juce::ListBox list { "Library", this };
        juce::Label hint;
        std::shared_ptr<const LibraryIndex> index;
        std::vector<const SampleInfo*> rows;
    };

    class DrumChimpEditor : public juce::AudioProcessorEditor,
                            public juce::DragAndDropContainer,
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
        void showKitsMenu();
        void saveKit();

        DrumChimpProcessor& processor;
        juce::LookAndFeel_V4 lookAndFeel;
        juce::TooltipWindow tooltips { this };

        juce::ComboBox source, noteMap;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> sourceAttachment, noteMapAttachment;
        DragHandle dragHandle;

        juce::TextButton randomise { "Randomise kit" }, undo { "Undo" }, redo { "Redo" }, kits { "Kits" };
        juce::Slider character { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> characterAttachment;
        juce::Label characterLabel, familyLabel;

        std::vector<std::unique_ptr<PadButton>> pads;
        int selected = 0;

        Browser browser;

        juce::Label padTitle, padSample;
        juce::ComboBox padRole;
        juce::TextButton loadButton { "Load..." }, clearButton { "Clear" };
        std::array<Knob, 5> knobs {{ { {}, {}, "level" }, { {}, {}, "pan" }, { {}, {}, "tune" }, { {}, {}, "decay" }, { {}, {}, "vel" } }};
        std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 5> knobAttachments;
        juce::ComboBox choke;
        juce::ToggleButton mute { "Mute" }, solo { "Solo" };
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> chokeAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> muteAttachment, soloAttachment;
        std::unique_ptr<juce::FileChooser> chooser;
        std::unique_ptr<juce::AlertWindow> saveWindow;

        juce::Label status, libraryStatus;
        juce::uint32 seenKit = ~0u, seenLibrary = ~0u;
        NoteMap seenMap = NoteMap::gm;
        juce::Rectangle<int> padPanelArea, browserArea;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumChimpEditor)
    };
}
