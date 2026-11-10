// The Tonnetz as the plug-in uses it (PLAN.md §2): lattice vectors, their pitch classes, the
// torus the plane wraps into in equal temperament, and note spelling from the line of fifths.
// Plain C++17, no JUCE, no allocation: everything here is safe on the audio thread.
//
//   node (a, b)  ->  pitch class 7a + 4b (mod 12)
//
//        E --- B --- F#          (1, 0)   one step right     perfect fifth  +7
//       / \   / \   /            (0, 1)   up and right       major third    +4
//      C --- G --- D             (1, -1)  down and right     minor third    +3
//
// Upward triangles (a,b) (a+1,b) (a,b+1) are major triads, downward triangles
// (a,b) (a+1,b) (a+1,b-1) minor triads.
#pragma once

#include <array>
#include <cstdint>

namespace tonnetz
{
    constexpr int mod12 (int x) noexcept { return ((x % 12) + 12) % 12; }

    // A pitch-class set: bit pc set = that pitch class is in the chord.
    using PcMask = uint16_t;

    constexpr bool hasPc (unsigned mask, int pc) noexcept { return ((mask >> mod12 (pc)) & 1u) != 0; }
    constexpr PcMask pcBit (int pc) noexcept { return (PcMask) (1u << mod12 (pc)); }

    constexpr int pcCount (unsigned mask) noexcept
    {
        int n = 0;
        for (int i = 0; i < 12; ++i)
            n += (int) ((mask >> i) & 1u);
        return n;
    }

    // `mask` moved up by `semitones` (any integer).
    constexpr PcMask transposeMask (unsigned mask, int semitones) noexcept
    {
        const int s = mod12 (semitones);
        mask &= 0xfffu;
        return (PcMask) (((mask << s) | (mask >> (12 - s))) & 0xfffu);
    }

    // A lattice vector, or a node of the unrolled plane.
    struct Vec
    {
        int a = 0;      // fifths
        int b = 0;      // major thirds

        constexpr Vec operator+ (Vec o) const noexcept { return { a + o.a, b + o.b }; }
        constexpr Vec operator- (Vec o) const noexcept { return { a - o.a, b - o.b }; }
        constexpr Vec operator- () const noexcept { return { -a, -b }; }
        constexpr Vec operator* (int k) const noexcept { return { a * k, b * k }; }
        constexpr bool operator== (Vec o) const noexcept { return a == o.a && b == o.b; }
        constexpr bool operator!= (Vec o) const noexcept { return ! (*this == o); }
        constexpr bool operator< (Vec o) const noexcept { return a != o.a ? a < o.a : b < o.b; }
    };

    constexpr Vec fifthStep { 1, 0 }, majorThirdStep { 0, 1 }, minorThirdStep { 1, -1 };

    // The six neighbours of a node, anticlockwise from the fifth above.
    constexpr std::array<Vec, 6> unitSteps { { { 1, 0 }, { 0, 1 }, { -1, 1 }, { -1, 0 }, { 0, -1 }, { 1, -1 } } };

    constexpr int pitchClass (Vec v) noexcept { return mod12 (7 * v.a + 4 * v.b); }

    // Steps between two nodes along the lattice's three axes: 1 for neighbours.
    constexpr int latticeNorm (Vec v) noexcept
    {
        const int a = v.a < 0 ? -v.a : v.a, b = v.b < 0 ? -v.b : v.b;
        return (v.a < 0) != (v.b < 0) ? (a > b ? a : b) : a + b;
    }
    constexpr int latticeDistance (Vec p, Vec q) noexcept { return latticeNorm (q - p); }
    constexpr bool adjacent (Vec p, Vec q) noexcept { return latticeDistance (p, q) == 1; }

    // ---- The torus -------------------------------------------------------------------------
    // Two nodes are the same pitch class exactly when they differ by i·(4,-1) + j·(0,3): twelve
    // nodes, twenty-four triangles.
    constexpr Vec torusPeriodA { 4, -1 }, torusPeriodB { 0, 3 };

    constexpr bool samePitchClass (Vec p, Vec q) noexcept { return pitchClass (p) == pitchClass (q); }

    // The copy of a pitch class in the 4 × 3 block a = 0..3, b = 0..2 (C at the origin): one node
    // for each of the twelve.
    constexpr Vec homeNode (int pc) noexcept
    {
        pc = mod12 (pc);
        for (int b = 0; b < 3; ++b)
            for (int a = 0; a < 4; ++a)
                if (mod12 (7 * a + 4 * b) == pc)
                    return { a, b };
        return {};
    }

