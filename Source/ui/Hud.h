// The controls that float over the lattice (PLAN.md §5): every host parameter has one, as a
// HUD toggle, a click-to-cycle label or a row of chips, never a panel, combo box or slider.
//
//   HudToggle    "● latch" / "○ latch": click flips it
//   HudCycle     "mix Generated": click for the next value (shift-click the one before), the
//                wheel steps, right-click lists them all; a number can also be dragged up and down
//                and double-clicked back to its default
//   HudChoices   every value in a row ("Mirror Walk Orbit Explore", "A B C … H"), the current one
//                in ink: click one
//   KeyBadge     the key (§3.6): "◉ C major 91%" while listening, hollow while tentative, "◼"
//                held; click cycles Listen / Hold / Off; right-click sets the key by hand and
//                toggles In key
//
// Each control's component ID is its parameter's ID (the snapshot test finds them so).
// `Header` is the top line: name, Enable, the mode, the key, the sentence, the theme. `Settings`
// is the column of the other parameters, right-aligned over the lattice's top-right corner.
#pragma once

#include "Ink.h"
#include "LatticeModel.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>

namespace tonnetz::ui
{
    class HudControl : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        HudControl (juce::RangedAudioParameter& p, const EditorView& v, juce::String captionText);

        virtual juce::String valueText() const = 0;
        // What a click does (`backwards`: shift-click).
        virtual void activate (bool backwards) = 0;
        virtual void showMenu();

        // Re-reads the parameter: repaints if what it shows changed.
        void refresh();
        virtual int preferredWidth() const;

        juce::RangedAudioParameter& parameter() noexcept { return param; }
        float plain() const;
        void setPlain (float value);

        void paint (juce::Graphics&) override;
        void mouseEnter (const juce::MouseEvent&) override { repaint(); }
        void mouseExit (const juce::MouseEvent&) override { repaint(); }
        void mouseUp (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    protected:
        const Ink& ink() const { return Ink::of (view.dark); }
        juce::Font font() const { return Ink::ui (14.5f); }
        juce::Font captionFont() const { return Ink::ui (13.0f); }
        float captionWidth() const;
        void step (int delta, bool wrap);

        juce::RangedAudioParameter& param;
        const EditorView& view;
        juce::String caption;
        juce::String shown;
        float wheelAccumulator = 0.0f;
    };

    class HudToggle final : public HudControl
    {
    public:
        HudToggle (juce::RangedAudioParameter& p, const EditorView& v, juce::String caption, juce::String onText, juce::String offText = {});
        juce::String valueText() const override;
        void activate (bool) override { setPlain (plain() > 0.5f ? 0.0f : 1.0f); }
        void showMenu() override { activate (false); }

    private:
        juce::String on, off;
    };

    class HudCycle final : public HudControl
    {
    public:
        // `clickStep`: how far a click moves a number (choices always move by one).
        HudCycle (juce::RangedAudioParameter& p, const EditorView& v, juce::String caption, juce::String suffix = {}, int clickStep = 1);
        juce::String valueText() const override;
        void activate (bool backwards) override;
        void showMenu() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;

    private:
        bool isNumber() const;
        juce::String suffix;
        int clickStep = 1;
        float dragStartValue = 0.0f;
        bool dragged = false;
    };

    class HudChoices final : public HudControl
    {
    public:
        HudChoices (juce::RangedAudioParameter& p, const EditorView& v, juce::String caption, juce::StringArray labels, float gap = 9.0f);
        juce::String valueText() const override;
        void activate (bool backwards) override { step (backwards ? -1 : 1, true); }
        int preferredWidth() const override;
        void paint (juce::Graphics&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent& e) override { hover = optionAt (e.x); repaint(); }
        void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
        juce::Rectangle<float> optionBounds (int index) const;
        int optionAt (int x) const;
        // Marks options (slots holding a word, say); drawn with a dot.
        std::function<bool (int)> marked;

    private:
        juce::StringArray labels;
        float gap;
        int hover = -1;
    };

