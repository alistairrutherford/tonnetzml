// The lattice operations of PLAN.md §2: P, L, R, N, S, H, translations, Flip, Pivot and Grow,
// on any chord, not only triads. Plain C++17, no JUCE, no allocation: safe on the audio thread.
//
// The ops work on pitch classes. A chord is a `Chord`: its pitch-class set, an anchor (one of its
// tones: the root when the chord comes in) and a parity (0 when it comes in). Every op is an
// affine map of the pitch classes (`PcMap`: x -> sign * x + shift), so each output tone has
// exactly one source tone (Grow apart, which adds one); the voicer (Phase 2) moves voices with
// that map.
//
// Choosing the edge (PLAN.md §2). P, R and L reflect through an edge of 7, 4 and 3 semitones:
// a pair of chord tones (p, p + k), x -> 2p + k - x. When the chord has several such edges
// (Cmaj7 has two fifths), the one used is the first by
//   1. it contains the anchor;
//   2. its midpoint, p + k/2, is nearest the anchor around the pitch-class circle;
//   3. (p - anchor) mod 12 is smallest at parity 0, (anchor - p - k) mod 12 at parity 1.
// A reflection maps the anchor with everything else and flips the parity; a translation moves the
// anchor. That makes the choice commute with every op (the rule is the same after any
// transposition or inversion of the whole state), which is what keeps P, L and R involutions on
// every chord: a reflection leaves its own edge in place, so the second P finds the same edge
// again. (Picking by the chord's root instead, as the plan first said, fails on Cmaj7: after P
// the namer's root is Ab, whose fifth edge is the other one.)
//
// For triads there is only one edge of each size, so the anchor never matters and P, L, R are
// the neo-Riemannian operations exactly. Note that after a reflection the anchor is the image of
// the old one, not the new chord's root (C major P is C minor with anchor G): the root, for names,
// comes from theory/ChordNamer.h.
#pragma once

#include "Lattice.h"

namespace tonnetz
{
    struct Chord
    {
        PcMask mask = 0;
        int8_t anchor = 0;        // a pitch class of the chord
        uint8_t parity = 0;       // 0 or 1: flipped by every reflection

        constexpr bool operator== (const Chord& o) const noexcept { return mask == o.mask && anchor == o.anchor && parity == o.parity; }
        constexpr bool operator!= (const Chord& o) const noexcept { return ! (*this == o); }
        constexpr bool empty() const noexcept { return (mask & 0xfffu) == 0; }
        constexpr int size() const noexcept { return pcCount (mask); }
        constexpr bool has (int pc) const noexcept { return hasPc (mask, pc); }
    };

    // A chord as it comes in: `root` anchors it (if `root` isn't one of its tones, the lowest pitch
    // class does).
    constexpr Chord makeChord (unsigned mask, int root) noexcept
    {
        mask &= 0xfffu;
        Chord c;
        c.mask = (PcMask) mask;
        if (hasPc (mask, root))
            c.anchor = (int8_t) mod12 (root);
        else
            for (int pc = 0; pc < 12; ++pc)
                if (hasPc (mask, pc)) { c.anchor = (int8_t) pc; break; }
        return c;
    }

    // x -> sign * x + shift (mod 12): a reflection (sign -1) or a translation (sign 1).
    struct PcMap
    {
        int8_t sign = 1;
        int8_t shift = 0;

        constexpr int operator() (int pc) const noexcept { return mod12 (sign * pc + shift); }
        constexpr bool isReflection() const noexcept { return sign < 0; }
        constexpr bool operator== (const PcMap& o) const noexcept { return sign == o.sign && shift == o.shift; }

        static constexpr PcMap identity() noexcept { return {}; }
        static constexpr PcMap reflection (int sum) noexcept { return { -1, (int8_t) mod12 (sum) }; }   // x -> sum - x
        static constexpr PcMap translation (int t) noexcept { return { 1, (int8_t) mod12 (t) }; }

        // This map, then `next`.
        constexpr PcMap then (PcMap next) const noexcept
        {
            return { (int8_t) (sign * next.sign), (int8_t) mod12 (next.sign * shift + next.shift) };
        }
    };

    constexpr PcMask mapMask (unsigned mask, PcMap m) noexcept
    {
        unsigned out = 0;
        for (int pc = 0; pc < 12; ++pc)
            if (hasPc (mask, pc))
                out |= 1u << m (pc);
        return (PcMask) out;
    }

    // ---- Ops -------------------------------------------------------------------------------

