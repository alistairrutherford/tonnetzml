// The capture strip (PLAN.md §5, §3.5; Phase 5), between the lattice and the keyboard: the
// capture (the take while Capture is armed or kept, else the last 64 bars; CaptureModel.h) as
// one small glyph per chord, its lattice shape in its ink (generated vermilion, source teal),
// named underneath, oldest on the left, scrolling left as chords arrive.
//
//   ⦿ capture / + source   the `capture` and `captureSource` parameters (HUD toggles)
//   a glyph                click: that chord, as it was voiced, becomes the source (held by the
//                          lattice, as a shift-clicked set: Release or a click on the lattice
//                          lets it go); click it again to let go. The wheel scrolls back
//   ⇩ drag MIDI            drag it out (into Live, or the Finder): the capture as a .mid
//                          (As played; with Alt, Chords). `performExternalDragDropOfFiles`
//   save…                  a file chooser (Alt-click: Chords)
#pragma once

#include "EditorNotes.h"
#include "Hud.h"
#include "LatticeView.h"

namespace tonnetz::ui
{
    class CaptureStrip final : public juce::Component
    {
    public:
        static constexpr int stripHeight = 46;
        static constexpr float glyphWidth = 40.0f;

        CaptureStrip (TonnetzProcessor&, const LatticeModel&, LatticeView&, EditorNotes&);

        // The capture or a parameter changed (cheap when nothing did).
        void refresh();
        void controls (juce::Array<HudControl*>& out) { out.add (&captureToggle); out.add (&sourceToggle); }

        // ---- For tests -----------------------------------------------------------------------
        int glyphCount() const noexcept { return (int) chords.size(); }
        const CapturedChord& glyph (int i) const { return chords[(size_t) i]; }
        juce::String glyphName (int i) const { return names[(size_t) i]; }
        juce::Rectangle<float> glyphBounds (int i) const;   // empty if scrolled out of sight
        void clickGlyph (int i);                            // what a click does
        int recalledGlyph() const noexcept { return recalled; }
        // What dragging the handle does, up to the drag itself (`performDrag` false: just the
        // file). Returns the file written, or an empty File.
        juce::File dragOut (bool chords, bool performDrag = true);
        juce::Component& dragHandle() noexcept { return handle; }
        juce::Component& saveButton() noexcept { return save; }

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    private:
        class Handle final : public juce::Component, public juce::SettableTooltipClient
        {
        public:
            explicit Handle (CaptureStrip& s);
            void paint (juce::Graphics&) override;
            void mouseDrag (const juce::MouseEvent&) override;
            void mouseUp (const juce::MouseEvent&) override { dragging = false; repaint(); }
            void mouseEnter (const juce::MouseEvent&) override { repaint(); }
            void mouseExit (const juce::MouseEvent&) override { repaint(); }
            bool dragging = false;

        private:
            CaptureStrip& strip;
        };

        const Ink& ink() const { return Ink::of (processor.editorView().dark); }
        juce::Rectangle<float> glyphArea() const;
        int glyphAt (juce::Point<float>) const;
        void drawGlyph (juce::Graphics&, const CapturedChord&, juce::Rectangle<float>, bool lit) const;
        void saveAs (bool chords);
        juce::String summary() const;

        TonnetzProcessor& processor;
        const LatticeModel& model;
        LatticeView& lattice;
        EditorNotes& notes;
        HudToggle captureToggle, sourceToggle;
        Handle handle;
        HudButton save;
        std::unique_ptr<juce::FileChooser> chooser;

        std::vector<CapturedChord> chords;
        std::vector<juce::String> names;
        uint32_t builtRevision = 0xffffffff;
        bool builtShowingTake = false, builtArmed = false;
        float scroll = 0.0f;                 // glyphs scrolled back (pixels)
        int recalled = -1;                   // the glyph whose chord the lattice holds
        double recalledStart = -1.0;
    };
}
