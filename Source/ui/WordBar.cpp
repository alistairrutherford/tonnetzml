#include "WordBar.h"
#include "../core/lattice/Cycles.h"

namespace tonnetz::ui
{
    namespace
    {
        juce::String utf8 (const char* s) { return juce::String::fromUTF8 (s); }
        float textWidth (const juce::Font& f, const juce::String& s) { return juce::GlyphArrangement::getStringWidth (f, s); }
        juce::Font chipFont() { return Ink::ui (13.5f, true); }

        juce::String opText (const Op& op)
        {
            Word w;
            w.push (op);
            return LatticeModel::pretty (printWord (w).c_str()).replace ("-", utf8 ("\xe2\x88\x92"));
        }

        constexpr int captionW = 40;
    }

    //==========================================================================
    OpPalette::OpPalette (const EditorView& v, std::function<void (const Op&)> onPick)
        : view (v), picked (std::move (onPick))
    {
        // The six translations on the lattice's own directions (x = fifths, y = up), P L R in the
        // middle as a small triangle, N S H underneath.
        const float h = 0.866f;
        items = {
            { ops::fifthUp, "+5", 1.0f, 0.0f },             { ops::fifthDown, utf8 ("\xe2\x88\x92" "5"), -1.0f, 0.0f },
            { ops::majorThirdUp, "+M3", 0.5f, -h },         { ops::majorThirdDown, utf8 ("\xe2\x88\x92M3"), -0.5f, h },
            { ops::minorThirdUp, "+m3", 0.5f, h },          { ops::minorThirdDown, utf8 ("\xe2\x88\x92m3"), -0.5f, -h },
            { ops::P, "P", 0.0f, 0.27f },                   { ops::L, "L", 0.23f, -0.14f },          { ops::R, "R", -0.23f, -0.14f },
            { ops::N, "N", -0.42f, 1.42f },                 { ops::S, "S", 0.0f, 1.42f },            { ops::H, "H", 0.42f, 1.42f },
        };
        setSize (size, size + 24);
    }

    juce::Point<float> OpPalette::centreOf (int i) const
    {
        const auto& it = items[(size_t) i];
        const float unit = 58.0f;
        return { getWidth() * 0.5f + it.dx * unit, size * 0.5f + it.dy * unit - 6.0f };
    }

    int OpPalette::itemAt (juce::Point<float> p) const
    {
        for (int i = 0; i < count(); ++i)
            if (centreOf (i).getDistanceFrom (p) < 15.0f)
                return i;
        return -1;
    }

    void OpPalette::pick (int i)
    {
        if (i >= 0 && i < count() && picked)
            picked (items[(size_t) i].op);
    }

    void OpPalette::mouseMove (const juce::MouseEvent& e)
    {
        const int h = itemAt (e.position);
        if (h != hover)
        {
            hover = h;
            repaint();
        }
    }

    void OpPalette::mouseUp (const juce::MouseEvent& e)
    {
        pick (itemAt (e.position));
    }

    void OpPalette::paint (juce::Graphics& g)
    {
        const auto& k = Ink::of (view.dark);
        // A hexagon of hairlines (the lattice around one node), the ops on it.
        const auto c = juce::Point<float> (getWidth() * 0.5f, size * 0.5f - 6.0f);
        g.setColour (k.wash);
        g.fillEllipse (juce::Rectangle<float> (size - 8.0f, size - 8.0f).withCentre (c));
        g.setColour (k.lineFaint);
        for (int i = 0; i < 6; ++i)
            g.drawLine ({ c, centreOf (i) }, 1.0f);
        for (int i = 0; i < count(); ++i)
        {
            const auto p = centreOf (i);
            const auto r = juce::Rectangle<float> (30.0f, 30.0f).withCentre (p);
            g.setColour (i == hover ? k.generated : k.ground);
            g.fillEllipse (r);
            g.setColour (i == hover ? k.generated : k.ghost);
            g.drawEllipse (r, 1.3f);
            g.setColour (i == hover ? k.onInk : k.text);
            g.setFont (Ink::ui (items[(size_t) i].label.length() > 2 ? 10.5f : 13.5f, true));
            g.drawText (items[(size_t) i].label, r.expanded (4.0f, 0.0f), juce::Justification::centred, false);
        }
    }

