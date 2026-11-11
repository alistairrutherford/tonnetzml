// The engine's clock (Chordism's PLAN.md §4.5): musical time for every sample of a block.
//
// While the host plays, it follows the host transport: PPQ, tempo, time signature, bar
// position and loop points. A block that crosses the loop end is split into segments, each a
// straight run of musical time, so events after the wrap land on the right sample even when
// the host doesn't split its blocks at the loop point. A host position that doesn't continue
// from the end of the last block (a locate, a loop wrap the host made itself, a start of
// playback) is reported as a jump, with how far musical time moved.
//
// While the host is stopped, an internal clock runs at the last known tempo, so strum, arp and
// repeat still work for auditioning. `anchor` restarts it at bar 1 on a given sample: a key
// pressed with nothing else playing then starts its patterns on a bar line of their own.
//
// Absolute sample time (`blockStart` + offset) counts every sample processed, for events timed
// in samples (Strum while stopped). Plain C++, no allocation.
// Copied from ChordMonkey (namespace tonnetz); TonnetzML schedules with it from Phase 3 (Orbit).
#pragma once

#include "Transport.h"
#include <cmath>
#include <cstdint>

namespace tonnetz
{
    struct ClockSegment
    {
        int start = 0, end = 0;           // samples [start, end) of the block
        double ppq = 0.0;                 // musical time at `start`
        double endPpq = HUGE_VAL;         // musical time the segment stops short of: the loop end,
                                          // if it ends at a wrap (an event there belongs after it)
    };

    class Clock
    {
    public:
        static constexpr int maxSegments = 8;

        void reset() noexcept
        {
            havePrev = false;
            wasPlaying = false;
            endPpq = 0.0;
            absolute = 0;
            nextAbsolute = 0;
            lastBpm = 0.0;
            lastRate = 0.0;
            count = 0;
        }

        // Call once per block, before anything asks for times in it.
        void beginBlock (const Transport& t, int numSamples) noexcept
        {
            numSamples = numSamples > 0 ? numSamples : 0;
            playing = t.playing;
            bpm = t.bpm > 0.0 && std::isfinite (t.bpm) ? t.bpm : (lastBpm > 0.0 ? lastBpm : 120.0);
            rate = t.sampleRate > 0.0 ? t.sampleRate : 44100.0;
            spq = rate * 60.0 / bpm;
            barLen = t.barLength();
            absolute = nextAbsolute;
            nextAbsolute = absolute + numSamples;

            tempoMoved = havePrev && (bpm != lastBpm || rate != lastRate);
            lastBpm = bpm;
            lastRate = rate;

            jumped = false;
            delta = 0.0;
            count = 0;

            if (! playing)
            {
                // Internal clock: carry on from where the last block ended.
                barZero = 0.0;
                segments[count++] = { 0, numSamples, havePrev ? endPpq : 0.0, HUGE_VAL };
            }
            else
            {
                barZero = t.hasBarStart && std::isfinite (t.barStartPpq) ? t.barStartPpq : 0.0;
                const double p0 = std::isfinite (t.ppq) ? t.ppq : 0.0;
                if (havePrev)
                {
                    delta = p0 - endPpq;
                    // A start of playback always counts (internal → host time); otherwise
                    // anything under half a sample is the host's rounding, not a move.
                    jumped = ! wasPlaying || std::abs (delta) * spq > 0.5;
                    if (! jumped)
                        delta = 0.0;
                }
                split (t, p0, numSamples);
            }

            havePrev = true;
            wasPlaying = playing;
            updateEnd (numSamples);
        }

        // Stopped only: musical time is 0 (bar 1, beat 1) at `sample` of this block.
        void anchor (int sample) noexcept
        {
            if (playing || count == 0)
                return;
            segments[0].ppq = -(double) sample / spq;
            updateEnd (segments[0].end);
        }

        bool isPlaying() const noexcept { return playing; }
        double samplesPerQuarter() const noexcept { return spq; }
        double sampleRate() const noexcept { return rate; }
        double tempo() const noexcept { return bpm; }

