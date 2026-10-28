// The plug-in's defines for the host test app, which compiles Source/plugin and Source/ui outside
// a plug-in target (forced include, DrumChimpHost.jucer). Keep in step with DrumChimp.jucer.
#pragma once
#define JucePlugin_Name "DrumChimp"
#define JucePlugin_VersionString "0.8.0"

// The tests run the message loop while samples load (runDispatchLoopUntil).
#define JUCE_MODAL_LOOPS_PERMITTED 1
