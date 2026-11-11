// The block processor (PLAN.md §4): MIDI in → ZoneRouter → ChordGatherer → W(chord) → Voicer →
// VoiceTracker → MIDI out. Phase 2 plays Mirror: W is the current slot's word, fixed while the
// chord sounds.
//
//   source notes   gathered into a chord (ChordGatherer.h: the gather window); when the window
//                  closes the held keys are voiced under W (Voicer.h). Each output voice is
//                  bound to the key it is the image of: releasing a key releases its image and
//                  nothing else, velocities carry across, and a re-evaluation (a late note, or W
//                  or the voicing settings changing while the chord is held) retunes only the
//                  voices whose pitch changed (note-off, note-on), on the sample it happens.
//   Mix            Generated only / Source + Generated / Source only. Source notes go out at
//                  once (not delayed by the window) on `sourceChannel`, generated notes on
//                  `genChannel`. A pitch both on one channel sounds once (VoiceTracker counts
//                  its owners).
//   operator keys  (the zone, ZoneRouter.h; off in Phase 2) are swallowed.
//   anything else  (controllers, pitch bend, program changes) goes through untouched. All
//                  Notes Off / All Sound Off releases everything first.
//   Enable off     MIDI through untouched, except that a duplicate note-on or a note-off with no
//                  note-on is dropped, so that switching Enable either way leaves nothing
//                  stuck: everything the engine sounded ends when Enable goes off, and every
//                  note passed through ends when it comes back on.
//   stop           the transport stopping (and `releaseAll`: bypass, reset, deleting the device)
//                  ends everything sounding and forgets the held keys; their note-offs, when
//                  they come, are ignored.
//   Mix or channel changes end what sounds and sound the held chord again under the new
//   settings.
//
// Events come out in sample order; on one sample, in the order they happened. Plain C++, no JUCE, no
// allocation, no locks: the whole engine is one fixed-size object.
#pragma once

#include "ChordGatherer.h"
#include "Clock.h"
#include "MidiEvents.h"
#include "Transport.h"
#include "Voicer.h"
#include "VoiceTracker.h"
#include "ZoneRouter.h"
#include <array>
#include <cstdint>

namespace tonnetz
{
    enum class Mix : uint8_t { Generated, SourceAndGenerated, SourceOnly };

    // Mirror's eight slots (A-H) and their factory words: A P, B L, C R, D N, E PL, F PR, G LR, H S.
    constexpr int numSlots = 8;
    constexpr const char* factorySlotTexts[numSlots] = { "P", "L", "R", "N", "PL", "PR", "LR", "S" };

    struct EngineSettings
    {
        bool enabled = true;
        int gatherMs = 20;                 // 0..80
        Mix mix = Mix::Generated;
        int sourceChannel = 0;             // 0..15 (MIDI channel 1)
        int genChannel = 1;                // 0..15 (MIDI channel 2)
        VoicerSettings voicing;
        ZoneSettings zone;
        Word word;                         // W: in Mirror, the slot's word

        bool hasSource() const noexcept { return mix != Mix::Generated; }
        bool hasGenerated() const noexcept { return mix != Mix::SourceOnly; }
    };

    class LatticeEngine
    {
    public:
        static constexpr int genSource (int ownerId) noexcept { return 128 + ownerId; }   // VoiceTracker sources

        LatticeEngine() { bound.fill (-1); }

        // Forgets everything without sending anything (after the receiver was reset some other way).
        void reset() noexcept
        {
            tracker.clear();
            gatherer.clear();
            bound.fill (-1);
            rawOn.fill (0);
            clock.reset();
            wasPlaying = false;
            haveLast = false;
            genMask = 0;
            genLowest = -1;
        }

