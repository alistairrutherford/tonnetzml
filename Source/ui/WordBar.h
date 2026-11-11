// The word bar (PLAN.md §5): W as a row of chips, not a menu. Which word it shows and edits
// follows the mode: Mirror, the slot's word (stored in the slot); Walk and Explore, the walk's W
// (sent to the engine); Orbit, the cycle (the slot's word when the cycle is "Slot", otherwise a
// factory cycle, read only), with the step reached marked.
//
//   +          an op palette laid out as the lattice's directions (±5 left and right, ±M3 and
//              ±m3 on the diagonals, P L R in the middle, N S H below)
//   chips      drag one to reorder; drop it off the bar (up or down) to delete it
//   ⌫ ⌂        Undo (the last op) and Home (none)
//   double-click the chips: type the word ("P L +5"); Return stores it, Escape leaves it
//   slots      A–H (the slot parameter), a dot on slots holding a word
#pragma once

#include "Hud.h"
#include "LatticeModel.h"

namespace tonnetz::ui
{
    class OpPalette;

    class WordBar final : public juce::Component
    {
    public:
        WordBar (TonnetzProcessor&, const LatticeModel&);
        ~WordBar() override;

        void refresh();
        // The word shown and whether it can be edited here.
        Word shownWord() const;
        bool editable() const;
        // Edits (what the chips, the palette and the keys do; public for tests).
        void setWord (const Word&);
        void appendOp (const Op&);
        void undo();
        void home();
        void moveChip (int from, int to);
        void deleteChip (int index);
        void beginTextEntry();
        bool commitText();                                   // false: not a word (shown in red)
        juce::TextEditor& textField() noexcept { return text; }
        const juce::String& message() const noexcept { return note; }
        void openPalette();
        OpPalette* palette() noexcept { return paletteView.get(); }
        juce::Rectangle<float> chipBounds (int index) const;
        HudChoices& slotChoices() noexcept { return slots; }
        int preferredHeight() const { return 46; }

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;

    private:
        const Ink& ink() const { return Ink::of (processor.editorView().dark); }
        int mode() const;
        int chipAt (juce::Point<float>) const;
        juce::Rectangle<float> chipsArea() const;

        TonnetzProcessor& processor;
        const LatticeModel& model;
        HudChoices slots;
        HudButton plus, undoButton, homeButton;
        juce::TextEditor text;
        std::unique_ptr<OpPalette> paletteView;
        juce::String note;
        bool noteIsError = false;

        // A word sent to the engine that its snapshot hasn't shown yet (Walk).
        Word pending;
        bool havePending = false;
        juce::uint32 pendingAt = 0;

        // Dragging a chip.
        int dragging = -1, dropAt = -1;
        bool dropOff = false;
        juce::Point<float> dragPos;
    };

    // The op palette: a floating rosette of ops in the lattice's directions.
    class OpPalette final : public juce::Component
    {
    public:
        OpPalette (const EditorView& v, std::function<void (const Op&)> onPick);
        void paint (juce::Graphics&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
        void mouseUp (const juce::MouseEvent&) override;
        static constexpr int size = 176;
        juce::Point<float> centreOf (int index) const;
        void pick (int index);
        int count() const { return (int) items.size(); }

    private:
        struct Item { Op op; juce::String label; float dx, dy; };
        int itemAt (juce::Point<float>) const;
        const EditorView& view;
        std::function<void (const Op&)> picked;
        std::vector<Item> items;
        int hover = -1;
    };
}
