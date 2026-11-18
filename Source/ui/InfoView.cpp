#include "InfoView.h"

namespace tonnetz::ui
{
    namespace
    {
        struct Info { const char* key; const char* title; const char* text; };

        // One entry per control, button and area. Keep each to a line or two: what it does, and
        // what each of its settings means. (UTF-8: \xe2\x80\x93 is an en dash, \xe2\x99\xad a flat.)
        const Info table[] = {
            // ---- The header -------------------------------------------------------------------
            { "area.header", "TonnetzML",
              "The sentence: the chord played (teal), the word applied to it, and the chord it makes (vermilion), "
              "named and numbered in the key." },
            { "enable", "Enable",
              "On: chords played in come out transformed by the word. Off: MIDI passes through untouched." },
            { "mode", "Mode",
              "Mirror: every chord through the slot's word. Walk: operator keys add ops. Orbit: a cycle steps with the song. "
              "Explore: play and walk with the mouse." },
            { "key", "Key",
              "The key heard, with its fit (hollow while unsure). Click: Listen, Hold, Off. Right-click: set it by hand, "
              "or In key. Its notes are tinted on the lattice." },
            { "ui.theme", "Theme", "Paper or night: the light or the dark page. Saved with the set." },

            // ---- The lattice ------------------------------------------------------------------
            { "area.lattice", "Lattice",
              "Click a triangle to play it, drag to glide, shift-click nodes for any set. Ghosts: one op away. "
              "Right- or \xe2\x8c\x98-drag pans, the wheel zooms, double-click recentres." },
            { "ui.follow", "Follow", "On: the view eases to keep the chord played and the chord made in sight." },
            { "ui.latticeRelease", "Release", "Lets go of the triangle or set the lattice holds (Sticky)." },
            { "ui.settings", "Settings", "Shows or folds this column of the other settings." },
            { "ui.resetSettings", "Reset settings",
              "Puts every setting in this column back to its default. Click, then click \"sure?\" within 3 seconds. "
              "Mode, Key, the slots' words and the capture stay." },

            // ---- The settings column ----------------------------------------------------------
            { "mix", "Out",
              "What goes out: Generated (the chord made), Source + Generated, or Source (the lattice still follows)." },
            { "sourceChannel", "Source channel", "MIDI channel (1 to 16) the chord played goes out on, when Out sends it." },
            { "genChannel", "Generated channel",
              "MIDI channel (1 to 16) the chord made goes out on. Record it on a MIDI track; Capture keeps this channel." },
            { "register", "Register",
              "Stay: a move along the lattice keeps the chord near where it was. Follow: it moves literally, so a walk climbs." },
            { "bass", "Bass", "Off: the chord only. Root below: adds the new chord's root one or two octaves under it." },
            { "octave", "Octave", "Moves the chord made up or down by whole octaves (\xe2\x88\x92" "2 to +2)." },
            { "gatherMs", "Gather",
              "Notes starting within this time are one chord. 0 ms: only notes on the same sample (clips, no delay). "
              "Hand-played chords need about 20." },
            { "orbitCycle", "Orbit cycle", "The word Orbit repeats, one op a step: PL, PR, LR, PLR, or Slot (the slot's word)." },
            { "orbitRate", "Orbit rate", "How often Orbit takes its next step, from a quarter bar to 4 bars, on the song's grid." },
            { "walkInput", "Walk input", "Relative: a new chord keeps the word. Reset: a new chord clears it (Orbit starts its cycle again)." },
            { "latch", "Latch", "On: the last chord stays as the source after its keys are let go, until the next chord." },
            { "opZoneOn", "Operator keys",
              "On: an octave of keys (P L R N S H \xc2\xb1" "5 \xc2\xb1M3 Undo Home) edits the word instead of sounding. "
              "Walk, Orbit and Explore." },
            { "opZoneStart", "Operator keys from", "The operator keys' lowest key (C0 by default). Click or drag to move them by octaves." },
            { "keyTonic", "Held key", "The key's tonic while Key is Hold. Hold copies the guess here; set it by hand too." },
            { "keyMinor", "Major / minor", "The held key: major or minor." },
            { "inKey", "In key",
              "On: ghosts that stay in the key are solid, the rest faint (nothing is blocked); Orbit skips steps outside it." },
            { "previewTone", "Preview tone",
              "A small built-in sound for what goes out and for hover auditions, to try TonnetzML with nothing routed." },
            { "testRig", "Test rig", "For testing: plays Keys.txt from the rig folder in time with the host, and writes Status.txt." },

            // ---- The word bar -----------------------------------------------------------------
            { "area.word", "Word",
              "The ops applied to every chord. Drag a chip to reorder, drop it off the bar to delete, double-click to type a word "
              "(\"P L +5\")." },
            { "slot", "Slot", "Which of the eight stored words Mirror plays (and Orbit's Slot cycle); a dot marks a slot holding one." },
            { "ui.add", "Add an op", "Opens the op palette, laid out in the lattice's directions: click an op to append it." },
            { "ui.undo", "Undo", "Removes the word's last op (Backspace does too)." },
            { "ui.home", "Home", "Clears the word: no ops, the chord comes out as played." },
            { "area.palette", "Op palette",
              "Ops in the lattice's directions: \xc2\xb1" "5 left and right, thirds on the diagonals, P L R in the middle, N S H "
              "below. Shift-click to add several." },

            // ---- The capture strip ------------------------------------------------------------
            { "area.capture", "Capture strip",
              "One glyph per chord made, newest at the right (the wheel scrolls back). Click one to play it as the source; "
              "click again to let go." },
            { "capture", "Capture",
              "On: a take, everything from now until it is turned off; the strip and the file show it. Off: the last 64 bars, always kept." },
            { "captureSource", "Capture source", "Also captures the chords played in, on a track of their own in the file." },
            { "ui.drag", "Drag MIDI",
              "Drag into a track or the Finder: the capture as a MIDI file, as played. Hold Alt for one block chord per change, on the grid." },
            { "ui.resetCapture", "Reset capture",
              "Forgets the chords captured, the take included; an armed take carries on, empty. Click, then click \"sure?\" "
              "within 3 seconds." },
            { "ui.save", "Save", "Saves the capture as a .mid file (as played; Alt-click: one block chord per change)." },

            // ---- The keyboard -----------------------------------------------------------------
            { "area.keyboard", "Keyboard",
              "Click to play (lower on a key is louder). Teal: notes played, vermilion: notes made. The tinted keys are the "
              "operator keys." },
            { "ui.sticky", "Sticky",
              "On: clicked keys and triangles stay down until clicked again or Release, so a chord can be built a key at a time." },
            { "ui.keysRelease", "Release", "Lets go of every key held by a click." },
            { "ui.octave", "Computer keyboard",
              "Moves the two octaves the computer keyboard plays (the Z to M and Q to U rows) down or up." },
        };

