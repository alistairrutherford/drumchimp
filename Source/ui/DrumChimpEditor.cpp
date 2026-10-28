#include "DrumChimpEditor.h"

namespace drumchimp
{
    namespace
    {
        const juce::Colour background { 0xff15171c }, panel { 0xff1e2128 }, raised { 0xff2a2e37 }, outline { 0xff383d48 };
        const juce::Colour text { 0xffe6e8ec }, dim { 0xff9aa1ad }, faint { 0xff5d6472 }, accent { 0xff5cc8a8 }, warning { 0xfff2a541 };

        juce::Font font (float height, bool bold = false)
        {
            return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
        }

        using Status = DrumChimpProcessor::PadSource::Status;

        juce::String sampleName (const DrumChimpProcessor::PadSource& s)
        {
            switch (s.status)
            {
                case Status::empty:   return "Empty";
                case Status::loading: return "Loading...";
                case Status::missing: return "Missing: " + juce::File (s.path).getFileNameWithoutExtension();
                case Status::failed:  return "Can't load: " + juce::File (s.path).getFileNameWithoutExtension();
                case Status::loaded:  break;
            }
            return juce::File (s.path).getFileNameWithoutExtension();
        }

        juce::StringArray audioFileTypes()
        {
            return { "*.wav", "*.aif", "*.aiff", "*.flac" };
        }
    }

    //==========================================================================
    DragHandle::DragHandle (DrumChimpProcessor& p) : processor (p)
    {
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        setTooltip ("Drag onto a clip slot or the Arrangement: 4 bars of the test pattern as a MIDI clip");
    }

