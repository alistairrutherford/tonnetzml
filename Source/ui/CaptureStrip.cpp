#include "CaptureStrip.h"
#include "../plugin/TonnetzProcessor.h"

namespace tonnetz::ui
{
    namespace
    {
        juce::String utf8 (const char* s) { return juce::String::fromUTF8 (s); }

        juce::RangedAudioParameter& paramOf (TonnetzProcessor& p, const char* id)
        {
            auto* param = p.getState().getParameter (id);
            jassert (param != nullptr);
            return *param;
        }

        constexpr float leftWidth = 150.0f;        // the toggles and the summary
        constexpr int handleWidth = 92, saveWidth = 44;
    }

    //==========================================================================
    CaptureStrip::Handle::Handle (CaptureStrip& s) : strip (s)
    {
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        setTooltip ("Drag into a track or the Finder: the capture as a MIDI file (As played). "
                    "Hold Alt for the Chords rendering: one block chord per change, on the sixteenth grid.");
    }

    void CaptureStrip::Handle::paint (juce::Graphics& g)
    {
        const auto& k = strip.ink();
        const bool any = strip.glyphCount() > 0;
        auto r = getLocalBounds().toFloat().reduced (2.0f, 7.0f);
        g.setColour (any ? (isMouseOver() || dragging ? k.generated : k.line) : k.lineFaint);
        g.drawRoundedRectangle (r, 4.0f, dragging ? 2.0f : 1.2f);
        g.setFont (Ink::ui (12.5f, dragging));
        g.setColour (any ? (isMouseOver() ? k.generated : k.text) : k.textFaint);
        const bool alt = juce::ModifierKeys::currentModifiers.isAltDown();
        g.drawText (utf8 ("\xe2\x87\xa9 drag MIDI") + (alt ? utf8 (" \xe2\x99\xa9") : juce::String()), r, juce::Justification::centred, false);
    }

    void CaptureStrip::Handle::mouseDrag (const juce::MouseEvent& e)
    {
        if (dragging || e.getDistanceFromDragStart() < 4 || strip.glyphCount() == 0)
            return;
        dragging = true;
        repaint();
        strip.dragOut (e.mods.isAltDown());
    }

    //==========================================================================
    CaptureStrip::CaptureStrip (TonnetzProcessor& p, const LatticeModel& m, LatticeView& l, EditorNotes& n)
        : processor (p), model (m), lattice (l), notes (n),
          captureToggle (paramOf (p, params::capture), p.editorView(), {}, utf8 ("\xe2\xa6\xbf capture"), utf8 ("\xe2\x97\x8b capture")),
          sourceToggle (paramOf (p, params::captureSource), p.editorView(), {}, "+ source"),
          handle (*this),
          save (p.editorView(), utf8 ("save\xe2\x80\xa6"), [this] { saveAs (juce::ModifierKeys::currentModifiers.isAltDown()); }, 12.5f)
    {
        captureToggle.setComponentID (params::capture);
        sourceToggle.setComponentID (params::captureSource);
        captureToggle.setTooltip ("Capture: arm a take. Everything played from now until it is disarmed is kept (the strip "
                                  "shows it); with it off the strip shows the last 64 bars, always kept.");
        sourceToggle.setTooltip ("Capture the source notes too (what was played into TonnetzML), on their own track.");
        save.setTooltip (utf8 ("Save the capture as a .mid (As played; Alt-click: Chords)."));
        addAndMakeVisible (captureToggle);
        addAndMakeVisible (sourceToggle);
        addAndMakeVisible (handle);
        addAndMakeVisible (save);
        refresh();
    }

    juce::Rectangle<float> CaptureStrip::glyphArea() const
    {
        return getLocalBounds().toFloat().withTrimmedLeft (leftWidth).withTrimmedRight ((float) (handleWidth + saveWidth + 8)).reduced (4.0f, 3.0f);
    }

    void CaptureStrip::resized()
    {
        auto r = getLocalBounds();
        auto left = r.removeFromLeft ((int) leftWidth).reduced (8, 0);
        const int h = 18;
        captureToggle.setBounds (left.getX(), 5, captureToggle.preferredWidth(), h);
        sourceToggle.setBounds (captureToggle.getRight() + 8, 5, sourceToggle.preferredWidth(), h);
        save.setBounds (r.removeFromRight (saveWidth).withSizeKeepingCentre (saveWidth, h));
        handle.setBounds (r.removeFromRight (handleWidth + 4).withTrimmedRight (4));
    }

    juce::String CaptureStrip::summary() const
    {
        const auto& cap = processor.captureModel();
        const int bars = cap.bars();
        const juce::String length = juce::String (bars) + (bars == 1 ? " bar" : " bars");
        if (cap.isArmed())
            return utf8 ("take \xc2\xb7 ") + length;
        if (cap.showingTake())
            return utf8 ("last take \xc2\xb7 ") + length;
        return bars > 0 ? utf8 ("last ") + length : utf8 ("last 64 bars");
    }