    //==========================================================================
    WordBar::WordBar (TonnetzProcessor& p, const LatticeModel& m)
        : processor (p), model (m),
          slots (*p.getState().getParameter (params::slot), p.editorView(), "slot", { "A", "B", "C", "D", "E", "F", "G", "H" }, 7.0f),
          plus (p.editorView(), "+", [this] { openPalette(); }, 17.0f),
          undoButton (p.editorView(), utf8 ("\xe2\x8c\xab"), [this] { undo(); }, 15.0f),
          homeButton (p.editorView(), utf8 ("\xe2\x8c\x82"), [this] { home(); }, 15.0f)
    {
        slots.marked = [this] (int i) { return ! processor.getSlotWord (i).empty(); };
        addAndMakeVisible (slots);
        addAndMakeVisible (plus);
        addAndMakeVisible (undoButton);
        addAndMakeVisible (homeButton);
        plus.setTooltip ("add an op");
        undoButton.setTooltip ("undo the last op");
        homeButton.setTooltip ("home: no ops");

        text.setFont (Ink::ui (15.0f, true));
        text.setJustification (juce::Justification::centredLeft);
        text.onReturnKey = [this] { commitText(); };
        text.onEscapeKey = [this] { text.setVisible (false); note = {}; repaint(); };
        text.onFocusLost = [this] { if (text.isVisible()) commitText(); };
        addChildComponent (text);
        refresh();
    }

    WordBar::~WordBar()
    {
        if (paletteView != nullptr && paletteView->getParentComponent() != nullptr)
            paletteView->getParentComponent()->removeChildComponent (paletteView.get());
    }

    int WordBar::mode() const { return juce::jlimit (0, 3, (int) std::lround (processor.getState().getRawParameterValue (params::mode)->load())); }

    Word WordBar::shownWord() const
    {
        const int m = mode();
        if (m == (int) Mode::Mirror)
            return processor.getSlotWord (processor.currentSlot());
        if (m == (int) Mode::Orbit)
        {
            const int c = juce::jlimit (0, 4, (int) std::lround (processor.getState().getRawParameterValue (params::orbitCycle)->load()));
            if (c == (int) OrbitCycle::Slot)
                return processor.getSlotWord (processor.currentSlot());
            Word w;
            parseWord (factoryCycles[(size_t) c].word, w);
            return w;
        }
        // Walk / Explore: what was sent until the engine shows it (or a moment has passed).
        if (havePending && juce::Time::getMillisecondCounter() - pendingAt < 400 && ! (model.word == pending))
            return pending;
        return processor.getEngineStatus().walk.word;
    }

    bool WordBar::editable() const
    {
        const int m = mode();
        if (m == (int) Mode::Orbit)
            return (int) std::lround (processor.getState().getRawParameterValue (params::orbitCycle)->load()) == (int) OrbitCycle::Slot;
        return true;
    }

    void WordBar::setWord (const Word& w)
    {
        const int m = mode();
        if (m == (int) Mode::Walk || m == (int) Mode::Explore)
        {
            processor.sendEditorCommand (EditorCommand::setWalk (w));
            pending = w;
            havePending = true;
            pendingAt = juce::Time::getMillisecondCounter();
        }
        else if (editable())
            processor.setSlotWord (processor.currentSlot(), w);
        refresh();
    }

    void WordBar::appendOp (const Op& op)
    {
        if (! editable())
            return;
        Word w = shownWord();
        const int m = mode();
        if (m == (int) Mode::Walk || m == (int) Mode::Explore)
            wordedit::append (w, op);              // as the operator keys: P after P cancels
        else
            w.push (op);
        setWord (w);
    }

    void WordBar::undo()
    {
        const int m = mode();
        if (m == (int) Mode::Mirror)
        {
            Word w = shownWord();
            w.pop();
            setWord (w);
        }
        else
            processor.sendEditorCommand (EditorCommand::operatorKey (opkey::undo));    // Walk: the last op; Orbit: a step back
    }

    void WordBar::home()
    {
        if (mode() == (int) Mode::Mirror)
            setWord ({});
        else
            processor.sendEditorCommand (EditorCommand::operatorKey (opkey::home));
    }

    void WordBar::moveChip (int from, int to)
    {
        Word w = shownWord();
        if (! editable() || from < 0 || from >= w.size || to < 0 || to >= w.size || from == to)
            return;
        Word out;
        std::vector<Op> v (w.begin(), w.end());
        const Op op = v[(size_t) from];
        v.erase (v.begin() + from);
        v.insert (v.begin() + to, op);
        for (const auto& o : v)
            out.push (o);
        setWord (out);
    }

