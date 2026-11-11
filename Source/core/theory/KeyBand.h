// A key's notes (PLAN.md §3.6): the seven pitch classes of a major or natural minor key, and
// whether a chord lies inside them (*In key*). On the lattice they are a compact band of nodes
// (C major: F C G D on one row, A E B above), which Phase 4 tints. Plain C++, no JUCE.
#pragma once

#include "../lattice/Lattice.h"
#include "Scales.h"

namespace tonnetz
{
    // The key's seven pitch classes.
    inline PcMask keyMask (int tonic, bool minor) noexcept
    {
        unsigned m = 0;
        for (const int st : scaleInfo (minor ? Scale::NaturalMinor : Scale::Major).semitones)
            m |= pcBit (mod12 (tonic + st));
        return (PcMask) m;
    }

    // Every tone of `chord` is in the key (an empty chord is not).
    inline bool chordInKey (PcMask chord, int tonic, bool minor) noexcept
    {
        const unsigned c = (unsigned) chord & 0xfffu;
        return c != 0 && (c & ~(unsigned) keyMask (tonic, minor)) == 0;
    }

    // A key, or none (`key` Off, or Listen with no guess yet).
    struct KeyRef
    {
        int tonic = -1;                    // 0..11, -1 = none
        bool minor = false;

        bool known() const noexcept { return tonic >= 0 && tonic < 12; }
        bool contains (PcMask chord) const noexcept { return known() && chordInKey (chord, tonic, minor); }
    };
}
