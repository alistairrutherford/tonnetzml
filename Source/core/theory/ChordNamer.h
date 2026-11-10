// Chord naming and root finding: a pitch-class set and its bass -> a name with root, quality,
// extensions, alterations and slash bass ("C", "Dm7", "Bm7b5", "G13b9", "Cmaj7/G"). Plain C++, no
// JUCE. Copied from ChordMonkey's ChordNamer.h (PLAN.md §4.1), with three changes:
//
//   - the parts that read ChordMonkey's stack-of-thirds chord slots are left out;
//   - the reading of a chord on a root is split into `analyse` (plain data and a cost, no
//     strings) and `qualityText`, so that `chordRoot` finds the root with no allocation: the
//     lattice ops need it on the audio thread (Source/core/lattice/Ops.h). The names and costs
//     are ChordMonkey's;
//   - equal readings are tried from the bass upward (ChordMonkey: from C upward), so a chord's
//     root moves with the chord when it is transposed.
//
// Every pitch class of the chord is tried as the root. Each reading gets a cost (sus, 6th,
// altered tones, tones it can't explain, a bass that isn't the root...) and the cheapest wins;
// ties go to the preferred root, then to the bass, then to the root nearest above the bass.
// Names are spelled in the key: scale tones take the scale's letters (F# in D major, Bb in F
// major), other pitch classes the nearer spelling.
#pragma once

#include "Scales.h"
#include <array>
#include <cstdint>
#include <cstdlib>
#include <string>

namespace tonnetz
{
    // How the twelve pitch classes are written in a key and scale.
    struct Spelling
    {
        std::array<int8_t, 12> letter {};      // 0..6 = C..B
        std::array<int8_t, 12> accidental {};  // -2..+2

        std::string name (int pc) const
        {
            pc = ((pc % 12) + 12) % 12;
            std::string s (1, "CDEFGAB"[letter[(size_t) pc]]);
            const int a = accidental[(size_t) pc];
            s.append ((size_t) (a < 0 ? -a : a), a < 0 ? 'b' : '#');
            return s;
        }
    };

    namespace naming
    {
        constexpr int naturalPc[7] = { 0, 2, 4, 5, 7, 9, 11 };

        constexpr int wrap (int semis) noexcept { return (((semis % 12) + 18) % 12) - 6; }   // -6..+5

        inline int popcount12 (unsigned m) noexcept
        {
            int n = 0;
            for (int i = 0; i < 12; ++i)
                n += (int) ((m >> i) & 1u);
            return n;
        }
    }

    // The spelling of a key and scale. The tonic's letter is the one that writes the scale with
    // the fewest accidentals (Db major, not C#; F# minor, not Gb). A tie goes to sharps in scales
    // with a major 3rd (F# major) and to flats in scales with a minor 3rd (Eb minor). Pitch classes
    // outside the scale take the spelling with the smaller accidental. On a tie, scales with a
    // major 3rd lower the tone above (Bb, Eb, Ab in C) except the #4 (F#), and scales with a minor
    // 3rd raise the tone below (G# in A minor) except the b2 (Bb in A minor).
    inline Spelling makeSpelling (int key, Scale scale) noexcept
    {
        using namespace naming;
        key = ((key % 12) + 12) % 12;
        const auto& steps = scaleInfo (scale).semitones;

        int bestLetter = 0, bestScore = 1 << 30;
        for (int L = 0; L < 7; ++L)
        {
            if (std::abs (wrap (key - naturalPc[L])) > 1)
                continue;
            int score = 0;
            for (int i = 0; i < 7; ++i)
            {
                const int a = wrap (key + steps[(size_t) i] - naturalPc[(L + i) % 7]);
                score += a < 0 ? -a : a;
                if (a < -2 || a > 2)
                    score += 100;
                else if (a == 2 || a == -2)
                    score += 10;
            }
            const bool preferred = (wrap (key - naturalPc[L]) > 0) == (steps[2] == 4);
            if (score < bestScore || (score == bestScore && preferred))
            {
                bestScore = score;
                bestLetter = L;
            }
        }

        Spelling sp;
        std::array<bool, 12> done {};
        for (int i = 0; i < 7; ++i)
        {
            const int pc = (key + steps[(size_t) i]) % 12;
            const int L = (bestLetter + i) % 7;
            sp.letter[(size_t) pc] = (int8_t) L;
            sp.accidental[(size_t) pc] = (int8_t) wrap (pc - naturalPc[L]);
            done[(size_t) pc] = true;
        }

        for (int pc = 0; pc < 12; ++pc)
        {
            if (done[(size_t) pc])
                continue;
            // Raise the scale tone below, or lower the scale tone above.
            int below = (pc + 11) % 12, above = (pc + 1) % 12;
            int sharpL = -1, sharpA = 0, flatL = -1, flatA = 0;
            if (done[(size_t) below])
            {
                sharpL = sp.letter[(size_t) below];
                sharpA = sp.accidental[(size_t) below] + 1;
            }
            if (done[(size_t) above])
            {
                flatL = sp.letter[(size_t) above];
                flatA = sp.accidental[(size_t) above] - 1;
            }
            if (sharpL < 0 && flatL < 0)
            {
                // No scale neighbour (never with seven-tone scales): plain names.
                static constexpr int8_t L[12] = { 0, 1, 1, 2, 2, 3, 3, 4, 5, 5, 6, 6 };
                sp.letter[(size_t) pc] = L[pc];
                sp.accidental[(size_t) pc] = (int8_t) wrap (pc - naturalPc[L[pc]]);
                continue;
            }
            bool useSharp = flatL < 0;
            if (sharpL >= 0 && flatL >= 0)
            {
                const int as = sharpA < 0 ? -sharpA : sharpA, af = flatA < 0 ? -flatA : flatA;
                const bool minorThird = steps[2] == 3;
                const bool raise = minorThird ? pc != (key + 1) % 12 : pc == (key + 6) % 12;
                useSharp = as < af || (as == af && raise);
            }
            sp.letter[(size_t) pc] = (int8_t) (useSharp ? sharpL : flatL);
            sp.accidental[(size_t) pc] = (int8_t) (useSharp ? sharpA : flatA);
        }
        return sp;
    }

