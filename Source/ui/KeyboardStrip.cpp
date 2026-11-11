#include "KeyboardStrip.h"

namespace tonnetz::ui
{
    namespace
    {
        juce::String utf8 (const char* s) { return juce::String::fromUTF8 (s); }

        // The computer keyboard's two rows: Z S X D C V G B H N J M (an octave from C), then
        // Q 2 W 3 E R 5 T 6 Y 7 U I 9 O 0 P (the next, up to E).
        constexpr const char* computerKeys = "zsxdcvgbhnjmq2w3er5t6y7ui9o0p";

        int whiteIndex (int note)
        {
            static constexpr int inOctave[12] = { 0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6 };
            return (note / 12) * 7 + inOctave[note % 12];
        }
    }

    KeyboardStrip::KeyboardStrip (TonnetzProcessor& p, const LatticeModel& m, EditorNotes& n)
        : processor (p), model (m), notes (n),
          sticky (p.editorView(), {}, [this]
          {
              auto& v = processor.editorView();
              v.sticky = ! v.sticky;
              if (! v.sticky)
                  notes.releaseHolder (EditorNotes::mouseKeys);
              refresh();
          }, 11.5f),
          release (p.editorView(), "release", [this] { notes.releaseHolder (EditorNotes::mouseKeys); refresh(); }, 11.5f),
          octaveDown (p.editorView(), utf8 ("\xe2\x80\xb9"), [this] { auto& v = processor.editorView(); v.computerOctave = juce::jmax (1, v.computerOctave - 1); refresh(); }, 13.0f),
          octaveUp (p.editorView(), utf8 ("\xe2\x80\xba"), [this] { auto& v = processor.editorView(); v.computerOctave = juce::jmin (8, v.computerOctave + 1); refresh(); }, 13.0f)
    {
        setOpaque (true);
        for (auto* b : { &sticky, &release, &octaveDown, &octaveUp })
            addAndMakeVisible (b);
        refresh();
    }

    juce::String KeyboardStrip::keyName (int note)
    {
        static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return LatticeModel::pretty (names[note % 12]) + juce::String (note / 12 - 2);
    }

    void KeyboardStrip::refresh()
    {
        sticky.setText (utf8 (processor.editorView().sticky ? "\xe2\x97\x8f sticky" : "\xe2\x97\x8b sticky"));
        release.setEnabled (notes.any (EditorNotes::mouseKeys));
        repaint();
    }

    void KeyboardStrip::forget()
    {
        mouseNote = -1;
        refresh();
    }

    void KeyboardStrip::resized()
    {
        auto bar = getLocalBounds().removeFromTop (barHeight).reduced (8, 0);
        bar.removeFromLeft (64);
        sticky.setBounds (bar.removeFromLeft (sticky.preferredWidth() + 6));
        bar.removeFromLeft (6);
        release.setBounds (bar.removeFromLeft (release.preferredWidth() + 6));
        octaveUp.setBounds (bar.removeFromRight (16));
        bar.removeFromRight (48);
        octaveDown.setBounds (bar.removeFromRight (16));
        scroll = juce::jlimit (0.0f, juce::jmax (0.0f, whiteWidth() * 52.0f - (float) getWidth()), scroll);
    }

    juce::Rectangle<float> KeyboardStrip::keysArea() const
    {
        return getLocalBounds().toFloat().withTrimmedTop ((float) barHeight);
    }

    float KeyboardStrip::whiteWidth() const
    {
        return juce::jmax (13.0f, (float) getWidth() / 52.0f);       // 52 white keys
    }

    juce::Rectangle<float> KeyboardStrip::keyBounds (int note) const
    {
        const auto area = keysArea();
        const float w = whiteWidth();
        const float x = area.getX() + (float) (whiteIndex (note) - whiteIndex (lowest)) * w - scroll;
        if (! isBlack (note))
            return { x, area.getY(), w, area.getHeight() };
        const float bw = w * 0.62f;
        return { x - bw * 0.5f, area.getY(), bw, area.getHeight() * 0.6f };
    }