    void CaptureStrip::refresh()
    {
        captureToggle.refresh();
        sourceToggle.refresh();
        const auto& cap = processor.captureModel();
        if (cap.revision() != builtRevision || cap.showingTake() != builtShowingTake || cap.isArmed() != builtArmed)
        {
            const bool wasAtEnd = scroll <= 0.5f;
            const auto before = chords.size();
            builtRevision = cap.revision();
            builtShowingTake = cap.showingTake();
            builtArmed = cap.isArmed();
            chords = chordsOf (cap.current());
            names.clear();
            for (const auto& c : chords)
                names.push_back (model.chordName (c.mask(), LatticeModel::rootInPosition (c.mask(), c.lowest() % 12)));   // in root position, as the header names the generated chord
            if (! wasAtEnd && chords.size() > before)
                scroll += glyphWidth * (float) (chords.size() - before);   // keep what was looked at in place
            // The recalled glyph, found again by its start.
            recalled = -1;
            for (size_t i = 0; i < chords.size() && recalledStart >= 0.0; ++i)
                if (chords[i].start == recalledStart)
                    recalled = (int) i;
            handle.repaint();
            repaint();
        }
        // The lattice let go of the recalled chord (Release, a click, a stop).
        if (recalled >= 0)
        {
            bool held = true;
            for (int p : chords[(size_t) recalled].pitches())
                held = held && notes.holds (p, EditorNotes::lattice);
            if (! held)
            {
                recalled = -1;
                recalledStart = -1.0;
                repaint();
            }
        }
    }

    juce::Rectangle<float> CaptureStrip::glyphBounds (int i) const
    {
        const auto area = glyphArea();
        const int n = (int) chords.size();
        if (i < 0 || i >= n)
            return {};
        // Newest at the right edge (scrolled back by `scroll`).
        const float right = area.getRight() + scroll - glyphWidth * (float) (n - 1 - i);
        auto b = juce::Rectangle<float> (right - glyphWidth, area.getY(), glyphWidth, area.getHeight());
        if (b.getX() < area.getX() - 0.5f || b.getRight() > area.getRight() + 0.5f)
            return {};
        return b;
    }

    int CaptureStrip::glyphAt (juce::Point<float> p) const
    {
        for (int i = (int) chords.size() - 1; i >= 0; --i)
            if (glyphBounds (i).contains (p))
                return i;
        return -1;
    }

    void CaptureStrip::clickGlyph (int i)
    {
        if (i < 0 || i >= (int) chords.size())
            return;
        if (recalled == i)
        {
            lattice.releaseLattice();       // clicked again: let go
            recalled = -1;
            recalledStart = -1.0;
        }
        else
        {
            lattice.holdSet (chords[(size_t) i].pitches());
            recalled = i;
            recalledStart = chords[(size_t) i].start;
        }
        repaint();
    }

    void CaptureStrip::mouseUp (const juce::MouseEvent& e)
    {
        if (e.mouseWasClicked())
            clickGlyph (glyphAt (e.position));
    }

    void CaptureStrip::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
    {
        const float maxScroll = juce::jmax (0.0f, glyphWidth * (float) chords.size() - glyphArea().getWidth());
        const float delta = (std::abs (w.deltaX) > std::abs (w.deltaY) ? -w.deltaX : w.deltaY) * 160.0f;
        scroll = juce::jlimit (0.0f, maxScroll, scroll + delta);
        repaint();
    }