    // A table of twelve line-of-fifths indices (Source/core/lattice/Lattice.h: F = -1, C = 0,
    // G = 1 ... each pitch class's index, as `SpellingMap::index`) as a Spelling, so chord names
    // can be spelled the way the lattice is labelled.
    inline Spelling spellingFromFifths (const std::array<int8_t, 12>& fifthsIndex) noexcept
    {
        Spelling sp;
        for (int pc = 0; pc < 12; ++pc)
        {
            const int f = fifthsIndex[(size_t) pc] + 1;            // F = 0
            const int acc = f >= 0 ? f / 7 : -((6 - f) / 7);
            static constexpr int8_t letterOf[7] = { 3, 0, 4, 1, 5, 2, 6 };   // F C G D A E B
            sp.letter[(size_t) pc] = letterOf[f - 7 * acc];
            sp.accidental[(size_t) pc] = (int8_t) acc;
        }
        return sp;
    }

    struct ChordName
    {
        int root = -1;            // pitch class, -1 for no notes
        int bass = -1;            // pitch class of the lowest note
        std::string rootName, quality, bassName;

        bool empty() const noexcept { return root < 0; }
        std::string text() const { return empty() ? std::string() : rootName + quality + (bass != root ? "/" + bassName : std::string()); }
    };

    namespace naming
    {
        enum class Triad : uint8_t { Major, Minor, Dim, Aug, Sus4, Sus2, Bare };
        enum class Seventh : uint8_t { None, Minor, Major, Dim };

        // A chord read on one root: plain data, no strings.
        struct Analysis
        {
            int cost = 0;
            Triad t = Triad::Bare;
            Seventh s = Seventh::None;
            bool p5 = false;
            bool flat5 = false, sharp5 = false, flat9 = false, sharp9 = false, sharp11 = false, flat13 = false;
            bool sixth = false, nine = false, eleven = false, thirteen = false;
        };

