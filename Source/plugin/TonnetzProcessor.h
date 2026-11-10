// TonnetzML's processor. Phase 0 (PLAN.md §6): MIDI in goes to MIDI out unchanged, the audio
// output is silent, and the test rig (§7.3) can play keys from a file as host MIDI.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../core/engine/MidiEvents.h"
#include "../core/engine/SnapshotExchange.h"
#include "../core/engine/TestRig.h"
#include "../core/engine/Transport.h"
#include <atomic>

namespace tonnetz
{
    namespace params
    {
        inline constexpr const char* enable = "enable";
        inline constexpr const char* testRig = "testRig";

        juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    }

    class TonnetzProcessor final : public juce::AudioProcessor,
                                   private juce::Timer
    {
    public:
        TonnetzProcessor();
        ~TonnetzProcessor() override;

        void prepareToPlay (double sampleRate, int samplesPerBlock) override;
        void releaseResources() override;
        void reset() override;
        bool isBusesLayoutSupported (const BusesLayout&) const override;
        void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
        void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

        juce::AudioProcessorEditor* createEditor() override;
        bool hasEditor() const override { return true; }

        const juce::String getName() const override { return JucePlugin_Name; }
        bool acceptsMidi() const override { return true; }
        bool producesMidi() const override { return true; }
        bool isMidiEffect() const override { return false; }
        double getTailLengthSeconds() const override { return 0.0; }

        int getNumPrograms() override { return 1; }
        int getCurrentProgram() override { return 0; }
        void setCurrentProgram (int) override {}
        const juce::String getProgramName (int) override { return {}; }
        void changeProgramName (int, const juce::String&) override {}

        void getStateInformation (juce::MemoryBlock&) override;
        void setStateInformation (const void*, int) override;

        juce::AudioProcessorValueTreeState& getState() noexcept { return state; }

        //======================================================================
        // The test rig (PLAN.md §7.3). Message thread.

        // ~/Music/TonnetzML/TestRig: Keys.txt is read from it, Status.txt written to it.
        static juce::File defaultRigFolder();
        juce::File getRigFolder() const { return rigFolder; }
        void setRigFolder (const juce::File& f) { rigFolder = f; rigKeysStamp = {}; statusWritten = {}; }   // for tests

        // What the timer does 20 times a second: with testRig on, loads Keys.txt if it changed
        // and writes Status.txt if what it says changed. Public so tests needn't run a message loop.
        void pollTestRig();

    private:
        static BusesProperties makeBuses();
        void timerCallback() override { pollTestRig(); }
        void run (juce::AudioBuffer<float>&, juce::MidiBuffer&, bool bypassed);
        Transport readTransport();

        juce::AudioProcessorValueTreeState state;
        std::atomic<float>* enableParam = nullptr;
        std::atomic<float>* testRigParam = nullptr;

        // Audio thread.
        RigPlayer rig;
        BlockEvents rigEvents;
        juce::MidiBuffer merged;            // sized in prepareToPlay
        Transport lastTransport;
        std::atomic<bool> resetRequested { false };
        std::atomic<double> preparedSampleRate { 44100.0 };

        // Message thread → audio thread.
        SnapshotExchange<RigKeys> rigKeysForAudio;

        // Audio thread → message thread, for Status.txt.
        std::atomic<bool> statPlaying { false };
        std::atomic<int> statHeld { 0 };
        std::atomic<uint32_t> statNoteOns { 0 }, statNoteOffs { 0 };

        // Message thread.
        juce::File rigFolder { defaultRigFolder() };
        struct FileStamp
        {
            juce::int64 modified = 0, size = -1;
            bool operator== (const FileStamp& o) const noexcept { return modified == o.modified && size == o.size; }
        };
        FileStamp rigKeysStamp;
        int rigKeyCount = 0, rigRejected = 0, rigDropped = 0;
        bool rigKeysFound = false, rigLoaded = false;
        juce::String statusWritten;
        juce::uint32 statusWrittenAt = 0;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TonnetzProcessor)
    };
}
