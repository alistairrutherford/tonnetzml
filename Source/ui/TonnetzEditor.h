// TonnetzML's editor. Phase 0: a placeholder (the name on a blank page). The lattice editor of
// PLAN.md §5 arrives in Phase 4; nothing here comes from the sibling plug-ins' UI.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace tonnetz
{
    class TonnetzProcessor;

    class TonnetzEditor final : public juce::AudioProcessorEditor
    {
    public:
        static constexpr int minWidth = 760, minHeight = 520;

        explicit TonnetzEditor (TonnetzProcessor&);

        void paint (juce::Graphics&) override;

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TonnetzEditor)
    };
}
