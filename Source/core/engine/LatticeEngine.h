// The block processor (PLAN.md §4): MIDI in → ZoneRouter → ChordGatherer → W(chord) → Voicer →
// VoiceTracker → MIDI out, with the operator keys editing W, Orbit's clock stepping it, and the
// key listener hearing the source notes.
//
//   source notes   gathered into a chord (ChordGatherer.h: the gather window); when the window
//                  closes the chord's keys are voiced under W (Voicer.h). Each output voice is
//                  bound to the key it is the image of: releasing a key releases its image and
//                  nothing else, velocities carry across, and a re-evaluation (a late note, or W
//                  or the voicing settings changing while the chord sounds) retunes only the
//                  voices whose pitch changed (note-off, note-on), on the sample it happens.
//   W, by mode     Mirror: the slot's word. Walk and Explore: the engine's own word, edited by
//                  the operator keys (WordEditor.h) and the editor's ghost clicks and word bar.
//                  Orbit: a cycle word (PL, PR, LR, PLR or the slot's word) stepped one op every
//                  Rate (OrbitClock.h) while a chord sounds: W is the cycle's first k ops, k
//                  counted modulo the cycle's length on the chord (Cycles.h), so it runs for ever.
//   new chord      a source key going down with none down. Walk input Reset then clears W
//                  (Walk, Explore) or restarts the cycle (Orbit); Relative keeps them.
//   operator keys  (the zone, ZoneRouter.h) never sound. In Walk and Explore each edits W; in
//                  Orbit Home restarts the cycle and Undo steps it back; in Mirror they do nothing.
//                  A change made on a sample takes effect after everything else on that sample,
//                  once, so keys pressed together give one retune.
//   Orbit steps    a grid line steps the cycle if a chord was sounding just before it (window
//                  closed); the retune comes after the line's other input. So a chord held
//                  across the line moves on it; a new chord played on the line comes in at the
//                  step the old one reached; a first chord played on a line starts at W = the
//                  cycle's first k ops and steps a Rate later. With *In key* and a key, a step
//                  goes on through the cycle past chords outside the key (at most one lap).
//                  While stopped Orbit runs on the internal clock, restarted on each new chord.
//   Latch          a key let go stays in the chord until a new chord replaces it (the latched
//                  keys go when the new chord's window closes) or latch goes off. Stop, Enable
//                  off and releaseAll end everything but keep the latched chord, silent; it
//                  sounds again when the transport starts (or Enable comes back while playing).
//   key            Listen: the source notes (only: never generated notes, never operator keys)
//                  go to ChordMonkey's KeyListener, in every mode; Hold freezes its guess (the
//                  key is then `keyTonic` / `keyMinor`); Off forgets it. Leaving Hold or Off for
//                  Listen starts a fresh analysis.
//   Mix            Generated only / Source + Generated / Source only. Source notes go out at
//                  once (not delayed by the window) on `sourceChannel`, generated notes on
//                  `genChannel`. A pitch both on one channel sounds once (VoiceTracker counts
//                  its owners).
//   anything else  (controllers, pitch bend, program changes) goes through untouched. All
//                  Notes Off / All Sound Off releases everything first.
//   Enable off     MIDI through untouched, except that a duplicate note-on or a note-off with no
//                  note-on is dropped, so that switching Enable either way leaves nothing
//                  stuck: everything the engine sounded ends when Enable goes off, and every
//                  note passed through ends when it comes back on.
//   stop           the transport stopping (and `releaseAll`: bypass, reset, deleting the device)
//                  ends everything sounding and forgets the keys down; their note-offs, when
//                  they come, are ignored.
//   Mix, channel or mode changes end what sounds and sound the chord again under the new
//   settings.
//   the editor     (Phase 4) its notes come in as input like the host's (the processor puts them
//                  there); its ghost-neighbour clicks (`editorOperator`) and word-bar edits of
//                  Walk's W (`editorSetWalk`) are taken at the start of the next block, on its
//                  first sample, as an operator key would be. What it draws comes from
//                  `sourceChord`, `generatedChord` and `forEachGenerated`.
//
// Events come out in sample order; on one sample, in the order they happened. Plain C++, no JUCE, no
// allocation, no locks: the whole engine is one fixed-size object.
#pragma once

