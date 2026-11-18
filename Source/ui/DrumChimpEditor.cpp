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
            if (onError)
                onError (error);
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
    IconButton::IconButton (Icon i, const juce::String& name) : juce::Button (name), icon (i)
    {
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
        const int beat = std::max (1, pattern.stepsPerBeat);

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

    bool StepGrid::cellAt (juce::Point<int> p, int& pad, int& step) const
    {
        const auto cells = cellArea();
        if (! cells.contains (p) || pattern.steps() <= 0)
            return false;
        step = juce::jlimit (0, pattern.steps() - 1, (int) ((float) (p.x - cells.getX()) * (float) pattern.steps() / (float) cells.getWidth()));
        pad = juce::jlimit (0, numPads - 1, (p.y - cells.getY()) / (cells.getHeight() / numPads));
        return true;
    }

    // An empty step: a click adds a hit. A hit: drag up or down for its velocity, or click to remove it.
    void StepGrid::mouseDown (const juce::MouseEvent& e)
    {
        pressedPad = pressedStep = -1;
        dragged = false;
        int pad, step;
        if (! cellAt (e.getPosition(), pad, step))
            return;
        const int velocity = pattern.lanes[(size_t) pad][(size_t) step].velocity;
        if (velocity == 0)
        {
            processor.toggleStep (pad, step);
            refresh();
            return;
        }
        pressedPad = pad;
        pressedStep = step;
        pressedVelocity = velocity;
    }

    void StepGrid::mouseDrag (const juce::MouseEvent& e)
    {
        if (pressedPad < 0 || (! dragged && e.getDistanceFromDragStart() < 3))
            return;
        dragged = true;
        processor.setStepVelocity (pressedPad, pressedStep, pressedVelocity - e.getDistanceFromDragStartY());
        refresh();
    }

    void StepGrid::mouseUp (const juce::MouseEvent&)
    {
        if (pressedPad >= 0 && ! dragged)
        {
            processor.toggleStep (pressedPad, pressedStep);
            refresh();
        }
        pressedPad = pressedStep = -1;
    }

    //==========================================================================
    SettingsPanel::SettingsPanel (DrumChimpProcessor& p) : processor (p)
    {
        list.setRowHeight (40);
        list.setColour (juce::ListBox::backgroundColourId, background);
        addAndMakeVisible (list);
        add.onClick = [this]
        {
            chooser = std::make_unique<juce::FileChooser> ("Add a folder of samples", juce::File::getSpecialLocation (juce::File::userMusicDirectory));
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                                  [this] (const juce::FileChooser& fc)
            {
                if (const auto folder = fc.getResult(); folder.isDirectory())
                    processor.library->addFolder (folder);
                refresh();
            });
        };
        remove.onClick = [this]
        {
            if (const int row = list.getSelectedRow(); row >= 0 && row < (int) places.size() && places[(size_t) row].removable)
                processor.library->removeFolder (places[(size_t) row].path);
            refresh();
        };
        rescan.onClick = [this] { processor.library->rescan(); refresh(); };
        close.onClick = [this] { if (onClose) onClose(); };
        for (auto* b : { &add, &remove, &rescan, &close })
            addAndMakeVisible (*b);
        note.setFont (font (12.0f));
        note.setColour (juce::Label::textColourId, dim);
        note.setJustificationType (juce::Justification::topLeft);
        addAndMakeVisible (note);
        refresh();
    }

    void SettingsPanel::refresh()
    {
        places.clear();
        const auto index = processor.library->index();
        const auto extras = processor.library->extraFolders();
        if (index != nullptr)
            for (const auto& r : index->scannedRoots())
            {
                Place place { kindName (r.kind), r.name, r.path, extras.contains (juce::String (r.path)), 0, 0 };
                const auto prefix = r.path + "/";
                for (const auto& s : index->all())
                    if (s.path.compare (0, prefix.size(), prefix) == 0)
                    {
                        ++place.samples;
                        place.drums += s.classification.role != Role::unsorted ? 1 : 0;
                    }
                places.push_back (place);
            }
        for (const auto& e : extras)              // added, not scanned yet
            if (std::none_of (places.begin(), places.end(), [&] (const Place& p) { return p.path == e; }))
                places.push_back ({ "Folder", juce::File (e).getFileName(), e, true, 0, 0 });
        list.updateContent();
        list.repaint();
        selectedRowsChanged (list.getSelectedRow());

        const auto status = processor.library->status();
        juce::String text = status.scanning ? "Scanning..." : "Scanned in " + juce::String (juce::roundToInt (status.milliseconds)) + " ms.";
        text << " Places come from Live's own library settings (the Core Library, your User Library, Places) and folders added here.";
        text << " " << (int) status.racks << " of Ableton's Drum Rack kits can be played.";
        if (status.protectedFiles > 0)
            text << "\n" << (int) status.protectedFiles << " samples in your installed Ableton Packs are protected: only Live can play them, "
                 << "so DrumChimp leaves them out.";
        note.setText (text, juce::dontSendNotification);
    }

    void SettingsPanel::selectedRowsChanged (int row)
    {
        remove.setEnabled (row >= 0 && row < (int) places.size() && places[(size_t) row].removable);
    }

    void SettingsPanel::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
    {
        if (row < 0 || row >= (int) places.size())
            return;
        const auto& p = places[(size_t) row];
        if (selected)
        {
            g.setColour (raised);
            g.fillRect (0, 0, width, height);
        }
        auto r = juce::Rectangle<int> (0, 0, width, height).reduced (10, 4);
        auto top = r.removeFromTop (r.getHeight() / 2);
        g.setFont (font (11.5f));
        g.setColour (faint);
        const bool packOnlyLivePlays = p.kind == kindName (LibraryRoot::Kind::pack) && p.samples == 0;   // its samples are protected
        g.drawText (packOnlyLivePlays ? juce::String ("protected: only Live plays these")
                                      : juce::String (p.samples) + " samples, " + juce::String (p.drums) + " drums",
                    top.removeFromRight (240), juce::Justification::centredRight);
        g.setFont (font (13.0f, true));
        g.setColour (text);
        g.drawText (p.name + "  ", top, juce::Justification::centredLeft);
        g.setFont (font (11.5f));
        g.setColour (dim);
        g.drawText (p.kind + " · " + p.path, r, juce::Justification::centredLeft);
    }

    void SettingsPanel::paint (juce::Graphics& g)
    {
        g.fillAll (panel);
        g.setColour (outline);
        g.drawRect (getLocalBounds(), 1);
        g.setColour (text);
        g.setFont (font (16.0f, true));
        g.drawText ("Library places", 20, 14, 300, 24, juce::Justification::centredLeft);
    }

    void SettingsPanel::resized()
    {
        auto r = getLocalBounds().reduced (20).withTrimmedTop (34);
        auto buttons = r.removeFromBottom (28);
        close.setBounds (buttons.removeFromRight (90));
        buttons.removeFromRight (8);
        rescan.setBounds (buttons.removeFromRight (90));
        add.setBounds (buttons.removeFromLeft (120));
        buttons.removeFromLeft (8);
        remove.setBounds (buttons.removeFromLeft (130));
        r.removeFromBottom (10);
        note.setBounds (r.removeFromBottom (52));
        r.removeFromBottom (8);
        list.setBounds (r);
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
    // The Info View (as NoteCap's): one entry per control, display and panel. Keep each to a line
    // or two: what it does, and what its settings mean. The per-pad parameters (p1_level …
    // p12_solo) share "pad.level" … "pad.solo".
    namespace
    {
        struct Info { const char* key; const char* title; const char* text; };

        const Info infoTable[] = {
            { "settings", "Settings", "The places DrumChimp looks for samples: the Core Library, your User Library, Live's Places, and folders you add." },
            { "settings.panel", "Library places",
              "Where DrumChimp finds samples, with how many. Add folder adds your own; Remove folder removes one you added; Rescan looks again." },
            { "source", "Source",
              "Generator: the pattern plays with Live's transport and goes to MIDI out. MIDI In: only incoming notes (clips, a keyboard) play the pads." },
            { "notemap", "Note map",
              "Which MIDI notes play the pads. GM: the General MIDI drum notes, as a Drum Rack has them. Chromatic: one pad per note from C1." },
            { "drag", "Drag MIDI",
              "Drag onto a clip slot or the Arrangement: the pattern as a MIDI clip, Bars long. On this track, set Source to MIDI In so it doesn't play twice." },

            { "randomise", "Randomise kit", "A new sample of its role for every unlocked pad, from your library or from Ableton's Drum Rack kits." },
            { "undo", "Undo", "Back to the kit before the last change: Randomise, a dice, a drop, Load, Clear or a loaded kit. The last 20 kits." },
            { "redo", "Redo", "Forward again to the kit you undid." },
            { "kitsource", "Randomise from",
              "From library: a kit built from your samples, matched by family. From Ableton kits: one of Ableton's Drum Rack kits, mapped onto the pads." },
            { "character", "Character",
              "How alike Randomise's samples are. Matched: one family (808, 909, Acoustified…). Wild: any sample of each role. From library only." },
            { "kit.family", "Kit", "The family, or the Ableton kit, that Randomise or the Kits menu last chose." },
            { "kits", "Kits", "Save this kit (samples, roles and pad settings), load a saved one, or load one of Ableton's kits by name." },

            { "pad", "Pad", "Click to select it and hear it. Drop a sample on it, from Live's browser, Finder or the browser here, to load it." },
            { "pad.lock", "Lock", "Randomise leaves a locked pad as it is." },
            { "pad.dice", "Dice", "A new sample for this pad alone, of its role." },

            { "browser", "Library browser", "Your library's samples, sorted by role. Pick what it lists, search it, and hear or load a sample." },
            { "browser.role", "Show", "Which samples the browser lists: the selected pad's role, one role, or everything." },
            { "browser.search", "Search", "Words to find in a sample's name, folder or pack." },
            { "browser.list", "Samples", "Click a sample to hear it. Double-click it to load it on the selected pad, or drag it onto any pad." },

            { "group.pad", "Pad settings", "The selected pad: its role, its sample, and how it sounds." },
            { "pad.role", "Role",
              "What this pad is for: what Randomise, the dice and the browser give it, and which part of the pattern it plays." },
            { "pad.sample", "Sample", "Where this pad's sample is and how the library sorted it, or why it can't be found or played." },
            { "pad.load", "Load...", "Choose a sample file for this pad." },
            { "pad.clear", "Clear", "Empty this pad." },
            { "pad.level", "Level", "The pad's volume, from -inf to +6 dB." },
            { "pad.pan", "Pan", "Where the pad sits between left and right." },
            { "pad.tune", "Tune", "Pitch, up or down 24 semitones. Higher also plays shorter, lower longer." },
            { "pad.decay", "Decay", "How fast the sound fades: down 60 dB at this time. Full: the whole sample plays." },
            { "pad.vel", "Velocity", "How much a hit's velocity changes its level. 0%: every hit at full level." },
            { "pad.choke", "Choke", "Pads in the same group cut each other off. The hats share A: a closed hat stops an open one." },
            { "pad.mute", "Mute", "Silences this pad." },
            { "pad.solo", "Solo", "Only soloed pads play." },

            { "group.pattern", "Pattern", "The settings the pattern is made from. Changing one remakes the pattern; Generate makes a new one." },
            { "style", "Style", "The groove the pattern is made from: 24 styles, in 4/4, 3/4 and 6/8. A new style brings its own swing." },
            { "bars", "Bars", "The pattern's length: 1, 2, 4 or 8 bars. It loops from the song's bar 1." },
            { "fill", "Fill", "A fill (toms or snare) in the last beat or two of every 2, 4 or 8 bars, then a crash. Or no fills." },
            { "ghosts", "Ghost notes", "On: quiet in-between notes, mostly on the snare. Off: none." },
            { "generate", "Generate", "A new pattern in this style. Changing a setting remakes this one instead." },
            { "density", "Density", "How many of the style's hits play. More only ever adds hits." },
            { "complexity", "Complexity",
              "How far the pattern strays from the style's groove: variations, ghost notes, rolls. At 0 (Density 50% or more) it plays the groove as written." },
            { "swing", "Swing", "Delays every second sixteenth. 50%: straight. 66%: a triplet feel. 75%: the most." },
            { "humanise", "Humanise", "Small random changes to timing (up to 12 ms) and velocity (up to 14)." },

            { "grid", "Step grid", "The pattern: a lane per pad, brighter for a harder hit, dots for a roll, and the playhead." },
            { "grid.step", "Step", "Click an empty step to add a hit, or a hit to remove it. Drag a hit up or down for its velocity." },
            { "grid.lock", "Lane lock", "A locked lane keeps its hits when you Generate or change a setting." },

            { "display.status", "Status",
              "Transport, tempo, notes in and out. Or a warning: a clip doubling the generator, a missing sample, or a time signature that differs from the style's." },
            { "display.library", "Library", "How many drums DrumChimp found in your Ableton library, and in how many places. Settings lists them." },

            { "instance", "Instance ID", "Tells this DrumChimp apart from others for scripts in Live. Not a sound setting, and not shown." },
        };

        const Info* findInfo (const juce::String& key)
        {
            const auto base = key.upToFirstOccurrenceOf (":", false, false);   // "pad:3" → "pad"
            for (const auto& i : infoTable)
                if (base == i.key)
                    return &i;
            return nullptr;
        }

        const char* defaultInfoText = "Point at a control or display to see what it does.";

        // The table is UTF-8 (…, ·): a juce::String made from a plain char* would read it as Latin-1.
        juce::String utf8 (const char* s) { return juce::String::fromUTF8 (s); }
    }

    void InfoView::show (const juce::String& newTitle, const juce::String& newText)
    {
        if (newTitle == title && newText == text)
            return;
        title = newTitle;
        text = newText;
        repaint();
    }

    void InfoView::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (panel);
        g.fillRoundedRectangle (r, 6.0f);
        r = r.reduced (14.0f, 8.0f);
        if (title.isNotEmpty())
        {
            g.setColour (drumchimp::text);                  // the colour (this->text is what's shown)
            g.setFont (font (13.0f, true));
            g.drawText (title, r.removeFromTop (18.0f), juce::Justification::centredLeft);
        }
        g.setColour (title.isEmpty() ? dim : drumchimp::text.withAlpha (0.85f));
        g.setFont (font (12.5f));
        g.drawFittedText (this->text, r.toNearestInt(), juce::Justification::topLeft, 2, 1.0f);
    }

    juce::String DrumChimpEditor::infoKeyForParameter (const juce::String& id)
    {
        // p1_level … p12_solo → pad.level … pad.solo
        if (id.startsWithChar ('p') && id.containsChar ('_') && id.substring (1, id.indexOfChar ('_')).containsOnly ("0123456789"))
            return "pad." + id.fromFirstOccurrenceOf ("_", false, false);
        return id;
    }

    juce::String DrumChimpEditor::infoTitle (const juce::String& key)
    {
        const auto* i = findInfo (key);
        return i != nullptr ? utf8 (i->title) : juce::String();
    }

    juce::String DrumChimpEditor::infoText (const juce::String& key)
    {
        const auto* i = findInfo (key);
        return i != nullptr ? utf8 (i->text) : juce::String();
    }

    juce::String DrumChimpEditor::infoKeyAt (juce::Point<int> p) const
    {
        // A velocity drag keeps showing the hit being dragged.
        int pad, step;
        if (grid.draggingCell (pad, step))
            return "grid.step:" + juce::String (pad) + ":" + juce::String (step);
        for (const auto& r : infoRegions)
            if (r.area.contains (p))
            {
                if (r.key == "grid" && grid.cellAt (p - grid.getPosition(), pad, step))
                    return "grid.step:" + juce::String (pad) + ":" + juce::String (step);
                return r.key;
            }
        return {};
    }

    // What a key shows: its entry, and for a pad, a step or the library, what's there now.
    void DrumChimpEditor::showInfoFor (const juce::String& key)
    {
        hoveredKey = key;
        const auto* i = findInfo (key);
        if (i == nullptr)
        {
            infoView.show ({}, defaultInfoText);
            return;
        }
        juce::String title = utf8 (i->title), body = utf8 (i->text);
        const auto dot = utf8 (" \xc2\xb7 ");
        const auto parts = juce::StringArray::fromTokens (key, ":", "");
        if (parts[0] == "pad" && parts.size() == 2)
        {
            const int pad = parts[1].getIntValue();
            const auto s = processor.padSource (pad);
            title = "Pad " + juce::String (pad + 1) + dot + padRoleName (processor.padRole (pad)) + dot
                    + juce::MidiMessage::getMidiNoteName (noteForPad (seenMap, pad), true, true, 3);
            body = (s.message.isNotEmpty() ? s.message : s.path.isNotEmpty() ? juce::File (s.path).getFileName() : juce::String ("Empty"))
                   + ". " + body;
        }
        else if (parts[0] == "grid.step" && parts.size() == 3)
        {
            const int pad = parts[1].getIntValue(), step = parts[2].getIntValue();
            const auto& pattern = grid.shownPattern();
            const int beat = std::max (1, pattern.stepsPerBeat);
            title = juce::String (padRoleName (processor.padRole (pad))) + dot + "bar " + juce::String (step / pattern.stepsPerBar + 1)
                    + ", beat " + juce::String ((step % pattern.stepsPerBar) / beat + 1);
            if (pad >= 0 && pad < numPads && step >= 0 && step < pattern.steps())
            {
                const auto& c = pattern.lanes[(size_t) pad][(size_t) step];
                title << dot << (c.velocity > 0 ? "velocity " + juce::String ((int) c.velocity) + (c.repeats > 1 ? ", a roll of " + juce::String ((int) c.repeats) : juce::String())
                                                : juce::String ("no hit"));
            }
        }
        else if (key == "display.library")
            if (const auto lib = processor.library->status(); lib.protectedFiles > 0)
                body << " " << (int) lib.protectedFiles << " samples in Ableton Packs are left out: they're protected, and only Live can play them.";
        infoView.show (title, body);
    }

    void DrumChimpEditor::updateInfo (const juce::MouseEvent& e)
    {
        const auto key = infoKeyAt (e.getEventRelativeTo (&content).getPosition());
        if (key != hoveredKey || key.startsWith ("grid.step"))   // a step's velocity can change under the mouse
            showInfoFor (key);
    }

    void DrumChimpEditor::mouseEnter (const juce::MouseEvent& e) { updateInfo (e); }
    void DrumChimpEditor::mouseMove (const juce::MouseEvent& e)  { updateInfo (e); }
    void DrumChimpEditor::mouseDrag (const juce::MouseEvent& e)  { updateInfo (e); }

    void DrumChimpEditor::mouseExit (const juce::MouseEvent& e)
    {
        // Moving between child components also sends exits; clear only when leaving the editor.
        if (! getLocalBounds().contains (e.getEventRelativeTo (this).getPosition()))
            showInfoFor ({});
    }

    void DrumChimpEditor::addInfoRegion (const juce::Component& c, const juce::String& key)
    {
        infoRegions.push_back ({ content.getLocalArea (&c, c.getLocalBounds()), key });
    }

    //==========================================================================
    DrumChimpEditor::DrumChimpEditor (DrumChimpProcessor& p)
        : AudioProcessorEditor (p), processor (p), dragHandle (p), browser (p, [this] { return selected; }), grid (p), settings (p)
    {
        addAndMakeVisible (content);
        content.onPaint = [this] (juce::Graphics& g) { paintContent (g); };
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
        sourceAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.state, "source", source);
        noteMap.addItemList ({ "GM", "Chromatic" }, 1);
        noteMapAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.state, "notemap", noteMap);
        content.addAndMakeVisible (source);
        content.addAndMakeVisible (noteMap);
        content.addAndMakeVisible (dragHandle);

        randomise.setColour (juce::TextButton::buttonColourId, accent.withAlpha (0.25f));
        randomise.onClick = [this] { processor.randomiseKit(); };
        undo.onClick = [this] { processor.undoKit(); };
        redo.onClick = [this] { processor.redoKit(); };
        kitSource.addItem ("From library", 1);
        kitSource.addItem ("From Ableton kits", 2);
        kitSource.setSelectedId (p.kitsFromRacks() ? 2 : 1, juce::dontSendNotification);
        kitSource.onChange = [this] { processor.setKitsFromRacks (kitSource.getSelectedId() == 2); };
        content.addAndMakeVisible (kitSource);
        kits.onClick = [this] { showKitsMenu(); };
        for (auto* b : { &randomise, &undo, &redo, &kits })
            content.addAndMakeVisible (*b);

        characterLabel.setText ("Matched", juce::dontSendNotification);
        characterLabel.setFont (font (12.0f));
        characterLabel.setColour (juce::Label::textColourId, dim);
        characterLabel.setJustificationType (juce::Justification::centredRight);
        characterAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.state, "character", character);
        familyLabel.setFont (font (12.0f));
        familyLabel.setColour (juce::Label::textColourId, dim);
        content.addAndMakeVisible (characterLabel);
        content.addAndMakeVisible (character);
        content.addAndMakeVisible (familyLabel);

        for (int i = 0; i < numPads; ++i)
        {
            pads.push_back (std::make_unique<PadButton> (p, i, [this] (int pad) { select (pad); }));
            content.addAndMakeVisible (*pads.back());
        }
        content.addAndMakeVisible (browser);

        padTitle.setFont (font (15.0f, true));
        padTitle.setColour (juce::Label::textColourId, text);
        padSample.setFont (font (12.0f));
        padSample.setColour (juce::Label::textColourId, dim);
        padSample.setJustificationType (juce::Justification::topLeft);
        content.addAndMakeVisible (padTitle);
        content.addAndMakeVisible (padSample);

        for (int r = 0; r < numPadRoles; ++r)
            padRole.addItem (padRoleNames[(size_t) r], r + 1);
        padRole.onChange = [this]
        {
            processor.setPadRole (selected, (PadRole) (padRole.getSelectedId() - 1));
            browser.refresh();
        };
        content.addAndMakeVisible (padRole);

        loadButton.onClick = [this] { chooseSample(); };
        clearButton.onClick = [this] { processor.clearPad (selected); };
        content.addAndMakeVisible (loadButton);
        content.addAndMakeVisible (clearButton);

        const std::array<const char*, 5> names { "Level", "Pan", "Tune", "Decay", "Velocity" };
        for (size_t i = 0; i < knobs.size(); ++i)
            setUpKnob (knobs[i], names[i]);

        // The pattern.
        const auto& styleList = DrumChimpProcessor::styles();
        for (size_t i = 0; i < styleList.size(); ++i)
            style.addItem (styleList[i].name, (int) i + 1);
        styleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.state, "style", style);
        bars.addItemList ({ "1 bar", "2 bars", "4 bars", "8 bars" }, 1);
        barsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.state, "bars", bars);
        fill.addItemList ({ "No fills", "Fill every 2", "Fill every 4", "Fill every 8" }, 1);
        fillAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.state, "fill", fill);
        ghostsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.state, "ghosts", ghosts);
        generate.setColour (juce::TextButton::buttonColourId, accent.withAlpha (0.25f));
        generate.onClick = [this] { processor.newPattern(); };
        for (auto* c : std::initializer_list<juce::Component*> { &style, &bars, &fill, &ghosts, &generate })
            content.addAndMakeVisible (*c);
        const std::array<const char*, 4> patternNames { "Density", "Complexity", "Swing", "Humanise" };
        for (size_t i = 0; i < patternKnobs.size(); ++i)
        {
            setUpKnob (patternKnobs[i], patternNames[i]);
            patternKnobAttachments[i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.state, patternKnobs[i].id, patternKnobs[i].slider);
        }
        content.addAndMakeVisible (grid);
        choke.addItemList (params::chokeGroups, 1);
        content.addAndMakeVisible (choke);
        content.addAndMakeVisible (mute);
        content.addAndMakeVisible (solo);

        status.setFont (font (12.0f));
        status.setColour (juce::Label::textColourId, faint);
        content.addAndMakeVisible (status);
        libraryStatus.setFont (font (12.0f));
        libraryStatus.setColour (juce::Label::textColourId, faint);
        libraryStatus.setJustificationType (juce::Justification::centredRight);
        libraryStatus.setComponentID ("libraryStatus");      // the snapshot test masks it: it's this Mac's
        content.addAndMakeVisible (libraryStatus);

        settingsButton.setComponentID ("settingsButton");
        settingsButton.onClick = [this]
        {
            settings.refresh();
            settings.setVisible (! settings.isVisible());
            settings.toFront (false);
            layout();                              // its Info View area comes and goes with it
        };
        content.addAndMakeVisible (settingsButton);
        settings.onClose = [this] { settings.setVisible (false); layout(); };
        content.addChildComponent (settings);
        content.addAndMakeVisible (infoView);
        infoView.show ({}, defaultInfoText);
        dragHandle.onError = [this] (const juce::String& error) { infoView.show ("Drag MIDI", error); };

        const float scale = processor.uiScale;   // read first: the limits below resize (and resized() saves the size)
        setResizable (true, true);
        setResizeLimits (baseWidth * 7 / 10, baseHeight * 7 / 10, baseWidth * 3 / 2, baseHeight * 3 / 2);
        getConstrainer()->setFixedAspectRatio ((double) baseWidth / baseHeight);
        setSize (juce::roundToInt (baseWidth * scale), juce::roundToInt (baseHeight * scale));
        addMouseListener (this, true);   // hover over any child updates the Info View
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
        content.addAndMakeVisible (k.slider);
        content.addAndMakeVisible (k.label);
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
        const auto family = processor.lastFamily(), rack = processor.lastRack();
        familyLabel.setText (rack.isNotEmpty() ? "Kit: " + rack : family.isEmpty() ? juce::String() : "Family: " + family, juce::dontSendNotification);
        kitSource.setSelectedId (processor.kitsFromRacks() ? 2 : 1, juce::dontSendNotification);
        character.setEnabled (! processor.kitsFromRacks());
        characterLabel.setEnabled (! processor.kitsFromRacks());
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

        // Ableton's Drum Rack kits, by folder.
        juce::PopupMenu ableton;
        if (const auto index = processor.library->index(); index != nullptr && ! index->racks().empty())
        {
            const auto& racks = index->racks();
            for (size_t i = 0; i < racks.size();)
            {
                juce::PopupMenu group;
                const auto name = racks[i].group;
                for (; i < racks.size() && racks[i].group == name; ++i)
                    group.addItem (juce::String (racks[i].name), [this, which = (int) i] { processor.loadRack (which); browser.refresh(); });
                ableton.addSubMenu (juce::String (name), group);
            }
        }
        menu.addSubMenu ("Ableton kits", ableton, ableton.containsAnyActiveItems());
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
            if (settings.isVisible())
                settings.refresh();
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
        const auto now = juce::Time::getMillisecondCounter();
        if (processor.takeDoubled())
            doubledUntil = now + 4000;
        if (now < doubledUntil)
        {
            status.setText ("Notes are coming in while the generator plays: set Source to MIDI In to hear only the clip", juce::dontSendNotification);
            status.setColour (juce::Label::textColourId, warning);
        }
        else if (const auto problem = [this]
                 {
                     juce::StringArray pads;
                     for (int pad = 0; pad < numPads; ++pad)
                         if (const auto st = processor.padSource (pad).status; st == Status::missing || st == Status::failed)
                             pads.add (juce::String (pad + 1));
                     return pads;
                 }(); ! problem.isEmpty())
        {
            status.setText ((problem.size() == 1 ? "Pad " + problem[0] + "'s sample" : "Pads " + problem.joinIntoString (", ") + ": samples")
                                + " can't be found or played. Select a pad to see why; drop a new sample on it, or Load...",
                            juce::dontSendNotification);
            status.setColour (juce::Label::textColourId, warning);
        }
        else if (const auto host = processor.hostTimeSignature(), mine = processor.patternTimeSignature();
                 host != 0 && host != mine)
        {
            const auto meter = [] (int m) { return juce::String (m / 100) + "/" + juce::String (m % 100); };
            status.setText ("Live's set is in " + meter (host) + ", and " + juce::String (processor.currentStyle().name) + " is in " + meter (mine)
                                + ": set Live's time signature to " + meter (mine) + " so the bars line up",
                            juce::dontSendNotification);
            status.setColour (juce::Label::textColourId, warning);
        }
        else
        {
            status.setText (juce::String (s.playing ? "Playing" : "Stopped") + " · " + juce::String (s.bpm, 1) + " BPM · beat "
                                + juce::String (s.ppq, 2) + " · notes in " + juce::String (s.notesIn) + ", out " + juce::String (s.notesOut),
                            juce::dontSendNotification);
            status.setColour (juce::Label::textColourId, faint);
        }
        const auto lib = processor.library->status();
        libraryStatus.setText (lib.scanning && lib.samples == 0 ? juce::String ("Library: scanning...")
                                   : "Library: " + juce::String ((int) lib.drums) + " drums in " + juce::String ((int) lib.samples) + " samples, "
                                         + juce::String ((int) lib.roots) + " places" + (lib.scanning ? juce::String (" (scanning again)") : juce::String()),
                               juce::dontSendNotification);
        randomise.setEnabled (processor.library->index() != nullptr);
    }

    void DrumChimpEditor::paint (juce::Graphics& g)
    {
        g.fillAll (background);
    }

    void DrumChimpEditor::resized()
    {
        const float scale = (float) getWidth() / (float) baseWidth;
        content.setTransform (juce::AffineTransform::scale (scale));
        content.setBounds (0, 0, baseWidth, baseHeight);
        processor.uiScale = scale;
        layout();
    }

    void DrumChimpEditor::paintContent (juce::Graphics& g)
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

    void DrumChimpEditor::layout()
    {
        settings.setBounds (content.getLocalBounds().reduced (120, 90).withTrimmedBottom (64));
        auto r = content.getLocalBounds().reduced (16);
        infoView.setBounds (r.removeFromBottom (54));
        r.removeFromBottom (10);
        auto top = r.removeFromTop (28);
        dragHandle.setBounds (top.removeFromRight (120));
        top.removeFromRight (10);
        noteMap.setBounds (top.removeFromRight (110));
        top.removeFromRight (8);
        source.setBounds (top.removeFromRight (120));
        top.removeFromRight (8);
        settingsButton.setBounds (top.removeFromRight (90));
        r.removeFromTop (12);

        auto kitBar = r.removeFromTop (28);
        randomise.setBounds (kitBar.removeFromLeft (130));
        kitBar.removeFromLeft (8);
        undo.setBounds (kitBar.removeFromLeft (60));
        kitBar.removeFromLeft (4);
        redo.setBounds (kitBar.removeFromLeft (60));
        kitBar.removeFromLeft (12);
        kitSource.setBounds (kitBar.removeFromLeft (160));
        kitBar.removeFromLeft (8);
        characterLabel.setBounds (kitBar.removeFromLeft (60));
        character.setBounds (kitBar.removeFromLeft (120));
        kitBar.removeFromLeft (12);
        kits.setBounds (kitBar.removeFromRight (80));
        familyLabel.setBounds (kitBar);
        r.removeFromTop (12);

        auto statusRow = r.removeFromBottom (20);
        libraryStatus.setBounds (statusRow.removeFromRight (380));
        status.setBounds (statusRow);
        r.removeFromBottom (14);
        grid.setBounds (r.removeFromBottom (212).reduced (10, 8));   // 12 lanes of 15, under the bar numbers
        r.removeFromBottom (12);

        // Left: the pads, and the pattern's settings under them.
        auto left = r.removeFromLeft (440);
        patternArea = left.removeFromBottom (150);
        left.removeFromBottom (10);
        const int rows = (numPads + padsPerRow - 1) / padsPerRow;
        const int w = left.getWidth() / padsPerRow, h = left.getHeight() / rows;
        for (int i = 0; i < numPads; ++i)
            pads[(size_t) i]->setBounds (left.getX() + (i % padsPerRow) * w, left.getY() + (i / padsPerRow) * h, w, h);

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

        // Info View areas, most specific first: a lock before its pad, a knob before its panel.
        infoRegions.clear();
        if (settings.isVisible())
            infoRegions.push_back ({ settings.getBounds(), "settings.panel" });
        const std::pair<const juce::Component*, const char*> hoverable[] {
            { &settingsButton, "settings" }, { &source, "source" }, { &noteMap, "notemap" }, { &dragHandle, "drag" },
            { &randomise, "randomise" }, { &undo, "undo" }, { &redo, "redo" }, { &kitSource, "kitsource" }, { &characterLabel, "character" },
            { &character, "character" }, { &familyLabel, "kit.family" }, { &kits, "kits" },
            { &padRole, "pad.role" }, { &padTitle, "pad.role" }, { &loadButton, "pad.load" }, { &clearButton, "pad.clear" }, { &padSample, "pad.sample" },
            { &choke, "pad.choke" }, { &mute, "pad.mute" }, { &solo, "pad.solo" },
            { &style, "style" }, { &bars, "bars" }, { &fill, "fill" }, { &ghosts, "ghosts" }, { &generate, "generate" },
            { &status, "display.status" }, { &libraryStatus, "display.library" },
        };
        for (const auto& [c, key] : hoverable)
            addInfoRegion (*c, key);
        for (auto& k : knobs)
        {
            const auto key = juce::String ("pad.") + k.id;
            infoRegions.push_back ({ content.getLocalArea (&k.slider, k.slider.getLocalBounds()).getUnion (content.getLocalArea (&k.label, k.label.getLocalBounds())), key });
        }
        for (auto& k : patternKnobs)
            infoRegions.push_back ({ content.getLocalArea (&k.slider, k.slider.getLocalBounds()).getUnion (content.getLocalArea (&k.label, k.label.getLocalBounds())), k.id });
        for (int i = 0; i < numPads; ++i)
        {
            addInfoRegion (pads[(size_t) i]->lockButton(), "pad.lock");
            addInfoRegion (pads[(size_t) i]->diceButton(), "pad.dice");
            addInfoRegion (*pads[(size_t) i], "pad:" + juce::String (i));
            addInfoRegion (grid.laneLock (i), "grid.lock");
        }
        addInfoRegion (browser.roleBox(), "browser.role");
        addInfoRegion (browser.searchBox(), "browser.search");
        addInfoRegion (browser.listBox(), "browser.list");
        infoRegions.push_back ({ grid.getBounds(), "grid" });
        infoRegions.push_back ({ padPanelArea, "group.pad" });
        infoRegions.push_back ({ patternArea, "group.pattern" });
        infoRegions.push_back ({ browserArea, "browser" });
    }
}
