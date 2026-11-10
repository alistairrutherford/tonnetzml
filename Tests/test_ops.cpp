// The lattice operations, words and cycles (PLAN.md §7.1 test_ops; Source/core/lattice/Ops.h,
// Word.h, Cycles.h): against the independent reference (Tools/gen_lattice_fixture.py) for every
// op × 30 chord types × 12 roots × both parities; P, L, R as the textbook neo-Riemannian
// operations on the 24 triads; P, L, R involutions on every chord; N = RLP, S = LPR, H = LPL;
// unavailable ops; one source tone per output tone; words parsed, printed, reduced (never
// changing the result) and applied; cycle lengths and the PLR group.
#include "TestUtil.h"
#include "Fixture.h"
#include "core/lattice/Cycles.h"
#include <map>
#include <random>
#include <set>

using namespace tonnetz;
using testutil::check;

namespace
{
    Op parseOne (const std::string& text)
    {
        Word w;
        if (! parseWord (text.c_str(), w) || w.size != 1)
        {
            check (false, "parse the fixture's op " + text);
            return ops::P;
        }
        return w[0];
    }

    void testAgainstFixture()
    {
        const auto& fx = fixture::lattice();
        const auto& opNames = fx["ops"];
        int compared = 0, wrong = 0;
        std::string first;
        for (const auto& type : fixture::chordTypes())
            for (size_t o = 0; o < opNames.size(); ++o)
            {
                const auto& name = opNames[o].str;
                const Op op = parseOne (name);
                const auto& row = fx["results"][type.name][name];
                for (int parity = 0; parity < 2; ++parity)
                    for (int root = 0; root < 12; ++root)
                    {
                        const auto& want = row[(size_t) (parity * 12 + root)];
                        const Step s = apply (op, type.at (root, parity));
                        ++compared;
                        if (s.chord != fixture::chordOf (want) || s.available != (want[3].asInt() != 0))
                            if (wrong++ == 0)
                                first = " first: " + name + " on " + type.name + " root " + std::to_string (root) + " parity "
                                        + std::to_string (parity) + " gave " + fixture::describe (s.chord)
                                        + ", expected " + fixture::describe (fixture::chordOf (want));
                    }
            }
        check (wrong == 0 && compared == 30 * 23 * 24,
               "every op x 30 chord types x 12 roots x 2 parities equals the reference: " + std::to_string (compared - wrong)
                   + "/" + std::to_string (compared) + first);

        // Words.
        compared = wrong = 0;
        first.clear();
        for (size_t w = 0; w < fx["words"].size(); ++w)
        {
            const auto& text = fx["words"][w].str;
            Word word;
            check (parseWord (text.c_str(), word), "parse the fixture's word " + text);
            for (const auto& type : fixture::chordTypes())
                for (int root = 0; root < 12; ++root)
                {
                    const auto want = fixture::chordOf (fx["wordResults"][type.name][text][(size_t) root]);
                    const auto got = applyWord (word, type.at (root)).chord;
                    ++compared;
                    if (got != want && wrong++ == 0)
                        first = " first: " + text + " on " + type.name + " root " + std::to_string (root) + " gave "
                                + fixture::describe (got) + ", expected " + fixture::describe (want);
                }
        }
        check (wrong == 0, "words (with Grow and Shrink) equal the reference: " + std::to_string (compared - wrong) + "/"
                               + std::to_string (compared) + first);
    }

