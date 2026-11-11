// The capture's audio-thread half (PLAN.md §3.5): every generated note TonnetzML sends (and,
// with `captureSource`, every source note played into it) goes into a fixed lock-free queue,
// stamped in beats, for the message thread's CaptureModel. Shaped after ChordMonkey's
// ExportRecorder (`core/midiio/ExportRecorder.h`: a preallocated ring, atomics, no locks), but
// a single-producer single-consumer FIFO drained by the message thread, not a ring it copies.
//
// Beats: a *tape* that never runs backwards. While the host plays, a stamp is the host's ppq
// plus an offset that stays fixed as long as the host plays on continuously, so stamps follow
// the host exactly (tempo changes included). At a start, a loop wrap or a locate the offset is
// moved on to the next point of the tape at the same place in the bar as the host (less than a
// bar later), so the tape's bar lines stay the host's bar lines and nothing goes back in time.
// While the host is stopped the tape runs on by itself at the host's tempo (the editor and the
// test rig can still play). A loop of whole bars therefore unrolls with no gap at all.
//
// What is captured (CaptureWriter::process, once per block after the engine):
//   generated  the engine's output notes on `genChannel` (while Mix sends generated notes):
//              exactly what a track recording TonnetzML gets on that channel (G6), including
//              the note-offs of a stop, Enable off, a Mix change, bypass
//   source     with `captureSource`, the source keys played into the engine (input notes
//              outside the operator zone, while enabled), whatever Mix sends, on
//              `sourceChannel`. When Mix sends source notes on the generated channel itself
//              they are already in the generated stream and are not captured twice
//   take marks `capture` going on or off (TakeStart / TakeEnd), on the block's first sample
// A note-off is captured if its note-on was (bitsets of open notes), so settings changing
// while notes sound never leave a captured note open. A full queue drops the event and counts
// it (`dropped`); the model then ends every open note (CaptureModel::lose).
//
// Plain C++, no JUCE, no allocation, no locks.
#pragma once

#include "../engine/EditorCommands.h"     // SpscFifo
#include "../engine/MidiEvents.h"
#include "../engine/SnapshotExchange.h"
#include "../engine/Transport.h"
#include "../engine/ZoneRouter.h"
#include <atomic>
#include <bitset>
#include <cmath>
#include <cstdint>

namespace tonnetz
{
    enum class CaptureKind : uint8_t { NoteOn, NoteOff, TakeStart, TakeEnd };
    enum class CaptureStream : uint8_t { Generated, Source };

    struct CaptureEvent
    {
        double beat = 0.0;                 // on the tape
        CaptureKind kind = CaptureKind::NoteOn;
        CaptureStream stream = CaptureStream::Generated;
        uint8_t channel = 0, pitch = 0, velocity = 0;
    };

    // The tape's state for the message thread (published every block).
    struct CaptureClockInfo
    {
        double now = 0.0;                  // the tape at the end of the last block
        double bpm = 120.0;
        int numerator = 4, denominator = 4;
        bool playing = false;
        uint64_t blocks = 0;

        double barLength() const noexcept { return numerator > 0 && denominator > 0 ? numerator * 4.0 / denominator : 4.0; }
    };

    // The tape (see the top of the file). Audio thread.
    class CaptureClock
    {
    public:
        // Beats more than this away from where the host should be is a jump (a loop wrap, a
        // locate); less is the host's rounding (a tempo ramp), followed as it is.
        static constexpr double jumpBeats = 0.01;

        void beginBlock (const Transport& t, int numSamples) noexcept
        {
            numSamples = numSamples > 0 ? numSamples : 0;
            bpm = t.bpm > 0.0 && std::isfinite (t.bpm) ? t.bpm : 120.0;
            const double rate = t.sampleRate > 0.0 && std::isfinite (t.sampleRate) ? t.sampleRate : 44100.0;
            beatsPerSample = bpm / (60.0 * rate);
            bar = t.barLength();
            playing = t.playing && std::isfinite (t.ppq);

            if (playing)
            {
                const bool continuous = wasPlaying && std::abs (t.ppq - expectedPpq) <= jumpBeats;
                if (! continuous)
                {
                    // Move on to the next tape point at the host's place in the bar.
                    const double origin = t.hasBarStart && std::isfinite (t.barStartPpq) && t.barStartPpq <= t.ppq + 1.0e-9
                                        ? t.barStartPpq
                                        : std::floor (t.ppq / bar + 1.0e-9) * bar;
                    const double hostPhase = wrap (t.ppq - origin);
                    double delta = wrap (hostPhase - wrap (end));
                    if (delta > bar - 1.0e-7)
                        delta = 0.0;
                    offset = end + delta - t.ppq;
                }
                start = t.ppq + offset;
                expectedPpq = t.ppq + numSamples * beatsPerSample;
            }
            else
            {
                start = end;                   // free-running
            }
            wasPlaying = playing;
            end = start + numSamples * beatsPerSample;
        }

        double beatAt (int sample) const noexcept { return start + (sample > 0 ? sample : 0) * beatsPerSample; }
        double blockStart() const noexcept { return start; }
        double blockEnd() const noexcept { return end; }
        double tempo() const noexcept { return bpm; }
        bool isPlaying() const noexcept { return playing; }

        void reset() noexcept { wasPlaying = false; }

    private:
        double wrap (double x) const noexcept
        {
            const double r = std::fmod (x, bar);
            return r < 0.0 ? r + bar : r;
        }