    void DragHandle::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (raised);
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (accent);
        g.drawRoundedRectangle (r, 4.0f, dragging ? 2.0f : 1.0f);
        g.setColour (text);
        g.setFont (font (12.5f, true));
        g.drawText ("Drag 4-bar MIDI", getLocalBounds(), juce::Justification::centred);
    }

    void DragHandle::mouseDrag (const juce::MouseEvent& e)
    {
        if (dragging || e.getDistanceFromDragStart() < 4)
            return;
        juce::String error;
        const auto file = processor.writeTestPattern (4, error);
        if (! file.existsAsFile())
        {
            setTooltip (error);
            return;
        }
        dragging = true;
        repaint();
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this,
                                                                    [safe = juce::Component::SafePointer<DragHandle> (this)]
        {
            if (safe != nullptr)
            {
                safe->dragging = false;
                safe->repaint();
            }
        });
    }

    void DragHandle::mouseUp (const juce::MouseEvent&)
    {
        dragging = false;
        repaint();
    }

    //==========================================================================
    PadButton::PadButton (DrumChimpProcessor& p, int i, std::function<void (int)> select)
        : processor (p), index (i), onSelect (std::move (select))
    {
    }

    void PadButton::refresh (bool isSelected, NoteMap map)
    {
        source = processor.padSource (index);
        selected = isSelected;
        noteMap = map;
        setTooltip (source.message.isNotEmpty() ? source.message : source.path);
        repaint();
    }

    void PadButton::fade()
    {
        if (flash > 0.0f)
        {
            flash = flash < 0.05f ? 0.0f : flash * 0.6f;
            repaint();
        }
    }

    void PadButton::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat().reduced (4.0f);
        g.setColour (raised.interpolatedWith (accent, 0.55f * flash));
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (dragOver ? accent : selected ? text : outline);
        g.drawRoundedRectangle (r, 6.0f, dragOver || selected ? 2.0f : 1.0f);

        auto inner = r.reduced (9.0f, 7.0f).toNearestInt();
        g.setFont (font (13.0f, true));
        g.setColour (text);
        g.drawText (defaultRoles[(size_t) index], inner.removeFromTop (18), juce::Justification::topLeft);
        g.setFont (font (11.0f));
        g.setColour (faint);
        g.drawText (juce::MidiMessage::getMidiNoteName (noteForPad (noteMap, index), true, true, 3), inner.removeFromBottom (14),
                    juce::Justification::bottomRight);
        const bool problem = source.status == Status::missing || source.status == Status::failed;
        g.setColour (problem ? warning : source.status == Status::loaded ? dim : faint);
        g.setFont (font (12.0f));
        g.drawFittedText (sampleName (source), inner, juce::Justification::topLeft, 3, 0.9f);
    }

    void PadButton::mouseDown (const juce::MouseEvent&)
    {
        onSelect (index);
        processor.audition (index);
    }

    void PadButton::filesDropped (const juce::StringArray& files, int, int)
    {
        dragOver = false;
        if (! files.isEmpty())
            processor.loadSample (index, juce::File (files[0]));
        onSelect (index);
    }

    //==========================================================================
    DrumChimpEditor::DrumChimpEditor (DrumChimpProcessor& p)
        : AudioProcessorEditor (p), processor (p), dragHandle (p)
    {
        lookAndFeel.setColourScheme ({ background, panel, raised, outline, text, accent, text, raised, text });
        lookAndFeel.setColour (juce::Slider::rotarySliderFillColourId, accent);
        lookAndFeel.setColour (juce::Slider::rotarySliderOutlineColourId, outline);
        lookAndFeel.setColour (juce::Slider::thumbColourId, text);
        lookAndFeel.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        lookAndFeel.setColour (juce::ToggleButton::tickColourId, accent);
        setLookAndFeel (&lookAndFeel);

        source.addItemList ({ "Generator", "MIDI In" }, 1);
        source.setTooltip ("Generator: the test pattern plays with Live's transport and goes to MIDI out. MIDI In: only incoming notes play the pads");
        sourceAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.state, "source", source);
        noteMap.addItemList ({ "GM", "Chromatic" }, 1);
        noteMap.setTooltip ("Which notes play the pads. GM: the General MIDI drum notes (they also play a Drum Rack). Chromatic: C1 to G1");
        noteMapAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.state, "notemap", noteMap);
        addAndMakeVisible (source);
        addAndMakeVisible (noteMap);
        addAndMakeVisible (dragHandle);

        for (int i = 0; i < numPads; ++i)
        {
            pads.push_back (std::make_unique<PadButton> (p, i, [this] (int pad) { select (pad); }));
            addAndMakeVisible (*pads.back());
        }

        padTitle.setFont (font (15.0f, true));
        padTitle.setColour (juce::Label::textColourId, text);
        padSample.setFont (font (12.0f));
        padSample.setColour (juce::Label::textColourId, dim);
        padSample.setJustificationType (juce::Justification::topLeft);
        addAndMakeVisible (padTitle);
        addAndMakeVisible (padSample);

        loadButton.onClick = [this] { chooseSample(); };
        clearButton.onClick = [this] { processor.clearPad (selected); };
        addAndMakeVisible (loadButton);
        addAndMakeVisible (clearButton);

        const std::array<const char*, 5> names { "Level", "Pan", "Tune", "Decay", "Velocity" };
        for (size_t i = 0; i < knobs.size(); ++i)
        {
            knobs[i].slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            knobs[i].slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 18);
            knobs[i].slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            knobs[i].label.setText (names[i], juce::dontSendNotification);
            knobs[i].label.setJustificationType (juce::Justification::centred);
            knobs[i].label.setFont (font (12.0f));
            knobs[i].label.setColour (juce::Label::textColourId, dim);
            addAndMakeVisible (knobs[i].slider);
            addAndMakeVisible (knobs[i].label);
        }
        choke.addItemList (params::chokeGroups, 1);
        choke.setTooltip ("Choke group: a pad cuts off the other pads in its group (the hats share A)");
        addAndMakeVisible (choke);
        addAndMakeVisible (mute);
        addAndMakeVisible (solo);

        status.setFont (font (12.0f));
        status.setColour (juce::Label::textColourId, faint);
        addAndMakeVisible (status);
        libraryStatus.setFont (font (12.0f));
        libraryStatus.setColour (juce::Label::textColourId, faint);
        libraryStatus.setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (libraryStatus);

        setSize (780, 430);
        select (0);
        startTimerHz (30);
    }

    DrumChimpEditor::~DrumChimpEditor()
    {
        stopTimer();
        setLookAndFeel (nullptr);
    }

    void DrumChimpEditor::select (int pad)
    {
        selected = juce::jlimit (0, numPads - 1, pad);
        for (size_t i = 0; i < knobs.size(); ++i)
        {
            knobAttachments[i].reset();
            knobAttachments[i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                processor.state, params::padID (selected, knobs[i].id), knobs[i].slider);
        }
        chokeAttachment.reset();
        chokeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (processor.state, params::padID (selected, "choke"), choke);
        muteAttachment.reset();
        muteAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.state, params::padID (selected, "mute"), mute);
        soloAttachment.reset();
        soloAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.state, params::padID (selected, "solo"), solo);
        refreshPads();
    }

    void DrumChimpEditor::refreshPads()
    {
        seenKit = processor.kitVersion();
        seenLibrary = processor.library->version();
        seenMap = (NoteMap) juce::roundToInt (processor.state.getRawParameterValue ("notemap")->load());
        for (int i = 0; i < numPads; ++i)
            pads[(size_t) i]->refresh (i == selected, seenMap);

        const auto s = processor.padSource (selected);
        padTitle.setText ("Pad " + juce::String (selected + 1) + " · " + defaultRoles[(size_t) selected], juce::dontSendNotification);
        // Where the library has it: its place and how the classifier sorted it.
        juce::String where;
        if (const auto index = processor.library->index(); index != nullptr && s.path.isNotEmpty())
            if (const auto* info = index->find (s.path.toStdString()))
                where = juce::String (info->root) + " · " + roleName (info->classification.role) + " · " + juce::String (info->seconds, 2) + " s\n";
        padSample.setText (s.message.isNotEmpty() ? s.message : s.path.isNotEmpty() ? where + s.path : juce::String ("Empty: drop a sample on the pad, or Load..."),
                           juce::dontSendNotification);
        padSample.setColour (juce::Label::textColourId, s.message.isNotEmpty() ? warning : dim);
        clearButton.setEnabled (s.status != Status::empty);
    }

    void DrumChimpEditor::chooseSample()
    {
        const auto current = processor.padSource (selected).path;
        chooser = std::make_unique<juce::FileChooser> ("Choose a sample for pad " + juce::String (selected + 1),
                                                       current.isNotEmpty() ? juce::File (current).getParentDirectory() : juce::File(),
                                                       audioFileTypes().joinIntoString (";"));
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this, pad = selected] (const juce::FileChooser& fc)
        {
            if (const auto file = fc.getResult(); file.existsAsFile())
                processor.loadSample (pad, file);
        });
    }

    void DrumChimpEditor::timerCallback()
    {
        const auto map = (NoteMap) juce::roundToInt (processor.state.getRawParameterValue ("notemap")->load());
        if (processor.kitVersion() != seenKit || map != seenMap || processor.library->version() != seenLibrary)
            refreshPads();

        const auto lib = processor.library->status();
        libraryStatus.setText (lib.scanning && lib.samples == 0 ? juce::String ("Library: scanning...")
                                   : "Library: " + juce::String ((int) lib.drums) + " drums in " + juce::String ((int) lib.samples) + " samples, "
                                         + juce::String ((int) lib.roots) + " places" + (lib.scanning ? juce::String (" (scanning again)") : juce::String()),
                               juce::dontSendNotification);

        const auto played = processor.takeHits();
        for (int i = 0; i < numPads; ++i)
        {
            if ((played & (1u << i)) != 0)
                pads[(size_t) i]->hit();
            else
                pads[(size_t) i]->fade();
        }

        const auto s = processor.status();
        status.setText (juce::String (s.playing ? "Playing" : "Stopped") + " · " + juce::String (s.bpm, 1) + " BPM · beat "
                            + juce::String (s.ppq, 2) + " · notes in " + juce::String (s.notesIn) + ", out " + juce::String (s.notesOut),
                        juce::dontSendNotification);
    }

    void DrumChimpEditor::paint (juce::Graphics& g)
    {
        g.fillAll (background);
        g.setColour (text);
        g.setFont (font (18.0f, true));
        g.drawText ("DrumChimp", 16, 12, 200, 28, juce::Justification::centredLeft);

        auto panelArea = getLocalBounds().reduced (16).withTrimmedTop (44).withTrimmedBottom (26).withTrimmedLeft (436);
        g.setColour (panel);
        g.fillRoundedRectangle (panelArea.toFloat(), 6.0f);
    }

    void DrumChimpEditor::resized()
    {
        auto r = getLocalBounds().reduced (16);
        auto top = r.removeFromTop (28);
        dragHandle.setBounds (top.removeFromRight (140));
        top.removeFromRight (10);
        noteMap.setBounds (top.removeFromRight (110));
        top.removeFromRight (8);
        source.setBounds (top.removeFromRight (120));
        r.removeFromTop (16);
        auto statusRow = r.removeFromBottom (20);
        libraryStatus.setBounds (statusRow.removeFromRight (340));
        status.setBounds (statusRow);
        r.removeFromBottom (6);

        auto padArea = r.removeFromLeft (420);
        const int w = padArea.getWidth() / 4, h = padArea.getHeight() / 2;
        for (int i = 0; i < numPads; ++i)
            pads[(size_t) i]->setBounds (padArea.getX() + (i % 4) * w, padArea.getY() + (i / 4) * h, w, h);

        auto panelArea = r.withTrimmedLeft (16).reduced (14, 12);
        auto titleRow = panelArea.removeFromTop (24);
        clearButton.setBounds (titleRow.removeFromRight (60));
        titleRow.removeFromRight (6);
        loadButton.setBounds (titleRow.removeFromRight (70));
        padTitle.setBounds (titleRow);
        padSample.setBounds (panelArea.removeFromTop (54));
        panelArea.removeFromTop (8);

        auto knobRow = panelArea.removeFromTop (110);
        const int kw = knobRow.getWidth() / (int) knobs.size();
        for (auto& k : knobs)
        {
            auto cell = knobRow.removeFromLeft (kw);
            k.label.setBounds (cell.removeFromTop (16));
            k.slider.setBounds (cell);
        }
        panelArea.removeFromTop (12);
        auto bottomRow = panelArea.removeFromTop (26);
        choke.setBounds (bottomRow.removeFromLeft (90));
        bottomRow.removeFromLeft (16);
        mute.setBounds (bottomRow.removeFromLeft (70));
        solo.setBounds (bottomRow.removeFromLeft (70));
    }
}