    // The textbook operations on the 24 triads.
    void testNeoRiemannian()
    {
        int wrong = 0;
        for (int root = 0; root < 12; ++root)
        {
            const Chord major = makeChord (majorTriadMask (root), root), minor = makeChord (minorTriadMask (root), root);
            const auto is = [] (const Step& s, PcMask want) { return s.available && s.chord.mask == want; };
            wrong += ! is (apply (ops::P, major), minorTriadMask (root));
            wrong += ! is (apply (ops::R, major), minorTriadMask (root + 9));
            wrong += ! is (apply (ops::L, major), minorTriadMask (root + 4));
            wrong += ! is (apply (ops::P, minor), majorTriadMask (root));
            wrong += ! is (apply (ops::R, minor), majorTriadMask (root + 3));
            wrong += ! is (apply (ops::L, minor), majorTriadMask (root + 8));
            wrong += ! is (apply (ops::N, major), minorTriadMask (root + 5));      // C -> Fm
            wrong += ! is (apply (ops::S, major), minorTriadMask (root + 1));      // C -> C#m
            wrong += ! is (apply (ops::H, major), minorTriadMask (root + 8));      // C -> Abm
            wrong += ! is (apply (ops::N, minor), majorTriadMask (root + 7));      // Cm -> G
            wrong += ! is (apply (ops::S, minor), majorTriadMask (root + 11));     // Cm -> B
            wrong += ! is (apply (ops::H, minor), majorTriadMask (root + 4));      // Cm -> E

        }
        // As sets, two tones are common and the new one is 1 or 2 semitones from the one it
        // replaces (which voice moves is the voicer's business, Phase 2: the map itself swaps the
        // edge's two tones).
        int newTones = 0;
        for (int root = 0; root < 12; ++root)
            for (const Op op : { ops::P, ops::L, ops::R })
                for (const PcMask m : { majorTriadMask (root), minorTriadMask (root) })
                {
                    const Step s = apply (op, makeChord (m, root));
                    const unsigned gained = s.chord.mask & ~m, lost = m & ~s.chord.mask;
                    int g = 0, l = 0;
                    while (((gained >> g) & 1u) == 0 && g < 12) ++g;
                    while (((lost >> l) & 1u) == 0 && l < 12) ++l;
                    const int d = mod12 (g - l);
                    newTones += pcCount (gained) != 1 || (d != 1 && d != 2 && d != 10 && d != 11);
                }
        check (wrong == 0 && newTones == 0, "P, L, R, N, S, H on the 24 triads are the textbook operations; P, L, R change one tone, by 1 or 2 semitones");

        const Chord c7 = makeChord (pcBit (0) | pcBit (4) | pcBit (7) | pcBit (10), 0);
        check (apply (ops::P, c7).chord.mask == (pcBit (0) | pcBit (3) | pcBit (7) | pcBit (9)), "C7 under P is C Eb G A (PLAN.md §2)");
        const Chord cmaj = makeChord (majorTriadMask (0), 0);
        check (apply (Op::grow (4, 7), cmaj).chord.mask == (majorTriadMask (0) | pcBit (11)), "Grow across E-G: Cmaj7");
        check (apply (Op::grow (0, 4), cmaj).chord.mask == (majorTriadMask (0) | pcBit (9)), "Grow across C-E: Am7");
        const int t[6][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 }, { 1, -1 }, { -1, 1 } };
        const int want[6] = { 7, 5, 4, 8, 3, 9 };      // G, F, E, Ab, Eb, A
        bool translations = true;
        for (int i = 0; i < 6; ++i)
            translations = translations && apply (Op::translate ({ t[i][0], t[i][1] }), cmaj).chord.mask == majorTriadMask (want[i]);
        check (translations, "translations of C: G, F, E, Ab, Eb, A");
    }

    // Every chord state: 4095 sets x each anchor x 2 parities.
    template <class Fn>
    void forEveryState (Fn fn)
    {
        for (unsigned m = 1; m < 4096; ++m)
            for (int a = 0; a < 12; ++a)
                if (hasPc (m, a))
                    for (int parity = 0; parity < 2; ++parity)
                    {
                        Chord c = makeChord (m, a);
                        c.parity = (uint8_t) parity;
                        fn (c);
                    }
    }

    void testInvolutionsAndCompounds()
    {
        int states = 0, broken = 0, compound = 0, bijection = 0, unavailableMoved = 0;
        std::string firstBroken;
        forEveryState ([&] (const Chord& c)
        {
            ++states;
            for (const Op op : { ops::P, ops::L, ops::R, ops::H })
            {
                const Step once = apply (op, c);
                if (! once.available)
                {
                    unavailableMoved += once.chord != c;
                    continue;
                }
                const Step twice = apply (op, once.chord);
                if ((! twice.available || twice.chord != c) && broken++ == 0)
                    firstBroken = fixture::describe (c);
            }

            const auto seq = [&c] (OpType a, OpType b, OpType d)
            {
                Step s1 = apply (Op::simple (a), c), s2 = apply (Op::simple (b), s1.chord), s3 = apply (Op::simple (d), s2.chord);
                return s1.available && s2.available && s3.available ? s3.chord : c;
            };
            compound += apply (ops::N, c).chord != seq (OpType::R, OpType::L, OpType::P);
            compound += apply (ops::S, c).chord != seq (OpType::L, OpType::P, OpType::R);
            compound += apply (ops::H, c).chord != seq (OpType::L, OpType::P, OpType::L);

            for (const Op op : { ops::P, ops::L, ops::R, ops::N, ops::S, ops::H, Op::flip (0, 5), Op::pivot (3), Op::translate ({ 2, -1 }) })
            {
                const Step s = apply (op, c);
                bijection += mapMask (c.mask, s.map) != s.chord.mask || ! s.chord.has (s.chord.anchor);
            }
        });
        check (states == 2 * 24576 && broken == 0, "P, L, R and H are involutions on every chord (" + std::to_string (states) + " states)"
                                                      + (broken > 0 ? ", broken from " + firstBroken : std::string()));
        check (compound == 0, "N = R L P, S = L P R, H = L P L on every chord (left to right, all or nothing)");
        check (bijection == 0 && unavailableMoved == 0, "each output tone has one source tone; the anchor stays a chord tone; an unavailable op changes nothing");

        // The edge choice commutes with transposition and inversion of the whole state.
        int equivariant = 0;
        std::mt19937 rng (5);
        for (int i = 0; i < 20000; ++i)
        {
            const unsigned m = 1u + rng() % 4095u;
            int a = (int) (rng() % 12);
            while (! hasPc (m, a)) a = (a + 1) % 12;
            Chord c = makeChord (m, a);
            c.parity = (uint8_t) (rng() % 2);
            const PcMap g = rng() % 2 ? PcMap::reflection ((int) (rng() % 12)) : PcMap::translation ((int) (rng() % 12));
            for (const Op op : { ops::P, ops::L, ops::R })
            {
                const Step direct = apply (op, c), conj = apply (op, mapChord (c, g));
                equivariant += direct.available != conj.available || mapChord (direct.chord, g) != conj.chord;
            }
        }
        check (equivariant == 0, "the edge choice commutes with every transposition and inversion (20000 random states)");
    }

    void testUnavailable()
    {
        const auto at = [] (std::initializer_list<int> pcs, int root)
        {
            unsigned m = 0;
            for (int pc : pcs) m |= pcBit (pc);
            return makeChord (m, root);
        };
        const Chord sus4 = at ({ 0, 5, 7 }, 0), power = at ({ 0, 7 }, 0), note = at ({ 0 }, 0), aug = at ({ 0, 4, 8 }, 0);
        check (! isAvailable (ops::R, sus4) && ! isAvailable (ops::L, sus4) && isAvailable (ops::P, sus4), "Csus4: no thirds, so R and L are unavailable; P is");
        check (! isAvailable (ops::R, power) && isAvailable (ops::P, power) && ! isAvailable (ops::N, power), "C5: P only; N needs all three steps");
        check (! isAvailable (ops::P, note) && ! isAvailable (ops::L, note) && isAvailable (ops::fifthUp, note) && isAvailable (Op::pivot (0), note)
                   && ! isAvailable (Op::pivot (1), note), "a single note: no P, L, R; translations and Pivot on its own tone are");
        check (! isAvailable (ops::P, aug) && ! isAvailable (ops::L, aug) && isAvailable (ops::R, aug), "C+: no fifth or minor third; R is available");
        check (! isAvailable (Op::grow (0, 7), makeChord (majorTriadMask (0) | pcBit (3), 0)), "Grow on an edge whose two triangles are both full: unavailable");
        check (! isAvailable (Op::grow (0, 2), sus4) && ! isAvailable (Op::flip (0, 1), sus4), "Grow off an edge, Flip off the chord: unavailable");
    }

    void testWords()
    {
        Word w;
        int at = 0;
        check (parseWord ("PL", w) && w.size == 2 && w[0] == ops::P && w[1] == ops::L, "parse: PL");
        check (parseWord ("P\xC2\xB7L, R  +5 -m3 T(2,-1) Flip(0,4) Pivot(7) Grow(4,7) Shrink N S H", w) && w.size == 13,
               "parse: separators, translations, Flip, Pivot, Grow, Shrink");
        check (std::string (printWord (w).c_str()) == "P L R +5 -m3 T(2,-1) Flip(0,4) Pivot(7) Grow(4,7) Shrink N S H", "print: " + std::string (printWord (w).c_str()));
        check (std::string (printWord (w, "\xC2\xB7").c_str()).rfind ("P\xC2\xB7L\xC2\xB7R", 0) == 0, "print with a middle dot");
        Word again;
        check (parseWord (printWord (w).c_str(), again) && again == w, "print then parse round-trips");
        check (! parseWord ("P X L", w, &at) && at == 2 && w.size == 1, "a bad op: false, its offset, the ops before it kept");
        check (! parseWord ("Flip(1)", w, &at) && at == 0 && ! parseWord ("+6", w) && ! parseWord ("T(1,", w), "bad arguments are rejected");
        check (parseWord ("", w) && w.empty() && parseWord ("   ", w) && w.empty(), "an empty word");
        std::string long33;
        for (int i = 0; i < 33; ++i) long33 += "P";
        check (! parseWord (long33.c_str(), w) && w.size == maxWordOps, "33 ops: the 32 that fit, and false");

        Word r;
        parseWord ("P P L +5 -5 R R L H H Grow(4,7) Shrink T(0,0) N N", r);
        check (std::string (printWord (reduce (r)).c_str()) == "N N", "reduce cancels P P, a translation and its opposite, R R, then L L, H H, Grow Shrink (N N stays)");
        parseWord ("Flip(0,4) Flip(8,0) Pivot(3) Pivot(9)", r);
        check (reduce (r).size == 4, "reduce keeps Flip and Pivot pairs");

        // Reduction never changes the result: random words on every type, root and parity.
        std::mt19937 rng (11);
        const Op pool[] = { ops::P, ops::L, ops::R, ops::N, ops::S, ops::H, ops::fifthUp, ops::fifthDown, ops::majorThirdUp,
                            ops::minorThirdDown, Op::flip (0, 7), Op::pivot (4), Op::grow (0, 4), Op::grow (4, 7), Op::shrink() };
        int words = 0, changed = 0, shorter = 0;
        std::string firstChanged;
        for (int i = 0; i < 3000; ++i)
        {
            Word word;
            const int n = 1 + (int) (rng() % 12);
            for (int k = 0; k < n; ++k)
            {
                // Plenty of adjacent repeats and inverses, so there is something to reduce.
                const Op op = pool[rng() % (sizeof (pool) / sizeof (pool[0]))];
                word.push (op);
                if (rng() % 3 == 0 && word.size < maxWordOps)
                {
                    Op inv;
                    word.push (op.type == OpType::Grow ? Op::shrink() : (inverseOf (op, inv) ? inv : op));
                }
            }
            const Word reduced = reduce (word);
            shorter += reduced.size < word.size;
            for (const auto& type : fixture::chordTypes())
                for (int root = 0; root < 12; ++root)
                {
                    ++words;
                    const Chord start = type.at (root, (int) (rng() % 2));
                    if (applyWord (word, start).chord != applyWord (reduced, start).chord && changed++ == 0)
                        firstChanged = std::string (printWord (word).c_str()) + " on " + type.name;
                }
        }
        check (changed == 0 && shorter > 1000, "reduction never changes the result (" + std::to_string (words) + " word applications, "
                                                  + std::to_string (shorter) + " of 3000 words shortened)" + (changed > 0 ? ": " + firstChanged : std::string()));

        // Grow and Shrink through other ops.
        WordRunner run (makeChord (majorTriadMask (0), 0));
        const Step grown = run.step (Op::grow (4, 7));
        run.step (ops::P);
        const Step shrunk = run.step (Op::shrink());
        check (grown.added == 11 && shrunk.available && shrunk.removed == 8
                   && pcCount (run.chord().mask) == 3, "Cmaj: Grow adds B, P (B goes to Ab), Shrink removes Ab: three tones again");
        check (! run.step (Op::shrink()).available && run.chord().size() == 3, "Shrink with nothing grown: a no-op");
        const auto skipped = applyWord ([] { Word x; parseWord ("Grow(0,1) Shrink", x); return x; }(), makeChord (majorTriadMask (0), 0));
        check (skipped.chord.mask == majorTriadMask (0) && skipped.skipped == 2, "an unavailable Grow, then Shrink: nothing removed");
    }

    void testCycles()
    {
        const auto& fx = fixture::lattice();
        const auto& cycles = fx["cycles"];
        std::map<std::string, const fixture::ChordType*> types;
        for (const auto& t : fixture::chordTypes())
            types[t.name] = &t;
        int wrong = 0;
        std::string first;
        for (size_t i = 0; i < cycles.size(); ++i)
        {
            const auto& c = cycles[i];
            Word w;
            parseWord (c["word"].str.c_str(), w);
            const auto info = cycleOf (w, types[c["type"].str]->at (c["root"].asInt()));
            if ((info.steps != c["length"].asInt() || info.chords != c["chords"].asInt()) && wrong++ == 0)
                first = " first: " + c["word"].str + " on " + c["type"].str + " " + std::to_string (c["root"].asInt()) + ": "
                        + std::to_string (info.steps) + " steps, expected " + std::to_string (c["length"].asInt());
        }
        check (wrong == 0, "cycle lengths and chords met equal the reference (" + std::to_string (cycles.size()) + " cycles)" + first);

        int triadCycles = 0;
        for (int root = 0; root < 12; ++root)
            for (const PcMask m : { majorTriadMask (root), minorTriadMask (root) })
            {
                const Chord c = makeChord (m, root);
                Word pl, pr, lr;
                parseWord ("PL", pl); parseWord ("PR", pr); parseWord ("LR", lr);
                triadCycles += cycleOf (pl, c).chords == 6 && cycleOf (pr, c).chords == 8 && cycleOf (lr, c).chords == 24;
            }
        check (triadCycles == 24, "from every triad: PL meets 6 chords, PR 8, LR all 24");
        for (const auto& f : factoryCycles)
        {
            Word w;
            check (parseWord (f.word, w) && cycleOf (w, makeChord (majorTriadMask (0), 0)).steps > 0, std::string ("factory cycle ") + f.name + " parses and closes");
        }

        // The PLR group: the permutations of the 24 triads that words in P, L, R make.
        std::vector<PcMask> triads;
        for (int r = 0; r < 12; ++r) triads.push_back (majorTriadMask (r));
        for (int r = 0; r < 12; ++r) triads.push_back (minorTriadMask (r));
        const auto indexOf = [&triads] (PcMask m) { for (size_t i = 0; i < triads.size(); ++i) if (triads[i] == m) return (int) i; return -1; };
        std::vector<std::vector<int>> gens;
        for (const Op op : { ops::P, ops::L, ops::R })
        {
            std::vector<int> g;
            for (const PcMask t : triads)
                g.push_back (indexOf (apply (op, makeChord (t, 0)).chord.mask));
            gens.push_back (g);
        }
        std::vector<int> id (24);
        for (int i = 0; i < 24; ++i) id[(size_t) i] = i;
        std::set<std::vector<int>> group { id };
        std::vector<std::vector<int>> frontier { id };
        while (! frontier.empty())
        {
            const auto g = frontier.back();
            frontier.pop_back();
            for (const auto& h : gens)
            {
                std::vector<int> gh (24);
                for (int i = 0; i < 24; ++i) gh[(size_t) i] = h[(size_t) g[(size_t) i]];
                if (group.insert (gh).second) frontier.push_back (gh);
            }
        }
        check ((int) group.size() == fx["plrGroupOrder"].asInt() && group.size() == 24, "the PLR group has 24 elements (" + std::to_string (group.size()) + ")");
    }
}

int main()
{
    testAgainstFixture();
    testNeoRiemannian();
    testInvolutionsAndCompounds();
    testUnavailable();
    testWords();
    testCycles();
    return testutil::finish ("test_ops");
}
