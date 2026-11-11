// Voicing (PLAN.md §3.4, goal G5): the notes of W(chord), placed near the notes that were
// played. Applied op by op, so a long word still leads smoothly.
//
// A voicing is a list of voices, each a MIDI pitch with an owner: the input key it is the image
// of (0..127), the bass (128), or a tone a Grow added (129..160). Owners are how the engine binds
// output notes to source keys (releasing a key releases its image) and how it retunes only the
// voices that changed.
//
// Per op (the chord before it A, after it B):
//   reflection  (P, L, R, Flip, Pivot) voices whose pitch class is in both A and B do
//               not move. The others go to the image of their pitch class under the op's map,
//               at the nearest pitch (at most 6 semitones; a tie, 6, goes toward the voicing's
//               centre). The map itself is NOT how voices move: P on C major maps C to G and G
//               to C (it swaps the two tones of the edge it reflects through); following it
//               would move two voices by a fifth. Every reflection is an involution, so it maps
//               the tones of A not in B exactly onto the tones of B not in A: binding by pitch
//               class is the map restricted to the tones that change.
//   N, S, H     voiced as their three reflections in turn (N = R L P, S = L P R, H = L P L), so
//               each voice moves by small steps (S on C E G: C and G up a semitone each).
//   translation every voice moves by the same interval: Register Stay picks, of the two
//               intervals with the right pitch class (up or down), the one that leaves the
//               voicing's centre nearest the input's; Follow uses the interval as written
//               (+5 is 7 semitones up, -m3 3 down).
//   Grow        adds a voice for the new tone at the pitch nearest the voicing's centre.
//   Shrink      removes the voices of the tone it removes.
//   unavailable nothing moves.
// Then the whole voicing moves by Octave, the bass is added (Root below: the output chord's
// root in any inversion (voicer::rootOf), the highest such pitch at least an octave under the lowest voice, so one to two octaves
// under), and the result is clamped into 0..127 by octave shifts of the whole voicing, never by
// dropping a voice (a voicing wider than the keyboard, which no input can produce once the bass
// is left out, is folded voice by voice as a last resort).
//
// Plain C++, no JUCE, no allocation.
#pragma once

