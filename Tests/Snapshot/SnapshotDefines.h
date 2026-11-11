// The plug-in's defines for the snapshot app, which compiles Source/plugin and Source/ui outside
// a plug-in target (forced include, TonnetzMLSnapshot.jucer). Keep in step with TonnetzML.jucer.
#pragma once
#define JucePlugin_Name "TonnetzML"
#define JucePlugin_VersionString "1.0.0"

// The test runs the message loop between blocks (runDispatchLoopUntil).
#define JUCE_MODAL_LOOPS_PERMITTED 1