    int KeyboardStrip::noteAt (juce::Point<float> p) const
    {
        for (int n = lowest; n <= highest; ++n)
            if (isBlack (n) && keyBounds (n).contains (p))
                return n;
        for (int n = lowest; n <= highest; ++n)
            if (! isBlack (n) && keyBounds (n).contains (p))
                return n;
        return -1;
    }

    void KeyboardStrip::clickKey (int note, int velocity)
    {
        if (note < 0)
            return;
        if (processor.editorView().sticky)
        {
            if (notes.holds (note, EditorNotes::mouseKeys))
                notes.release (note, EditorNotes::mouseKeys);
            else
                notes.press (note, velocity, EditorNotes::mouseKeys);
        }
        else
        {
            notes.press (note, velocity, EditorNotes::mouseKeys);
            mouseNote = note;
        }
        refresh();
    }

    void KeyboardStrip::mouseDown (const juce::MouseEvent& e)
    {
        const int n = noteAt (e.position);
        if (n < 0)
            return;
        const auto k = keyBounds (n);
        const int velocity = juce::jlimit (30, 127, (int) (40 + 87 * (e.position.y - k.getY()) / juce::jmax (1.0f, k.getHeight())));
        clickKey (n, velocity);
    }

    void KeyboardStrip::mouseDrag (const juce::MouseEvent& e)
    {
        if (processor.editorView().sticky || mouseNote < 0)
            return;
        const int n = noteAt (e.position);
        if (n >= 0 && n != mouseNote)
        {
            notes.release (mouseNote, EditorNotes::mouseKeys);
            notes.press (n, 96, EditorNotes::mouseKeys);
            mouseNote = n;
            refresh();
        }
    }

    void KeyboardStrip::mouseUp (const juce::MouseEvent&)
    {
        if (! processor.editorView().sticky && mouseNote >= 0)
        {
            notes.release (mouseNote, EditorNotes::mouseKeys);
            mouseNote = -1;
            refresh();
        }
    }

    void KeyboardStrip::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
    {
        const float span = juce::jmax (0.0f, whiteWidth() * 52.0f - (float) getWidth());
        scroll = juce::jlimit (0.0f, span, scroll - (std::abs (w.deltaX) > std::abs (w.deltaY) ? w.deltaX : w.deltaY) * 200.0f);
        repaint();
    }

    bool KeyboardStrip::keyPressed (const juce::KeyPress& k)
    {
        const auto c = (char) juce::CharacterFunctions::toLowerCase (k.getTextCharacter());
        if (c == '-' || c == '=')
        {
            (c == '-' ? octaveDown : octaveUp).click();
            return true;
        }
        return c != 0 && std::strchr (computerKeys, c) != nullptr;
    }