#include "../lattice/Word.h"
#include "../theory/ChordNamer.h"
#include <array>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace tonnetz
{
    enum class Register : uint8_t { Stay, Follow };
    enum class BassMode : uint8_t { Off, RootBelow };

    struct VoicerSettings
    {
        Register reg = Register::Stay;
        BassMode bass = BassMode::Off;
        int octave = 0;                    // -2..+2

        bool operator== (const VoicerSettings& o) const noexcept { return reg == o.reg && bass == o.bass && octave == o.octave; }
        bool operator!= (const VoicerSettings& o) const noexcept { return ! (*this == o); }
    };

    namespace owner
    {
        constexpr int bass = 128;
        constexpr int grownBase = 129;
        constexpr int count = grownBase + maxWordOps;   // 161
    }

    struct Voice
    {
        int pitch = 0;                     // may leave 0..127 until the final clamp
        uint16_t owner = 0;
    };

    struct Voicing
    {
        static constexpr int capacity = owner::count;
        std::array<Voice, capacity> voices {};
        int count = 0;

        void clear() noexcept { count = 0; }
        bool push (int pitch, int ownerId) noexcept
        {
            if (count >= capacity)
                return false;
            voices[(size_t) count++] = { pitch, (uint16_t) ownerId };
            return true;
        }
        Voice& operator[] (int i) noexcept { return voices[(size_t) i]; }
        const Voice& operator[] (int i) const noexcept { return voices[(size_t) i]; }
        const Voice* begin() const noexcept { return voices.data(); }
        const Voice* end() const noexcept { return voices.data() + count; }

        double centre() const noexcept
        {
            if (count == 0)
                return 60.0;
            double sum = 0.0;
            for (const auto& v : *this)
                sum += v.pitch;
            return sum / count;
        }

        PcMask mask() const noexcept
        {
            unsigned m = 0;
            for (const auto& v : *this)
                m |= pcBit (v.pitch);
            return (PcMask) m;
        }

        int lowest() const noexcept
        {
            int lo = 1 << 20;
            for (const auto& v : *this)
                lo = v.pitch < lo ? v.pitch : lo;
            return count > 0 ? lo : -1;
        }

        int highest() const noexcept
        {
            int hi = -(1 << 20);
            for (const auto& v : *this)
                hi = v.pitch > hi ? v.pitch : hi;
            return count > 0 ? hi : -1;
        }

        void shift (int semitones) noexcept
        {
            for (int i = 0; i < count; ++i)
                voices[(size_t) i].pitch += semitones;
        }
    };

    namespace voicer
    {
        // The pitch with pitch class `pc` nearest `pitch`; a tie (6 semitones) goes toward `centre`.
        constexpr int nearest (int pitch, int pc, double centre) noexcept
        {
            const int up = mod12 (pc - pitch);
            if (up < 6) return pitch + up;
            if (up > 6) return pitch + up - 12;
            return pitch > centre ? pitch - 6 : pitch + 6;
        }

        // One op's effect on the voicing. `op` is the op, `s` what it did (WordRunner::step),
        // `sourceCentre` the input voicing's centre (Register: Stay), `grown` counts Grows so far.
        // A reflection from chord `from`: voices whose pitch class `s` keeps stay, the rest go to
        // their image's nearest pitch.
        inline void reflect (Voicing& v, const Step& s) noexcept
        {
            const double c = v.centre();
            for (int i = 0; i < v.count; ++i)
            {
                const int pc = mod12 (v[i].pitch);
                if (s.chord.has (pc))
                    continue;                     // a common tone: stays where it is
                v[i].pitch = nearest (v[i].pitch, s.map (pc), c);
            }
        }

        inline void applyStep (Voicing& v, const Op& op, const Chord& from, const Step& s, const VoicerSettings& settings,
                               double sourceCentre, int& grown) noexcept
        {
            if (op.type == OpType::Grow)
            {
                const int g = grown++;
                if (s.available && s.added >= 0 && g < maxWordOps)
                {
                    const double c = v.centre();
                    const int base = (int) (c >= 0.0 ? c + 0.5 : c - 0.5);
                    v.push (nearest (base, s.added, c), owner::grownBase + g);
                }
                return;
            }
            if (! s.available)
                return;
            if (op.type == OpType::Shrink)
            {
                int out = 0;
                for (int i = 0; i < v.count; ++i)
                    if (mod12 (v[i].pitch) != s.removed)
                        v[out++] = v[i];
                v.count = out;
                return;
            }
            if (! s.map.isReflection())
            {
                int d = 0;
                if (settings.reg == Register::Follow)
                    d = s.semitones;
                else
                {
                    const int t = mod12 (s.map.shift);
                    const double c = v.centre();
                    const double up = c + t - sourceCentre, down = c + t - 12 - sourceCentre;
                    const double au = up < 0 ? -up : up, ad = down < 0 ? -down : down;
                    d = (ad < au || (ad == au && 12 - t < t)) ? t - 12 : t;
                }
                v.shift (d);
                return;
            }
            if (op.type == OpType::N || op.type == OpType::S || op.type == OpType::H)
            {
                // A compound is voiced through its three reflections (N = R L P, S = L P R,
                // H = L P L), so each voice takes the smooth path: S on C E G moves C and G up a
                // semitone each, where its single map would send C down to G# and G down to C#.
                const OpType seq[3][3] = { { OpType::R, OpType::L, OpType::P },
                                           { OpType::L, OpType::P, OpType::R },
                                           { OpType::L, OpType::P, OpType::L } };
                Chord cur = from;
                for (const auto t : seq[op.type == OpType::N ? 0 : (op.type == OpType::S ? 1 : 2)])
                {
                    const Step sub = apply (Op::simple (t), cur);
                    reflect (v, sub);
                    cur = sub.chord;
                }
                return;
            }
            reflect (v, s);
        }

        // Octave shifts of the whole voicing into 0..127.
        inline void clamp (Voicing& v) noexcept
        {
            if (v.count == 0)
                return;
            while (v.lowest() < 0 && v.highest() + 12 <= 127)
                v.shift (12);
            while (v.highest() > 127 && v.lowest() - 12 >= 0)
                v.shift (-12);
            // Wider than the keyboard: fold the voices that are still out.
            for (int i = 0; i < v.count; ++i)
            {
                while (v[i].pitch < 0) v[i].pitch += 12;
                while (v[i].pitch > 127) v[i].pitch -= 12;
            }
        }

        // The root of a pitch-class set whatever its inversion: the cheapest reading of the
        // namer's (ChordNamer.h) with no bonus for the bass, ties to `bassPc`, then upwards from
        // it. (chordRoot favours the bass: A C E over C is C6 there, but the bass under an A
        // minor that L or R made must be A.)
        inline int rootOf (unsigned mask, int bassPc) noexcept
        {
            mask &= 0xfffu;
            int best = -1, bestCost = 1 << 30;
            for (int k = 0; k < 12; ++k)
            {
                const int r = mod12 (bassPc + k);
                if (! hasPc (mask, r))
                    continue;
                const int cost = naming::analyse (naming::relativeTo (mask, r)).cost;
                if (cost < bestCost)
                {
                    bestCost = cost;
                    best = r;
                }
            }
            return best;
        }

        // The bass under the voicing: the root of its pitch classes, at the highest pitch at
        // least an octave under its lowest voice. False for an empty voicing.
        inline bool bassPitch (const Voicing& v, int& pitch) noexcept
        {
            if (v.count == 0)
                return false;
            const int top = v.lowest() - 12;
            const int root = rootOf (v.mask(), mod12 (v.lowest()));
            if (root < 0)
                return false;
            pitch = top - mod12 (top - root);
            return true;
        }
    }

    // `in` (the input keys' pitches, owners = keys) under `count` ops, the i-th `opAt (i)`, from
    // `start` (the input chord, anchored at its root): the voicing in `out`, and the output chord.
    // `stepTrace`, if given, is called after every op with the voicing before and after it
    // (tests). Orbit's word (a cycle stepped past 32 ops) comes through here; a Word through
    // voiceWord below.
    template <class OpAt, class Trace>
    Chord voiceOps (int count, OpAt&& opAt, const Chord& start, const Voicing& in, const VoicerSettings& settings,
                    Voicing& out, Trace&& stepTrace) noexcept
    {
        out = in;
        const double sourceCentre = in.centre();
        WordRunner run (start);
        int grown = 0;
        for (int i = 0; i < count; ++i)
        {
            const Op op = opAt (i);
            const Chord from = run.chord();
            const Step s = run.step (op);
            if constexpr (! std::is_same_v<std::decay_t<Trace>, std::nullptr_t>)
            {
                const Voicing before = out;
                voicer::applyStep (out, op, from, s, settings, sourceCentre, grown);
                stepTrace (op, s, before, out);
            }
            else
            {
                voicer::applyStep (out, op, from, s, settings, sourceCentre, grown);
            }
        }
        const int octave = settings.octave < -2 ? -2 : (settings.octave > 2 ? 2 : settings.octave);
        out.shift (12 * octave);
        if (settings.bass == BassMode::RootBelow)
        {
            int b = 0;
            if (voicer::bassPitch (out, b))
                out.push (b, owner::bass);
        }
        voicer::clamp (out);
        return run.chord();
    }

    template <class Trace>
    Chord voiceWord (const Word& word, const Chord& start, const Voicing& in, const VoicerSettings& settings,
                     Voicing& out, Trace&& stepTrace) noexcept
    {
        return voiceOps (word.size, [&word] (int i) { return word[i]; }, start, in, settings, out, std::forward<Trace> (stepTrace));
    }

    inline Chord voiceWord (const Word& word, const Chord& start, const Voicing& in, const VoicerSettings& settings, Voicing& out) noexcept
    {
        return voiceWord (word, start, in, settings, out, nullptr);
    }

    // The input chord of a voicing: its pitch classes, anchored at their root over the lowest
    // voice (PLAN.md §2: the anchor is the root when a chord comes in).
    inline Chord inputChord (const Voicing& in) noexcept
    {
        const unsigned mask = in.mask();
        return makeChord (mask, chordRoot (mask, in.count > 0 ? mod12 (in.lowest()) : -1));
    }
}
