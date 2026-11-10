// The two scales TonnetzML needs, major and natural minor (PLAN.md §3.6: the key guess is major
// or minor only). Copied from ChordMonkey's Scales.h and cut down. Plain C++, no JUCE.
#pragma once

#include <array>
#include <cstdint>

namespace tonnetz
{
    constexpr int numKeys = 12;

    enum class Scale : uint8_t
    {
        Major,
        NaturalMinor,
        Count
    };

    constexpr int numScales = (int) Scale::Count;

    using ScaleSteps = std::array<int8_t, 7>;   // semitones above the tonic, degrees 1..7

    struct ScaleInfo
    {
        const char* name;
        ScaleSteps semitones;
    };

    inline const ScaleInfo& scaleInfo (Scale s) noexcept
    {
        static constexpr ScaleInfo table[numScales] = {
            { "Major",         { 0, 2, 4, 5, 7, 9, 11 } },
            { "Natural Minor", { 0, 2, 3, 5, 7, 8, 10 } },
        };
        const int i = (int) s;
        return table[i >= 0 && i < numScales ? i : 0];
    }

    // Semitones from the tonic to scale index `i` (0 = tonic, 7 = the tonic an octave up,
    // negative = below the tonic).
    inline int scaleOffset (Scale s, int i) noexcept
    {
        const int octave = i >= 0 ? i / 7 : -((6 - i) / 7);
        return scaleInfo (s).semitones[(size_t) (i - 7 * octave)] + 12 * octave;
    }
}