    bool KeyboardStrip::keyStateChanged()
    {
        bool any = false;
        const int base = 12 * processor.editorView().computerOctave;
        const int count = (int) std::strlen (computerKeys);
        for (int i = 0; i < count; ++i)
        {
            const bool down = juce::KeyPress::isKeyCurrentlyDown ((int) computerKeys[i]) || juce::KeyPress::isKeyCurrentlyDown ((int) juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) computerKeys[i]));
            if (down == computerDown[(size_t) i])
                continue;
            computerDown[(size_t) i] = down;
            any = true;
            const int note = base + i - (i >= 12 ? 0 : 0);
            if (down)
                notes.press (note, 100, EditorNotes::computerKeys);
            else
                notes.release (note, EditorNotes::computerKeys);
        }
        if (any)
            refresh();
        return any;
    }

    void KeyboardStrip::paint (juce::Graphics& g)
    {
        const auto& k = ink();
        g.fillAll (k.ground);
        // The bar: a caption, Sticky, Release, the computer keyboard's octave.
        g.setColour (k.textFaint);
        g.setFont (Ink::ui (11.5f));
        g.drawText ("keyboard", juce::Rectangle<int> (8, 0, 64, barHeight), juce::Justification::centredLeft, false);
        const int oct = processor.editorView().computerOctave;
        g.drawText (utf8 ("\xe2\x8c\xa8 ") + keyName (12 * oct), juce::Rectangle<int> (octaveDown.getRight(), 0, octaveUp.getX() - octaveDown.getRight(), barHeight),
                    juce::Justification::centred, false);

        const auto zone = processor.getState().getRawParameterValue (params::opZoneOn)->load() > 0.5f;
        const int zoneStart = (int) std::lround (processor.getState().getRawParameterValue (params::opZoneStart)->load());
        const int base = 12 * oct;
        const auto area = keysArea();

        auto drawKey = [&] (int n)
        {
            const auto r = keyBounds (n);
            if (r.getRight() < 0 || r.getX() > getWidth())
                return;
            const bool black = isBlack (n);
            const int sv = model.sourceVelocity[(size_t) n], gv = model.generatedVelocity[(size_t) n];
            const bool inZone = zone && n >= zoneStart && n < zoneStart + numOperatorKeys;
            juce::Colour fill = black ? k.line : k.node;
            if (inZone)
                fill = black ? k.line.interpolatedWith (k.bandStrong, 0.45f) : k.node.interpolatedWith (k.bandStrong, 0.28f);
            g.setColour (fill);
            g.fillRect (r.reduced (black ? 0.0f : 0.5f, 0.0f));
            if (gv > 0 || sv > 0)
            {
                const auto lower = r.reduced (black ? 1.5f : 2.5f, 0.0f).withTrimmedTop (r.getHeight() * (black ? 0.35f : 0.55f)).withTrimmedBottom (3.0f);
                if (gv > 0 && sv > 0)
                {
                    g.setColour (k.source);
                    g.fillRoundedRectangle (lower.withWidth (lower.getWidth() * 0.5f), 2.0f);
                    g.setColour (k.generated);
                    g.fillRoundedRectangle (lower.withTrimmedLeft (lower.getWidth() * 0.5f), 2.0f);
                }
                else
                {
                    g.setColour (gv > 0 ? k.generated : k.source);
                    g.fillRoundedRectangle (lower, 2.0f);
                }
            }
            if (notes.holds (n))
            {
                g.setColour (k.source);
                g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre ({ r.getCentreX(), r.getY() + 7.0f }));
            }
            if (! black)
            {
                g.setColour (k.line.withAlpha (0.6f));
                g.drawRect (r, 0.6f);
            }
            if (inZone)
            {
                // The op's name, small, at the foot of the key (⌫ for Undo, ⌂ for Home).
                const int index = n - zoneStart;
                const auto label = index == opkey::undo ? juce::String::fromUTF8 ("\xe2\x8c\xab") : index == opkey::home ? juce::String::fromUTF8 ("\xe2\x8c\x82")
                                                         : LatticeModel::opLabel (index);
                const float size = juce::jlimit (7.0f, 10.5f, r.getWidth() * (label.length() > 2 ? 0.42f : 0.62f));
                g.setColour (black ? k.onInk : k.text);
                g.setFont (Ink::ui (size, true));
                const auto foot = black ? r.withTrimmedTop (r.getHeight() - size - 6.0f) : r.withTrimmedTop (r.getHeight() - size - 16.0f).withTrimmedBottom (12.0f);
                g.drawText (label, foot.expanded (black ? 3.0f : 0.0f, 0.0f), juce::Justification::centred, false);
            }
            if (! black && n % 12 == 0)
            {
                g.setColour (k.textFaint);
                g.setFont (Ink::ui (9.0f));
                g.drawText (keyName (n), r.withTrimmedTop (r.getHeight() - 14.0f), juce::Justification::centred, false);
            }
            if (n >= base && n < base + (int) std::strlen (computerKeys) && n % 12 == 0)
            {
                g.setColour (k.bandStrong);
                g.fillRect (r.getX() + 1.0f, area.getY(), r.getWidth() - 2.0f, 2.0f);
            }
        };
        for (int n = lowest; n <= highest; ++n)
            if (! isBlack (n))
                drawKey (n);
        for (int n = lowest; n <= highest; ++n)
            if (isBlack (n))
                drawKey (n);
        g.setColour (k.line);
        g.fillRect (0.0f, area.getY(), (float) getWidth(), 1.2f);
    }
}
