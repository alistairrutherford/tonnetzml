// TonnetzML's editor. Phase 2: still a stub (the lattice editor of PLAN.md §5 arrives in Phase 4),
// with one text field: the current slot's word. Return (or leaving the field) stores it; text
// that isn't a word is shown in red and not stored. Nothing here comes from the sibling plug-ins'
// UI.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace tonnetz
{
    class TonnetzProcessor;

    class TonnetzEditor final : public juce::AudioProcessorEditor,
                                private juce::Timer
    {
    public:
        static constexpr int minWidth = 760, minHeight = 520;

        explicit TonnetzEditor (TonnetzProcessor&);
        ~TonnetzEditor() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        // For tests: the word field, and what it says about the text.
        juce::TextEditor& wordField() noexcept { return word; }
        const juce::String& wordMessage() const noexcept { return message; }
        void commitWord();

    private:
        void timerCallback() override;
        void showSlot();

        TonnetzProcessor& processor;
        juce::TextEditor word;
        int shownSlot = -1;
        juce::String message;
        bool messageIsError = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TonnetzEditor)
    };
}