    enum class OpType : uint8_t
    {
        P, L, R,              // reflect through the chord's fifth / minor-third / major-third edge
        N, S, H,              // R L P, L P R, L P L (left to right), as one step
        Translate,            // by the lattice vector (x, y): 7x + 4y semitones
        Flip,                 // reflect through the chord tones anchor + x and anchor + y
        Pivot,                // reflect through the chord tone anchor + x
        Grow,                 // add the vertex across the edge (anchor + x, anchor + y)
        Shrink                // remove the tone the latest Grow added (Word.h; a no-op on its own)
    };

    struct Op
    {
        OpType type = OpType::P;
        int8_t x = 0, y = 0;

        constexpr bool operator== (const Op& o) const noexcept
        {
            if (type != o.type) return false;
            switch (type)
            {
                case OpType::Translate:
                case OpType::Flip:
                case OpType::Grow:  return x == o.x && y == o.y;
                case OpType::Pivot: return x == o.x;
                default:            return true;
            }
        }
        constexpr bool operator!= (const Op& o) const noexcept { return ! (*this == o); }

        static constexpr Op simple (OpType t) noexcept { return { t, 0, 0 }; }
        static constexpr Op translate (Vec v) noexcept { return { OpType::Translate, (int8_t) v.a, (int8_t) v.b }; }
        static constexpr Op flip (int i, int j) noexcept { return { OpType::Flip, (int8_t) mod12 (i), (int8_t) mod12 (j) }; }
        static constexpr Op pivot (int i) noexcept { return { OpType::Pivot, (int8_t) mod12 (i), 0 }; }
        static constexpr Op grow (int i, int j) noexcept { return { OpType::Grow, (int8_t) mod12 (i), (int8_t) mod12 (j) }; }
        static constexpr Op shrink() noexcept { return { OpType::Shrink, 0, 0 }; }

        constexpr Vec vector() const noexcept { return { x, y }; }
    };

    namespace ops
    {
        constexpr Op P = Op::simple (OpType::P), L = Op::simple (OpType::L), R = Op::simple (OpType::R);
        constexpr Op N = Op::simple (OpType::N), S = Op::simple (OpType::S), H = Op::simple (OpType::H);
        constexpr Op fifthUp = Op::translate (fifthStep), fifthDown = Op::translate (-fifthStep);
        constexpr Op majorThirdUp = Op::translate (majorThirdStep), majorThirdDown = Op::translate (-majorThirdStep);
        constexpr Op minorThirdUp = Op::translate (minorThirdStep), minorThirdDown = Op::translate (-minorThirdStep);
    }

    // What an op did.
    struct Step
    {
        bool available = false;     // false: the chord is unchanged (map identity)
        Chord chord;                // the result
        PcMap map;                  // where each old tone went
        int8_t added = -1;          // Grow: the tone it added
        int8_t removed = -1;        // Shrink: the tone it removed (Word.h)
        int16_t semitones = 0;      // Translate: the interval as written, 7x + 4y (Register: Follow)
    };

    // The semitones of P, R and L's edges.
    constexpr int edgeInterval (OpType t) noexcept
    {
        return t == OpType::P ? 7 : (t == OpType::R ? 4 : (t == OpType::L ? 3 : 0));
    }

    // The lower tone p of the edge (p, p + k) that P / R / L use on `c`, or -1 if it has none.
    constexpr int chooseEdge (const Chord& c, int k) noexcept
    {
        const int a = c.anchor;
        int best = -1, bestContains = 0, bestDistance = 0, bestOrder = 0;
        for (int p = 0; p < 12; ++p)
        {
            if (! c.has (p) || ! c.has (p + k))
                continue;
            const int contains = (p == a || mod12 (p + k) == a) ? 0 : 1;
            int d = ((2 * p + k - 2 * a) % 24 + 24) % 24;
            d = d < 24 - d ? d : 24 - d;
            const int order = c.parity == 0 ? mod12 (p - a) : mod12 (a - p - k);
            if (best < 0 || contains < bestContains
                || (contains == bestContains && (d < bestDistance || (d == bestDistance && order < bestOrder))))
            {
                best = p;
                bestContains = contains;
                bestDistance = d;
                bestOrder = order;
            }
        }
        return best;
    }

    constexpr Chord mapChord (const Chord& c, PcMap m) noexcept
    {
        Chord out;
        out.mask = mapMask (c.mask, m);
        out.anchor = (int8_t) m (c.anchor);
        out.parity = (uint8_t) (m.isReflection() ? 1 - c.parity : c.parity);
        return out;
    }