    // The copy of `pc` nearest `near` (ties: the smaller a, then the smaller b).
    constexpr Vec nearestNode (int pc, Vec near) noexcept
    {
        const Vec home = homeNode (pc);
        // Round to the period lattice, then look around.
        const Vec d = near - home;
        const int i0 = (d.a >= 0 ? d.a + 2 : d.a - 2) / 4;
        const int bRest = d.b + i0;
        const int j0 = (bRest >= 0 ? bRest + 1 : bRest - 1) / 3;
        Vec best = home;
        int bestDistance = 1 << 30;
        for (int i = i0 - 2; i <= i0 + 2; ++i)
            for (int j = j0 - 2; j <= j0 + 2; ++j)
            {
                const Vec v = home + torusPeriodA * i + torusPeriodB * j;
                const int dist = latticeDistance (v, near);
                if (dist < bestDistance || (dist == bestDistance && v < best))
                {
                    bestDistance = dist;
                    best = v;
                }
            }
        return best;
    }

    // Steps between two pitch classes on the torus (0, 1 or 2: no two are further apart).
    constexpr int torusDistance (int pcFrom, int pcTo) noexcept
    {
        constexpr int table[12] = { 0, 2, 2, 1, 1, 1, 2, 1, 1, 1, 2, 2 };
        return table[mod12 (pcTo - pcFrom)];
    }

    // ---- Triangles -------------------------------------------------------------------------
    // The major triad (upward triangle) and minor triad (downward triangle) rooted at a node.
    constexpr std::array<Vec, 3> majorTriangle (Vec root) noexcept { return { { root, root + Vec { 0, 1 }, root + Vec { 1, 0 } } }; }
    constexpr std::array<Vec, 3> minorTriangle (Vec root) noexcept { return { { root, root + Vec { 1, -1 }, root + Vec { 1, 0 } } }; }

    constexpr PcMask majorTriadMask (int root) noexcept { return (PcMask) (pcBit (root) | pcBit (root + 4) | pcBit (root + 7)); }
    constexpr PcMask minorTriadMask (int root) noexcept { return (PcMask) (pcBit (root) | pcBit (root + 3) | pcBit (root + 7)); }

    // ---- Spelling --------------------------------------------------------------------------
    // A note's place on the line of fifths fixes its letter and accidentals:
    //
    //   ... Bbb(-9) Fb(-8) Cb Gb Db Ab Eb Bb(-2) | F(-1) C(0) G D A E B(5) | F#(6) C# ... B#(12) Fx(13) ...
    //
    // A node's own index is a + 4b; its pitch class is 7·index (mod 12), so moving an index by 12
    // is an enharmonic respelling.
    constexpr int fifthsIndex (Vec v) noexcept { return v.a + 4 * v.b; }
    constexpr int pitchClassOfFifths (int index) noexcept { return mod12 (7 * index); }

    // Double flats to double sharps.
    constexpr int minFifthsIndex = -15, maxFifthsIndex = 19;

    constexpr int floorDiv (int x, int d) noexcept { return x >= 0 ? x / d : -((d - 1 - x) / d); }

    struct NoteSpelling
    {
        int8_t letter = 0;        // 0..6 = C D E F G A B
        int8_t accidental = 0;    // flats negative, sharps positive

        constexpr bool operator== (NoteSpelling o) const noexcept { return letter == o.letter && accidental == o.accidental; }
    };

    constexpr NoteSpelling spellingOfFifths (int index) noexcept
    {
        constexpr int8_t letterOf[7] = { 3, 0, 4, 1, 5, 2, 6 };      // F C G D A E B
        const int acc = floorDiv (index + 1, 7);
        return { letterOf[index + 1 - 7 * acc], (int8_t) acc };
    }

    constexpr int fifthsOfSpelling (NoteSpelling s) noexcept
    {
        constexpr int8_t indexOf[7] = { 0, 2, 4, -1, 1, 3, 5 };      // C D E F G A B
        return indexOf[s.letter] + 7 * s.accidental;
    }

    // A note name in a fixed buffer ("C", "F#", "Bbb"; or with ♯ and ♭ as UTF-8).
    struct NoteName
    {
        char text[16] {};
        const char* c_str() const noexcept { return text; }
    };

