// A chord's Roman numeral in a key (PLAN.md §3.6, "Numerals": `Cmaj7 (I) ──P·L──► A♭maj7 (♭VI)`).
// ChordMonkey's convention (its RomanNumeral.h, which works on its own chord slots and so isn't
// copied): upper case for a major triad, lower case for minor, "°" after a diminished one and "+"
// after an augmented one; chords without a third (sus, power, single notes) in upper case; a root
// off the scale marked ♭ or ♯ ("♭VI", "♯iv°"). The numeral is the triad's: a seventh changes the
// chord's name, not its numeral.
//
// The degree comes from the root's letter in the key's spelling (Lattice.h, spellingForKey), so
// A♭ in C major is ♭VI and G♯ is ♯V, as they are spelled. Plain C++, no JUCE; std::string, so
// not for the audio thread.
#pragma once

#include "../lattice/Lattice.h"
#include "Scales.h"
#include <string>

namespace tonnetz
{
    // `rootPc` the chord's root, `mask` its pitch classes. Empty if there is no root or no key.
    inline std::string romanNumeral (int rootPc, unsigned mask, int tonicPc, bool minor, bool unicode = true)
    {
        if (rootPc < 0 || tonicPc < 0 || (mask & 0xfffu) == 0)
            return {};
        static constexpr const char* upper[7] = { "I", "II", "III", "IV", "V", "VI", "VII" };
        static constexpr const char* lower[7] = { "i", "ii", "iii", "iv", "v", "vi", "vii" };

        const SpellingMap spelling = spellingForKey (tonicPc, minor);
        const int degree = (spelling.spell (rootPc).letter - spelling.spell (tonicPc).letter + 14) % 7;
        const int expected = mod12 (tonicPc + scaleOffset (minor ? Scale::NaturalMinor : Scale::Major, degree));
        int accidental = mod12 (rootPc - expected + 6) - 6;          // -6..5
        accidental = accidental < -2 ? -2 : (accidental > 2 ? 2 : accidental);

        std::string s;
        for (int i = 0; i < (accidental < 0 ? -accidental : accidental); ++i)
            s += unicode ? (accidental < 0 ? "\xe2\x99\xad" : "\xe2\x99\xaf") : (accidental < 0 ? "b" : "#");

        const unsigned rel = transposeMask (mask, -rootPc);
        const bool m3 = hasPc (rel, 3), M3 = hasPc (rel, 4), p5 = hasPc (rel, 7), d5 = hasPc (rel, 6), a5 = hasPc (rel, 8);
        if (M3 && a5 && ! p5)       { s += upper[degree]; s += "+"; }
        else if (M3)                { s += upper[degree]; }
        else if (m3 && d5 && ! p5)  { s += lower[degree]; s += unicode ? "\xc2\xb0" : "o"; }
        else if (m3)                { s += lower[degree]; }
        else                        { s += upper[degree]; }
        return s;
    }
}