#include "ChordGatherer.h"
#include "Clock.h"
#include "KeyListener.h"
#include "MidiEvents.h"
#include "OrbitClock.h"
#include "Transport.h"
#include "Voicer.h"
#include "VoiceTracker.h"
#include "WordEditor.h"
#include "ZoneRouter.h"
#include "../lattice/Cycles.h"
#include <array>
#include <cstdint>

namespace tonnetz
{
    enum class Mix : uint8_t { Generated, SourceAndGenerated, SourceOnly };
    enum class Mode : uint8_t { Mirror, Walk, Orbit, Explore };
    enum class WalkInput : uint8_t { Relative, Reset };
    enum class OrbitCycle : uint8_t { PL, PR, LR, PLR, Slot };
    enum class KeyMode : uint8_t { Off, Listen, Hold };

    constexpr const char* modeNames[4] = { "Mirror", "Walk", "Orbit", "Explore" };
    constexpr const char* orbitCycleNames[5] = { "PL", "PR", "LR", "PLR", "Slot" };
    constexpr const char* keyModeNames[3] = { "Off", "Listen", "Hold" };

    // Mirror's eight slots (A-H) and their factory words: A P, B L, C R, D N, E PL, F PR, G LR, H S.
    constexpr int numSlots = 8;
    constexpr const char* factorySlotTexts[numSlots] = { "P", "L", "R", "N", "PL", "PR", "LR", "S" };

    // Orbit's cycle runs at most this many ops past its start when it has no period (a word
    // with Grow): then it stops stepping.
    constexpr int maxOrbitOps = 4096;

    struct EngineSettings
    {
        bool enabled = true;
        int gatherMs = 20;                 // 0..80
        Mix mix = Mix::Generated;
        int sourceChannel = 0;             // 0..15 (MIDI channel 1)
        int genChannel = 1;                // 0..15 (MIDI channel 2)
        VoicerSettings voicing;
        ZoneSettings zone;
        Word word;                         // the slot's word: W in Mirror, the cycle for Orbit's "Slot"

        // Phase 3.
        Mode mode = Mode::Mirror;
        bool latch = false;
        WalkInput walkInput = WalkInput::Relative;
        OrbitCycle orbitCycle = OrbitCycle::PL;
        int orbitRate = defaultOrbitRate;  // index into orbitRateBars
        KeyMode key = KeyMode::Listen;
        int keyTonic = 0;                  // Hold's key (0..11)
        bool keyMinor = false;
        bool inKey = false;

        bool hasSource() const noexcept { return mix != Mix::Generated; }
        bool hasGenerated() const noexcept { return mix != Mix::SourceOnly; }
    };

    // What the plug-in saves of the engine (PLAN.md §3.7: the current W and Latch's chord), and
    // restores.
    struct WalkState
    {
        Word word;                         // Walk / Explore's W
        uint32_t orbitStep = 0;            // Orbit's k
        std::array<uint8_t, 128> latched {};   // velocity of each latched key, 0 = not latched

        bool operator== (const WalkState& o) const noexcept { return word == o.word && orbitStep == o.orbitStep && latched == o.latched; }
        bool operator!= (const WalkState& o) const noexcept { return ! (*this == o); }
    };

    class LatticeEngine
    {
    public:
        static constexpr int genSource (int ownerId) noexcept { return 128 + ownerId; }   // VoiceTracker sources

        LatticeEngine()
        {
            bound.fill (-1);
            for (size_t i = 0; i < factoryCycles.size(); ++i)
                parseWord (factoryCycles[i].word, cycles[i]);
        }

        // Forgets everything sounding and every key without sending anything (after the receiver
        // was reset some other way). W, Orbit's step and the latched chord (state) are kept; the
        // latched chord sleeps.
        void reset() noexcept
        {
            sleepLatched();
            tracker.clear();
            gatherer.clear();
            bound.fill (-1);
            rawOn.fill (0);
            clock.reset();
            orbit.reset();
            wasPlaying = false;
            haveLast = false;
            genMask = 0;
            genLowest = -1;
            dirty = false;
            dropPending = false;
        }

