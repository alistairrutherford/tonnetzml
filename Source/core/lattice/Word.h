// Words (PLAN.md §3.1): a short list of ops (up to 32), applied left to right, as the engine's
// `W`. Parse and print as text, reduce, apply. Plain C++17, no JUCE, no allocation.
//
// Text: ops one after another, separated by spaces, commas or `·` (or nothing, between letters):
//
//   P L R N S H                     the reflections and their compounds
//   +5 -5 +M3 -M3 +m3 -m3           translations by one lattice step (a fifth, a major third, a
//                                   minor third, up or down)
//   T(a,b)                          translation by any lattice vector: 7a + 4b semitones
//   Flip(i,j) Pivot(i) Grow(i,j)    with tones given as semitones above the chord's anchor
//   Shrink                          undo the latest Grow
//
// Applying: each op in turn; an op that is unavailable on the chord it meets is skipped (Mirror
// passes the chord through rather than dropping it, PLAN.md §8). Shrink removes the tone the
// latest Grow not yet shrunk added, followed through the ops since; after a Grow that was
// unavailable, nothing.
//
// Reducing: adjacent pairs that undo each other are removed (P P, L L, R R, H H, a translation
// and its opposite, Grow Shrink), repeatedly. That never changes what the word does to any chord
// (test_ops checks it). Flip and Pivot pairs are kept: an op that was unavailable is skipped,
// and its "inverse" might not be.
#pragma once

#include "Ops.h"

namespace tonnetz
{
    constexpr int maxWordOps = 32;

    struct Word
    {
        std::array<Op, maxWordOps> ops {};
        int size = 0;

        bool push (const Op& op) noexcept
        {
            if (size >= maxWordOps)
                return false;
            ops[(size_t) size++] = op;
            return true;
        }
        void pop() noexcept { if (size > 0) --size; }
        void clear() noexcept { size = 0; }
        bool empty() const noexcept { return size == 0; }
        const Op& operator[] (int i) const noexcept { return ops[(size_t) i]; }
        const Op* begin() const noexcept { return ops.data(); }
        const Op* end() const noexcept { return ops.data() + size; }

        bool operator== (const Word& o) const noexcept
        {
            if (size != o.size)
                return false;
            for (int i = 0; i < size; ++i)
                if (ops[(size_t) i] != o.ops[(size_t) i])
                    return false;
            return true;
        }
        bool operator!= (const Word& o) const noexcept { return ! (*this == o); }
    };

    // ---- Text ------------------------------------------------------------------------------

    struct WordText
    {
        char text[maxWordOps * 12] {};
        const char* c_str() const noexcept { return text; }
    };

    namespace word_detail
    {
        inline bool startsWith (const char* p, const char* end, const char* s) noexcept
        {
            for (; *s != 0; ++s, ++p)
                if (p >= end || *p != *s)
                    return false;
            return true;
        }

        inline size_t length (const char* s) noexcept
        {
            size_t n = 0;
            while (s[n] != 0) ++n;
            return n;
        }

        // "(i)" or "(i,j)": `count` integers, each optionally signed. Moves `p` past it.
        inline bool args (const char*& p, const char* end, int count, int* out) noexcept
        {
            const char* q = p;
            if (q >= end || *q != '(')
                return false;
            ++q;
            for (int n = 0; n < count; ++n)
            {
                while (q < end && *q == ' ') ++q;
                bool negative = false;
                if (q < end && (*q == '-' || *q == '+')) negative = *q++ == '-';
                int v = 0, digits = 0;
                for (; q < end && *q >= '0' && *q <= '9' && digits < 4; ++q, ++digits) v = v * 10 + (*q - '0');
                if (digits == 0)
                    return false;
                out[n] = negative ? -v : v;
                while (q < end && *q == ' ') ++q;
                if (n + 1 < count)
                {
                    if (q >= end || *q != ',')
                        return false;
                    ++q;
                }
            }
            if (q >= end || *q != ')')
                return false;
            p = q + 1;
            return true;
        }

