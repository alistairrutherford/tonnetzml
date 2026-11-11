// The keyboard along the bottom (PLAN.md §5, "Keyboard strip"): 88 keys, A0..C8, scrollable
// when the window is too narrow for them all. Source notes in teal, generated in vermilion
// (a key in both carries both), the operator zone tinted with each key's op written on it.
// Click to play (lower on the key is louder); with Sticky on (the default) a clicked key stays
// down until it is clicked again or Release; the computer keyboard plays a two-octave window
// (Z..M and Q..U rows, from the octave shown; ‹ › move it).
#pragma once

#include "EditorNotes.h"
#include "Hud.h"
#include "LatticeModel.h"

namespace tonnetz::ui
{
    class KeyboardStrip final : public juce::Component
    {
    public:
        static constexpr int lowest = 21, highest = 108;     // A0..C8
        static constexpr int barHeight = 18;

        KeyboardStrip (TonnetzProcessor&, const LatticeModel&, EditorNotes&);

        void refresh();                                      // the model or a parameter changed
        void forget();                                       // the engine let go of everything

        // Keys and where they are (public for tests).
        juce::Rectangle<float> keyBounds (int note) const;
        int noteAt (juce::Point<float>) const;
        void clickKey (int note, int velocity);              // what a click does
        static juce::String keyName (int note);              // as Live names it: C3 = 60

        // The computer keyboard: true if it took the key.
        bool keyStateChanged();
        bool keyPressed (const juce::KeyPress&);

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    private:
        static bool isBlack (int note) { const int pc = note % 12; return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10; }
        float whiteWidth() const;
        const Ink& ink() const { return Ink::of (processor.editorView().dark); }
        juce::Rectangle<float> keysArea() const;

        TonnetzProcessor& processor;
        const LatticeModel& model;
        EditorNotes& notes;
        HudButton sticky, release, octaveDown, octaveUp;
        float scroll = 0.0f;                                 // pixels the keys are moved left
        int mouseNote = -1;                                  // non-sticky: the key under the button
        bool mouseDownSticky = false;
        std::array<bool, 32> computerDown {};
    };
}
