#include "DrumChimpEditor.h"

namespace drumchimp
{
    namespace
    {
        const juce::Colour background { 0xff15171c }, panel { 0xff1e2128 }, raised { 0xff2a2e37 }, outline { 0xff383d48 };
        const juce::Colour text { 0xffe6e8ec }, dim { 0xff9aa1ad }, accent { 0xff5cc8a8 };

        constexpr std::array<const char*, numPads> padNames { "Kick", "Snare", "Clap", "Closed hat", "Open hat", "Low tom", "High tom", "Crash" };

        juce::Font font (float height, bool bold = false)
        {
            return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
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
        DrumChimpProcessor::logSpike ("drag-out started: " + file.getFullPathName());
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this,
                                                                    [safe = juce::Component::SafePointer<DragHandle> (this)]
        {
            DrumChimpProcessor::logSpike ("drag-out finished");
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
    DrumChimpEditor::DrumChimpEditor (DrumChimpProcessor& p)
        : AudioProcessorEditor (p), processor (p), dragHandle (p)
    {
        source.addItemList ({ "Generator", "MIDI In" }, 1);
        source.setTooltip ("Generator: the test pattern plays with Live's transport and goes to MIDI out. MIDI In: only incoming notes play the pads");
        sourceAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.state, "source", source);
        addAndMakeVisible (source);
        addAndMakeVisible (dragHandle);

        for (int i = 0; i < numPads; ++i)
        {
            auto& pad = pads[(size_t) i];
            pad.setButtonText (juce::String (padNames[(size_t) i]) + "\n" + juce::MidiMessage::getMidiNoteName (gmNotes[(size_t) i], true, true, 3));
            pad.setColour (juce::TextButton::buttonColourId, raised);
            pad.setColour (juce::TextButton::textColourOffId, text);
            pad.onClick = [this, i] { processor.audition (i); };
            addAndMakeVisible (pad);
        }

        status.setFont (font (12.5f));
        status.setColour (juce::Label::textColourId, dim);
        status.setJustificationType (juce::Justification::topLeft);
        addAndMakeVisible (status);

        log.setMultiLine (true);
        log.setReadOnly (true);
        log.setFont (font (12.0f));
        log.setColour (juce::TextEditor::backgroundColourId, panel);
        log.setColour (juce::TextEditor::textColourId, text);
        log.setColour (juce::TextEditor::outlineColourId, outline);
        log.setText ("Drop a sample from Live's browser or Finder onto a pad: what arrives shows here and in ~/Library/Logs/DrumChimp/spike.log.\n");
        addAndMakeVisible (log);

        setSize (720, 440);
        startTimerHz (10);
        timerCallback();
    }

    DrumChimpEditor::~DrumChimpEditor()
    {
        stopTimer();
    }

    void DrumChimpEditor::paint (juce::Graphics& g)
    {
        g.fillAll (background);
        g.setColour (text);
        g.setFont (font (18.0f, true));
        g.drawText ("DrumChimp", 16, 12, 200, 28, juce::Justification::centredLeft);
        g.setColour (dim);
        g.setFont (font (12.5f));
        g.drawText ("Phase 0 spikes", 130, 12, 200, 28, juce::Justification::centredLeft);
    }

    void DrumChimpEditor::resized()
    {
        auto r = getLocalBounds().reduced (16);
        auto top = r.removeFromTop (28);
        top.removeFromLeft (250);
        dragHandle.setBounds (top.removeFromRight (150));
        top.removeFromRight (10);
        source.setBounds (top.removeFromRight (140));
        r.removeFromTop (14);

        auto padArea = r.removeFromLeft (400).removeFromTop (180);
        const int w = padArea.getWidth() / 4, h = padArea.getHeight() / 2;
        for (int i = 0; i < numPads; ++i)
            pads[(size_t) i].setBounds (juce::Rectangle<int> (padArea.getX() + (i % 4) * w, padArea.getY() + (i / 4) * h, w, h).reduced (4));
        r.removeFromLeft (16);
        status.setBounds (r.removeFromTop (180));
        auto bottom = getLocalBounds().reduced (16);
        log.setBounds (bottom.removeFromBottom (bottom.getHeight() - 236));
    }

    void DrumChimpEditor::timerCallback()
    {
        const auto s = processor.status();
        const auto mode = (DrumChimpProcessor::Source) juce::roundToInt (processor.state.getRawParameterValue ("source")->load());
        status.setText ("Host: " + processor.hostDescription
                        + "\nTransport: " + juce::String (s.playing ? "playing" : "stopped")
                        + "  " + juce::String (s.bpm, 1) + " BPM  beat " + juce::String (s.ppq, 2)
                        + "\nSource: " + juce::String (mode == DrumChimpProcessor::Source::generator ? "Generator" : "MIDI In")
                        + "\nNotes in: " + juce::String (s.notesIn)
                        + "\nNotes out: " + juce::String (s.notesOut)
                        + "\nTransport jumps: " + juce::String (s.jumps)
                        + "\nInstance ID: " + juce::String (juce::roundToInt (processor.state.getRawParameterValue ("instance")->load())),
                        juce::dontSendNotification);
    }

    int DrumChimpEditor::padAt (int x, int y) const
    {
        for (int i = 0; i < numPads; ++i)
            if (pads[(size_t) i].getBounds().contains (x, y))
                return i;
        return -1;
    }

    void DrumChimpEditor::report (const juce::String& line)
    {
        DrumChimpProcessor::logSpike (line);
        log.moveCaretToEnd();
        log.insertTextAtCaret (line + "\n");
    }

    void DrumChimpEditor::filesDropped (const juce::StringArray& files, int x, int y)
    {
        const int pad = padAt (x, y);
        const juce::String where = pad >= 0 ? "pad " + juce::String (pad + 1) : "outside the pads";
        for (const auto& f : files)
        {
            const juce::File file (f);
            report ("file drop on " + where + ": " + f + (file.existsAsFile() ? " (" + juce::String (file.getSize()) + " bytes)" : " (not a file)"));
        }
        if (pad >= 0 && ! files.isEmpty())
            pads[(size_t) pad].setButtonText (juce::String (padNames[(size_t) pad]) + "\n" + juce::File (files[0]).getFileNameWithoutExtension());
    }

    void DrumChimpEditor::textDropped (const juce::String& dropped, int x, int y)
    {
        const int pad = padAt (x, y);
        report ("text drop on " + (pad >= 0 ? "pad " + juce::String (pad + 1) : juce::String ("outside the pads")) + ": " + dropped.substring (0, 400));
    }
}
