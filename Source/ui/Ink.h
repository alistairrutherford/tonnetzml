// The editor's colours and type (PLAN.md §5): two inks, teal for the source chord and vermilion
// for the generated one, on a light "paper" ground with ink-weight lines, or on a dark ground.
// Nothing here comes from the sibling plug-ins (no panels, no amber accent): the page is the
// lattice, drawn as if with a pen.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace tonnetz::ui
{
    struct Ink
    {
        bool dark = false;

        juce::Colour ground;        // the page
        juce::Colour groundEdge;    // the page's darker edge (a soft vignette)
        juce::Colour line;          // lattice lines, ink at its usual weight
        juce::Colour lineFaint;     // the far plane, hairlines
        juce::Colour text;          // labels in ink
        juce::Colour textFaint;     // captions, hints
        juce::Colour node;          // a node disc's fill
        juce::Colour wash;          // behind floating text, so it reads over the lines
        juce::Colour source;        // teal
        juce::Colour generated;     // vermilion
        juce::Colour onInk;         // text on a filled disc of either ink
        juce::Colour band;          // the key's band
        juce::Colour bandStrong;    // its compact copy and the tonic mark
        juce::Colour ghost;         // a neighbour one op away
        juce::Colour warn;          // refused text

        static Ink paper()
        {
            Ink k;
            k.dark = false;
            k.ground = juce::Colour (0xfff3eee2);
            k.groundEdge = juce::Colour (0xffe6dfcd);
            k.line = juce::Colour (0xff2a2724);
            k.lineFaint = juce::Colour (0x552a2724);
            k.text = juce::Colour (0xff221f1c);
            k.textFaint = juce::Colour (0xff7d756a);
            k.node = juce::Colour (0xfffbf8f1);
            k.wash = juce::Colour (0xd9f3eee2);
            k.source = juce::Colour (0xff0e7c7b);
            k.generated = juce::Colour (0xffd2452a);
            k.onInk = juce::Colour (0xfffbf8f1);
            k.band = juce::Colour (0x40d4a73e);
            k.bandStrong = juce::Colour (0xffb98a1f);
            k.ghost = juce::Colour (0xff5d564d);
            k.warn = juce::Colour (0xffb0301c);
            return k;
        }

        static Ink night()
        {
            Ink k;
            k.dark = true;
            k.ground = juce::Colour (0xff15191c);
            k.groundEdge = juce::Colour (0xff0d1012);
            k.line = juce::Colour (0xffcfc8b6);
            k.lineFaint = juce::Colour (0x40cfc8b6);
            k.text = juce::Colour (0xffe9e3d4);
            k.textFaint = juce::Colour (0xff8c8778);
            k.node = juce::Colour (0xff1d2226);
            k.wash = juce::Colour (0xd015191c);
            k.source = juce::Colour (0xff3cbcb2);
            k.generated = juce::Colour (0xffff6b48);
            k.onInk = juce::Colour (0xff101315);
            k.band = juce::Colour (0x38c9a14a);
            k.bandStrong = juce::Colour (0xffd9b25a);
            k.ghost = juce::Colour (0xffb3ad9e);
            k.warn = juce::Colour (0xffff7a5c);
            return k;
        }

        static const Ink& of (bool isDark)
        {
            static const Ink p = paper(), n = night();
            return isDark ? n : p;
        }

        // Type: a humanist sans for the interface, an old-style serif for note names and the
        // title (both ship with macOS).
        // Neither has ♭ ♯ or the interface's symbols (● ○ → ─ ► ⌫ ⌂ ☾ ◉ ↺ ⌨): those come from
        // Apple Symbols, which has them all, rather than whatever the system would pick.
        static juce::FontOptions withSymbols (juce::FontOptions o)
        {
            return o.withFallbacks ({ "Apple Symbols", "Menlo" }).withFallbackEnabled (true);
        }
        static juce::Font ui (float size, bool bold = false)
        {
            return juce::Font (withSymbols (juce::FontOptions ("Avenir Next", size, bold ? juce::Font::bold : juce::Font::plain)));
        }
        static juce::Font note (float size, bool bold = false)
        {
            return juce::Font (withSymbols (juce::FontOptions ("Georgia", size, bold ? juce::Font::bold : juce::Font::plain)));
        }
        static juce::Font title (float size)
        {
            return juce::Font (withSymbols (juce::FontOptions ("Georgia", size, juce::Font::italic)));
        }
    };
}
