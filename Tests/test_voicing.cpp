// The voicer (PLAN.md §3.4, §7.1 test_voicing; goal G5):
//
//   G5         P, L and R on every triad in every inversion, close and open, in every register:
//              exactly one voice moves, by 1 or 2 semitones, and the others stay
//   the trap   common tones stay by pitch class (P on C E G keeps C and G where they are,
//              though P's map swaps them)
//   reflections no voice ever moves more than 6 semitones per reflection (N, S, H: per each of
//              their three), common tones never move, in random words over random chords; the
//              voicing's pitch classes are the word's chord after every op
//   compounds  N, S, H on every triad voicing: every voice moves by at most 2 in all (three
//              reflections each moving one voice by 1 or 2, so the smooth path)
//   translations every voice by the same interval; Stay keeps the centre near the input's,
//              Follow moves by the interval as written
//   Grow/Shrink a voice added near the centre and removed again
//   bass, octave and clamping at both ends of the keyboard, no voice ever dropped
#include "TestUtil.h"
#include "core/engine/Voicer.h"
#include <algorithm>
#include <random>
#include <string>
#include <vector>

using namespace tonnetz;
using testutil::check;

namespace
{
    Voicing voicingOf (std::initializer_list<int> pitches)
    {
        Voicing v;
        for (int p : pitches)
            v.push (p, p);
        return v;
    }

    Voicing voicingOf (const std::vector<int>& pitches)
    {
        Voicing v;
        for (int p : pitches)
            v.push (p, p);
        return v;
    }

    Word word (const char* text)
    {
        Word w;
        parseWord (text, w);
        return w;
    }

    int pitchOf (const Voicing& v, int ownerId)
    {
        for (const auto& x : v)
            if (x.owner == ownerId)
                return x.pitch;
        return -999;
    }

    std::vector<int> pitches (const Voicing& v)
    {
        std::vector<int> p;
        for (const auto& x : v)
            p.push_back (x.pitch);
        std::sort (p.begin(), p.end());
        return p;
    }

    std::string str (const std::vector<int>& p)
    {
        std::string s;
        for (int x : p)
            s += (s.empty() ? "" : " ") + std::to_string (x);
        return s;
    }

    void testG5()
    {
        // Every triad: 12 roots x major/minor; voicings: the three close inversions and three
        // open ones (a tone raised an octave), in every register that stays inside 2..125.
        int cases = 0, failures = 0;
        std::string first;
        for (int root = 0; root < 12; ++root)
            for (int minor = 0; minor < 2; ++minor)
            {
                const int third = minor ? 3 : 4;
                const int tones[3] = { 0, third, 7 };
                for (int shape = 0; shape < 6; ++shape)
                    for (int base = 0; base < 128; ++base)
                    {
                        if (mod12 (base) != root)
                            continue;
                        std::vector<int> p { base + tones[0], base + tones[1], base + tones[2] };
                        if (shape == 1 || shape == 2) p[0] += 12;            // first inversion
                        if (shape == 2) p[1] += 12;                          // second inversion
                        if (shape == 3) p[1] += 12;                          // open: root 5th 3rd
                        if (shape == 4) p[0] -= 12;                          // open: root below
                        if (shape == 5) p[2] += 12;                          // open: 5th on top
                        const int lo = *std::min_element (p.begin(), p.end()), hi = *std::max_element (p.begin(), p.end());
                        if (lo < 2 || hi > 125)
                            continue;
                        const Voicing in = voicingOf (p);
                        for (const char* op : { "P", "L", "R" })
                        {
                            Voicing out;
                            const Chord c = voiceWord (word (op), inputChord (in), in, {}, out);
                            int moved = 0, by = 0;
                            for (const auto& v : in)
                            {
                                const int d = pitchOf (out, v.owner) - v.pitch;
                                if (d != 0) { ++moved; by = d < 0 ? -d : d; }
                            }
                            ++cases;
                            const bool ok = out.count == 3 && moved == 1 && (by == 1 || by == 2) && out.mask() == c.mask
                                         && c.mask == applyWord (word (op), inputChord (in)).chord.mask;
                            if (! ok && failures++ == 0)
                                first = std::string (" first: ") + op + " on " + str (p) + " -> " + str (pitches (out));
                        }
                    }
            }
        check (failures == 0 && cases > 2000, "G5: P, L, R on " + std::to_string (cases)
                                                  + " triad voicings (24 triads x 6 shapes x every register): exactly one voice moves, by 1 or 2" + first);
    }