        const Info* find (const juce::String& key)
        {
            if (key.isEmpty())
                return nullptr;
            for (const auto& i : table)
                if (key == i.key)
                    return &i;
            return nullptr;
        }
    }

    juce::String InfoView::titleFor (const juce::String& key)
    {
        const auto* i = find (key);
        return i != nullptr ? juce::String::fromUTF8 (i->title) : juce::String();
    }

    juce::String InfoView::textFor (const juce::String& key)
    {
        const auto* i = find (key);
        return i != nullptr ? juce::String::fromUTF8 (i->text) : juce::String();
    }

    bool InfoView::has (const juce::String& key) { return find (key) != nullptr; }

    juce::StringArray InfoView::keys()
    {
        juce::StringArray out;
        for (const auto& i : table)
            out.add (i.key);
        return out;
    }

    juce::String InfoView::hint() { return "Point at anything to see what it does."; }

    void InfoView::showKey (const juce::String& newKey)
    {
        const auto* i = find (newKey);
        const auto newTitle = i != nullptr ? juce::String::fromUTF8 (i->title) : juce::String();
        const auto newText = i != nullptr ? juce::String::fromUTF8 (i->text) : hint();
        key = i != nullptr ? newKey : juce::String();
        if (newTitle == title && newText == text)
            return;
        title = newTitle;
        text = newText;
        repaint();
    }

    void InfoView::paint (juce::Graphics& g)
    {
        const auto& k = Ink::of (dark);
        // A rule above, as under the header: ink on the page, not a panel.
        g.setColour (k.line.withAlpha (0.8f));
        g.fillRect (0.0f, 0.0f, (float) getWidth(), 1.0f);

        // The title in bold ink, run into the text, wrapped to two lines at most.
        juce::AttributedString s;
        s.setWordWrap (juce::AttributedString::byWord);
        s.setJustification (juce::Justification::topLeft);
        s.setLineSpacing (1.0f);
        if (title.isNotEmpty())
        {
            s.append (title + "   ", Ink::ui (13.0f, true), k.text);
            s.append (text, Ink::ui (12.5f), k.text.withAlpha (0.82f));
        }
        else
        {
            s.append (text, Ink::ui (12.5f), k.textFaint);
        }
        auto area = getLocalBounds().toFloat().reduced (14.0f, 0.0f).withTrimmedTop (6.0f).withTrimmedBottom (3.0f);
        juce::TextLayout layout;
        layout.createLayout (s, area.getWidth());
        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (area.toNearestInt());
        // One line sits in the middle of the strip; two fill it.
        const float spare = area.getHeight() - layout.getHeight();
        layout.draw (g, spare > 0.0f ? area.withTrimmedTop (spare * 0.5f) : area.withHeight (layout.getHeight()));
    }
}