        // Ends everything sounding (stop, bypass, reset, deleting the device) and forgets the
        // held keys.
        template <class Out>
        void releaseAll (int sample, Out& out) noexcept
        {
            tracker.releaseAll (sample, out);
            for (int ch = 0; ch < 16; ++ch)
                for (int n = 0; n < 128; ++n)
                    if (rawOn[(size_t) (ch * 128 + n)] != 0)
                    {
                        out.push (MidiEvent::noteOff (ch, n, sample));
                        rawOn[(size_t) (ch * 128 + n)] = 0;
                    }
            gatherer.clear();
            bound.fill (-1);
            genMask = 0;
            genLowest = -1;
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
            const int64_t blockStart = clock.blockStart();
            window = gatherSamples (s.gatherMs, clock.sampleRate());

            // What changed since the last block.
            if (wasPlaying && ! t.playing)
                releaseAll (0, out);
            wasPlaying = t.playing;

            if (haveLast && last.enabled && ! s.enabled)
                releaseAll (0, out);
            else if (haveLast && ! last.enabled && s.enabled)
                endRaw (0, out);
            else if (s.enabled && haveLast)
            {
                if (last.mix != s.mix || last.sourceChannel != s.sourceChannel || last.genChannel != s.genChannel)
                    resound (0, s, out);
                else if (last.word != s.word || last.voicing != s.voicing)
                    reevaluate (0, s, out);
            }
            copySettings (s);

            int at = 0;                    // output never goes back in time
            for (int i = 0; i < numIn; ++i)
            {
                MidiEvent e = in[i];
                e.sample = e.sample < at ? at : (e.sample > lastSample ? lastSample : e.sample);
                if (! s.enabled)
                {
                    through (e, out);
                    at = e.sample;
                    continue;
                }
                const int64_t abs = blockStart + e.sample;
                if (gatherer.dueBefore (abs))
                    closeWindow (blockSample (gatherer.closesAt(), blockStart, at, lastSample), s, out);
                at = e.sample;
                handle (e, abs, s, out);
            }
            if (s.enabled && gatherer.dueBy (blockStart + numSamples))
                closeWindow (blockSample (gatherer.closesAt(), blockStart, at, lastSample), s, out);

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

    private:
        static int blockSample (int64_t abs, int64_t blockStart, int floor, int lastSample) noexcept
        {
            const int64_t s = abs - blockStart;
            return s < floor ? floor : (s > lastSample ? lastSample : (int) s);
        }

        void copySettings (const EngineSettings& s) noexcept
        {
            last = s;
            haveLast = true;
        }

        int rawCount() const noexcept
        {
            int n = 0;
            for (const auto r : rawOn)
                n += r;
            return n;
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
                if (! gatherer.release (k, sounded))
                    return;                // an operator key, or a key we already let go
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
                    return;                // Phase 3 gives these keys their ops
                const int k = e.note();
                if (! gatherer.press (k, e.velocity(), abs, window))
                    return;
                if (s.hasSource())
                    tracker.start ((VoiceTracker::Source) k, s.sourceChannel, k, e.velocity(), e.sample, out);
                return;
            }
            if (e.isAllNotesOff())
                releaseAll (e.sample, out);
            if (roomFor (out))
                out.push (e);
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
            gatherer.close();
            evaluate (sample, s, out);
        }

        // W or the voicing changed: retune the held chord, unless a window is still gathering.
        template <class Out>
        void reevaluate (int sample, const EngineSettings& s, Out& out) noexcept
        {
            if (gatherer.held() > 0 && ! gatherer.windowOpen())
                evaluate (sample, s, out);
        }

        // Mix or a channel changed: end everything and sound the held chord again.
        template <class Out>
        void resound (int sample, const EngineSettings& s, Out& out) noexcept
        {
            tracker.releaseAll (sample, out);
            bound.fill (-1);
            for (int k = 0; k < 128; ++k)
                if (gatherer.isHeld (k) && s.hasSource())
                    tracker.start ((VoiceTracker::Source) k, s.sourceChannel, k, gatherer.velocity (k), sample, out);
            reevaluate (sample, s, out);
            refreshGenerated();
        }

        // The held keys under W: bind each output voice to its owner, retuning only what changed.
        template <class Out>
        void evaluate (int sample, const EngineSettings& s, Out& out) noexcept
        {
            input.clear();
            int loudest = 1;
            for (int k = 0; k < 128; ++k)
                if (gatherer.isHeld (k))
                {
                    input.push (k, k);
                    loudest = gatherer.velocity (k) > loudest ? gatherer.velocity (k) : loudest;
                }

            target.fill (-1);
            if (input.count > 0)
            {
                voiceWord (s.word, inputChord (input), input, s.voicing, output);
                for (const auto& v : output)
                    if (v.owner < owner::count)
                        target[v.owner] = (int16_t) v.pitch;
            }
            if (! s.hasGenerated())
                target.fill (-1);

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
                    bound[(size_t) o] = (int16_t) p;
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
        VoiceTracker tracker;
        ChordGatherer gatherer;
        std::array<int16_t, owner::count> bound {};     // each owner's sounding pitch, or -1
        std::array<int16_t, owner::count> target {};
        std::array<uint8_t, 16 * 128> rawOn {};          // Enable off: notes passed through
        Voicing input, output;
        EngineSettings last;
        bool haveLast = false, wasPlaying = false;
        int64_t window = 0;
        PcMask genMask = 0;
        int genLowest = -1;
    };
}