        // Ends everything sounding (stop, bypass, reset, deleting the device) and forgets the
        // keys down. A latched chord is kept, silent, until the transport next starts.
        template <class Out>
        void releaseAll (int sample, Out& out) noexcept
        {
            ++releaseCount;
            tracker.releaseAll (sample, out);
            for (int ch = 0; ch < 16; ++ch)
                for (int n = 0; n < 128; ++n)
                    if (rawOn[(size_t) (ch * 128 + n)] != 0)
                    {
                        out.push (MidiEvent::noteOff (ch, n, sample));
                        rawOn[(size_t) (ch * 128 + n)] = 0;
                    }
            sleepLatched();
            gatherer.clear();
            listener.allUp (secondsAt (sample));
            bound.fill (-1);
            genMask = 0;
            genLowest = -1;
            dirty = false;
            dropPending = false;
        }

        // One block. `in` is this block's input, in sample order (samples outside the block are
        // clamped into it). Output is appended to `out` (which may already hold events of this
        // block, e.g. from releaseAll), and the whole of `out` is put in sample order (stable).
        template <class Out>
        void process (const Transport& t, const MidiEvent* in, int numIn, int numSamples, const EngineSettings& s, Out& out) noexcept
        {
            numSamples = numSamples > 0 ? numSamples : 0;
            const int lastSample = numSamples > 0 ? numSamples - 1 : 0;
            clock.beginBlock (t, numSamples);
            orbit.beginBlock (clock);
            blockStart = clock.blockStart();
            blockSeconds = seconds;
            rate = clock.sampleRate();
            window = gatherSamples (s.gatherMs, rate);

            // What changed since the last block.
            const bool started = t.playing && ! wasPlaying;
            if (wasPlaying && ! t.playing)
                releaseAll (0, out);
            wasPlaying = t.playing;

            bool wake = started || wakeRequested;
            wakeRequested = false;
            if (haveLast && last.enabled && ! s.enabled)
                releaseAll (0, out);
            else if (haveLast && ! last.enabled && s.enabled)
            {
                endRaw (0, out);
                wake = wake || t.playing;
            }
            else if (s.enabled && haveLast)
            {
                if (last.mix != s.mix || last.sourceChannel != s.sourceChannel || last.genChannel != s.genChannel || last.mode != s.mode)
                    resound (0, s, out);
                else if (last.word != s.word || last.voicing != s.voicing || last.orbitCycle != s.orbitCycle)
                    markDirty (0);
                if (last.latch && ! s.latch)
                    unlatch (0, out);
            }
            if (! s.latch)
                sleeping.fill (0);
            if (haveLast && last.key != s.key && s.key != KeyMode::Hold)
                listener.reset();          // Listen starts afresh after Hold or Off; Off forgets; Hold keeps the guess
            copySettings (s);
            if (s.enabled && s.latch && t.playing && wake)
                wakeLatched (0, s, out);
            takeEditorCommands (s);

            if (! s.enabled)
            {
                int at = 0;
                for (int i = 0; i < numIn; ++i)
                {
                    MidiEvent e = in[i];
                    e.sample = e.sample < at ? at : (e.sample > lastSample ? lastSample : e.sample);
                    through (e, out);
                    at = e.sample;
                }
            }
            else
            {
                run (in, numIn, numSamples, s, out);
            }

            seconds = blockSeconds + numSamples / rate;
            if (s.key == KeyMode::Listen && s.enabled)
                listener.advance (seconds);

            // The engine emits in time order, and on one sample in the order that is meant (a
            // voice released before its retune starts; a key pressed and let go on one sample
            // sounds, then ends). This only merges in what `out` already held.
            out.sortBySample (false);
        }

        // What sounds, for the editor and the rig's status file.
        PcMask sourceMask() const noexcept { return gatherer.heldMask(); }
        int sourceLowest() const noexcept { return gatherer.lowestHeld(); }
        PcMask generatedMask() const noexcept { return genMask; }
        int generatedLowest() const noexcept { return genLowest; }
        int soundingNotes() const noexcept { return tracker.soundingNotes() + rawCount(); }
        bool isSilent() const noexcept { return tracker.isEmpty() && rawCount() == 0; }
        const VoiceTracker& voices() const noexcept { return tracker; }
        const ChordGatherer& keys() const noexcept { return gatherer; }

        // The key: the listener's guess (Listen; frozen in Hold), and the key in force.
        KeyGuess keyGuess() const noexcept { return last.key == KeyMode::Off ? KeyGuess() : listener.guess(); }
        KeyRef currentKey() const noexcept { return keyOf (last); }