    void testCompounds()
    {
        int cases = 0, failures = 0;
        std::string first;
        for (int root = 0; root < 12; ++root)
            for (int minor = 0; minor < 2; ++minor)
                for (int inversion = 0; inversion < 3; ++inversion)
                    for (int base = 24; base < 96; base += 12)
                    {
                        std::vector<int> p { base + root, base + root + (minor ? 3 : 4), base + root + 7 };
                        for (int i = 0; i < inversion; ++i) p[(size_t) i] += 12;
                        const Voicing in = voicingOf (p);
                        for (const char* op : { "N", "S", "H" })
                        {
                            Voicing out;
                            const Chord c = voiceWord (word (op), inputChord (in), in, {}, out);
                            bool ok = out.mask() == c.mask && c.mask == applyWord (word (op), inputChord (in)).chord.mask;
                            for (const auto& v : in)
                                ok = ok && std::abs (pitchOf (out, v.owner) - v.pitch) <= 2;
                            ++cases;
                            if (! ok && failures++ == 0)
                                first = std::string (" first: ") + op + " on " + str (p) + " -> " + str (pitches (out));
                        }
                    }
        check (failures == 0, "N, S, H on " + std::to_string (cases) + " triad voicings: voiced through their three reflections, no voice moves more than 2" + first);
        Voicing out;
        const Voicing in = voicingOf ({ 60, 64, 67 });
        voiceWord (word ("S"), inputChord (in), in, {}, out);
        check (pitches (out) == std::vector<int> { 61, 64, 68 }, "S on C E G: C# E G#, C and G up a semitone (not C down to G#): " + str (pitches (out)));
        voiceWord (word ("H"), inputChord (in), in, {}, out);
        check (pitches (out) == std::vector<int> { 59, 63, 68 }, "H on C E G: B D# G# (Ab minor), each voice by 1: " + str (pitches (out)));
    }

    void testTrap()
    {
        Voicing out;
        const Voicing in = voicingOf ({ 60, 64, 67 });
        voiceWord (word ("P"), inputChord (in), in, {}, out);
        check (pitchOf (out, 60) == 60 && pitchOf (out, 64) == 63 && pitchOf (out, 67) == 67,
               "P on C4 E4 G4: C and G stay put (the map swaps them), E goes to Eb: " + str (pitches (out)));

        voiceWord (word ("PL"), inputChord (in), in, {}, out);
        check (pitches (out) == std::vector<int> { 60, 63, 68 }, "PL on C E G: C Eb Ab (" + str (pitches (out)) + ")");

        voiceWord (word ("L"), inputChord (in), in, {}, out);
        check (pitches (out) == std::vector<int> { 59, 64, 67 }, "L on C E G: B E G (" + str (pitches (out)) + ")");

        voiceWord (word ("R"), inputChord (in), in, {}, out);
        check (pitches (out) == std::vector<int> { 60, 64, 69 }, "R on C E G: C E A (" + str (pitches (out)) + ")");

        voiceWord (word ("N"), inputChord (in), in, {}, out);
        check (out.mask() == (pcBit (5) | pcBit (8) | pcBit (0)) && pitchOf (out, 60) == 60,
               "N on C E G: F minor, C stays (" + str (pitches (out)) + ")");

        // A seventh chord: C7 under P is C Eb G A (PLAN.md §2), C, G stay; E and Bb move by one.
        const Voicing c7 = voicingOf ({ 60, 64, 67, 70 });
        voiceWord (word ("P"), inputChord (c7), c7, {}, out);
        check (pitches (out) == std::vector<int> { 60, 63, 67, 69 }, "P on C7: C Eb G A, two voices move by 1 (" + str (pitches (out)) + ")");

        // Doubled tones move together.
        const Voicing doubled = voicingOf ({ 48, 60, 64, 67, 76 });
        voiceWord (word ("P"), inputChord (doubled), doubled, {}, out);
        check (pitches (out) == std::vector<int> { 48, 60, 63, 67, 75 }, "P on C3 C4 E4 G4 E5: both Es go to Eb (" + str (pitches (out)) + ")");

        // An unavailable op (P on Csus4 has no fifth? it has: C-G; R has no major third): nothing moves.
        const Voicing sus = voicingOf ({ 60, 65, 67 });
        voiceWord (word ("R"), inputChord (sus), sus, {}, out);
        check (pitches (out) == std::vector<int> { 60, 65, 67 }, "R on Csus4 (no major third): unavailable, nothing moves");
    }