    juce::File CaptureStrip::dragOut (bool chordsRendering, bool performDrag)
    {
        juce::String error;
        const auto file = processor.writeCaptureForDrag (chordsRendering ? CaptureRendering::Chords : CaptureRendering::AsPlayed, error);
        if (! file.existsAsFile())
        {
            handle.setTooltip (error);
            handle.dragging = false;
            handle.repaint();
            return {};
        }
        if (performDrag)
            juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, &handle,
                                                                        [safe = juce::Component::SafePointer<Handle> (&handle)]
            {
                if (safe != nullptr)
                {
                    safe->dragging = false;
                    safe->repaint();
                }
            });
        else
            handle.dragging = false;
        return file;
    }

    void CaptureStrip::saveAs (bool chordsRendering)
    {
        if (chords.empty())
            return;
        const auto rendering = chordsRendering ? CaptureRendering::Chords : CaptureRendering::AsPlayed;
        const auto start = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("TonnetzML")
                               .getChildFile (juce::File::createLegalFileName (processor.captureName (rendering)) + ".mid");
        chooser = std::make_unique<juce::FileChooser> ("Save the capture as a MIDI file", start, "*.mid");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this, rendering, safe = juce::Component::SafePointer<CaptureStrip> (this)] (const juce::FileChooser& fc)
        {
            if (safe == nullptr)
                return;
            auto file = fc.getResult();
            if (file == juce::File())
                return;
            if (! file.hasFileExtension ("mid"))
                file = file.withFileExtension ("mid");
            juce::String error;
            if (! processor.writeCapture (file, rendering, error))
                save.setTooltip (error);
        });
    }

    //==========================================================================
    void CaptureStrip::drawGlyph (juce::Graphics& g, const CapturedChord& c, juce::Rectangle<float> box, bool lit) const
    {
        const auto& k = ink();
        const auto colour = c.stream == CaptureStream::Source ? k.source : k.generated;
        if (lit)
        {
            g.setColour (colour.withAlpha (0.14f));
            g.fillRoundedRectangle (box.reduced (1.0f), 4.0f);
            g.setColour (colour.withAlpha (0.8f));
            g.drawRoundedRectangle (box.reduced (1.0f), 4.0f, 1.2f);
        }
        auto shapeBox = box.reduced (5.0f, 2.0f);
        shapeBox.removeFromBottom (12.0f);

        const auto e = embed ((unsigned) c.mask());
        if (e.size == 0)
            return;
        // The nodes on the plane (a, b) → x = a + b/2, y = -b·√3/2, fitted into the box.
        std::vector<juce::Point<float>> pts;
        float minX = 1.0e9f, maxX = -1.0e9f, minY = 1.0e9f, maxY = -1.0e9f;
        for (int i = 0; i < e.size; ++i)
        {
            const auto v = e.at[(size_t) i];
            const juce::Point<float> p ((float) v.a + 0.5f * (float) v.b, -0.8660254f * (float) v.b);
            pts.push_back (p);
            minX = juce::jmin (minX, p.x); maxX = juce::jmax (maxX, p.x);
            minY = juce::jmin (minY, p.y); maxY = juce::jmax (maxY, p.y);
        }
        const float spanX = juce::jmax (1.0f, maxX - minX), spanY = juce::jmax (0.8660254f, maxY - minY);
        const float scale = juce::jmin ((shapeBox.getWidth() - 6.0f) / spanX, (shapeBox.getHeight() - 6.0f) / spanY, 16.0f);
        const auto centre = shapeBox.getCentre();
        for (auto& p : pts)
            p = { centre.x + (p.x - 0.5f * (minX + maxX)) * scale, centre.y + (p.y - 0.5f * (minY + maxY)) * scale };

        const auto tris = trianglesOf (e);
        for (int t = 0; t < tris.size; ++t)
        {
            const auto& idx = tris.tones[(size_t) t];
            juce::Path p;
            p.addTriangle (pts[(size_t) idx[0]], pts[(size_t) idx[1]], pts[(size_t) idx[2]]);
            g.setColour (colour.withAlpha (0.35f));
            g.fillPath (p);
        }
        g.setColour (colour);
        for (int i = 0; i < e.size; ++i)
            for (int j = i + 1; j < e.size; ++j)
                if (adjacent (e.at[(size_t) i], e.at[(size_t) j]))
                    g.drawLine ({ pts[(size_t) i], pts[(size_t) j] }, 1.4f);
        for (const auto& p : pts)
            g.fillEllipse (p.x - 2.0f, p.y - 2.0f, 4.0f, 4.0f);
    }

    void CaptureStrip::paint (juce::Graphics& g)
    {
        const auto& k = ink();
        g.fillAll (k.ground);
        g.setColour (k.line.withAlpha (0.8f));
        g.fillRect (0.0f, 0.0f, (float) getWidth(), 1.0f);

        // The summary under the toggles.
        g.setFont (Ink::ui (11.5f));
        g.setColour (k.textFaint);
        g.drawText (summary(), juce::Rectangle<float> (8.0f, 25.0f, leftWidth - 12.0f, 16.0f), juce::Justification::centredLeft, false);

        const auto area = glyphArea();
        if (chords.empty())
        {
            g.setFont (Ink::ui (12.0f));
            g.drawText (utf8 ("the chords played collect here \xe2\x80\x94 click one to play it again, drag the handle out for MIDI"),
                        area, juce::Justification::centredLeft, true);
            return;
        }
        g.saveState();
        g.reduceClipRegion (area.toNearestInt());
        const int n = (int) chords.size();
        for (int i = n - 1; i >= 0; --i)
        {
            const float right = area.getRight() + scroll - glyphWidth * (float) (n - 1 - i);
            juce::Rectangle<float> box (right - glyphWidth, area.getY(), glyphWidth, area.getHeight());
            if (box.getRight() < area.getX())
                break;
            drawGlyph (g, chords[(size_t) i], box, i == recalled);
            g.setFont (Ink::note (10.5f));
            g.setColour (chords[(size_t) i].stream == CaptureStream::Source ? k.source : k.text);
            g.drawFittedText (names[(size_t) i], box.removeFromBottom (13.0f).toNearestInt(), juce::Justification::centred, 1, 0.7f);
        }
        g.restoreState();
        if (scroll > 0.5f)
        {
            g.setColour (k.textFaint);
            g.setFont (Ink::ui (12.0f));
            g.drawText (utf8 ("\xe2\x80\xba"), area.withLeft (area.getRight() - 8.0f).translated (6.0f, 0.0f), juce::Justification::centred, false);
        }
    }
}