    class KeyBadge final : public HudControl
    {
    public:
        KeyBadge (juce::RangedAudioParameter& keyMode, juce::RangedAudioParameter& tonic, juce::RangedAudioParameter& minor,
                  juce::RangedAudioParameter& inKey, const EditorView& v, const LatticeModel& m);
        juce::String valueText() const override;
        void activate (bool backwards) override;
        void showMenu() override;
        void paint (juce::Graphics&) override;
        int preferredWidth() const override;

    private:
        juce::String glyph() const;
        juce::RangedAudioParameter &tonicParam, &minorParam, &inKeyParam;
        const LatticeModel& model;
    };

    // A clickable word or glyph that isn't a parameter (the theme, Release, ‹ ›, ⌫, reset).
    // With a confirm text (`setConfirm`) a click only asks: the button shows that text in
    // vermilion, and a second click within 3 seconds acts (the resets).
    class HudButton final : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
    {
    public:
        HudButton (const EditorView& v, juce::String text, std::function<void()> onClick, float size = 13.0f);
        void setText (const juce::String& t) { if (t != text) { text = t; repaint(); } }
        void setLit (bool on) { if (on != lit) { lit = on; repaint(); } }
        void setConfirm (juce::String askText) { confirmText = std::move (askText); }
        bool isAsking() const noexcept { return asking; }
        int preferredWidth() const;
        void paint (juce::Graphics&) override;
        void mouseEnter (const juce::MouseEvent&) override { repaint(); }
        void mouseExit (const juce::MouseEvent&) override { repaint(); }
        void mouseUp (const juce::MouseEvent& e) override { if (getLocalBounds().contains (e.getPosition())) press(); }
        // What a click does (tests call it).
        void press();
        std::function<void()> click;

    private:
        void timerCallback() override { stopTimer(); asking = false; repaint(); }
        const EditorView& view;
        juce::String text, confirmText;
        float size;
        bool lit = false, asking = false;
    };

    //==========================================================================
    class Header final : public juce::Component
    {
    public:
        Header (TonnetzProcessor&, const LatticeModel&, std::function<void()> onTheme);
        void refresh();
        void paint (juce::Graphics&) override;
        void resized() override;
        void controls (juce::Array<HudControl*>& out) { out.add (&enable); out.add (&mode); out.add (&key); }

    private:
        TonnetzProcessor& processor;
        const LatticeModel& model;
        HudToggle enable;
        HudChoices mode;
        KeyBadge key;
        HudButton theme;
        juce::Rectangle<int> sentenceArea;
        juce::String sentence;
    };

    // The settings panel (after 1.0.1): the parameters the header and word bar don't show, as a
    // panel of its own docked to the right of the lattice, between the header and the capture
    // strip. A title line ("settings", reset, the fold ›), then one line per group: a caption on
    // the left, its controls after it. Folded, it is a slim edge with ‹ to open it again.
    class Settings final : public juce::Component
    {
    public:
        Settings (TonnetzProcessor&, std::function<void()> onLayoutChange);
        void refresh();
        void paint (juce::Graphics&) override;
        void resized() override;
        // The panel's width for its contents (folded: the slim edge).
        int preferredWidth() const;
        void controls (juce::Array<HudControl*>& out);
        // The Info View's key for a point on the panel itself (a caption, a gap, the title).
        juce::String infoKeyAt (juce::Point<int>) const;
        HudButton& resetButton() noexcept { return reset; }
        HudButton& foldButton() noexcept { return fold; }

        static constexpr int foldedWidth = 26;

    private:
        struct Line
        {
            juce::String caption;
            juce::Array<HudControl*> items;
        };
        TonnetzProcessor& processor;
        std::function<void()> layoutChanged;
        juce::OwnedArray<HudControl> owned;
        juce::Array<Line> lines;
        HudButton fold, reset;
        int rowHeight() const;
        juce::Rectangle<int> content() const { return getLocalBounds().withTrimmedLeft (1).reduced (12, 8); }
        static constexpr int maxRow = 24, minRow = 18, captionWidth = 66, gap = 10;
    };
}
