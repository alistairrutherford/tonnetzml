#include "TonnetzEditor.h"
#include "../plugin/TonnetzProcessor.h"

namespace tonnetz
{
    namespace
    {
        const juce::Colour paper (0xfff4efe4), ink (0xff1f2a30), faint (0xff6b767c), vermilion (0xffc8452c);
    }

    TonnetzEditor::TonnetzEditor (TonnetzProcessor& p)
        : AudioProcessorEditor (p), processor (p)
    {
        setOpaque (true);

        word.setFont (juce::FontOptions (20.0f));
        word.setJustification (juce::Justification::centred);
        word.setColour (juce::TextEditor::backgroundColourId, juce::Colours::white);
        word.setColour (juce::TextEditor::textColourId, ink);
        word.setColour (juce::TextEditor::outlineColourId, faint);
        word.setColour (juce::TextEditor::focusedOutlineColourId, ink);
        word.setTextToShowWhenEmpty ("(no ops: the chord as played)", faint);
        word.onReturnKey = [this] { commitWord(); };
        word.onFocusLost = [this] { commitWord(); };
        word.onEscapeKey = [this] { shownSlot = -1; showSlot(); };
        addAndMakeVisible (word);

        setResizable (true, true);
        setResizeLimits (minWidth, minHeight, 2400, 1600);
        setSize (minWidth, minHeight);
        showSlot();
        refreshStatus();
        startTimerHz (10);
    }

    TonnetzEditor::~TonnetzEditor()
    {
        stopTimer();
    }

    void TonnetzEditor::showSlot()
    {
        const int slot = processor.currentSlot();
        if (slot == shownSlot)
            return;
        shownSlot = slot;
        word.setText (juce::String::fromUTF8 (printWord (processor.getSlotWord (slot)).c_str()), false);
        message = {};
        messageIsError = false;
        repaint();
    }

    void TonnetzEditor::commitWord()
    {
        const int slot = processor.currentSlot();
        if (processor.setSlotWordText (slot, word.getText()))
        {
            const auto stored = juce::String::fromUTF8 (printWord (processor.getSlotWord (slot)).c_str());
            word.setText (stored, false);
            message = "Slot " + TonnetzProcessor::slotName (slot) + " stored";
            messageIsError = false;
            shownSlot = slot;
        }
        else
        {
            message = "Not a word. Ops: P L R N S H +5 -5 +M3 -M3 +m3 -m3 T(a,b) Flip(i,j) Pivot(i) Grow(i,j) Shrink";
            messageIsError = true;
        }
        repaint();
    }

    // "Walk · W = P L · D major 90%": the mode, W as it stands, the key (hollow ◦ while tentative).
    void TonnetzEditor::refreshStatus()
    {
        const auto s = processor.getEngineStatus();
        const int mode = juce::jlimit (0, 3, (int) std::lround (processor.getState().getRawParameterValue (params::mode)->load()));
        // Mirror's W is the slot's word, whether or not the audio thread has run yet.
        juce::String w = juce::String::fromUTF8 (printWord (mode == (int) Mode::Mirror ? processor.getSlotWord (processor.currentSlot()) : s.current).c_str());
        if (mode != (int) Mode::Mirror && s.currentOps > s.current.size)
            w << juce::String::fromUTF8 (" \u2026 (") << s.currentOps << " ops)";
        const auto g = KeyGuess::unpack (s.keyGuess);
        juce::String key = "no key";
        if (s.keyTonic >= 0)
            key = juce::String (guessKeyName (s.keyTonic, s.keyMinor)) + (s.keyMinor ? " minor" : " major");
        if (g.has() && g.tonic == s.keyTonic && g.minor == s.keyMinor)
            key << " " << g.fit << "%" << (g.state == GuessState::Confident ? "" : " ?");
        const auto dot = juce::String::fromUTF8 ("  \u00b7  ");
        const auto line = juce::String (modeNames[mode]) + dot + "W = " + (w.isEmpty() ? juce::String ("(none)") : w) + dot + key;
        if (line != engineLine)
        {
            engineLine = line;
            repaint();
        }
    }

    void TonnetzEditor::timerCallback()
    {
        refreshStatus();
        if (! word.hasKeyboardFocus (true))
            showSlot();
        else if (processor.currentSlot() != shownSlot)
        {
            shownSlot = -1;              // the slot changed while typing: show the new slot's word
            showSlot();
        }
    }

    void TonnetzEditor::resized()
    {
        const auto b = getLocalBounds();
        word.setBounds (b.getCentreX() - 180, b.getCentreY() + 10, 360, 36);
    }

    void TonnetzEditor::paint (juce::Graphics& g)
    {
        g.fillAll (paper);
        const auto b = getLocalBounds();
        g.setColour (ink);
        g.setFont (juce::FontOptions (34.0f));
        g.drawText ("TonnetzML", b.withHeight (b.getCentreY() - 70).withY (0).withTrimmedTop (b.getCentreY() - 140), juce::Justification::centredBottom);

        g.setFont (juce::FontOptions (14.0f));
        g.setColour (faint);
        g.drawText ("Every chord comes out as W(chord): Mirror, Walk (operator keys), Orbit. The lattice arrives in Phase 4.",
                    juce::Rectangle<int> (0, b.getCentreY() - 62, b.getWidth(), 20), juce::Justification::centred);
        g.setColour (ink);
        g.drawText (engineLine, juce::Rectangle<int> (0, b.getCentreY() + 100, b.getWidth(), 20),
                    juce::Justification::centred);

        g.setColour (ink);
        g.setFont (juce::FontOptions (16.0f));
        g.drawText ("Slot " + TonnetzProcessor::slotName (shownSlot < 0 ? 0 : shownSlot) + " word",
                    juce::Rectangle<int> (0, b.getCentreY() - 22, b.getWidth(), 24), juce::Justification::centred);

        g.setFont (juce::FontOptions (13.0f));
        g.setColour (messageIsError ? vermilion : faint);
        g.drawFittedText (message, juce::Rectangle<int> (20, b.getCentreY() + 56, b.getWidth() - 40, 36), juce::Justification::centredTop, 2);
    }
}