        // W as it stands: Walk's word, Orbit's step, the latched chord (for the plug-in's state).
        WalkState walkState() const noexcept
        {
            WalkState w;
            w.word = walk;
            w.orbitStep = orbitK;
            if (sleepingAny())
                w.latched = sleeping;
            else
                for (int n = 0; n < 128; ++n)
                    if (gatherer.isLatched (n) || (last.latch && gatherer.isDown (n)))
                        w.latched[(size_t) n] = (uint8_t) gatherer.velocity (n);
            return w;
        }

        // Restores the state: W, Orbit's step and the latched chord (asleep until the transport
        // next starts, or at once if it is playing). Call between blocks; sounds nothing itself.
        template <class Out>
        void restore (const WalkState& w, Out& out) noexcept
        {
            releaseAll (0, out);
            walk = w.word;
            orbitK = w.orbitStep;
            sleeping = w.latched;
            for (auto& v : sleeping)
                v = v > 127 ? 127 : v;
            wakeRequested = true;
        }

        // The current W, for display: in Orbit the cycle's first k ops (up to 32 of them), and how
        // many ops W has in all.
        Word currentWord (int& totalOps) const noexcept
        {
            if (last.mode == Mode::Orbit)
            {
                const Word& c = cycleWord (last);
                const int r = orbitOps (c, sourceChord());
                Word w;
                for (int i = 0; i < r && i < maxWordOps; ++i)
                    w.push (c[i % c.size]);
                totalOps = r;
                return w;
            }
            const Word& w = last.mode == Mode::Mirror ? last.word : walk;
            totalOps = w.size;
            return w;
        }

        uint32_t orbitStep() const noexcept { return orbitK; }

        // ---- The editor (Phase 4) ----------------------------------------------------------
        // Operator key `index` (0..11), as a ghost-neighbour click: taken on the next block's
        // first sample, as the key would be (whether or not the zone is on).
        void editorOperator (int index) noexcept
        {
            if (index >= 0 && index < numOperatorKeys && numPendingOps < (int) pendingOps.size())
                pendingOps[(size_t) numPendingOps++] = (int8_t) index;
        }

        // Walk / Explore's W replaced (the word bar), on the next block's first sample.
        void editorSetWalk (const Word& w) noexcept
        {
            pendingWalk = w;
            pendingWalkSet = true;
        }

        // The chord the source keys make (anchored at its root, as the engine takes it), and
        // W(chord) as last evaluated, with its anchor and parity: the editor's ghost neighbours
        // are one op from it. Meaningful while the source mask isn't empty; computed under
        // every Mix (Source only keeps the lattice live).
        Chord sourceChord() const noexcept
        {
            unsigned mask = 0;
            int lowest = -1;
            for (int k = 0; k < 128; ++k)
                if (gatherer.isHeld (k))
                {
                    mask |= pcBit (k);
                    lowest = lowest < 0 ? k : lowest;
                }
            return makeChord (mask, chordRoot (mask, lowest >= 0 ? mod12 (lowest) : -1));
        }
        const Chord& generatedChord() const noexcept { return genChord; }

        // Every generated note sounding: f (pitch, velocity).
        template <class F>
        void forEachGenerated (F&& f) const noexcept
        {
            for (int o = 0; o < owner::count; ++o)
                if (bound[(size_t) o] >= 0)
                    f ((int) bound[(size_t) o], (int) boundVelocity[(size_t) o]);
        }

        // How many times everything was ended (stop, Enable off, releaseAll): the editor forgets
        // the keys it holds when this moves, since the engine has forgotten them.
        uint32_t releases() const noexcept { return releaseCount; }

    private:
        static constexpr int never = 1 << 30;

        static int blockSample (int64_t abs, int64_t start, int floor, int lastSample) noexcept
        {
            const int64_t s = abs - start;
            return s < floor ? floor : (s > lastSample ? lastSample : (int) s);
        }

        double secondsAt (int sample) const noexcept { return blockSeconds + sample / (rate > 0.0 ? rate : 44100.0); }

        void copySettings (const EngineSettings& s) noexcept
        {
            last = s;
            haveLast = true;
        }

