// The editor (PLAN.md §5), Phase 6. Everything is laid out at 960 × 760 in `content`, which is
// scaled to the window: drag the corner to resize (70%–150%, kept with the set).
//
//   Header      Source (Generator / MIDI In), Note map (GM / Chromatic), and the drag handle:
//               the pattern as a .mid to drop on a clip slot
//   Kit bar     Randomise kit, Undo, Redo, where Randomise takes kits from (your library, or
//               Ableton's Drum Rack kits), Character (Matched … Wild), the family or kit chosen,
//               and Kits (save the kit as a preset, load one, or load an Ableton kit by name)
//   Pads        eight pads with their role and sample, a lock and a dice. Click to select and play
//               one; drop a sample on one (from Live's browser, Finder or DrumChimp's browser) to
//               load it. A pad lights when it plays.
//   Browser     the library, filtered by role (the selected pad's, by default) and by text. Click
//               a sample to hear it; double-click it, or drag it onto a pad, to load it.
//   Pad panel   the selected pad: its role, its sample (Load…, Clear), Level, Pan, Tune, Decay,
//               Velocity, Choke, Mute and Solo
//   Pattern     Style, Bars, Fill, Ghost notes, Generate (a new seed), and Density, Complexity,
//               Swing and Humanise
//   Step grid   the pattern: a lane per pad, velocity as brightness, rolls as dots, the playhead.
//               Click a step to add or remove a hit, drag a hit up or down for its velocity; lock a
//               lane to keep it through Generate.
//   Settings    the library's places (from Live, and folders added here), Add folder, Remove,
//               Rescan; why Ableton Pack samples aren't there
//   Status      transport, notes in and out, or a warning (a clip doubling the generator, a
//               missing sample); the library scan
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

    // The pattern, a lane per pad.
    class StepGrid : public juce::Component,
                     public juce::SettableTooltipClient
    {
    public:
        explicit StepGrid (DrumChimpProcessor& p);
        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void refresh();                    // the pattern, the locks or the pads' roles changed
        void setPlayhead (int step);

    private:
        juce::Rectangle<int> cellArea() const;
        bool cellAt (juce::Point<int> p, int& pad, int& step) const;

        DrumChimpProcessor& processor;
        Pattern pattern;
        int pressedPad = -1, pressedStep = -1, pressedVelocity = 0;
        bool dragged = false;
        std::vector<std::unique_ptr<IconButton>> locks;
        int playhead = -1;
        static constexpr int labelWidth = 92;
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

    // The library's places, over the rest of the editor.
    class SettingsPanel : public juce::Component,
                          private juce::ListBoxModel
    {
    public:
        explicit SettingsPanel (DrumChimpProcessor& p);
        void paint (juce::Graphics&) override;
        void resized() override;
        void refresh();
        std::function<void()> onClose;

    private:
        struct Place { juce::String kind, name, path; bool removable; int samples, drums; };
        int getNumRows() override { return (int) places.size(); }
        void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
        void selectedRowsChanged (int) override;

        DrumChimpProcessor& processor;
        juce::ListBox list { "Places", this };
        juce::TextButton add { "Add folder..." }, remove { "Remove folder" }, rescan { "Rescan" }, close { "Close" };
        juce::Label note;
        std::vector<Place> places;
        std::unique_ptr<juce::FileChooser> chooser;
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

        struct Content : juce::Component
        {
            std::function<void (juce::Graphics&)> onPaint;
            void paint (juce::Graphics& g) override { if (onPaint) onPaint (g); }
        };
        static constexpr int baseWidth = 960, baseHeight = 760;

        void timerCallback() override;
        void layout();
        void paintContent (juce::Graphics&);
        void select (int pad);
        void refreshPads();
        void chooseSample();
        void showKitsMenu();
        void saveKit();
        void setUpKnob (Knob& k, const char* name);

        DrumChimpProcessor& processor;
        juce::LookAndFeel_V4 lookAndFeel;
        Content content;                   // first: every other component is its child
        juce::TooltipWindow tooltips { this };
        juce::TextButton settingsButton { "Settings" };

        juce::ComboBox source, noteMap;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> sourceAttachment, noteMapAttachment;
        DragHandle dragHandle;

        juce::TextButton randomise { "Randomise kit" }, undo { "Undo" }, redo { "Redo" }, kits { "Kits" };
        juce::Slider character { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
        juce::ComboBox kitSource;
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

        juce::ComboBox style, bars, fill;
        juce::ToggleButton ghosts { "Ghost notes" };
        juce::TextButton generate { "Generate" };
        std::array<Knob, 4> patternKnobs {{ { {}, {}, "density" }, { {}, {}, "complexity" }, { {}, {}, "swing" }, { {}, {}, "humanise" } }};
        std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 4> patternKnobAttachments;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> styleAttachment, barsAttachment, fillAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> ghostsAttachment;
        StepGrid grid;
        SettingsPanel settings;
        juce::uint32 seenPattern = ~0u;
        juce::uint32 doubledUntil = 0;     // ms: the status line warns until then

        juce::Label status, libraryStatus;
        juce::uint32 seenKit = ~0u, seenLibrary = ~0u;
        NoteMap seenMap = NoteMap::gm;
        juce::Rectangle<int> padPanelArea, browserArea, patternArea;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumChimpEditor)
    };
}
