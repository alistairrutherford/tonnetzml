// Cycles (PLAN.md §2, §3.1): a word repeated op by op until the chord comes back. The factory
// cycles for Orbit are PL (the hexatonic cycle: 6 triads), PR (octatonic: 8), LR (all 24
// triads) and PLR (6). Plain C++17, no JUCE, no allocation.
#pragma once

#include "Word.h"

namespace tonnetz
{
    struct FactoryCycle
    {
        const char* name;
        const char* word;
    };

    inline constexpr std::array<FactoryCycle, 4> factoryCycles { { { "PL", "PL" }, { "PR", "PR" }, { "LR", "LR" }, { "PLR", "PLR" } } };

    struct CycleInfo
    {
        int steps = -1;           // ops until the whole state (tones, anchor, parity) is back, at
                                  // the end of a pass through the word; -1 if not within the limit
        int chords = 0;           // distinct pitch-class sets met on the way
    };

    // The cycle `word` makes from `start`, stepped op by op (as Orbit steps it). Every op is a
    // bijection on chord states, so every walk comes back; `maxSteps` only bounds the search.
    inline CycleInfo cycleOf (const Word& word, const Chord& start, int maxSteps = 4096) noexcept
    {
        CycleInfo info;
        if (word.empty())
            return info;
        WordRunner run (start);
        std::array<uint64_t, 64> seen {};         // a bit per pitch-class set (4096)
        int distinct = 0;
        for (int step = 1; step <= maxSteps; ++step)
        {
            run.step (word[(step - 1) % word.size]);
            const unsigned m = run.chord().mask & 0xfffu;
            if ((seen[m >> 6] & (1ull << (m & 63))) == 0)
            {
                seen[m >> 6] |= 1ull << (m & 63);
                ++distinct;
            }
            if (step % word.size == 0 && run.chord() == start)
            {
                info.steps = step;
                info.chords = distinct;
                return info;
            }
        }
        return info;
    }
}
