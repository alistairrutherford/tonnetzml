// TonnetzML's processor. Phases 2-4 (PLAN.md §6): Mirror, Walk, Orbit, the key, and the lattice
// editor's commands (EditorCommands.h: its notes join the host's input). The host's notes
// (and the test rig's keys, §7.3, as if they came from the host) go through the LatticeEngine:
// every chord comes out as W(chord), W the slot's word (Mirror), the word the operator keys walk
// (Walk, Explore) or a cycle stepped on the host's grid (Orbit). The key listener hears the
// source notes. Other MIDI goes through. The audio output is silent.
//
// The eight slot words are plug-in state, not parameters (§3.7): kept on the message thread,
// saved with the set, handed to the audio thread through a SnapshotExchange. So are the current
// W (Walk's word and Orbit's step) and Latch's chord, which the audio thread owns: it publishes
// them every block, and a restored state goes back to it as a request.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../core/engine/EditorCommands.h"
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
        // Phase 3 (appended).
        inline constexpr const char* mode = "mode";
        inline constexpr const char* latch = "latch";
        inline constexpr const char* walkInput = "walkInput";
        inline constexpr const char* orbitCycle = "orbitCycle";
        inline constexpr const char* orbitRate = "orbitRate";
        inline constexpr const char* opZoneStart = "opZoneStart";
        inline constexpr const char* opZoneOn = "opZoneOn";
        inline constexpr const char* key = "key";
        inline constexpr const char* keyTonic = "keyTonic";
        inline constexpr const char* keyMinor = "keyMinor";
        inline constexpr const char* inKey = "inKey";

        juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    }

    // The eight slot words (A–H).
    struct SlotWords
    {
        std::array<Word, numSlots> words {};
    };

    // The factory words (LatticeEngine.h).
    SlotWords factorySlotWords();

    // What the audio thread shows the message thread every block (Status.txt, the state, the editor).
    struct EngineStatus
    {
        WalkState walk;                    // Walk's W, Orbit's step, the latched chord
        Word current;                      // W as it stands (Orbit: its first 32 ops)
        int currentOps = 0;                // how many ops W has in all
        uint64_t keyGuess = 0;             // KeyGuess::pack()
        int keyTonic = -1;                 // the key in force (KeyRef), -1 = none
        bool keyMinor = false;

        // Phase 4, for the editor: what sounds, note by note (velocity, 0 = silent), the chord
        // and W(chord) with W's anchor (LatticeEngine::sourceChord, generatedChord), and the
        // engine's release count (it forgot every key: the editor forgets the ones it holds).
        std::array<uint8_t, 128> sourceVelocity {}, generatedVelocity {};
        Chord source, generated;
        uint32_t releases = 0;
    };

    // The editor's view (PLAN.md §5): kept by the processor so it outlives the editor window.
    // Message thread. (Saved with the set in Phase 6, with the camera.)
    struct EditorView
    {
        bool dark = false;                 // the paper theme by default
        bool follow = true;                // the camera eases to keep the chords in view
        bool sticky = true;                // mouse-played keys and triangles stay down until released
        bool showSettings = true;          // the settings column on the lattice
        double zoom = 1.0;                 // times the size-dependent default spacing
        double centreA = 0.75, centreB = 0.5;   // the camera, in lattice coordinates
        int computerOctave = 5;            // the computer keyboard's lower C: MIDI 12 * this (60 = C3 in Live)
    };

    // A restored W and latched chord for the audio thread.
    struct WalkRestore
    {
        uint32_t seq = 0;
        WalkState state;
    };

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
        // The engine's state (PLAN.md §3.7: the current W and Latch's chord). Message thread (or
        // whichever thread asks for the state: locked).

        // What the audio thread last published, or a restore it hasn't taken yet.
        WalkState getWalkState() const;
        // Hands W, Orbit's step and the latched chord to the audio thread.
        void setWalkState (const WalkState&);
        EngineStatus getEngineStatus() const;

        //======================================================================
        // The editor (Phase 4). Message thread only (one producer).

        // Queues a command for the audio thread (EditorCommands.h): false if the queue is full.
        bool sendEditorCommand (const EditorCommand& c) noexcept { return editorCommands.push (c); }
        EditorView& editorView() noexcept { return view; }

        //======================================================================
        // The test rig (PLAN.md §7.3). Message thread.

        // ~/Music/TonnetzML/TestRig: Keys.txt is read from it, Status.txt written to it.
        static juce::File defaultRigFolder();
        juce::File getRigFolder() const { return rigFolder; }
        void setRigFolder (const juce::File& f) { rigFolder = f; rigKeysStamp = {}; statusWritten = {}; }   // for tests

        // What the timer does 20 times a second: with testRig on, loads Keys.txt if it changed
        // and writes Status.txt if what it says changed. Public so tests needn't run a message loop.
        void pollTestRig();

        // Also on the timer: Key going from Listen to Hold puts the guess into keyTonic and
        // keyMinor (PLAN.md §3.6). Public for the same reason.
        void followKeyMode();

    private:
        static BusesProperties makeBuses();
        void timerCallback() override { followKeyMode(); pollTestRig(); }
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
        std::atomic<float>* modeParam = nullptr;
        std::atomic<float>* latchParam = nullptr;
        std::atomic<float>* walkInputParam = nullptr;
        std::atomic<float>* orbitCycleParam = nullptr;
        std::atomic<float>* orbitRateParam = nullptr;
        std::atomic<float>* opZoneStartParam = nullptr;
        std::atomic<float>* opZoneOnParam = nullptr;
        std::atomic<float>* keyParam = nullptr;
        std::atomic<float>* keyTonicParam = nullptr;
        std::atomic<float>* keyMinorParam = nullptr;
        std::atomic<float>* inKeyParam = nullptr;
        uint32_t restoredSeq = 0;           // audio thread: the last WalkRestore taken
        EngineStatus status;                // audio thread: filled every block
        Transport lastTransport;
        std::atomic<bool> resetRequested { false };
        std::atomic<double> preparedSampleRate { 44100.0 };

        // Message thread → audio thread.
        EditorCommandFifo editorCommands;
        EditorView view;
        SnapshotExchange<RigKeys> rigKeysForAudio;
        SnapshotExchange<SlotWords> slotWordsForAudio;
        SnapshotExchange<WalkRestore> walkForAudio;
        std::atomic<uint32_t> walkTaken { 0 };     // the last WalkRestore the audio thread took

        // Audio thread → message thread.
        mutable SnapshotExchange<EngineStatus> statusForMessage;
        mutable juce::CriticalSection statusLock;  // one reader at a time
        mutable EngineStatus lastStatus;
        WalkRestore pendingWalk;                    // the last restore handed over (statusLock)
        int lastKeyMode = -1;                       // followKeyMode

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