        // Reads `m` (bit i = the tone i semitones above the root; bit 0 set) as a chord on that
        // root.
        inline Analysis analyse (unsigned m) noexcept
        {
            auto has = [m] (int i) { return ((m >> i) & 1u) != 0; };
            Analysis a;
            Triad& t = a.t;
            Seventh& s = a.s;
            unsigned used = 1;
            auto use = [&used] (int i) { used |= 1u << i; };
            auto unused = [&used, &has] (int i) { return has (i) && ((used >> i) & 1u) == 0; };

            bool& sharp9 = a.sharp9;
            if (has (4))
            {
                t = Triad::Major;
                use (4);
                if (has (3)) { sharp9 = true; use (3); }
            }
            else if (has (3)) { t = Triad::Minor; use (3); }
            else if (has (5)) { t = Triad::Sus4; use (5); }
            else if (has (2)) { t = Triad::Sus2; use (2); }
            else t = Triad::Bare;

            const bool p5 = has (7);
            a.p5 = p5;
            if (p5)
                use (7);
            if (t == Triad::Minor && ! p5 && has (6) && ! sharp9) { t = Triad::Dim; use (6); }
            else if (t == Triad::Major && ! p5 && has (8) && ! sharp9) { t = Triad::Aug; use (8); }

            if (has (10)) { s = Seventh::Minor; use (10); }
            else if (has (11)) { s = Seventh::Major; use (11); }
            else if (t == Triad::Dim && has (9)) { s = Seventh::Dim; use (9); }

            bool &flat5 = a.flat5, &sharp5 = a.sharp5, &flat9 = a.flat9, &sharp11 = a.sharp11, &flat13 = a.flat13;
            bool &sixth = a.sixth, &nine = a.nine, &eleven = a.eleven, &thirteen = a.thirteen;
            if (unused (6)) { (p5 ? sharp11 : flat5) = true; use (6); }
            if (unused (8)) { (p5 || t == Triad::Dim ? flat13 : sharp5) = true; use (8); }
            if (unused (9)) { (s != Seventh::None ? thirteen : sixth) = true; use (9); }
            if (unused (1)) { flat9 = true; use (1); }
            if (unused (2)) { nine = true; use (2); }
            if (unused (5)) { eleven = true; use (5); }

            // An augmented triad with a 7th is written as a #5 ("C7#5", "Cmaj7#5").
            if (t == Triad::Aug && s != Seventh::None) { t = Triad::Major; sharp5 = true; }

            int& cost = a.cost;
            cost += 3 * popcount12 (m & ~used);
            const int alterations = (int) flat5 + (int) sharp5 + (int) flat9 + (int) sharp9 + (int) flat13 + (int) sharp11;
            const int extensions = (int) nine + (int) eleven + (int) thirteen;
            switch (t)
            {
                case Triad::Bare:  cost += (p5 && s == Seventh::None && m == (1u | 1u << 7)) ? 1 : 4; break;
                case Triad::Sus4:
                case Triad::Sus2:  cost += 1; break;
                case Triad::Dim:
                    // Diminished chords rarely carry tensions: such a reading is usually a
                    // dominant 7b9 or an altered chord on another root.
                    cost += (s == Seventh::None ? 1 : 0) + extensions + 2 * (int) thirteen
                          + (s == Seventh::Major ? 2 : 0) + (s == Seventh::Dim && extensions + alterations > 0 ? 2 : 0);
                    break;
                case Triad::Aug:   cost += 1; break;
                case Triad::Major:
                case Triad::Minor: break;
            }
            // Altered tones are at home on a dominant 7th (C7b9, G7#5#9), less so elsewhere.
            const bool dominant = t == Triad::Major && s == Seventh::Minor;
            cost += dominant ? alterations : 2 * (alterations - (int) sharp11) + (int) sharp11;
            cost += (int) (t == Triad::Minor && s == Seventh::Major);
            cost += (int) sixth + (int) (eleven && has (4))                   // the 11th clashes with a major 3rd
                  + (int) (eleven && ! nine && s != Seventh::None)            // an 11th chord without its 9th
                  + (int) (thirteen && ! nine)                                // a 13th chord without its 9th
                  + (int) (s == Seventh::None && ! p5 && (nine || eleven)     // "add" chords keep their 5th
                           && (t == Triad::Major || t == Triad::Minor));
            return a;
        }

