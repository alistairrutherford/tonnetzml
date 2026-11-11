// TonnetzML's editor (PLAN.md §5, Phase 4): the lattice is the whole window. A header line
// (name, Enable, the mode, the key, the sentence `Cmaj7 (I) ──P·L──► A♭maj7 (♭VI)`, the theme),
// the lattice (LatticeView) with the settings column floating over its top-right corner and the
// word bar over its foot, the capture strip (Phase 5) and the keyboard strip along the bottom. No panels, combo-box rows or
// sliders; nothing comes from the sibling plug-ins' UI.
//
// The editor reads the engine's snapshot (TonnetzProcessor::getEngineStatus) on a 60 Hz timer
// and writes commands (EditorCommands.h), never engine state.
#pragma once

#include "CaptureStrip.h"
#include "Hud.h"
#include "KeyboardStrip.h"
#include "LatticeModel.h"
#include "LatticeView.h"
#include "WordBar.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace tonnetz
{
    class TonnetzProcessor;

    class TonnetzEditor final : public juce::AudioProcessorEditor,
                                private juce::Timer
    {
    public:
        static constexpr int minWidth = 760, minHeight = 520;
        static constexpr int headerHeight = 38;

        explicit TonnetzEditor (TonnetzProcessor&);
        ~TonnetzEditor() override;

        void paint (juce::Graphics&) override;
        void resized() override;
        bool keyPressed (const juce::KeyPress&) override;
        bool keyStateChanged (bool isKeyDown) override;

        // For tests: take the snapshot now (what the timer does), and hold animation still.
        void refreshNow();
        void freezeAnimation (bool frozen);
        void setDark (bool dark);

        ui::LatticeView& latticeView() noexcept { return lattice; }
        ui::KeyboardStrip& keyboardStrip() noexcept { return keyboard; }
        ui::CaptureStrip& captureStrip() noexcept { return strip; }
        ui::WordBar& wordBar() noexcept { return word; }
        ui::Header& header() noexcept { return head; }
        ui::Settings& settings() noexcept { return side; }
        const ui::LatticeModel& model() const noexcept { return picture; }
        ui::EditorNotes& editorNotes() noexcept { return notes; }

        // Every parameter's control (its component ID is the parameter's ID), or nullptr.
        ui::HudControl* controlFor (const juce::String& parameterID);
        juce::Array<ui::HudControl*> controls();

    private:
        void timerCallback() override;
        void layoutFloating();
        void themeChanged();

        TonnetzProcessor& processor;
        ui::LatticeModel picture;
        ui::EditorNotes notes;
        ui::LatticeView lattice;
        ui::KeyboardStrip keyboard;
        ui::CaptureStrip strip;
        ui::WordBar word;
        ui::Header head;
        ui::Settings side;
        juce::TooltipWindow tooltips { this, 600 };
        uint32_t lastReleases = 0;
        bool haveReleases = false, constructed = false;
        int ticks = 0;
        double lastTick = 0.0;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TonnetzEditor)
    };
}