        static KeyRef keyOfSettings (const EngineSettings& s, const KeyGuess& g) noexcept
        {
            KeyRef k;
            if (s.key == KeyMode::Hold)
            {
                k.tonic = s.keyTonic < 0 ? 0 : (s.keyTonic > 11 ? 11 : s.keyTonic);
                k.minor = s.keyMinor;
            }
            else if (s.key == KeyMode::Listen && g.has())
            {
                k.tonic = g.tonic;
                k.minor = g.minor;
            }
            return k;
        }
        KeyRef keyOf (const EngineSettings& s) const noexcept { return keyOfSettings (s, listener.guess()); }

        int rawCount() const noexcept
        {
            int n = 0;
            for (const auto r : rawOn)
                n += r;
            return n;
        }

        bool sleepingAny() const noexcept
        {
            for (const auto v : sleeping)
                if (v != 0)
                    return true;
            return false;
        }

        // The latched chord (and, with latch on, the keys down, which would latch when let go)
        // goes to sleep: kept, silent.
        void sleepLatched() noexcept
        {
            if (! last.latch)
                return;
            bool any = false;
            for (int n = 0; n < 128; ++n)
                any = any || gatherer.isHeld (n);
            if (! any)
                return;
            sleeping.fill (0);
            for (int n = 0; n < 128; ++n)
                if (gatherer.isHeld (n))
                    sleeping[(size_t) n] = (uint8_t) gatherer.velocity (n);
        }

        // The sleeping latched chord sounds again (transport start; Enable back while playing).
        template <class Out>
        void wakeLatched (int sample, const EngineSettings& s, Out& out) noexcept
        {
            if (! sleepingAny() || gatherer.held() > 0)
                return;
            for (int n = 0; n < 128; ++n)
                if (sleeping[(size_t) n] != 0)
                {
                    gatherer.addLatched (n, sleeping[(size_t) n]);
                    startSource (n, sample, s, out);
                }
            sleeping.fill (0);
            markDirty (sample);
        }

        // Latch goes off: the latched keys leave the chord.
        template <class Out>
        void unlatch (int sample, Out& out) noexcept
        {
            gatherer.dropLatched ([&] (int n) { tracker.release ((VoiceTracker::Source) n, sample, out); });
            sleeping.fill (0);
            dropPending = false;
            markDirty (sample);
        }

        template <class Out>
        void startSource (int k, int sample, const EngineSettings& s, Out& out) noexcept
        {
            if (s.hasSource())
                tracker.start ((VoiceTracker::Source) k, s.sourceChannel, k, gatherer.velocity (k), sample, out);
        }

        void markDirty (int sample) noexcept
        {
            if (! dirty || sample < dirtyAt)
                dirtyAt = sample;
            dirty = true;
        }

        // Enabled: input, gather windows, Orbit's lines and pending retunes, in time order. On
        // one sample: Orbit's line, a window (of more than 0 ms) closing there, the input, a
        // 0 ms window closing after it, then one retune for whatever changed W.
        template <class Out>
        void run (const MidiEvent* in, int numIn, int numSamples, const EngineSettings& s, Out& out) noexcept
        {
            const int lastSample = numSamples > 0 ? numSamples - 1 : 0;
            const int64_t blockEnd = blockStart + numSamples;
            int at = 0, i = 0;
            while (true)
            {
                int x = never;
                if (i < numIn)
                {
                    const int e = in[i].sample;
                    x = e < at ? at : (e > lastSample ? lastSample : e);
                }
                if (gatherer.dueBy (blockEnd))
                {
                    const int c = blockSample (gatherer.closesAt(), blockStart, at, lastSample);
                    x = c < x ? c : x;
                }
                int tick = -1;
                if (s.mode == Mode::Orbit && numSamples > 0)
                {
                    tick = orbit.next (clock, at, s.orbitRate);
                    x = tick >= 0 && tick < x ? tick : x;
                }
                if (dirty)
                    x = dirtyAt < x ? (dirtyAt < at ? at : dirtyAt) : x;
                if (x == never)
                    break;

                const int64_t abs = blockStart + x;
                if (tick == x)
                    orbitLine (x, s);
                if (gatherer.dueBefore (abs))
                    closeWindow (x, s, out);
                for (; i < numIn; ++i)
                {
                    const int e = in[i].sample < at ? at : (in[i].sample > lastSample ? lastSample : in[i].sample);
                    if (e != x)
                        break;
                    MidiEvent ev = in[i];
                    ev.sample = x;
                    handle (ev, abs, s, out);
                }
                if (gatherer.dueBy (abs + 1))
                    closeWindow (x, s, out);
                if (dirty && dirtyAt <= x)
                {
                    dirty = false;
                    reevaluate (x, s, out);
                }
                at = x;
            }
        }