    void testRandomWords()
    {
        std::mt19937 rng (20261111);
        const char* opsText[] = { "P", "L", "R", "N", "S", "H", "+5", "-5", "+M3", "-M3", "+m3", "-m3", "T(2,-1)",
                                  "Flip(0,4)", "Flip(0,7)", "Pivot(0)", "Pivot(4)", "Grow(0,4)", "Grow(4,7)", "Grow(0,3)", "Shrink" };
        int words = 0, steps = 0, worstReflection = 0, failures = 0;
        std::string first;
        for (int trial = 0; trial < 20000; ++trial)
        {
            std::vector<int> p;
            const int n = 1 + (int) (rng() % 6);
            while ((int) p.size() < n)
            {
                const int x = 30 + (int) (rng() % 60);
                if (std::find (p.begin(), p.end(), x) == p.end())
                    p.push_back (x);
            }
            std::string text;
            const int len = 1 + (int) (rng() % 8);
            for (int i = 0; i < len; ++i)
                text += std::string (opsText[rng() % (sizeof (opsText) / sizeof (*opsText))]) + " ";
            const Word w = word (text.c_str());
            const Voicing in = voicingOf (p);
            const Chord start = inputChord (in);
            VoicerSettings vs;
            vs.reg = (rng() & 1) ? Register::Stay : Register::Follow;
            Voicing out;
            WordRunner shadow (start);
            bool ok = true;
            std::string why;
            const Chord result = voiceWord (w, start, in, vs, out, [&] (const Op& op, const Step& s, const Voicing& before, const Voicing& after)
            {
                ++steps;
                shadow.step (op);
                if (after.mask() != shadow.chord().mask && after.count > 0) { ok = false; why = "pitch classes differ from the word's chord"; }
                if (! s.available || op.type == OpType::Grow || op.type == OpType::Shrink)
                    return;
                const bool compound = op.type == OpType::N || op.type == OpType::S || op.type == OpType::H;
                if (s.map.isReflection())
                {
                    for (int i = 0; i < before.count; ++i)
                    {
                        const int d = std::abs (after[i].pitch - before[i].pitch);
                        if (! compound) worstReflection = std::max (worstReflection, d);
                        if (d > (compound ? 18 : 6)) { ok = false; why = "a voice moved " + std::to_string (d); }
                        if (! compound && s.chord.has (before[i].pitch) && d != 0) { ok = false; why = "a common tone moved"; }
                    }
                }
                else
                {
                    const int d = after.count > 0 ? after[0].pitch - before[0].pitch : 0;
                    for (int i = 0; i < before.count; ++i)
                        if (after[i].pitch - before[i].pitch != d) { ok = false; why = "a translation moved voices apart"; }
                    if (vs.reg == Register::Follow && d != s.semitones) { ok = false; why = "Follow didn't move by the written interval"; }
                    if (vs.reg == Register::Stay && mod12 (d) != mod12 (s.semitones)) { ok = false; why = "Stay moved to the wrong pitch class"; }
                }
            });
            const auto inRange = [&out] { for (const auto& v : out) if (v.pitch < 0 || v.pitch > 127) return false; return true; };
            if (out.count > 0 && out.mask() != result.mask) { ok = false; why = "final pitch classes"; }
            if (! inRange()) { ok = false; why = "out of 0..127"; }
            ++words;
            if (! ok && failures++ == 0)
                first = " first: " + text + "on " + str (p) + ": " + why;
        }
        check (failures == 0, "random words: " + std::to_string (words) + " words, " + std::to_string (steps)
                                  + " steps over random chords; worst reflection move " + std::to_string (worstReflection)
                                  + " semitones (<= 6), common tones still, pitch classes = the word's chord after every op" + first);
    }

    void testTranslations()
    {
        const Voicing in = voicingOf ({ 60, 64, 67 });
        Voicing out;
        VoicerSettings stay, follow;
        follow.reg = Register::Follow;
        voiceWord (word ("+5"), inputChord (in), in, stay, out);
        check (pitches (out) == std::vector<int> { 55, 59, 62 }, "+5, Stay: G major below (centre nearest the input's): " + str (pitches (out)));
        voiceWord (word ("+5"), inputChord (in), in, follow, out);
        check (pitches (out) == std::vector<int> { 67, 71, 74 }, "+5, Follow: a fifth up: " + str (pitches (out)));
        voiceWord (word ("+5 +5 +5 +5"), inputChord (in), in, follow, out);
        check (pitches (out) == std::vector<int> { 88, 92, 95 }, "+5 x4, Follow: the chord climbs: " + str (pitches (out)));
        voiceWord (word ("+5 +5 +5 +5"), inputChord (in), in, stay, out);
        const double c = out.centre();
        check (out.mask() == (pcBit (4) | pcBit (8) | pcBit (11)) && c > 63.67 - 6.01 && c < 63.67 + 6.01,
               "+5 x4, Stay: E major, centre within a tritone of the input's: " + str (pitches (out)));
        voiceWord (word ("-m3"), inputChord (in), in, follow, out);
        check (pitches (out) == std::vector<int> { 57, 61, 64 }, "-m3, Follow: A major, 3 down: " + str (pitches (out)));
    }

