// The Info View (like Live's, and NoteCap's): a strip along the bottom of the editor that says
// what the thing under the mouse does and what its settings mean, in a bold title and a line or
// two of text; elsewhere a short hint. No panel: a rule above it, ink on the page.
//
// Every description is in one table (InfoView.cpp), keyed by the component ID of what is
// pointed at: a parameter's ID for its control (HudControl sets it), "ui.…" for the buttons
// that aren't parameters, "area.…" for the lattice, word bar, capture strip, keyboard and
// header. The editor finds the key under the mouse (TonnetzEditor::infoKeyAt) and shows it.
#pragma once

#include "Ink.h"

namespace tonnetz::ui
{
    class InfoView final : public juce::Component
    {
    public:
        static constexpr int stripHeight = 46;

        explicit InfoView (const bool& darkTheme) : dark (darkTheme) {}

        // The entry for a key (empty if there is none).
        static juce::String titleFor (const juce::String& key);
        static juce::String textFor (const juce::String& key);
        static bool has (const juce::String& key);
        // Every key in the table (for tests).
        static juce::StringArray keys();
        static juce::String hint();

        // Shows the key's entry, or the hint for an empty or unknown key.
        void showKey (const juce::String& key);
        const juce::String& shownKey() const noexcept { return key; }
        const juce::String& shownTitle() const noexcept { return title; }
        const juce::String& shownText() const noexcept { return text; }

        void paint (juce::Graphics&) override;

    private:
        const bool& dark;
        juce::String key, title, text { hint() };
    };
}