        // Enable off: through, keeping the stream clean.
        template <class Out>
        void through (const MidiEvent& e, Out& out) noexcept
        {
            if (e.isNoteOn() || e.isNoteOff())
            {
                auto& r = rawOn[(size_t) (e.channel() * 128 + e.note())];
                if (e.isNoteOn() == (r != 0))
                    return;                // a second note-on, or a note-off with no note-on
                if (e.isNoteOn() && ! roomFor (out))
                    return;
                if (out.push (e))
                    r = e.isNoteOn() ? 1 : 0;
                return;
            }
            if (e.isAllNotesOff())
                rawOn.fill (0);
            if (roomFor (out))
                out.push (e);
        }

        // Anything that isn't a note-off goes out only while half the buffer is free: the rest is
        // kept for note-offs, so everything sounding can always be ended (as VoiceTracker does).
        template <class Out>
        static bool roomFor (const Out& out) noexcept { return out.size() < Out::capacity / 2; }

        template <class Out>
        void endRaw (int sample, Out& out) noexcept
        {
            for (int ch = 0; ch < 16; ++ch)
                for (int n = 0; n < 128; ++n)
                    if (rawOn[(size_t) (ch * 128 + n)] != 0)
                    {
                        out.push (MidiEvent::noteOff (ch, n, sample));
                        rawOn[(size_t) (ch * 128 + n)] = 0;
                    }
        }

        template <class Out>
        void handle (const MidiEvent& e, int64_t abs, const EngineSettings& s, Out& out) noexcept
        {
            if (e.isNoteOff())
            {
                bool sounded = false;
                const int k = e.note();
                if (! gatherer.isDown (k))
                    return;                // an operator key, or a key we already let go
                if (s.key == KeyMode::Listen)
                    listener.noteOff (k, secondsAt (e.sample));
                if (s.latch)
                {
                    gatherer.release (k, sounded, true);    // stays in the chord, sounding
                    return;
                }
                gatherer.release (k, sounded);
                tracker.release ((VoiceTracker::Source) k, e.sample, out);
                releaseOwner (k, e.sample, out);
                if (gatherer.held() == 0)
                    for (int o = owner::bass; o < owner::count; ++o)
                        releaseOwner (o, e.sample, out);
                refreshGenerated();
                return;
            }
            if (e.isNoteOn())
            {
                if (route (e, s.zone) == Route::Operator)
                {
                    operatorKey (s.zone.operatorIndex (e.note()), e.sample, s);
                    return;
                }
                const int k = e.note();
                if (gatherer.isDown (k))
                    return;
                if (gatherer.down() == 0)
                    newChord (e.sample, s);
                bool wasLatched = false;
                if (! gatherer.press (k, e.velocity(), abs, window, wasLatched))
                    return;
                if (s.key == KeyMode::Listen)
                    listener.noteOn (k, e.velocity(), secondsAt (e.sample));
                if (wasLatched)
                    tracker.release ((VoiceTracker::Source) k, e.sample, out);   // struck again
                startSource (k, e.sample, s, out);
                return;
            }
            if (e.isAllNotesOff())
                releaseAll (e.sample, out);
            if (roomFor (out))
                out.push (e);
        }

        // The editor's operator clicks and W edits, on the block's first sample (enabled only).
        void takeEditorCommands (const EngineSettings& s) noexcept
        {
            if (s.enabled)
            {
                for (int i = 0; i < numPendingOps; ++i)
                    operatorKey (pendingOps[(size_t) i], 0, s);
                if (pendingWalkSet && (s.mode == Mode::Walk || s.mode == Mode::Explore) && ! (pendingWalk == walk))
                {
                    walk = pendingWalk;
                    markDirty (0);
                }
            }
            numPendingOps = 0;
            pendingWalkSet = false;
        }

        // A source key goes down with none down.
        void newChord (int sample, const EngineSettings& s) noexcept
        {
            sleeping.fill (0);
            dropPending = gatherer.held() > 0;      // latched keys: replaced when the window closes
            if (s.walkInput == WalkInput::Reset)
            {
                walk.clear();
                orbitK = 0;
            }
            if (! clock.isPlaying())
            {
                // Stopped: the internal clock starts a bar here, so Orbit steps a Rate from now.
                clock.anchor (sample);
                orbit.restartAt (0.0);
            }
        }