    void WordBar::deleteChip (int index)
    {
        Word w = shownWord();
        if (! editable() || index < 0 || index >= w.size)
            return;
        Word out;
        for (int i = 0; i < w.size; ++i)
            if (i != index)
                out.push (w[i]);
        setWord (out);
    }

    void WordBar::beginTextEntry()
    {
        if (! editable())
            return;
        text.setText (juce::String::fromUTF8 (printWord (shownWord()).c_str()), false);
        text.setColour (juce::TextEditor::backgroundColourId, ink().ground);
        text.setColour (juce::TextEditor::textColourId, ink().text);
        text.setColour (juce::TextEditor::outlineColourId, ink().textFaint);
        text.setColour (juce::TextEditor::focusedOutlineColourId, ink().generated);
        text.setVisible (true);
        text.grabKeyboardFocus();
        text.selectAll();
    }

    bool WordBar::commitText()
    {
        Word w;
        const auto t = text.getText();
        const auto u = t.toRawUTF8();
        if (! parseWord (u, std::strlen (u), w))
        {
            note = "Not a word. Ops: P L R N S H +5 -5 +M3 -M3 +m3 -m3 T(a,b) Flip(i,j) Pivot(i) Grow(i,j) Shrink";
            noteIsError = true;
            repaint();
            return false;
        }
        text.setVisible (false);
        setWord (w);
        note = {};
        noteIsError = false;
        repaint();
        return true;
    }

    void WordBar::openPalette()
    {
        if (paletteView != nullptr && paletteView->isVisible())
        {
            paletteView->setVisible (false);
            return;
        }
        if (! editable())
            return;
        if (paletteView == nullptr)
            paletteView = std::make_unique<OpPalette> (processor.editorView(), [this] (const Op& op)
            {
                appendOp (op);
                if (! juce::ModifierKeys::currentModifiers.isShiftDown())
                    paletteView->setVisible (false);
            });
        if (auto* parent = getParentComponent())
        {
            parent->addAndMakeVisible (*paletteView);
            const auto at = parent->getLocalPoint (this, plus.getBounds().getCentre());
            paletteView->setTopLeftPosition (juce::jlimit (0, parent->getWidth() - paletteView->getWidth(), at.x - paletteView->getWidth() / 2),
                                             juce::jmax (0, getY() - paletteView->getHeight()));
            paletteView->toFront (false);
        }
    }

    void WordBar::refresh()
    {
        slots.refresh();
        const bool e = editable();
        plus.setEnabled (e);
        homeButton.setEnabled (mode() != (int) Mode::Mirror || ! shownWord().empty());
        resized();
        repaint();
    }

    juce::Rectangle<float> WordBar::chipsArea() const
    {
        return getLocalBounds().toFloat().withTrimmedLeft ((float) captionW + 8.0f).withTrimmedTop (4.0f).withHeight (26.0f);
    }

    juce::Rectangle<float> WordBar::chipBounds (int index) const
    {
        const Word w = shownWord();
        float x = chipsArea().getX();
        for (int i = 0; i < w.size; ++i)
        {
            const float cw = textWidth (chipFont(), opText (w[i])) + 16.0f;
            if (i == index)
                return { x, chipsArea().getY(), cw, chipsArea().getHeight() };
            x += cw + 4.0f;
        }
        return { x, chipsArea().getY(), 0.0f, chipsArea().getHeight() };
    }

    int WordBar::chipAt (juce::Point<float> p) const
    {
        const Word w = shownWord();
        for (int i = 0; i < w.size; ++i)
            if (chipBounds (i).expanded (2.0f).contains (p))
                return i;
        return -1;
    }

    void WordBar::resized()
    {
        const Word w = shownWord();
        const auto end = chipBounds (w.size);
        const int y = (int) chipsArea().getY();
        plus.setBounds ((int) end.getX() + 2, y, 24, 26);
        undoButton.setBounds (plus.getRight() + 8, y, 22, 26);
        homeButton.setBounds (undoButton.getRight() + 2, y, 22, 26);
        slots.setBounds (getWidth() - slots.preferredWidth() - 10, y, slots.preferredWidth(), 26);
        text.setBounds (chipsArea().toNearestInt().withWidth (juce::jmax (220, slots.getX() - captionW - 30)));
    }

