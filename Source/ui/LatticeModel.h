// What the editor shows, worked out from the engine's snapshot (PLAN.md §4: the editor reads
// snapshots, never engine state) on the message thread: the source chord and W(chord) as shapes
// on the plane, W's path between them, the ghost neighbours one op from the generated chord
// (named, with how the voices would move, and whether they stay in the key), the trail of the
// last generated chords, Orbit's cycle, the key, and the header's sentence with names spelled
// and numbered in the key (§3.6).
//
// Shapes are embedded near the shape before them (Embedding.h), so a walk is drawn as a path
// across the plane and the camera can follow it.
#pragma once

#include "../plugin/TonnetzProcessor.h"
#include "../core/lattice/Embedding.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace tonnetz::ui
{
    struct ChordView
    {
        bool present = false;
        Chord chord;                   // with the engine's anchor and parity
        Embedding shape;
        int root = -1;
        juce::String name;             // spelled in the key ("A♭maj7", "C/E")
        juce::String numeral;          // in the key, or empty
        double a = 0.0, b = 0.0;       // the shape's centre, lattice coordinates
    };

    struct GhostView
    {
        int key = 0;                   // operator key 0..9 (P L R N S H +5 -5 +M3 -M3)
        bool available = false;
        Chord result;
        Embedding shape;
        double a = 0.0, b = 0.0;       // where its chip goes (lattice coordinates)
        bool inKey = false;            // its result lies in the key in force
        bool faint = false;            // In key is on and it doesn't
        juce::String label;            // "P", "+5"
        juce::String description;      // "L → E minor, 1 voice moves 1 semitone"
    };

    struct TrailPoint
    {
        double a = 0.0, b = 0.0;
        PcMask mask = 0;
    };

    class LatticeModel
    {
    public:
        struct Inputs
        {
            EngineStatus status;
            int mode = 0;                  // Mode
            Word slotWord;                 // the slot's word (Mirror's W, Orbit's "Slot" cycle)
            int orbitCycle = 0;            // OrbitCycle
            bool inKey = false;
            int keyMode = 1;               // KeyMode
            VoicerSettings voicing;
        };

        // Takes a new snapshot; true if anything drawn changed.
        bool update (const Inputs&);

        // ---- What is drawn -----------------------------------------------------------------
        ChordView source, generated;
        std::vector<ChordView> path;       // W's chords after each op (the last is W(chord))
        std::array<GhostView, numOperatorOps> ghosts {};
        std::vector<TrailPoint> trail;     // the last generated chords, oldest first
        std::vector<TrailPoint> cycle;     // Orbit: the cycle from the source, back to it
        int cycleAt = -1;                  // where the generated chord is on it
        std::array<uint8_t, 128> sourceVelocity {}, generatedVelocity {};
        KeyRef key;                        // in force
        KeyGuess guess;
        int keyMode = 1;
        Word word;                         // W as shown (Mirror: the slot's)
        int wordOps = 0;
        int mode = 0;
        bool inKey = false;
        uint16_t inKeyBits = 0;            // opsInKey (generated, key)
        juce::String sentence;             // "Cmaj7 (I) ──P·L──► A♭maj7 (♭VI)"
        juce::String keyText;              // "C major 91%"

        // Names, for the views: a chord spelled in the key in force (C major's when there is none).
        juce::String chordName (PcMask mask, int bassPc, int* root = nullptr) const;
        juce::String longChordName (PcMask mask) const;          // "E minor", "A♭ major", else as chordName
        static int rootInPosition (PcMask mask, int hint);       // the cheapest reading's root
        static juce::String opLabel (int key);                    // "P", "+5", "−M3"
        static juce::String pretty (const std::string& text);     // # and b to ♯ and ♭ (in note names)

        static constexpr int maxTrail = 12;

    private:
        void rebuildShapes (const Inputs&);
        void rebuildGhosts (const Inputs&);
        void rebuildCycle (const Inputs&);
        void rebuildText();

        Spelling spellingNow() const;

        // What the shapes were built from.
        Chord builtSource, builtGenerated;
        Word builtWord, builtCycle;
        bool builtPresent = false;
        int builtMode = -1;
        KeyRef builtKey;
        VoicerSettings builtVoicing;
        std::array<uint8_t, 128> builtGenVel {};
        Embedding lastSourceShape;
        bool haveLastSource = false;
        juce::String lastSignature;
    };
}