        void operatorKey (int index, int sample, const EngineSettings& s) noexcept
        {
            switch (s.mode)
            {
                case Mode::Walk:
                case Mode::Explore:
                    if (wordedit::apply (walk, index))
                        markDirty (sample);
                    break;
                case Mode::Orbit:
                    if (index == opkey::home && orbitK != 0)
                    {
                        orbitK = 0;
                        markDirty (sample);
                    }
                    else if (index == opkey::undo && orbitK > 0)
                    {
                        --orbitK;
                        markDirty (sample);
                    }
                    break;
                case Mode::Mirror:
                    break;
            }
        }

        // A grid line: the cycle steps if a chord was sounding just before it.
        void orbitLine (int sample, const EngineSettings& s) noexcept
        {
            orbit.fired();
            if (! gatherer.sounding())
                return;
            const Word& c = cycleWord (s);
            if (c.empty())
                return;
            const Chord start = sourceChord();
            const int period = periodOf (c, start);
            if (period <= 0 && orbitK >= (uint32_t) maxOrbitOps)
                return;
            const uint32_t from = orbitK;
            ++orbitK;
            const KeyRef key = keyOf (s);
            if (s.inKey && key.known())
            {
                // On through the cycle past chords outside the key, for at most one lap.
                const int lap = period > 0 ? period : 64;
                const int r = orbitOps (c, start);
                WordRunner runner (start);
                for (int j = 0; j < r; ++j)
                    runner.step (c[j % c.size]);
                bool found = false;
                for (int tries = 0; tries < lap; ++tries)
                {
                    if (key.contains (runner.chord().mask))
                    {
                        found = true;
                        break;
                    }
                    ++orbitK;
                    const int next = orbitOps (c, start);
                    if (next == 0)
                        runner.reset (start);
                    else
                        runner.step (c[(next - 1) % c.size]);
                }
                if (! found)
                    orbitK = from + 1;
            }
            markDirty (sample);
        }

        const Word& cycleWord (const EngineSettings& s) const noexcept
        {
            const int c = (int) s.orbitCycle;
            return c >= 0 && c < (int) cycles.size() ? cycles[(size_t) c] : s.word;
        }

        // The cycle's length on `start` (ops until the chord comes back), or 0 if it has none
        // within maxOrbitOps. Cached.
        int periodOf (const Word& c, const Chord& start) const noexcept
        {
            if (! (c == periodWord) || ! (start == periodStart) || ! periodKnown)
            {
                periodWord = c;
                periodStart = start;
                periodKnown = true;
                const CycleInfo info = cycleOf (c, start, maxOrbitOps);
                cachedPeriod = info.steps > 0 ? info.steps : 0;
            }
            return cachedPeriod;
        }

        // How many of the cycle's ops W is: k modulo the cycle's length on the chord.
        int orbitOps (const Word& c, const Chord& start) const noexcept
        {
            if (c.empty())
                return 0;
            const int p = periodOf (c, start);
            if (p > 0)
                return (int) (orbitK % (uint32_t) p);
            return orbitK > (uint32_t) maxOrbitOps ? maxOrbitOps : (int) orbitK;
        }

        // The chord's keys as a voicing, and as a chord.
        void gatherInput() noexcept
        {
            input.clear();
            for (int k = 0; k < 128; ++k)
                if (gatherer.isHeld (k))
                    input.push (k, k);
        }

        template <class Out>
        void releaseOwner (int o, int sample, Out& out) noexcept
        {
            if (bound[(size_t) o] < 0)
                return;
            tracker.release ((VoiceTracker::Source) genSource (o), sample, out);
            bound[(size_t) o] = -1;
        }

        template <class Out>
        void closeWindow (int sample, const EngineSettings& s, Out& out) noexcept
        {
            if (dropPending)
            {
                dropPending = false;
                gatherer.dropLatched ([&] (int n) { tracker.release ((VoiceTracker::Source) n, sample, out); });
            }
            gatherer.close();
            evaluate (sample, s, out);
        }

        // W or the voicing changed: retune the chord, unless a window is still gathering.
        template <class Out>
        void reevaluate (int sample, const EngineSettings& s, Out& out) noexcept
        {
            if (! gatherer.windowOpen())
                evaluate (sample, s, out);
        }