    inline NoteName noteName (NoteSpelling s, bool unicode = false) noexcept
    {
        NoteName n;
        int at = 0;
        n.text[at++] = "CDEFGAB"[s.letter >= 0 && s.letter < 7 ? s.letter : 0];
        const int count = s.accidental < 0 ? -s.accidental : s.accidental;
        for (int i = 0; i < count && i < 4; ++i)
        {
            if (unicode)
            {
                n.text[at++] = (char) 0xE2;
                n.text[at++] = (char) 0x99;
                n.text[at++] = (char) (s.accidental < 0 ? 0xAD : 0xAF);
            }
            else
            {
                n.text[at++] = s.accidental < 0 ? 'b' : '#';
            }
        }
        n.text[at] = 0;
        return n;
    }

    inline NoteName noteNameOfFifths (int index, bool unicode = false) noexcept { return noteName (spellingOfFifths (index), unicode); }

    // The line-of-fifths index a tonic is written with: the key with the fewer accidentals (Db
    // major, not C#; G# minor, not Ab), a tie going to sharps in major (F#) and flats in minor
    // (Eb). Major tonics land in -5..6, minor tonics in -3..8.
    constexpr int tonicFifthsIndex (int tonicPc, bool minor) noexcept
    {
        const int lo = minor ? -3 : -5;
        return lo + mod12 (7 * mod12 (tonicPc) - lo);
    }

    // How the twelve pitch classes are written around an anchor: each one's line-of-fifths index.
    struct SpellingMap
    {
        std::array<int8_t, 12> index {};

        constexpr int fifths (int pc) const noexcept { return index[(size_t) mod12 (pc)]; }
        constexpr int fifths (Vec node) const noexcept { return fifths (pitchClass (node)); }
        constexpr NoteSpelling spell (int pc) const noexcept { return spellingOfFifths (fifths (pc)); }
        constexpr NoteSpelling spell (Vec node) const noexcept { return spell (pitchClass (node)); }
        NoteName name (int pc, bool unicode = false) const noexcept { return noteName (spell (pc), unicode); }
        NoteName name (Vec node, bool unicode = false) const noexcept { return noteName (spell (node), unicode); }
    };

    // The spelling of a key (PLAN.md §3.6). The seven scale tones are seven neighbours on the line
    // of fifths (major: one below the tonic to five above; minor: four below to two above). Each
    // other pitch class takes whichever of its two nearest spellings has the fewer accidentals;
    // on a tie, the one within five fifths below and six above the tonic (in C: Db Eb F# Ab Bb;
    // in A minor: Bb C# D# F# G#). This is ChordMonkey's `makeSpelling` for major and natural
    // minor, derived from the lattice (checked against the copy in theory/ChordNamer.h).
    constexpr SpellingMap spellingForKey (int tonicPc, bool minor) noexcept
    {
        const int t = tonicFifthsIndex (tonicPc, minor);
        const int scaleLo = minor ? t - 4 : t - 1, scaleHi = scaleLo + 6;
        SpellingMap m;
        for (int pc = 0; pc < 12; ++pc)
        {
            // The candidate inside the window t-5 .. t+6, and its enharmonic twin on the other
            // side of the natural notes.
            const int inWindow = t - 5 + mod12 (7 * pc - (t - 5));
            int chosen = inWindow;
            if (inWindow < scaleLo || inWindow > scaleHi)
            {
                const int other = inWindow + (inWindow > 2 ? -12 : 12);
                const int a = spellingOfFifths (inWindow).accidental, b = spellingOfFifths (other).accidental;
                if ((b < 0 ? -b : b) < (a < 0 ? -a : a))
                    chosen = other;
            }
            m.index[(size_t) pc] = (int8_t) chosen;
        }
        return m;
    }

    // The spelling around a node of the lattice (the editor's camera, with no key to go by): the
    // node's pitch class as a major tonic.
    constexpr SpellingMap spellingAround (Vec centre) noexcept { return spellingForKey (pitchClass (centre), false); }

    // The plane spelled literally from a centre: each node's index is the centre's plus its
    // offset a + 4b, so the same pitch class is written differently as the plane repeats (E, then
    // Fb three major thirds down). Indices past a double accidental are respelled by 12.
    constexpr int literalFifthsIndex (Vec node, Vec centre, int centreIndex) noexcept
    {
        int f = centreIndex + fifthsIndex (node - centre);
        while (f > maxFifthsIndex) f -= 12;
        while (f < minFifthsIndex) f += 12;
        return f;
    }
}
