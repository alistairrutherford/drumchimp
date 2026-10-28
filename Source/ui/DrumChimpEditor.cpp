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

        juce::String fileName (const SampleInfo& s)
        {
            const auto slash = s.relative.rfind ('/');
            auto name = s.relative.substr (slash == std::string::npos ? 0 : slash + 1);
            if (const auto dot = name.rfind ('.'); dot != std::string::npos)
                name.erase (dot);
            return name;
        }

        // Browser filter items: 1 = the selected pad's role, then each role, then everything.
        constexpr int padRoleItem = 1, firstRoleItem = 2, allItem = firstRoleItem + numRoles;
    }

    //==========================================================================
    DragHandle::DragHandle (DrumChimpProcessor& p) : processor (p)
    {
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        setTooltip ("Drag onto a clip slot or the Arrangement: the pattern as a MIDI clip");
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
        g.drawText ("Drag MIDI", getLocalBounds(), juce::Justification::centred);
    }

    void DragHandle::mouseDrag (const juce::MouseEvent& e)
    {
        if (dragging || e.getDistanceFromDragStart() < 4)
            return;
        juce::String error;
        const auto file = processor.writePatternMidi (error);
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
    IconButton::IconButton (Icon i, const juce::String& tip) : juce::Button (tip), icon (i)
    {
        setTooltip (tip);
        setClickingTogglesState (icon == Icon::lock);
    }

    void IconButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
    {
        const auto r = getLocalBounds().toFloat().reduced (3.0f);
        const bool on = getToggleState();
        g.setColour (on ? accent : highlighted || down ? drumchimp::text : faint);   // (Button has its own `text`)
        if (icon == Icon::lock)
        {
            const auto body = r.withTrimmedTop (r.getHeight() * 0.45f);
            g.fillRoundedRectangle (body, 1.5f);
            juce::Path shackle;
            const float w = body.getWidth() * 0.62f, x = body.getCentreX() - w / 2.0f;
            const float top = r.getY() + 1.0f, bottom = body.getY();
            shackle.startNewSubPath (x, bottom);
            shackle.lineTo (x, top + w / 2.0f);
            shackle.addArc (x, top, w, w, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi);
            if (on)
                shackle.lineTo (x + w, bottom);
            else
                shackle.lineTo (x + w, top + w / 2.0f + 1.0f);    // open: the shackle stops short
            g.strokePath (shackle, juce::PathStrokeType (1.6f));
        }
        else
        {
            g.drawRoundedRectangle (r.reduced (0.5f), 2.5f, 1.4f);
            const float d = r.getWidth() / 4.0f;
            for (const auto& [px, py] : { std::pair { 1, 1 }, { 3, 3 }, { 1, 3 }, { 3, 1 }, { 2, 2 } })
                g.fillEllipse (r.getX() + px * d - 1.3f, r.getY() + py * d - 1.3f, 2.6f, 2.6f);
        }
    }

    //==========================================================================
    PadButton::PadButton (DrumChimpProcessor& p, int i, std::function<void (int)> select)
        : processor (p), index (i), onSelect (std::move (select))
    {
        lock.onClick = [this] { processor.setLocked (index, lock.getToggleState()); };
        dice.onClick = [this]
        {
            processor.randomisePad (index);
            onSelect (index);
        };
        addAndMakeVisible (lock);
        addAndMakeVisible (dice);
    }

    void PadButton::resized()
    {
        auto r = getLocalBounds().reduced (10, 8).removeFromBottom (20);
        lock.setBounds (r.removeFromLeft (20));
        r.removeFromLeft (4);
        dice.setBounds (r.removeFromLeft (20));
    }

    void PadButton::refresh (bool isSelected, NoteMap map)
    {
        source = processor.padSource (index);
        selected = isSelected;
        noteMap = map;
        lock.setToggleState (processor.isLocked (index), juce::dontSendNotification);
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
        g.drawText (padRoleName (processor.padRole (index)), inner.removeFromTop (18), juce::Justification::topLeft);
        auto bottom = inner.removeFromBottom (20);
        g.setFont (font (11.0f));
        g.setColour (faint);
        g.drawText (juce::MidiMessage::getMidiNoteName (noteForPad (noteMap, index), true, true, 3), bottom, juce::Justification::bottomRight);
        const bool problem = source.status == Status::missing || source.status == Status::failed;
        g.setColour (problem ? warning : source.status == Status::loaded ? dim : faint);
        g.setFont (font (12.0f));
        g.drawFittedText (sampleName (source), inner, juce::Justification::topLeft, 4, 0.9f);
    }

    void PadButton::mouseDown (const juce::MouseEvent&)
    {
        onSelect (index);
        processor.audition (index);
    }

    void PadButton::filesDropped (const juce::StringArray& files, int, int)
    {
        setDragOver (false);
        if (! files.isEmpty())
            processor.loadSample (index, juce::File (files[0]));
        onSelect (index);
    }

    void PadButton::itemDropped (const SourceDetails& d)
    {
        setDragOver (false);
        processor.loadSample (index, juce::File (d.description.toString()));
        onSelect (index);
    }

    //==========================================================================
    StepGrid::StepGrid (DrumChimpProcessor& p) : processor (p)
    {
        for (int pad = 0; pad < numPads; ++pad)
        {
            locks.push_back (std::make_unique<IconButton> (IconButton::Icon::lock, "Lock this lane: Generate and setting changes keep it"));
            locks.back()->onClick = [this, pad] { processor.setLaneLocked (pad, locks[(size_t) pad]->getToggleState()); };
            addAndMakeVisible (*locks.back());
        }
        refresh();
    }

    juce::Rectangle<int> StepGrid::cellArea() const
    {
        return getLocalBounds().withTrimmedLeft (labelWidth).withTrimmedTop (16);
    }

    void StepGrid::resized()
    {
        const auto cells = cellArea();
        const int rowHeight = cells.getHeight() / numPads;
        for (int pad = 0; pad < numPads; ++pad)
            locks[(size_t) pad]->setBounds (labelWidth - 22, cells.getY() + pad * rowHeight + (rowHeight - 16) / 2, 16, 16);
    }

    void StepGrid::refresh()
    {
        pattern = processor.currentPattern();
        for (int pad = 0; pad < numPads; ++pad)
            locks[(size_t) pad]->setToggleState (processor.isLaneLocked (pad), juce::dontSendNotification);
        repaint();
    }

    void StepGrid::setPlayhead (int step)
    {
        if (step != playhead)
        {
            playhead = step;
            repaint();
        }
    }

    void StepGrid::paint (juce::Graphics& g)
    {
        const auto cells = cellArea();
        const int steps = pattern.steps();
        if (steps <= 0)
            return;
        const float w = (float) cells.getWidth() / (float) steps;
        const int rowHeight = cells.getHeight() / numPads;
        const int beat = pattern.stepsPerBar / 4;

        // Bar numbers along the top; beats and bars as lines.
        g.setFont (font (11.0f));
        for (int bar = 0; bar < pattern.bars; ++bar)
        {
            g.setColour (faint);
            g.drawText (juce::String (bar + 1), (int) (cells.getX() + bar * pattern.stepsPerBar * w) + 2, 0, 40, 14, juce::Justification::centredLeft);
        }
        for (int s = 0; s < steps; ++s)
        {
            const float x = cells.getX() + s * w;
            if (s == playhead)
            {
                g.setColour (raised.brighter (0.15f));
                g.fillRect (x, (float) cells.getY(), w, (float) (rowHeight * numPads));
            }
            if (s % beat == 0)
            {
                g.setColour (s % pattern.stepsPerBar == 0 ? dim.withAlpha (0.5f) : outline);
                g.drawVerticalLine ((int) x, (float) cells.getY(), (float) (cells.getY() + rowHeight * numPads));
            }
        }

        for (int pad = 0; pad < numPads; ++pad)
        {
            const int y = cells.getY() + pad * rowHeight;
            g.setColour (dim);
            g.setFont (font (11.5f));
            g.drawText (padRoleName (processor.padRole (pad)), 0, y, labelWidth - 26, rowHeight, juce::Justification::centredLeft);
            for (int s = 0; s < steps; ++s)
            {
                const auto r = juce::Rectangle<float> (cells.getX() + s * w, (float) y, w, (float) rowHeight).reduced (w > 8.0f ? 1.5f : 0.5f, 2.0f);
                const auto& c = pattern.lanes[(size_t) pad][(size_t) s];
                if (c.velocity == 0)
                {
                    g.setColour (panel.brighter (0.05f));
                    g.fillRoundedRectangle (r, 2.0f);
                    continue;
                }
                g.setColour (accent.withAlpha (0.3f + 0.7f * (float) c.velocity / 127.0f));
                g.fillRoundedRectangle (r, 2.0f);
                if (c.repeats > 1 && r.getWidth() > 6.0f)
                {
                    g.setColour (background);
                    for (int k = 0; k < c.repeats; ++k)
                        g.fillEllipse (r.getX() + r.getWidth() * (float) (k + 1) / (float) (c.repeats + 1) - 1.2f, r.getCentreY() - 1.2f, 2.4f, 2.4f);
                }
            }
        }
    }

    void StepGrid::mouseDown (const juce::MouseEvent& e)
    {
        const auto cells = cellArea();
        if (! cells.contains (e.getPosition()) || pattern.steps() <= 0)
            return;
        const int step = (int) ((float) (e.x - cells.getX()) * (float) pattern.steps() / (float) cells.getWidth());
        const int pad = (e.y - cells.getY()) / (cells.getHeight() / numPads);
        processor.toggleStep (pad, step);
        refresh();
    }

    //==========================================================================
    Browser::Browser (DrumChimpProcessor& p, std::function<int()> selectedPadFn)
        : processor (p), selectedPad (std::move (selectedPadFn))
    {
        role.addItem ("The pad's role", padRoleItem);
        role.addSeparator();
        for (int r = 0; r < numRoles; ++r)
            role.addItem (roleNames[(size_t) r], firstRoleItem + r);
        role.addItem ("Everything", allItem);
        role.setSelectedId (padRoleItem, juce::dontSendNotification);
        role.onChange = [this] { filter(); };
        addAndMakeVisible (role);

        search.setTextToShowWhenEmpty ("Search", faint);
        search.onTextChange = [this] { filter(); };
        addAndMakeVisible (search);

        list.setRowHeight (22);
        list.setColour (juce::ListBox::backgroundColourId, panel);
        list.setOutlineThickness (0);
        addAndMakeVisible (list);

        hint.setFont (font (11.5f));
        hint.setColour (juce::Label::textColourId, faint);
        hint.setText (juce::String (juce::CharPointer_UTF8 ("Click to hear \xc2\xb7 double-click, or drag onto a pad, to load")), juce::dontSendNotification);
        addAndMakeVisible (hint);
    }

    void Browser::resized()
    {
        auto r = getLocalBounds();
        auto top = r.removeFromTop (26);
        role.setBounds (top.removeFromLeft (150));
        top.removeFromLeft (8);
        search.setBounds (top);
        r.removeFromTop (6);
        hint.setBounds (r.removeFromBottom (18));
        list.setBounds (r);
    }

    void Browser::refresh()
    {
        index = processor.library->index();
        filter();
    }

    void Browser::filter()
    {
        rows.clear();
        if (index != nullptr)
        {
            const int id = role.getSelectedId();
            const auto wanted = id == padRoleItem ? libraryRole (processor.padRole (selectedPad())) : (Role) (id - firstRoleItem);
            const auto words = juce::StringArray::fromTokens (search.getText().toLowerCase(), " ", "");
            const auto matches = [&] (const SampleInfo& s)
            {
                if (words.isEmpty())
                    return true;
                const auto haystack = juce::String (s.root + " " + s.relative).toLowerCase();
                for (const auto& w : words)
                    if (w.isNotEmpty() && ! haystack.contains (w))
                        return false;
                return true;
            };
            if (id == allItem)
            {
                for (const auto& s : index->all())
                    if (matches (s))
                        rows.push_back (&s);
            }
            else
                for (const auto i : index->withRole (wanted))
                    if (matches (index->all()[i]))
                        rows.push_back (&index->all()[i]);
        }
        list.updateContent();
        list.deselectAllRows();
        list.repaint();
    }

    void Browser::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool isSelected)
    {
        if (row < 0 || row >= (int) rows.size())
            return;
        const auto& s = *rows[(size_t) row];
        if (isSelected)
        {
            g.setColour (raised);
            g.fillRect (0, 0, width, height);
        }
        auto r = juce::Rectangle<int> (0, 0, width, height).reduced (8, 0);
        g.setFont (font (11.0f));
        g.setColour (faint);
        g.drawText (juce::String (s.root), r.removeFromRight (120), juce::Justification::centredRight);
        g.setFont (font (12.5f));
        g.setColour (isSelected ? text : dim);
        g.drawText (fileName (s), r, juce::Justification::centredLeft);
    }

    void Browser::listBoxItemClicked (int row, const juce::MouseEvent&)
    {
        if (row >= 0 && row < (int) rows.size())
            processor.previewSample (juce::File (rows[(size_t) row]->path));
    }

    void Browser::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
    {
        if (row >= 0 && row < (int) rows.size())
            processor.loadSample (selectedPad(), juce::File (rows[(size_t) row]->path));
    }

    juce::var Browser::getDragSourceDescription (const juce::SparseSet<int>& selectedRows)
    {
        const int row = selectedRows.isEmpty() ? -1 : selectedRows[0];
        return row >= 0 && row < (int) rows.size() ? juce::var (juce::String (rows[(size_t) row]->path)) : juce::var();
    }

    //==========================================================================
    DrumChimpEditor::DrumChimpEditor (DrumChimpProcessor& p)
        : AudioProcessorEditor (p), processor (p), dragHandle (p), browser (p, [this] { return selected; }), grid (p)
    {
        lookAndFeel.setColourScheme ({ background, panel, raised, outline, text, accent, text, raised, text });
        lookAndFeel.setColour (juce::Slider::rotarySliderFillColourId, accent);
        lookAndFeel.setColour (juce::Slider::rotarySliderOutlineColourId, outline);
        lookAndFeel.setColour (juce::Slider::thumbColourId, text);
        lookAndFeel.setColour (juce::Slider::trackColourId, accent);
        lookAndFeel.setColour (juce::Slider::backgroundColourId, outline);
        lookAndFeel.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        lookAndFeel.setColour (juce::ToggleButton::tickColourId, accent);
        lookAndFeel.setColour (juce::TextEditor::backgroundColourId, raised);
        lookAndFeel.setColour (juce::TextEditor::outlineColourId, outline);
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

        randomise.setTooltip ("A new kit from your library for every unlocked pad");
        randomise.setColour (juce::TextButton::buttonColourId, accent.withAlpha (0.25f));
        randomise.onClick = [this] { processor.randomiseKit(); };
        undo.onClick = [this] { processor.undoKit(); };
        redo.onClick = [this] { processor.redoKit(); };
        kits.setTooltip ("Save this kit (samples, roles and sounds) or load a saved one");
        kits.onClick = [this] { showKitsMenu(); };
        for (auto* b : { &randomise, &undo, &redo, &kits })
            addAndMakeVisible (*b);

        characterLabel.setText ("Matched", juce::dontSendNotification);
        characterLabel.setFont (font (12.0f));
        characterLabel.setColour (juce::Label::textColourId, dim);
        characterLabel.setJustificationType (juce::Justification::centredRight);
        character.setTooltip ("Randomise: Matched keeps a kit to one family (a machine, a pack's sound); Wild takes any sample of each role");
        characterAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.state, "character", character);
        familyLabel.setFont (font (12.0f));
        familyLabel.setColour (juce::Label::textColourId, dim);
        addAndMakeVisible (characterLabel);
        addAndMakeVisible (character);
        addAndMakeVisible (familyLabel);

        for (int i = 0; i < numPads; ++i)
        {
            pads.push_back (std::make_unique<PadButton> (p, i, [this] (int pad) { select (pad); }));
            addAndMakeVisible (*pads.back());
        }
        addAndMakeVisible (browser);

        padTitle.setFont (font (15.0f, true));
        padTitle.setColour (juce::Label::textColourId, text);
        padSample.setFont (font (12.0f));
        padSample.setColour (juce::Label::textColourId, dim);
        padSample.setJustificationType (juce::Justification::topLeft);
        addAndMakeVisible (padTitle);
        addAndMakeVisible (padSample);

        for (int r = 0; r < numPadRoles; ++r)
            padRole.addItem (padRoleNames[(size_t) r], r + 1);
        padRole.setTooltip ("What this pad is for: what Randomise and its dice pick, and what the browser shows");
        padRole.onChange = [this]
        {
            processor.setPadRole (selected, (PadRole) (padRole.getSelectedId() - 1));
            browser.refresh();
        };
        addAndMakeVisible (padRole);

        loadButton.onClick = [this] { chooseSample(); };
        clearButton.onClick = [this] { processor.clearPad (selected); };
        addAndMakeVisible (loadButton);
        addAndMakeVisible (clearButton);

        const std::array<const char*, 5> names { "Level", "Pan", "Tune", "Decay", "Velocity" };
        for (size_t i = 0; i < knobs.size(); ++i)
            setUpKnob (knobs[i], names[i]);

        // The pattern.
        const auto& styleList = DrumChimpProcessor::styles();
        for (size_t i = 0; i < styleList.size(); ++i)
            style.addItem (styleList[i].name, (int) i + 1);
        style.setTooltip ("The style the pattern is made from. A new style brings its own swing");
        styleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.state, "style", style);
        bars.addItemList ({ "1 bar", "2 bars", "4 bars", "8 bars" }, 1);
        barsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.state, "bars", bars);
        fill.addItemList ({ "No fills", "Fill every 2", "Fill every 4", "Fill every 8" }, 1);
        fill.setTooltip ("A fill in the last beat (or two) of every 2, 4 or 8 bars, then a crash");
        fillAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.state, "fill", fill);
        ghostsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.state, "ghosts", ghosts);
        generate.setTooltip ("A new pattern in this style (a new seed). Changing a setting remakes this one");
        generate.setColour (juce::TextButton::buttonColourId, accent.withAlpha (0.25f));
        generate.onClick = [this] { processor.newPattern(); };
        for (auto* c : std::initializer_list<juce::Component*> { &style, &bars, &fill, &ghosts, &generate })
            addAndMakeVisible (*c);
        const std::array<const char*, 4> patternNames { "Density", "Complexity", "Swing", "Humanise" };
        for (size_t i = 0; i < patternKnobs.size(); ++i)
        {
            setUpKnob (patternKnobs[i], patternNames[i]);
            patternKnobAttachments[i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.state, patternKnobs[i].id, patternKnobs[i].slider);
        }
        addAndMakeVisible (grid);
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

        setSize (960, 760);
        select (0);
        startTimerHz (30);
    }

    DrumChimpEditor::~DrumChimpEditor()
    {
        stopTimer();
        setLookAndFeel (nullptr);
    }

    void DrumChimpEditor::setUpKnob (Knob& k, const char* name)
    {
        k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 18);
        k.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        k.label.setText (name, juce::dontSendNotification);
        k.label.setJustificationType (juce::Justification::centred);
        k.label.setFont (font (12.0f));
        k.label.setColour (juce::Label::textColourId, dim);
        addAndMakeVisible (k.slider);
        addAndMakeVisible (k.label);
    }

    void DrumChimpEditor::select (int pad)
    {
        const bool changed = pad != selected;
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
        if (changed || seenLibrary == ~0u)
            browser.refresh();
    }

    void DrumChimpEditor::refreshPads()
    {
        seenKit = processor.kitVersion();
        seenMap = (NoteMap) juce::roundToInt (processor.state.getRawParameterValue ("notemap")->load());
        for (int i = 0; i < numPads; ++i)
            pads[(size_t) i]->refresh (i == selected, seenMap);

        const auto s = processor.padSource (selected);
        padTitle.setText ("Pad " + juce::String (selected + 1), juce::dontSendNotification);
        padRole.setSelectedId ((int) processor.padRole (selected) + 1, juce::dontSendNotification);

        // Where the library has it: its place and how the classifier sorted it.
        juce::String where;
        if (const auto index = processor.library->index(); index != nullptr && s.path.isNotEmpty())
            if (const auto* info = index->find (s.path.toStdString()))
                where = juce::String (info->root) + " · sorted as " + roleName (info->classification.role) + " · " + juce::String (info->seconds, 2) + " s\n";
        const bool problem = s.status == Status::missing || s.status == Status::failed;
        padSample.setText (s.path.isEmpty() ? juce::String ("Empty: drop a sample on the pad, or Load...")
                                            : (s.message.isNotEmpty() ? s.message + "\n" : juce::String()) + where + s.path,
                           juce::dontSendNotification);
        padSample.setColour (juce::Label::textColourId, problem ? warning : dim);
        clearButton.setEnabled (s.status != Status::empty);
        undo.setEnabled (processor.canUndo());
        redo.setEnabled (processor.canRedo());
        const auto family = processor.lastFamily();
        familyLabel.setText (family.isEmpty() ? juce::String() : "Family: " + family, juce::dontSendNotification);
    }

    void DrumChimpEditor::chooseSample()
    {
        const auto current = processor.padSource (selected).path;
        chooser = std::make_unique<juce::FileChooser> ("Choose a sample for pad " + juce::String (selected + 1),
                                                       current.isNotEmpty() ? juce::File (current).getParentDirectory() : juce::File(),
                                                       "*.wav;*.aif;*.aiff;*.flac");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this, pad = selected] (const juce::FileChooser& fc)
        {
            if (const auto file = fc.getResult(); file.existsAsFile())
                processor.loadSample (pad, file);
        });
    }

    void DrumChimpEditor::showKitsMenu()
    {
        juce::PopupMenu menu;
        menu.addItem ("Save kit...", [this] { saveKit(); });
        const auto presets = processor.kitPresets();
        menu.addSeparator();
        if (presets.isEmpty())
            menu.addItem ("No saved kits yet", false, false, [] {});
        for (const auto& file : presets)
            menu.addItem (file.getFileNameWithoutExtension(), [this, file]
            {
                juce::String error;
                if (! processor.loadKitPreset (file, error))
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Kit not loaded", error);
                select (selected);
                browser.refresh();
            });
        menu.addSeparator();
        menu.addItem ("Show kits folder", [] { DrumChimpProcessor::kitsFolder().createDirectory(); DrumChimpProcessor::kitsFolder().revealToUser(); });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (kits));
    }

    void DrumChimpEditor::saveKit()
    {
        const auto family = processor.lastFamily();
        saveWindow = std::make_unique<juce::AlertWindow> ("Save kit", "A name for this kit:", juce::MessageBoxIconType::NoIcon, this);
        saveWindow->addTextEditor ("name", family.isNotEmpty() ? family + " kit" : juce::String ("My kit"));
        saveWindow->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
        saveWindow->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        saveWindow->enterModalState (true, juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer<DrumChimpEditor> (this)] (int result)
        {
            if (safe == nullptr)
                return;
            if (result == 1)
            {
                juce::String error;
                if (! safe->processor.saveKitPreset (safe->saveWindow->getTextEditorContents ("name"), error))
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Kit not saved", error);
            }
            safe->saveWindow.reset();
        }), false);
    }

    void DrumChimpEditor::timerCallback()
    {
        const auto map = (NoteMap) juce::roundToInt (processor.state.getRawParameterValue ("notemap")->load());
        const bool libraryChanged = processor.library->version() != seenLibrary;
        if (processor.kitVersion() != seenKit || map != seenMap || libraryChanged)
            refreshPads();
        if (libraryChanged)
        {
            seenLibrary = processor.library->version();
            browser.refresh();
        }

        if (processor.patternVersion() != seenPattern || processor.kitVersion() != seenKit)
        {
            seenPattern = processor.patternVersion();
            grid.refresh();
        }
        grid.setPlayhead (processor.playingStep());

        const auto played = processor.takeHits();
        for (int i = 0; i < numPads; ++i)
        {
            if ((played & (1u << i)) != 0)
                pads[(size_t) i]->hit();
            else
                pads[(size_t) i]->fade();
        }

        const float c = processor.state.getRawParameterValue ("character")->load();
        characterLabel.setText (c < 0.34f ? "Matched" : c < 0.67f ? "Mixed" : "Wild", juce::dontSendNotification);

        const auto s = processor.status();
        status.setText (juce::String (s.playing ? "Playing" : "Stopped") + " · " + juce::String (s.bpm, 1) + " BPM · beat "
                            + juce::String (s.ppq, 2) + " · notes in " + juce::String (s.notesIn) + ", out " + juce::String (s.notesOut),
                        juce::dontSendNotification);
        const auto lib = processor.library->status();
        libraryStatus.setText (lib.scanning && lib.samples == 0 ? juce::String ("Library: scanning...")
                                   : "Library: " + juce::String ((int) lib.drums) + " drums in " + juce::String ((int) lib.samples) + " samples, "
                                         + juce::String ((int) lib.roots) + " places" + (lib.scanning ? juce::String (" (scanning again)") : juce::String()),
                               juce::dontSendNotification);
        libraryStatus.setTooltip (lib.protectedFiles > 0 ? juce::String ((int) lib.protectedFiles) + " samples in your Ableton Packs are protected: only Live can play them, "
                                                                "so DrumChimp leaves them out. It uses the Core Library, your User Library and your own folders"
                                                         : juce::String());
        randomise.setEnabled (processor.library->index() != nullptr);
    }

    void DrumChimpEditor::paint (juce::Graphics& g)
    {
        g.fillAll (background);
        g.setColour (text);
        g.setFont (font (18.0f, true));
        g.drawText ("DrumChimp", 16, 12, 200, 28, juce::Justification::centredLeft);

        g.setColour (panel);
        g.fillRoundedRectangle (browserArea.toFloat(), 6.0f);
        g.fillRoundedRectangle (padPanelArea.toFloat(), 6.0f);
        g.fillRoundedRectangle (patternArea.toFloat(), 6.0f);
        g.fillRoundedRectangle (grid.getBounds().toFloat().expanded (10.0f, 8.0f), 6.0f);
    }

    void DrumChimpEditor::resized()
    {
        auto r = getLocalBounds().reduced (16);
        auto top = r.removeFromTop (28);
        dragHandle.setBounds (top.removeFromRight (120));
        top.removeFromRight (10);
        noteMap.setBounds (top.removeFromRight (110));
        top.removeFromRight (8);
        source.setBounds (top.removeFromRight (120));
        r.removeFromTop (12);

        auto kitBar = r.removeFromTop (28);
        randomise.setBounds (kitBar.removeFromLeft (130));
        kitBar.removeFromLeft (8);
        undo.setBounds (kitBar.removeFromLeft (60));
        kitBar.removeFromLeft (4);
        redo.setBounds (kitBar.removeFromLeft (60));
        kitBar.removeFromLeft (16);
        characterLabel.setBounds (kitBar.removeFromLeft (64));
        character.setBounds (kitBar.removeFromLeft (150));
        kitBar.removeFromLeft (12);
        kits.setBounds (kitBar.removeFromRight (80));
        familyLabel.setBounds (kitBar);
        r.removeFromTop (12);

        auto statusRow = r.removeFromBottom (20);
        libraryStatus.setBounds (statusRow.removeFromRight (380));
        status.setBounds (statusRow);
        r.removeFromBottom (14);
        grid.setBounds (r.removeFromBottom (168).reduced (10, 8));
        r.removeFromBottom (12);

        // Left: the pads, and the pattern's settings under them.
        auto left = r.removeFromLeft (440);
        patternArea = left.removeFromBottom (150);
        left.removeFromBottom (10);
        const int w = left.getWidth() / 4, h = left.getHeight() / 2;
        for (int i = 0; i < numPads; ++i)
            pads[(size_t) i]->setBounds (left.getX() + (i % 4) * w, left.getY() + (i / 4) * h, w, h);

        auto pattern = patternArea.reduced (12, 10);
        auto controls = pattern.removeFromTop (26);
        style.setBounds (controls.removeFromLeft (120));
        controls.removeFromLeft (6);
        bars.setBounds (controls.removeFromLeft (80));
        controls.removeFromLeft (6);
        fill.setBounds (controls.removeFromLeft (110));
        generate.setBounds (controls.removeFromRight (90));
        pattern.removeFromTop (6);
        ghosts.setBounds (pattern.removeFromRight (100).withSizeKeepingCentre (100, 24));
        const int pw = pattern.getWidth() / (int) patternKnobs.size();
        for (auto& k : patternKnobs)
        {
            auto cell = pattern.removeFromLeft (pw);
            k.label.setBounds (cell.removeFromTop (14));
            k.slider.setBounds (cell);
        }
        r.removeFromLeft (16);

        // Right: the browser, and the selected pad under it.
        padPanelArea = r.removeFromBottom (250);
        r.removeFromBottom (10);
        browserArea = r;
        browser.setBounds (browserArea.reduced (12, 10));

        auto panelArea = padPanelArea.reduced (14, 10);
        auto titleRow = panelArea.removeFromTop (26);
        clearButton.setBounds (titleRow.removeFromRight (60));
        titleRow.removeFromRight (6);
        loadButton.setBounds (titleRow.removeFromRight (70));
        padTitle.setBounds (titleRow.removeFromLeft (60));
        padRole.setBounds (titleRow.removeFromLeft (130));
        panelArea.removeFromTop (2);
        padSample.setBounds (panelArea.removeFromTop (46));
        panelArea.removeFromTop (2);

        auto knobRow = panelArea.removeFromTop (104);
        const int kw = knobRow.getWidth() / (int) knobs.size();
        for (auto& k : knobs)
        {
            auto cell = knobRow.removeFromLeft (kw);
            k.label.setBounds (cell.removeFromTop (14));
            k.slider.setBounds (cell);
        }
        panelArea.removeFromTop (6);
        auto bottomRow = panelArea.removeFromTop (26);
        choke.setBounds (bottomRow.removeFromLeft (90));
        bottomRow.removeFromLeft (16);
        mute.setBounds (bottomRow.removeFromLeft (70));
        solo.setBounds (bottomRow.removeFromLeft (70));
    }
}