        // Mix, a channel or the mode changed: end everything and sound the chord again.
        template <class Out>
        void resound (int sample, const EngineSettings& s, Out& out) noexcept
        {
            tracker.releaseAll (sample, out);
            bound.fill (-1);
            for (int k = 0; k < 128; ++k)
                if (gatherer.isHeld (k))
                    startSource (k, sample, s, out);
            reevaluate (sample, s, out);
            refreshGenerated();
        }

        // The chord's keys under W: bind each output voice to its owner, retuning only what changed.
        template <class Out>
        void evaluate (int sample, const EngineSettings& s, Out& out) noexcept
        {
            dirty = false;
            gatherInput();
            int loudest = 1;
            for (const auto& v : input)
                loudest = gatherer.velocity (v.owner) > loudest ? gatherer.velocity (v.owner) : loudest;

            target.fill (-1);
            genChord = {};
            if (input.count > 0)
            {
                const Chord start = inputChord (input);
                if (s.mode == Mode::Orbit)
                {
                    const Word& c = cycleWord (s);
                    genChord = voiceOps (orbitOps (c, start), [&c] (int i) { return c[i % c.size]; }, start, input, s.voicing, output, nullptr);
                }
                else
                {
                    genChord = voiceWord (s.mode == Mode::Mirror ? s.word : walk, start, input, s.voicing, output);
                }
                if (s.hasGenerated())
                    for (const auto& v : output)
                        if (v.owner < owner::count)
                            target[v.owner] = (int16_t) v.pitch;
            }

            for (int o = 0; o < owner::count; ++o)
                if (bound[(size_t) o] != target[(size_t) o])
                    releaseOwner (o, sample, out);
            for (int o = 0; o < owner::count; ++o)
            {
                const int p = target[(size_t) o];
                if (p < 0 || bound[(size_t) o] >= 0)
                    continue;
                const int velocity = o < 128 ? gatherer.velocity (o) : loudest;
                if (tracker.start ((VoiceTracker::Source) genSource (o), s.genChannel, p, velocity, sample, out))
                {
                    bound[(size_t) o] = (int16_t) p;
                    boundVelocity[(size_t) o] = (uint8_t) velocity;
                }
            }
            refreshGenerated();
        }

        void refreshGenerated() noexcept
        {
            unsigned m = 0;
            int lo = -1;
            for (int o = 0; o < owner::count; ++o)
                if (bound[(size_t) o] >= 0)
                {
                    m |= pcBit (bound[(size_t) o]);
                    lo = lo < 0 || bound[(size_t) o] < lo ? bound[(size_t) o] : lo;
                }
            genMask = (PcMask) m;
            genLowest = lo;
        }

        Clock clock;
        OrbitClock orbit;
        VoiceTracker tracker;
        ChordGatherer gatherer;
        KeyListener listener;
        std::array<int16_t, owner::count> bound {};     // each owner's sounding pitch, or -1
        std::array<int16_t, owner::count> target {};
        std::array<uint8_t, owner::count> boundVelocity {};   // the velocity each bound voice started with
        std::array<uint8_t, 16 * 128> rawOn {};          // Enable off: notes passed through
        std::array<uint8_t, 128> sleeping {};            // a latched chord, silent (velocities)
        std::array<Word, 4> cycles {};                   // PL, PR, LR, PLR
        Voicing input, output;
        EngineSettings last;
        Word walk;                                       // Walk / Explore's W
        uint32_t orbitK = 0;                             // Orbit: ops of the cycle taken
        bool haveLast = false, wasPlaying = false, wakeRequested = false;
        bool dirty = false, dropPending = false;
        int dirtyAt = 0;
        int64_t window = 0, blockStart = 0;
        double seconds = 0.0, blockSeconds = 0.0, rate = 44100.0;
        PcMask genMask = 0;
        int genLowest = -1;
        Chord genChord;                                  // W(chord) as last evaluated
        uint32_t releaseCount = 0;

        // The editor's commands, taken at the next block's start.
        std::array<int8_t, 64> pendingOps {};
        int numPendingOps = 0;
        Word pendingWalk;
        bool pendingWalkSet = false;

        // periodOf's cache.
        mutable Word periodWord;
        mutable Chord periodStart;
        mutable int cachedPeriod = 0;
        mutable bool periodKnown = false;
    };
}
