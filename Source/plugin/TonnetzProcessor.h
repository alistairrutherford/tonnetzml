// TonnetzML's processor. Phase 2 (PLAN.md §6): Mirror. The host's notes (and the test rig's keys,
// §7.3, as if they came from the host) go through the LatticeEngine: every chord comes out as
// W(chord), W the current slot's word. Other MIDI goes through. The audio output is silent.
//
// The eight slot words are plug-in state, not parameters (§3.7): kept on the message thread,
// saved with the set, handed to the audio thread through a SnapshotExchange.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../core/engine/LatticeEngine.h"
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
        // Phase 2 (appended: hosts keep automation by ID, but the order is the host's list).
        inline constexpr const char* slot = "slot";
        inline constexpr const char* gatherMs = "gatherMs";
        inline constexpr const char* mix = "mix";
        inline constexpr const char* sourceChannel = "sourceChannel";
        inline constexpr const char* genChannel = "genChannel";
        inline constexpr const char* registerMode = "register";
        inline constexpr const char* bass = "bass";
        inline constexpr const char* octave = "octave";

        juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    }

    // The eight slot words (A–H).
    struct SlotWords
    {
        std::array<Word, numSlots> words {};
    };

    // The factory words (LatticeEngine.h).
    SlotWords factorySlotWords();

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
        // Slot words (PLAN.md §3.1, Mirror). Message thread.

        int currentSlot() const noexcept;
        Word getSlotWord (int slot) const;
        void setSlotWord (int slot, const Word& word);
        // Parses `text` into slot `slot`; false (and nothing changes) if it isn't a word.
        bool setSlotWordText (int slot, const juce::String& text);
        static juce::String slotName (int slot) { return juce::String::charToString ((juce::juce_wchar) ('A' + slot)); }

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
        BlockEvents rigEvents, engineIn, engineOut;
        LatticeEngine engine;
        EngineSettings engineSettings;
        juce::MidiBuffer merged;            // sized in prepareToPlay
        std::atomic<float>* slotParam = nullptr;
        std::atomic<float>* gatherParam = nullptr;
        std::atomic<float>* mixParam = nullptr;
        std::atomic<float>* sourceChannelParam = nullptr;
        std::atomic<float>* genChannelParam = nullptr;
        std::atomic<float>* registerParam = nullptr;
        std::atomic<float>* bassParam = nullptr;
        std::atomic<float>* octaveParam = nullptr;
        Transport lastTransport;
        std::atomic<bool> resetRequested { false };
        std::atomic<double> preparedSampleRate { 44100.0 };

        // Message thread → audio thread.
        SnapshotExchange<RigKeys> rigKeysForAudio;
        SnapshotExchange<SlotWords> slotWordsForAudio;

        // Audio thread → message thread, for Status.txt.
        std::atomic<bool> statPlaying { false };
        std::atomic<int> statHeld { 0 };
        std::atomic<uint32_t> statNoteOns { 0 }, statNoteOffs { 0 };
        std::atomic<int> statBlock { 0 }, statMaxBlock { 0 };
        std::atomic<double> statRate { 0.0 };
        std::atomic<int> statSourceMask { 0 }, statSourceLowest { -1 }, statGenMask { 0 }, statGenLowest { -1 }, statSounding { 0 };

        // Message thread (getStateInformation may come from another thread: the lock).
        SlotWords slotWords;
        juce::CriticalSection slotLock;
        void publishSlotWords();

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