        struct Writer
        {
            char* out;
            size_t cap, at = 0;
            void put (char c) noexcept { if (at + 1 < cap) { out[at++] = c; out[at] = 0; } }
            void put (const char* s) noexcept { for (; *s != 0; ++s) put (*s); }
            void put (int v) noexcept
            {
                if (v < 0) { put ('-'); v = -v; }
                char digits[12];
                int n = 0;
                do { digits[n++] = (char) ('0' + v % 10); v /= 10; } while (v > 0 && n < 11);
                while (n > 0) put (digits[--n]);
            }
        };
    }

    // Parses `text` (`length` bytes) into `out`. On failure returns false and sets `errorAt` to
    // the offset of the first byte that could not be read (out is then the ops before it).
    inline bool parseWord (const char* text, size_t length, Word& out, int* errorAt = nullptr) noexcept
    {
        using namespace word_detail;
        out.clear();
        const char* p = text;
        const char* const end = text + length;
        auto fail = [&] { if (errorAt != nullptr) *errorAt = (int) (p - text); return false; };

        while (p < end)
        {
            if (*p == ' ' || *p == ',' || *p == '\t' || *p == '.')
            {
                ++p;
                continue;
            }
            if (startsWith (p, end, "\xC2\xB7"))      // ·
            {
                p += 2;
                continue;
            }

            Op op;
            int v[2] {};
            const char* const at = p;
            if (startsWith (p, end, "Pivot"))
            {
                p += 5;
                if (! args (p, end, 1, v)) { p = at; return fail(); }
                op = Op::pivot (v[0]);
            }
            else if (startsWith (p, end, "Flip") || startsWith (p, end, "Grow"))
            {
                const bool flip = *p == 'F';
                p += 4;
                if (! args (p, end, 2, v)) { p = at; return fail(); }
                op = flip ? Op::flip (v[0], v[1]) : Op::grow (v[0], v[1]);
            }
            else if (startsWith (p, end, "Shrink"))
            {
                p += 6;
                op = Op::shrink();
            }
            else if (*p == 'T' && p + 1 < end && p[1] == '(')
            {
                ++p;
                if (! args (p, end, 2, v) || v[0] < -64 || v[0] > 64 || v[1] < -64 || v[1] > 64) { p = at; return fail(); }
                op = Op::translate ({ v[0], v[1] });
            }
            else if (*p == '+' || *p == '-')
            {
                const int sign = *p == '+' ? 1 : -1;
                ++p;
                if (startsWith (p, end, "5"))       { op = Op::translate (fifthStep * sign); p += 1; }
                else if (startsWith (p, end, "M3")) { op = Op::translate (majorThirdStep * sign); p += 2; }
                else if (startsWith (p, end, "m3")) { op = Op::translate (minorThirdStep * sign); p += 2; }
                else { p = at; return fail(); }
            }
            else
            {
                switch (*p)
                {
                    case 'P': op = ops::P; break;
                    case 'L': op = ops::L; break;
                    case 'R': op = ops::R; break;
                    case 'N': op = ops::N; break;
                    case 'S': op = ops::S; break;
                    case 'H': op = ops::H; break;
                    default: return fail();
                }
                ++p;
            }
            if (! out.push (op))
            {
                p = at;
                return fail();
            }
        }
        if (errorAt != nullptr)
            *errorAt = -1;
        return true;
    }

    inline bool parseWord (const char* text, Word& out, int* errorAt = nullptr) noexcept
    {
        return parseWord (text, word_detail::length (text), out, errorAt);
    }