        // The quality of an analysed chord as text ("m7", "maj9", "7sus4", "dim7"...).
        inline std::string qualityText (const Analysis& a)
        {
            const Triad t = a.t;
            const Seventh s = a.s;
            const bool p5 = a.p5, sixth = a.sixth, nine = a.nine, eleven = a.eleven, thirteen = a.thirteen;

            // The number of a seventh chord: its highest natural extension.
            const int ext = thirteen ? 13 : (eleven && t != Triad::Sus4 ? 11 : (nine && t != Triad::Sus2 ? 9 : 7));
            const std::string num = std::to_string (ext);
            const std::string six = sixth ? (nine ? "6/9" : "6") : "";
            std::string adds;
            if (s == Seventh::None || s == Seventh::Dim)
            {
                if (nine && ! (sixth && t != Triad::Sus2)) adds += "add9";
                if (eleven) adds += "add11";
            }

            std::string q;
            switch (t)
            {
                case Triad::Major:
                    q = s == Seventh::None ? six + adds : (s == Seventh::Minor ? num : "maj" + num);
                    break;
                case Triad::Minor:
                    q = s == Seventh::None ? "m" + six + adds : (s == Seventh::Minor ? "m" + num : "m(maj" + num + ")");
                    break;
                case Triad::Dim:
                    q = s == Seventh::None ? "dim" + adds
                      : s == Seventh::Dim ? "dim7" + adds
                      : s == Seventh::Minor ? "m" + num + "b5" : "dim(maj" + num + ")";
                    break;
                case Triad::Aug:
                    q = "aug" + adds;
                    break;
                case Triad::Sus4:
                    q = s == Seventh::None ? six + "sus4" + adds : (s == Seventh::Minor ? num : "maj" + num) + "sus4";
                    break;
                case Triad::Sus2:
                    q = s == Seventh::None ? six + "sus2" + adds : (s == Seventh::Minor ? num : "maj" + num) + "sus2";
                    break;
                case Triad::Bare:
                    q = s == Seventh::None ? (p5 ? "5" : "") + adds : (s == Seventh::Minor ? num : "maj" + num) + "(no3)";
                    break;
            }

            std::string alt;
            if (a.flat5)   alt += "b5";
            if (a.sharp5)  alt += "#5";
            if (a.flat9)   alt += "b9";
            if (a.sharp9)  alt += "#9";
            if (a.sharp11) alt += "#11";
            if (a.flat13)  alt += "b13";
            if (! alt.empty())
                q += (s == Seventh::None || s == Seventh::Dim) ? "(" + alt + ")" : alt;
            return q;
        }

        // The pitch classes of `pcMask` (bit pc) as bit i = the tone i semitones above `root`.
        constexpr unsigned relativeTo (unsigned pcMask, int root) noexcept
        {
            return ((pcMask >> root) | (pcMask << (12 - root))) & 0xfffu;
        }
    }

    // The root of the pitch classes in `pcMask` (bit pc) over `bassPc`: the cheapest reading, as
    // `nameChord` names it. -1 for no notes. `bassPc` < 0: the lowest pitch class set stands in
    // for the bass. No allocation: safe on the audio thread.
    inline int chordRoot (unsigned pcMask, int bassPc, int preferredRoot = -1) noexcept
    {
        pcMask &= 0xfffu;
        if (pcMask == 0)
            return -1;
        if (bassPc < 0)
            for (bassPc = 0; ((pcMask >> bassPc) & 1u) == 0; ++bassPc) {}
        bassPc = ((bassPc % 12) + 12) % 12;
        pcMask |= 1u << bassPc;

        int best = -1, bestCost = 1 << 30, bestRank = -1;
        for (int k = 0; k < 12; ++k)
        {
            const int r = (bassPc + k) % 12;
            if (((pcMask >> r) & 1u) == 0)
                continue;
            const int cost = naming::analyse (naming::relativeTo (pcMask, r)).cost + (r != bassPc ? 1 : 0);
            const int rank = (r == preferredRoot ? 2 : 0) + (r == bassPc ? 1 : 0);
            if (cost < bestCost || (cost == bestCost && rank > bestRank))
            {
                bestCost = cost;
                bestRank = rank;
                best = r;
            }
        }
        return best;
    }

    // Names the pitch classes in `pcMask` (bit pc) over `bassPc`. `preferredRoot` (a pitch class,
    // or -1) wins ties.
    inline ChordName nameChord (unsigned pcMask, int bassPc, const Spelling& spelling, int preferredRoot = -1)
    {
        ChordName best;
        pcMask &= 0xfffu;
        if (pcMask == 0)
            return best;
        bassPc = ((bassPc % 12) + 12) % 12;
        pcMask |= 1u << bassPc;

        best.root = chordRoot (pcMask, bassPc, preferredRoot);
        if (naming::popcount12 (pcMask) > 1)
            best.quality = naming::qualityText (naming::analyse (naming::relativeTo (pcMask, best.root)));
        best.bass = bassPc;
        best.rootName = spelling.name (best.root);
        best.bassName = spelling.name (bassPc);
        return best;
    }
}
