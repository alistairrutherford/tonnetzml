// Editing W (PLAN.md §3.1, Walk): the twelve operator keys and what they do to the word.
//
//   keys 0..11 of the zone (default C0-B0):  P  L  R  N  S  H  +5  -5  +M3  -M3  Undo  Home
//
// An op key appends its op. Adjacent identical involutions cancel (`P P` is nothing, as are a
// translation and its opposite: Word.h's `cancels`), so appending P to a word that ends in P
// removes it, and Undo, "append the inverse", is removing the last op. Home clears the word. A
// word that is full (32 ops) takes no more ops: the key does nothing, rather than dropping the
// oldest op, which would move the chord somewhere unrelated to the walk.
//
// `opsInKey` (PLAN.md §3.6, *In key*): which of the ten op keys lead from a chord to one inside a
// key; Phase 4 draws those ghost neighbours solid. Plain C++, no JUCE, no allocation.
#pragma once

#include "../lattice/Word.h"
#include "../theory/KeyBand.h"
#include <cstdint>

namespace tonnetz
{
    constexpr int numOperatorKeys = 12;
    constexpr int numOperatorOps = 10;     // keys 0..9 are ops; 10 is Undo, 11 is Home
    namespace opkey
    {
        constexpr int undo = 10, home = 11;
    }

    constexpr const char* operatorKeyNames[numOperatorKeys] = { "P", "L", "R", "N", "S", "H", "+5", "-5", "+M3", "-M3", "Undo", "Home" };

    // The op of operator key `index` (0..9); false for Undo, Home and anything else.
    inline bool operatorOp (int index, Op& op) noexcept
    {
        constexpr Op table[numOperatorOps] = { ops::P, ops::L, ops::R, ops::N, ops::S, ops::H,
                                               ops::fifthUp, ops::fifthDown, ops::majorThirdUp, ops::majorThirdDown };
        if (index < 0 || index >= numOperatorOps)
            return false;
        op = table[index];
        return true;
    }

    namespace wordedit
    {
        // Appends `op` to `w`: cancels the last op if they undo each other. False if `w` is full.
        inline bool append (Word& w, const Op& op) noexcept
        {
            if (! w.empty() && cancels (w[w.size - 1], op))
            {
                w.pop();
                return true;
            }
            return w.push (op);
        }

        // What operator key `index` does to `w`. True if `w` changed.
        inline bool apply (Word& w, int index) noexcept
        {
            if (index == opkey::home)
            {
                const bool changed = ! w.empty();
                w.clear();
                return changed;
            }
            if (index == opkey::undo)
            {
                const bool changed = ! w.empty();
                w.pop();
                return changed;
            }
            Op op;
            return operatorOp (index, op) && append (w, op);
        }
    }

    // Bit i set: operator key i's op is available on `chord` and its result lies in `key`.
    inline uint16_t opsInKey (const Chord& chord, const KeyRef& key) noexcept
    {
        uint16_t bits = 0;
        if (! key.known())
            return bits;
        for (int i = 0; i < numOperatorOps; ++i)
        {
            Op op;
            operatorOp (i, op);
            const Step s = apply (op, chord);
            if (s.available && key.contains (s.chord.mask))
                bits = (uint16_t) (bits | (1u << i));
        }
        return bits;
    }
}