        double bpm = 120.0, beatsPerSample = 120.0 / (60.0 * 44100.0), bar = 4.0;
        double offset = 0.0, expectedPpq = 0.0;
        double start = 0.0, end = 0.0;
        bool playing = false, wasPlaying = false;
    };

    class CaptureQueue
    {
    public:
        static constexpr int capacity = 1 << 14;

        // Audio thread.
        bool push (const CaptureEvent& e) noexcept
        {
            if (fifo.push (e))
                return true;
            droppedCount.fetch_add (1, std::memory_order_relaxed);
            return false;
        }
        void publish (const CaptureClockInfo& i) noexcept { info.publish (i); }

        // Message thread.
        bool pop (CaptureEvent& e) noexcept
        {
            if (const auto* p = fifo.peek())
            {
                e = *p;
                fifo.pop();
                return true;
            }
            return false;
        }
        const CaptureClockInfo& clock() noexcept { return info.read(); }
        uint32_t dropped() const noexcept { return droppedCount.load (std::memory_order_relaxed); }

    private:
        SpscFifo<CaptureEvent, capacity> fifo;
        SnapshotExchange<CaptureClockInfo> info;
        std::atomic<uint32_t> droppedCount { 0 };
    };

    struct CaptureSettings
    {
        bool enabled = true;               // Enable on and not bypassed
        bool capture = false;              // the take is armed
        bool captureSource = false;
        bool sendsGenerated = true;        // Mix sends generated notes
        bool sendsSource = false;          // Mix sends source notes
        int sourceChannel = 0, genChannel = 1;
        ZoneSettings zone;
    };

    // Audio thread: watches a block's input and output and writes the queue.
    class CaptureWriter
    {
    public:
        explicit CaptureWriter (CaptureQueue& q) : queue (q) {}

        // After the engine: `in` is what went into it this block (host, rig, editor), `out` what
        // came out.
        void process (const Transport& t, int numSamples, const MidiEvent* in, int numIn,
                      const MidiEvent* out, int numOut, const CaptureSettings& s) noexcept
        {
            clock.beginBlock (t, numSamples);

            if (s.capture != armed)
            {
                armed = s.capture;
                push ({ clock.blockStart(), armed ? CaptureKind::TakeStart : CaptureKind::TakeEnd, CaptureStream::Generated, 0, 0, 0 });
            }

            // Source keys (input), unless Mix already sends them on the generated channel.
            const bool sourceInGenerated = s.sendsSource && s.sourceChannel == s.genChannel;
            for (int i = 0; i < numIn; ++i)
            {
                const MidiEvent& e = in[i];
                const size_t key = (size_t) (e.channel() * 128 + e.note());
                if (e.isNoteOn())
                {
                    if (s.enabled && s.captureSource && ! sourceInGenerated && route (e, s.zone) == Route::Source && ! sourceOpen[key])
                    {
                        if (push ({ clock.beatAt (e.sample), CaptureKind::NoteOn, CaptureStream::Source, (uint8_t) (s.sourceChannel & 15), (uint8_t) e.note(), (uint8_t) e.velocity() }))
                        {
                            sourceOpen[key] = true;
                            sourceChannelOf[key] = (uint8_t) (s.sourceChannel & 15);
                        }
                    }
                }
                else if (e.isNoteOff() && sourceOpen[key])
                {
                    sourceOpen[key] = false;
                    push ({ clock.beatAt (e.sample), CaptureKind::NoteOff, CaptureStream::Source, sourceChannelOf[key], (uint8_t) e.note(), 0 });
                }
            }

            // Generated notes (output on the generated channel).
            for (int i = 0; i < numOut; ++i)
            {
                const MidiEvent& e = out[i];
                if (! e.isNoteOn() && ! e.isNoteOff())
                    continue;
                const size_t key = (size_t) (e.channel() * 128 + e.note());
                if (e.isNoteOn())
                {
                    if (s.enabled && s.sendsGenerated && e.channel() == s.genChannel && ! genOpen[key])
                        if (push ({ clock.beatAt (e.sample), CaptureKind::NoteOn, CaptureStream::Generated, (uint8_t) e.channel(), (uint8_t) e.note(), (uint8_t) e.velocity() }))
                            genOpen[key] = true;
                }
                else if (genOpen[key])
                {
                    genOpen[key] = false;
                    push ({ clock.beatAt (e.sample), CaptureKind::NoteOff, CaptureStream::Generated, (uint8_t) e.channel(), (uint8_t) e.note(), 0 });
                }
            }

            CaptureClockInfo info;
            info.now = clock.blockEnd();
            info.bpm = clock.tempo();
            info.numerator = t.numerator > 0 ? t.numerator : 4;
            info.denominator = t.denominator > 0 ? t.denominator : 4;
            info.playing = clock.isPlaying();
            info.blocks = ++blocks;
            queue.publish (info);
        }

        const CaptureClock& tape() const noexcept { return clock; }
        int openNotes() const noexcept { return (int) (genOpen.count() + sourceOpen.count()); }

    private:
        bool push (const CaptureEvent& e) noexcept { return queue.push (e); }

        CaptureQueue& queue;
        CaptureClock clock;
        bool armed = false;
        uint64_t blocks = 0;
        std::bitset<16 * 128> genOpen, sourceOpen;
        std::array<uint8_t, 16 * 128> sourceChannelOf {};
    };
}