    void testGrow()
    {
        const Voicing in = voicingOf ({ 60, 64, 67 });
        Voicing out;
        voiceWord (word ("Grow(4,7)"), inputChord (in), in, {}, out);
        check (pitches (out) == std::vector<int> { 59, 60, 64, 67 } && pitchOf (out, owner::grownBase) == 59,
               "Grow(4,7) on C E G: B added nearest the centre, B3 (Cmaj7): " + str (pitches (out)));
        voiceWord (word ("Grow(0,4)"), inputChord (in), in, {}, out);
        check (pitches (out) == std::vector<int> { 60, 64, 67, 69 }, "Grow(0,4): A added (Am7): " + str (pitches (out)));
        voiceWord (word ("Grow(4,7) P Shrink"), inputChord (in), in, {}, out);
        check (out.count == 3 && out.mask() == applyWord (word ("Grow(4,7) P Shrink"), inputChord (in)).chord.mask,
               "Grow P Shrink: the grown voice goes again: " + str (pitches (out)));
    }

    void testBassOctaveClamp()
    {
        Voicing out;
        VoicerSettings vs;
        vs.bass = BassMode::RootBelow;
        const Voicing in = voicingOf ({ 60, 64, 67 });
        voiceWord (word ("P"), inputChord (in), in, vs, out);
        check (pitches (out) == std::vector<int> { 48, 60, 63, 67 } && pitchOf (out, owner::bass) == 48, "Bass: C minor over C3: " + str (pitches (out)));
        voiceWord (word ("R"), inputChord (in), in, vs, out);
        check (pitchOf (out, owner::bass) == 45, "Bass: R gives A minor, root A2 (1-2 octaves under C4): " + str (pitches (out)));
        const Voicing inv = voicingOf ({ 64, 67, 72 });
        voiceWord (word ("L"), inputChord (inv), inv, vs, out);
        const int b = pitchOf (out, owner::bass);
        check (mod12 (b) == 4 && b <= 64 - 12 && b >= 64 - 23, "Bass: E minor's root E, 12-23 semitones under the lowest voice: " + str (pitches (out)));

        vs = {};
        vs.octave = 2;
        voiceWord (word ("P"), inputChord (in), in, vs, out);
        check (pitches (out) == std::vector<int> { 84, 87, 91 }, "Octave +2: " + str (pitches (out)));
        vs.octave = -2;
        voiceWord (word ("P"), inputChord (in), in, vs, out);
        check (pitches (out) == std::vector<int> { 36, 39, 43 }, "Octave -2: " + str (pitches (out)));

        // Clamping: whole-chord octave shifts, nothing dropped.
        vs = {};
        vs.octave = 2;
        const Voicing top = voicingOf ({ 116, 120, 123 });
        voiceWord (word ("R"), inputChord (top), top, vs, out);
        bool inRange = true;
        for (const auto& v : out) inRange = inRange && v.pitch >= 0 && v.pitch <= 127;
        check (out.count == 3 && inRange && out.mask() == applyWord (word ("R"), inputChord (top)).chord.mask && out.highest() - out.lowest() == 9,
               "clamp at the top (R, Octave +2 on G#8 C9 D#9): shifted down whole: " + str (pitches (out)));
        vs.octave = -2;
        vs.bass = BassMode::RootBelow;
        const Voicing bottom = voicingOf ({ 3, 7, 10 });
        voiceWord (word ("L"), inputChord (bottom), bottom, vs, out);
        inRange = true;
        for (const auto& v : out) inRange = inRange && v.pitch >= 0 && v.pitch <= 127;
        check (out.count == 4 && inRange && pitchOf (out, owner::bass) >= 0, "clamp at the bottom (L, Octave -2, bass): shifted up whole, bass kept: " + str (pitches (out)));
        const Voicing edge = voicingOf ({ 127 });
        vs = {};
        voiceWord (word ("+M3"), inputChord (edge), edge, VoicerSettings { Register::Follow, BassMode::Off, 0 }, out);
        check (out.count == 1 && out[0].pitch == 119, "a single G9 (127) up a major third: B8 (119): " + str (pitches (out)));
        const Voicing wide = voicingOf ({ 0, 127 });
        voiceWord (word ("+5"), inputChord (wide), wide, VoicerSettings { Register::Follow, BassMode::RootBelow, 2 }, out);
        inRange = true;
        for (const auto& v : out) inRange = inRange && v.pitch >= 0 && v.pitch <= 127;
        check (out.count == 3 && inRange, "a voicing wider than the keyboard is folded, nothing dropped: " + str (pitches (out)));
    }
}

int main()
{
    std::cout << "test_voicing\n";
    testG5();
    testCompounds();
    testTrap();
    testRandomWords();
    testTranslations();
    testGrow();
    testBassOctaveClamp();
    return testutil::finish ("test_voicing");
}