        // A discontinuity at the first sample of the block, and how far musical time moved.
        bool jumpedAtStart() const noexcept { return jumped; }
        double jumpDelta() const noexcept { return delta; }
        bool tempoChanged() const noexcept { return tempoMoved; }

        int numSegments() const noexcept { return count; }
        const ClockSegment& segment (int i) const noexcept { return segments[i >= 0 && i < count ? i : 0]; }

        // How far musical time moves at the start of segment i (> 0): a loop wrap.
        double wrapDelta (int i) const noexcept
        {
            if (i <= 0 || i >= count)
                return 0.0;
            const auto& a = segments[i - 1];
            const auto& b = segments[i];
            return b.ppq - (a.ppq + (b.start - a.start) / spq);
        }

        double ppqAt (int seg, double sample) const noexcept
        {
            const auto& s = segment (seg);
            return s.ppq + (sample - s.start) / spq;
        }

        // The (fractional) sample of the block where segment `seg` reaches `ppq`.
        double sampleAt (int seg, double ppq) const noexcept
        {
            const auto& s = segment (seg);
            return s.start + (ppq - s.ppq) * spq;
        }

        int64_t blockStart() const noexcept { return absolute; }

        // Bars: lines at barOrigin() + k · barLength().
        double barLength() const noexcept { return barLen; }
        double barOrigin() const noexcept { return barZero; }

        // The bar line at or before `ppq`.
        double barStart (double ppq) const noexcept
        {
            return barZero + std::floor ((ppq - barZero) / barLen + 1.0e-9) * barLen;
        }

        // The first bar line after `ppq`.
        double nextBarLine (double ppq) const noexcept { return barStart (ppq) + barLen; }

        // Position in the bar, 0 ≤ x < 1.
        double barFraction (double ppq) const noexcept
        {
            const double x = (ppq - barStart (ppq)) / barLen;
            return x < 0.0 ? 0.0 : (x >= 1.0 ? 0.0 : x);
        }

    private:
        // Splits the block at each loop end it crosses. A segment that ends at a wrap (inside the
        // block, or exactly at its end, when the host splits its blocks there) stops short of
        // the loop end in musical time too.
        void split (const Transport& t, double p0, int numSamples) noexcept
        {
            const bool loop = t.looping && std::isfinite (t.loopStart) && std::isfinite (t.loopEnd)
                           && t.loopEnd - t.loopStart > 1.0e-6;
            int start = 0;
            double ppq = p0;
            while (true)
            {
                if (loop && count < maxSegments - 1 && ppq < t.loopEnd - 1.0e-9)
                {
                    // First sample whose musical time reaches the loop end.
                    const double at = start + (t.loopEnd - ppq) * spq;
                    const int wrap = (int) std::ceil (at - 1.0e-6);
                    if (wrap > start && wrap < numSamples)
                    {
                        segments[count++] = { start, wrap, ppq, t.loopEnd };
                        ppq = t.loopStart + (ppq + (wrap - start) / spq - t.loopEnd);
                        start = wrap;
                        continue;
                    }
                    if (wrap == numSamples)
                    {
                        segments[count++] = { start, numSamples, ppq, t.loopEnd };
                        return;
                    }
                }
                segments[count++] = { start, numSamples, ppq, HUGE_VAL };
                return;
            }
        }

        void updateEnd (int numSamples) noexcept
        {
            const auto& last = segments[count - 1];
            endPpq = last.ppq + (numSamples - last.start) / spq;
        }

        ClockSegment segments[maxSegments] {};
        int count = 0;
        bool playing = false, wasPlaying = false, havePrev = false;
        bool jumped = false, tempoMoved = false;
        double delta = 0.0;
        double bpm = 120.0, rate = 44100.0, spq = 22050.0, barLen = 4.0, barZero = 0.0;
        double lastBpm = 0.0, lastRate = 0.0;
        double endPpq = 0.0;              // musical time at the end of the last block
        int64_t absolute = 0, nextAbsolute = 0;
    };
}