    constexpr Step unavailable (const Chord& c) noexcept
    {
        Step s;
        s.chord = c;
        return s;
    }

    constexpr Step moved (const Chord& c, PcMap m) noexcept
    {
        Step s;
        s.available = true;
        s.map = m;
        s.chord = mapChord (c, m);
        return s;
    }

    // The two tones that make a major or minor triad with the pair (p, q), or false if (p, q)
    // isn't a lattice edge (a third or a fifth). The second is always p + q - the first.
    constexpr bool trianglesOn (int p, int q, int& r1, int& r2) noexcept
    {
        int lo = mod12 (p), hi = mod12 (q);
        int d = mod12 (hi - lo);
        if (d == 5 || d == 8 || d == 9)
        {
            const int t = lo; lo = hi; hi = t;
            d = 12 - d;
        }
        // lo + 7: the fifth over a root-and-third pair (major or minor); over a root and its fifth,
        // the major third. The other triangle's tone is the reflection of that one through the
        // edge: the minor third over a fifth, the root below a third.
        if (d != 3 && d != 4 && d != 7)
            return false;
        r1 = mod12 (d == 7 ? lo + 4 : lo + 7);
        r2 = mod12 (lo + hi - r1);
        return true;
    }

    // The op on `c` (Shrink is a no-op here: Word.h keeps the history it needs).
    constexpr Step apply (const Op& op, const Chord& c) noexcept
    {
        const int a = c.anchor;
        switch (op.type)
        {
            case OpType::P:
            case OpType::L:
            case OpType::R:
            {
                const int k = edgeInterval (op.type);
                const int p = chooseEdge (c, k);
                return p < 0 ? unavailable (c) : moved (c, PcMap::reflection (2 * p + k));
            }
            case OpType::N:
            case OpType::S:
            case OpType::H:
            {
                const OpType seq[3][3] = { { OpType::R, OpType::L, OpType::P },
                                           { OpType::L, OpType::P, OpType::R },
                                           { OpType::L, OpType::P, OpType::L } };
                const auto& steps = seq[op.type == OpType::N ? 0 : (op.type == OpType::S ? 1 : 2)];
                Chord cur = c;
                PcMap m;
                for (const auto t : steps)
                {
                    const Step s = apply (Op::simple (t), cur);
                    if (! s.available)
                        return unavailable (c);
                    cur = s.chord;
                    m = m.then (s.map);
                }
                Step s;
                s.available = true;
                s.chord = cur;
                s.map = m;
                return s;
            }
            case OpType::Translate:
            {
                const int semitones = 7 * op.x + 4 * op.y;
                Step s = moved (c, PcMap::translation (semitones));
                s.semitones = (int16_t) semitones;
                return s;
            }
            case OpType::Flip:
            {
                const int p = mod12 (a + op.x), q = mod12 (a + op.y);
                return c.has (p) && c.has (q) ? moved (c, PcMap::reflection (p + q)) : unavailable (c);
            }
            case OpType::Pivot:
            {
                const int p = mod12 (a + op.x);
                return c.has (p) ? moved (c, PcMap::reflection (2 * p)) : unavailable (c);
            }
            case OpType::Grow:
            {
                const int p = mod12 (a + op.x), q = mod12 (a + op.y);
                int r1 = 0, r2 = 0;
                if (p == q || ! c.has (p) || ! c.has (q) || ! trianglesOn (p, q, r1, r2) || c.has (r1) == c.has (r2))
                    return unavailable (c);
                const int added = c.has (r1) ? r2 : r1;
                Step s;
                s.available = true;
                s.chord = c;
                s.chord.mask = (PcMask) (c.mask | pcBit (added));
                s.added = (int8_t) added;
                return s;
            }
            case OpType::Shrink:
                return unavailable (c);
        }
        return unavailable (c);
    }

    constexpr bool isAvailable (const Op& op, const Chord& c) noexcept { return apply (op, c).available; }

    // The op that undoes `op` whenever `op` was available, as one op, if there is one: P, L, R,
    // H and their own inverses, translations by the opposite vector. (Flip and Pivot have
    // inverses only where they were available; N and S only as words; Grow is undone by Shrink.)
    constexpr bool inverseOf (const Op& op, Op& inverse) noexcept
    {
        switch (op.type)
        {
            case OpType::P: case OpType::L: case OpType::R: case OpType::H:
                inverse = op;
                return true;
            case OpType::Translate:
                inverse = Op::translate (-op.vector());
                return true;
            default:
                return false;
        }
    }
}