    void WordBar::paint (juce::Graphics& g)
    {
        const auto& k = ink();
        const auto row = chipsArea();
        g.setColour (k.textFaint);
        g.setFont (Ink::ui (11.5f));
        const int m = mode();
        g.drawText (m == (int) Mode::Orbit ? "cycle" : m == (int) Mode::Mirror ? "slot W" : "W",
                    juce::Rectangle<float> (8.0f, row.getY(), (float) captionW, row.getHeight()), juce::Justification::centredLeft, false);

        const Word w = shownWord();
        const bool e = editable();
        // Orbit: the cycle op last taken.
        const int at = m == (int) Mode::Orbit && model.wordOps > 0 && w.size > 0 ? (model.wordOps - 1) % w.size : -1;
        for (int i = 0; i < w.size; ++i)
        {
            auto r = chipBounds (i);
            if (i == dragging)
                continue;
            if (dragging >= 0 && dropAt >= 0 && ! dropOff)
            {
                // Make room where the dragged chip would land.
                const float shift = chipBounds (dragging).getWidth() + 4.0f;
                if (i > dragging && i <= dropAt) r.translate (-shift, 0.0f);
                if (i < dragging && i >= dropAt) r.translate (shift, 0.0f);
            }
            const bool lit = i == at;
            g.setColour (lit ? k.generated : k.ground);
            g.fillRoundedRectangle (r, 13.0f);
            g.setColour (lit ? k.generated : (e ? k.line : k.textFaint));
            g.drawRoundedRectangle (r.reduced (0.6f), 13.0f, 1.3f);
            g.setColour (lit ? k.onInk : (e ? k.text : k.textFaint));
            g.setFont (chipFont());
            g.drawText (opText (w[i]), r, juce::Justification::centred, false);
        }
        if (w.empty())
        {
            g.setColour (k.textFaint);
            g.setFont (Ink::ui (12.0f));
            g.drawText (m == (int) Mode::Orbit ? "(no cycle)" : utf8 ("none: the chord as played"), juce::Rectangle<float> (row.getX() + 30.0f, row.getY(), 220.0f, row.getHeight()),
                        juce::Justification::centredLeft, false);
        }
        if (dragging >= 0)
        {
            const auto r = chipBounds (dragging).withCentre (dragPos);
            g.setColour ((dropOff ? k.warn : k.generated).withAlpha (0.9f));
            g.fillRoundedRectangle (r, 13.0f);
            g.setColour (k.onInk);
            g.setFont (chipFont());
            g.drawText (dropOff ? utf8 ("\xc3\x97 ") + opText (w[dragging]) : opText (w[dragging]), r, juce::Justification::centred, false);
        }
        if (note.isNotEmpty())
        {
            g.setColour (noteIsError ? k.warn : k.textFaint);
            g.setFont (Ink::ui (11.0f));
            g.drawText (note, juce::Rectangle<float> (row.getX(), row.getBottom() + 1.0f, (float) getWidth() - row.getX(), 14.0f), juce::Justification::centredLeft, true);
        }
    }

    void WordBar::mouseDown (const juce::MouseEvent& e)
    {
        dragging = editable() ? chipAt (e.position) : -1;
        dropAt = dragging;
        dropOff = false;
        dragPos = e.position;
    }

    void WordBar::mouseDrag (const juce::MouseEvent& e)
    {
        if (dragging < 0)
            return;
        dragPos = e.position;
        dropOff = e.position.y < chipsArea().getY() - 22.0f || e.position.y > chipsArea().getBottom() + 22.0f;
        const Word w = shownWord();
        dropAt = dragging;
        for (int i = 0; i < w.size; ++i)
            if (e.position.x >= chipBounds (i).getX())
                dropAt = i;
        repaint();
    }

    void WordBar::mouseUp (const juce::MouseEvent&)
    {
        const int from = dragging, to = dropAt;
        const bool off = dropOff;
        dragging = dropAt = -1;
        dropOff = false;
        if (from >= 0)
        {
            if (off)
                deleteChip (from);
            else if (to != from)
                moveChip (from, to);
        }
        repaint();
    }

    void WordBar::mouseDoubleClick (const juce::MouseEvent& e)
    {
        if (chipsArea().contains (e.position))
            beginTextEntry();
    }
}
