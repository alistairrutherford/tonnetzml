// Orbit's clock (PLAN.md §3.1, §3.3): the grid lines at which Orbit takes its next step, placed
// on the samples of a block from the host's ppq (Clock.h), across loops, locates and tempo
// changes.
//
// The grid is every Rate (1/4, 1/2, 1, 2 or 4 bars) from the bar lines: lines at
// origin + n · Rate · barLength, origin the host's bar start (or ppq 0 without one). Rates of
// several bars count bars from ppq 0, so 2 bars falls on bars 1, 3, 5, … whatever bar the host
// reports.
//
// A line falls on its nearest sample (as the test rig places its keys, TestRig.h, so a chord
// written on a line and the line land on one sample; the line goes first). Each is taken once: within continuous musical time a line already taken is never taken again, and a
// line up to one sample before a segment's start (a block boundary, the start after a loop wrap
// or a locate) belongs to that segment. A line at a loop's end belongs after the wrap: it is
// the loop start, taken there. Tempo changes need nothing: each block's samples come from its
// own tempo.
//
// While the host is stopped the Clock runs on its own at the last tempo; `restartAt` puts a line
// at a given sample (a chord pressed with nothing playing starts an orbit of its own).
//
// Not an EventQueue user: the lines are worked out from the grid in every block, which is what
// keeps them on the grid through loops and locates without moving queued events about.
// Plain C++, no JUCE, no allocation.
#pragma once

#include "Clock.h"
#include <cmath>
#include <cstdint>

namespace tonnetz
{
    constexpr int numOrbitRates = 5;
    constexpr double orbitRateBars[numOrbitRates] = { 0.25, 0.5, 1.0, 2.0, 4.0 };
    constexpr const char* orbitRateNames[numOrbitRates] = { "1/4 bar", "1/2 bar", "1 bar", "2 bars", "4 bars" };
    constexpr int defaultOrbitRate = 2;    // 1 bar

    class OrbitClock
    {
    public:
        void reset() noexcept
        {
            guardValid = false;
            prevSegments = 0;
            pendingValid = false;
        }

        // Once per block, after Clock::beginBlock.
        void beginBlock (const Clock& c) noexcept
        {
            guardValid = guardValid && guardSeg == prevSegments - 1 && ! c.jumpedAtStart();
            guardSeg = 0;
            prevSegments = c.numSegments();
            pendingValid = false;
        }

        // The block sample of the next line at or after block sample `from` at rate `rateIndex`,
        // or -1 if there is none in this block. `fired` takes it.
        int next (const Clock& c, int from, int rateIndex) noexcept
        {
            pendingValid = false;
            rateIndex = rateIndex < 0 ? 0 : (rateIndex >= numOrbitRates ? numOrbitRates - 1 : rateIndex);
            const double barLen = c.barLength() > 0.0 ? c.barLength() : 4.0;
            const double bars = orbitRateBars[rateIndex];
            const double step = bars * barLen;
            double origin = c.barOrigin();
            if (bars > 1.0)
            {
                // Count bars from ppq 0: bar index of the host's bar start, back to a multiple of the rate.
                const auto n = (int64_t) bars;
                const auto bar = (int64_t) std::llround (origin / barLen);
                origin -= (double) (((bar % n) + n) % n) * barLen;
            }
            const double spq = c.samplesPerQuarter();
            for (int i = 0; i < c.numSegments(); ++i)
            {
                const auto& seg = c.segment (i);
                if (seg.end <= from)
                    continue;
                const int s0 = seg.start > from ? seg.start : from;
                const double p = c.ppqAt (i, s0);
                const double tol = s0 == seg.start ? 1.0 / spq : 0.0;
                double g = origin + std::ceil ((p - tol - origin) / step - 1.0e-9) * step;
                if (guardValid && guardSeg == i)
                    while (g <= lastTick + 1.0e-7 * step)
                        g += step;
                if (g >= seg.endPpq - 1.0e-9)
                    continue;
                auto s = (int64_t) std::llround (seg.start + (g - seg.ppq) * spq);
                s = s < s0 ? s0 : s;
                if (s >= seg.end)
                    continue;               // its nearest sample is in the next block: taken there
                pending = { (int) s, g, i };
                pendingValid = true;
                return (int) s;
            }
            return -1;
        }

        // The line `next` returned is taken.
        void fired() noexcept
        {
            if (! pendingValid)
                return;
            lastTick = pending.ppq;
            guardSeg = pending.seg;
            guardValid = true;
            pendingValid = false;
        }

        // The internal clock was anchored (Clock::anchor): its line at `ppq` counts as taken.
        void restartAt (double ppq) noexcept
        {
            lastTick = ppq;
            guardSeg = 0;
            guardValid = true;
            pendingValid = false;
        }

    private:
        struct Pending
        {
            int sample = 0;
            double ppq = 0.0;
            int seg = 0;
        };

        Pending pending;
        bool pendingValid = false, guardValid = false;
        double lastTick = 0.0;
        int guardSeg = 0, prevSegments = 0;
    };
}