    // One op as text ("P", "+M3", "T(2,-1)", "Flip(0,4)").
    inline void printOp (const Op& op, word_detail::Writer& w) noexcept
    {
        switch (op.type)
        {
            case OpType::P: w.put ('P'); return;
            case OpType::L: w.put ('L'); return;
            case OpType::R: w.put ('R'); return;
            case OpType::N: w.put ('N'); return;
            case OpType::S: w.put ('S'); return;
            case OpType::H: w.put ('H'); return;
            case OpType::Shrink: w.put ("Shrink"); return;
            case OpType::Translate:
            {
                const Vec v = op.vector();
                if (v == fifthStep) w.put ("+5");
                else if (v == -fifthStep) w.put ("-5");
                else if (v == majorThirdStep) w.put ("+M3");
                else if (v == -majorThirdStep) w.put ("-M3");
                else if (v == minorThirdStep) w.put ("+m3");
                else if (v == -minorThirdStep) w.put ("-m3");
                else { w.put ("T("); w.put ((int) v.a); w.put (','); w.put ((int) v.b); w.put (')'); }
                return;
            }
            case OpType::Flip:
            case OpType::Grow:
                w.put (op.type == OpType::Flip ? "Flip(" : "Grow(");
                w.put ((int) op.x); w.put (','); w.put ((int) op.y); w.put (')');
                return;
            case OpType::Pivot:
                w.put ("Pivot("); w.put ((int) op.x); w.put (')');
                return;
        }
    }

    // The word as text, ops separated by `separator` (" " by default; "·" for display).
    inline WordText printWord (const Word& word, const char* separator = " ") noexcept
    {
        WordText t;
        word_detail::Writer w { t.text, sizeof (t.text) };
        for (int i = 0; i < word.size; ++i)
        {
            if (i > 0) w.put (separator);
            printOp (word[i], w);
        }
        return t;
    }

    // ---- Reducing --------------------------------------------------------------------------

    // True if `second` straight after `first` always undoes it.
    constexpr bool cancels (const Op& first, const Op& second) noexcept
    {
        if (first.type == OpType::Grow && second.type == OpType::Shrink)
            return true;
        Op inv;
        return inverseOf (first, inv) && inv == second;
    }

    inline Word reduce (const Word& word) noexcept
    {
        Word out;
        for (const auto& op : word)
        {
            if (op.type == OpType::Translate && op.x == 0 && op.y == 0)
                continue;
            if (! out.empty() && cancels (out[out.size - 1], op))
                out.pop();
            else
                out.push (op);
        }
        return out;
    }

    // ---- Applying --------------------------------------------------------------------------

    // Applies ops one at a time, keeping what Shrink needs: the engine's walk (one op per
    // operator key or Orbit step) and `applyWord` both go through it.
    class WordRunner
    {
    public:
        WordRunner() = default;
        explicit WordRunner (const Chord& start) noexcept : current (start) {}

        const Chord& chord() const noexcept { return current; }
        void reset (const Chord& start) noexcept { current = start; depth = 0; }

        Step step (const Op& op) noexcept
        {
            if (op.type == OpType::Shrink)
            {
                Step s = unavailable (current);
                if (depth == 0)
                    return s;
                const int g = grown[(size_t) --depth];
                if (g >= 0 && current.has (g) && g != current.anchor)
                {
                    current.mask = (PcMask) (current.mask & ~pcBit (g));
                    s.available = true;
                    s.chord = current;
                    s.removed = (int8_t) g;
                }
                return s;
            }

            const Step s = apply (op, current);
            if (s.available)
                for (int i = 0; i < depth; ++i)
                    if (grown[(size_t) i] >= 0)
                        grown[(size_t) i] = (int8_t) s.map (grown[(size_t) i]);
            if (op.type == OpType::Grow)
            {
                if (depth == maxWordOps)     // keep the latest
                {
                    for (int i = 1; i < maxWordOps; ++i)
                        grown[(size_t) i - 1] = grown[(size_t) i];
                    --depth;
                }
                grown[(size_t) depth++] = s.available ? s.added : (int8_t) -1;
            }
            current = s.chord;
            return s;
        }

    private:
        Chord current;
        std::array<int8_t, maxWordOps> grown {};
        int depth = 0;
    };

    struct WordResult
    {
        Chord chord;
        int applied = 0;          // ops that were available
        int skipped = 0;          // ops that were not
    };

    inline WordResult applyWord (const Word& word, const Chord& start) noexcept
    {
        WordRunner run (start);
        WordResult r;
        for (const auto& op : word)
            (run.step (op).available ? r.applied : r.skipped)++;
        r.chord = run.chord();
        return r;
    }
}
