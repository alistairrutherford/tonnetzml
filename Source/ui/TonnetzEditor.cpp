#include "TonnetzEditor.h"
#include "../plugin/TonnetzProcessor.h"

namespace tonnetz
{
    TonnetzEditor::TonnetzEditor (TonnetzProcessor& p)
        : AudioProcessorEditor (p), processor (p),
          notes (p),
          lattice (p, picture, notes),
          keyboard (p, picture, notes),
          word (p, picture),
          head (p, picture, [this] { setDark (! processor.editorView().dark); }),
          side (p, [this] { if (constructed) layoutFloating(); })
    {
        setOpaque (true);
        setWantsKeyboardFocus (true);
        addAndMakeVisible (lattice);
        addAndMakeVisible (keyboard);
        addAndMakeVisible (word);
        addAndMakeVisible (head);
        addAndMakeVisible (side);
        lattice.onFocusWanted = [this]
        {
            if (auto* pal = word.palette())
                pal->setVisible (false);
            grabKeyboardFocus();
        };

        setResizable (true, true);
        setResizeLimits (minWidth, minHeight, 2400, 1600);
        setSize (minWidth, minHeight);
        constructed = true;
        refreshNow();
        resized();
        lastTick = juce::Time::getMillisecondCounterHiRes() * 0.001;
        startTimerHz (60);
    }

    TonnetzEditor::~TonnetzEditor()
    {
        stopTimer();
        notes.releaseAll();
    }

    void TonnetzEditor::setDark (bool dark)
    {
        processor.editorView().dark = dark;
        themeChanged();
    }

    void TonnetzEditor::themeChanged()
    {
        head.refresh();
        side.refresh();
        word.refresh();
        keyboard.refresh();
        repaint();
        lattice.repaint();
    }

    void TonnetzEditor::freezeAnimation (bool frozen)
    {
        lattice.freezeAnimation (frozen);
        if (frozen)
            stopTimer();
        else
            startTimerHz (60);
    }

    juce::Array<ui::HudControl*> TonnetzEditor::controls()
    {
        juce::Array<ui::HudControl*> out;
        head.controls (out);
        side.controls (out);
        out.add (&word.slotChoices());
        return out;
    }

    ui::HudControl* TonnetzEditor::controlFor (const juce::String& id)
    {
        for (auto* c : controls())
            if (c->getComponentID() == id)
                return c;
        return nullptr;
    }

    void TonnetzEditor::refreshNow()
    {
        const auto status = processor.getEngineStatus();
        if (! haveReleases)
        {
            lastReleases = status.releases;
            haveReleases = true;
        }
        else if (status.releases != lastReleases)
        {
            // The engine let go of everything: it ignores the note-offs that would follow.
            lastReleases = status.releases;
            notes.forget();
            lattice.forget();
            keyboard.forget();
        }

        auto& state = processor.getState();
        const auto value = [&state] (const char* id) { return state.getRawParameterValue (id)->load(); };
        ui::LatticeModel::Inputs in;
        in.status = status;
        in.mode = juce::jlimit (0, 3, (int) std::lround (value (params::mode)));
        in.slotWord = processor.getSlotWord (processor.currentSlot());
        in.orbitCycle = juce::jlimit (0, 4, (int) std::lround (value (params::orbitCycle)));
        in.inKey = value (params::inKey) > 0.5f;
        in.keyMode = juce::jlimit (0, 2, (int) std::lround (value (params::key)));
        in.voicing.reg = (Register) juce::jlimit (0, 1, (int) std::lround (value (params::registerMode)));
        const bool changed = picture.update (in);
        if (changed)
        {
            lattice.modelChanged();
            keyboard.refresh();
            word.refresh();
            head.refresh();
        }
        // Parameters can change from the host at any time: the labels re-read them (cheap).
        if (changed || ++ticks % 6 == 0)
        {
            head.refresh();
            side.refresh();
            word.refresh();
            keyboard.refresh();
        }
    }

    void TonnetzEditor::timerCallback()
    {
        refreshNow();
        const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
        lattice.tick (juce::jlimit (0.0, 0.1, now - lastTick));
        lastTick = now;
    }

    void TonnetzEditor::layoutFloating()
    {
        const auto lat = lattice.getBounds();
        const auto pref = side.preferredSize();
        side.setBounds (lat.getRight() - pref.getWidth() - 4, lat.getY() + 4, pref.getWidth(), pref.getHeight());
        word.setBounds (lat.getX(), lat.getBottom() - 22 - word.preferredHeight(), lat.getWidth(), word.preferredHeight());
    }

    void TonnetzEditor::resized()
    {
        auto r = getLocalBounds();
        head.setBounds (r.removeFromTop (headerHeight));
        const int keysHeight = ui::KeyboardStrip::barHeight + juce::jlimit (56, 96, getHeight() / 9);
        keyboard.setBounds (r.removeFromBottom (keysHeight));
        lattice.setBounds (r);
        layoutFloating();
    }

    void TonnetzEditor::paint (juce::Graphics& g)
    {
        const auto& k = ui::Ink::of (processor.editorView().dark);
        g.fillAll (k.ground);
        // The header's rule: one ink line, as on a page.
        g.setColour (k.line.withAlpha (0.8f));
        g.fillRect (0.0f, (float) headerHeight - 1.0f, (float) getWidth(), 1.0f);
    }

    bool TonnetzEditor::keyPressed (const juce::KeyPress& k)
    {
        if (k == juce::KeyPress::escapeKey)
        {
            lattice.releaseLattice();
            notes.releaseHolder (ui::EditorNotes::mouseKeys);
            if (auto* pal = word.palette())
                pal->setVisible (false);
            keyboard.refresh();
            return true;
        }
        if (k == juce::KeyPress::backspaceKey)
        {
            word.undo();
            return true;
        }
        return keyboard.keyPressed (k);
    }

    bool TonnetzEditor::keyStateChanged (bool)
    {
        return keyboard.keyStateChanged();
    }
}
