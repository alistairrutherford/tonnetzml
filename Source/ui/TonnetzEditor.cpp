#include "TonnetzEditor.h"
#include "../plugin/TonnetzProcessor.h"

namespace tonnetz
{
    TonnetzEditor::TonnetzEditor (TonnetzProcessor& p)
        : AudioProcessorEditor (p)
    {
        setOpaque (true);
        setResizable (true, true);
        setResizeLimits (minWidth, minHeight, 2400, 1600);
        setSize (minWidth, minHeight);
    }

    void TonnetzEditor::paint (juce::Graphics& g)
    {
        g.fillAll (juce::Colour (0xfff4efe4));
        g.setColour (juce::Colour (0xff1f2a30));
        g.setFont (juce::FontOptions (34.0f));
        g.drawText ("TonnetzML", getLocalBounds().withTrimmedBottom (40), juce::Justification::centred);
        g.setFont (juce::FontOptions (14.0f));
        g.setColour (juce::Colour (0xff6b767c));
        g.drawText ("Phase 0: MIDI through. The lattice arrives in Phase 4.",
                    getLocalBounds().withTrimmedTop (50), juce::Justification::centred);
    }
}
