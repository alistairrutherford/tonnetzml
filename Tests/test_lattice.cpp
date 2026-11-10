// The lattice itself (PLAN.md §7.1 test_lattice; Source/core/lattice/Lattice.h, Embedding.h):
// torus identities, lattice and torus distances, spelling (a 9 × 7 window of nodes, the 24 keys,
// and ChordMonkey's spelling for the same keys), embeddings (cost against the independent
// reference, triads as triangles, maj7 and m7 as rhombi, invariant under transposition, the
// previous chord deciding which copy), and the chord namer's root finding.
#include "TestUtil.h"
#include "Fixture.h"
#include "core/lattice/Embedding.h"
#include "core/theory/ChordNamer.h"
#include <algorithm>
#include <chrono>
#include <map>
#include <set>

using namespace tonnetz;
using testutil::check;

namespace
{
    // Breadth-first distances over the lattice graph, as a reference for latticeNorm.
    std::map<std::pair<int, int>, int> bfs (int radius)
    {
        std::map<std::pair<int, int>, int> d { { { 0, 0 }, 0 } };
        std::vector<std::pair<int, int>> frontier { { 0, 0 } };
        for (int r = 1; r <= radius; ++r)
        {
            std::vector<std::pair<int, int>> next;
            for (const auto& n : frontier)
                for (const Vec s : unitSteps)
                {
                    const std::pair<int, int> m { n.first + s.a, n.second + s.b };
                    if (d.emplace (m, r).second)
                        next.push_back (m);
                }
            frontier = next;
        }
        return d;
    }

    void testTorus()
    {
        int wrong = 0;
        for (int a = -12; a <= 12; ++a)
            for (int b = -12; b <= 12; ++b)
            {
                const Vec v { a, b };
                wrong += pitchClass (v + torusPeriodA) != pitchClass (v) || pitchClass (v + torusPeriodB) != pitchClass (v);
                wrong += pitchClass (v + fifthStep) != mod12 (pitchClass (v) + 7) || pitchClass (v + majorThirdStep) != mod12 (pitchClass (v) + 4)
                       || pitchClass (v + minorThirdStep) != mod12 (pitchClass (v) + 3);
            }
        check (wrong == 0, "the torus: (4,-1) and (0,3) leave the pitch class alone; the three steps are +7, +4, +3");

        int homes = 0;
        PcMask seen = 0;
        for (int pc = 0; pc < 12; ++pc)
        {
            const Vec h = homeNode (pc);
            homes += pitchClass (h) == pc && h.a >= 0 && h.a < 4 && h.b >= 0 && h.b < 3;
            seen = (PcMask) (seen | pcBit (pitchClass (h)));
        }
        check (homes == 12 && seen == 0xfff, "twelve nodes: each pitch class once in the 4 x 3 block");

        std::set<PcMask> triangles;
        for (int a = 0; a < 4; ++a)
            for (int b = 0; b < 3; ++b)
            {
                PcMask up = 0, down = 0;
                for (const Vec v : majorTriangle ({ a, b })) up = (PcMask) (up | pcBit (pitchClass (v)));
                for (const Vec v : minorTriangle ({ a, b })) down = (PcMask) (down | pcBit (pitchClass (v)));
                const int root = pitchClass ({ a, b });
                wrong += up != majorTriadMask (root) || down != minorTriadMask (root);
                triangles.insert (up);
                triangles.insert (down);
            }
        check (wrong == 0 && triangles.size() == 24, "twenty-four triangles: the 12 major (up) and 12 minor (down) triads");

        const auto dist = bfs (12);
        int normWrong = 0;
        for (const auto& [n, d] : dist)
            normWrong += latticeNorm ({ n.first, n.second }) != d;
        check (normWrong == 0, "latticeNorm equals breadth-first distance on the lattice graph (" + std::to_string (dist.size()) + " nodes)");

        int nearWrong = 0, torusWrong = 0;
        for (int pc = 0; pc < 12; ++pc)
        {
            for (int a = -6; a <= 6; ++a)
                for (int b = -6; b <= 6; ++b)
                {
                    const Vec near { a, b }, got = nearestNode (pc, near);
                    int best = 1 << 30;
                    for (int x = a - 6; x <= a + 6; ++x)
                        for (int y = b - 6; y <= b + 6; ++y)
                            if (pitchClass ({ x, y }) == pc)
                                best = std::min (best, latticeDistance ({ x, y }, near));
                    nearWrong += pitchClass (got) != pc || latticeDistance (got, near) != best;
                }
            int best = 1 << 30;
            for (const auto& [n, d] : dist)
                if (pitchClass ({ n.first, n.second }) == pc)
                    best = std::min (best, d);
            torusWrong += torusDistance (0, pc) != best;
        }
        check (nearWrong == 0, "nearestNode: the right pitch class at the least distance (12 x 169 positions)");
        check (torusWrong == 0, "torusDistance is the least lattice distance between copies");
    }

    void testSpelling()
    {
        const auto& sp = fixture::lattice()["spelling"];
        const auto& window = sp["window"];
        const SpellingMap inC = spellingForKey (0, false);
        int wrong = 0;
        std::string first;
        for (size_t i = 0; i < window.size(); ++i)
        {
            const Vec v { window[i]["a"].asInt(), window[i]["b"].asInt() };
            const std::string literal = noteNameOfFifths (literalFifthsIndex (v, {}, 0)).c_str(), key = inC.name (v).c_str();
            if ((literal != window[i]["literal"].str || key != window[i]["inC"].str) && wrong++ == 0)
                first = " first at (" + std::to_string (v.a) + "," + std::to_string (v.b) + "): " + literal + "/" + key;
        }
        check (wrong == 0 && window.size() == 63, "the 9 x 7 window: literal names and C major names equal the reference" + first);

        const auto& keys = sp["keys"];
        wrong = 0;
        int monkey = 0;
        first.clear();
        for (size_t k = 0; k < keys.size(); ++k)
        {
            const int tonic = keys[k]["tonic"].asInt();
            const bool minor = keys[k]["minor"].b;
            const SpellingMap m = spellingForKey (tonic, minor);
            const Spelling cm = makeSpelling (tonic, minor ? Scale::NaturalMinor : Scale::Major);
            for (int pc = 0; pc < 12; ++pc)
            {
                const std::string got = m.name (pc).c_str();
                if (got != keys[k]["names"][(size_t) pc].str && wrong++ == 0)
                    first = " first: " + std::to_string (tonic) + (minor ? "m" : "") + " pc " + std::to_string (pc) + " " + got;
                monkey += cm.name (pc) != got;
            }
        }
        check (wrong == 0 && keys.size() == 24, "the 24 keys spell all 12 pitch classes as the reference does" + first);
        check (monkey == 0, "and as ChordMonkey's makeSpelling does (theory/ChordNamer.h)");
        check (std::string (noteName (spellingOfFifths (-9), true).c_str()) == "B\xE2\x99\xAD\xE2\x99\xAD", "unicode accidentals: Bbb");
        bool roundTrip = true;
        for (int f = minFifthsIndex; f <= maxFifthsIndex; ++f)
            roundTrip = roundTrip && fifthsOfSpelling (spellingOfFifths (f)) == f && pitchClassOfFifths (f) == mod12 (7 * f);
        check (roundTrip, "line of fifths <-> spelling round-trips from Bbb to Fx... (-15..19)");
    }

    void testEmbeddings()
    {
        const auto& costs = fixture::lattice()["embeddingCost"];
        int costWrong = 0, inconsistent = 0, variant = 0;
        std::string first;
        for (const auto& type : fixture::chordTypes())
        {
            std::vector<Vec> shape0;
            for (int root = 0; root < 12; ++root)
            {
                const Embedding e = embed (type.maskAt (root));
                if (e.cost != costs[type.name].asInt() && costWrong++ == 0)
                    first = " first: " + type.name + " cost " + std::to_string (e.cost) + ", expected " + std::to_string (costs[type.name].asInt());
                inconsistent += ! isConsistent (e) || e.mask() != type.maskAt (root);
                // The same shape in every key: the nodes, sorted and moved so the first is at the
                // origin. (Which node is the root can differ for chords that are their own
                // transpositions, aug, dim7 and 7b5: their tones are interchangeable.)
                std::vector<Vec> shape (e.at.begin(), e.at.begin() + e.size);
                std::sort (shape.begin(), shape.end());
                for (size_t i = shape.size(); i-- > 0;)
                    shape[i] = shape[i] - shape[0];
                if (root == 0) shape0 = shape;
                variant += shape != shape0;
            }
        }
        check (costWrong == 0, "embedding cost equals the brute-force reference for all 30 types x 12 roots" + first);
        check (inconsistent == 0, "every embedding puts each tone on a node of its pitch class, at the cost it reports");
        check (variant == 0, "embeddings are transposition-invariant: one shape per chord type");

        int shapes = 0;
        for (int root = 0; root < 12; ++root)
        {
            const auto up = classify (embed (majorTriadMask (root))), down = classify (embed (minorTriadMask (root)));
            shapes += up.shape == Shape::Triangle && up.major && down.shape == Shape::Triangle && ! down.major;
            const auto maj7 = classify (embed (majorTriadMask (root) | pcBit (root + 11)));
            const auto m7 = classify (embed (minorTriadMask (root) | pcBit (root + 10)));
            shapes += maj7.shape == Shape::Rhombus && m7.shape == Shape::Rhombus;
            shapes += trianglesOf (embed (majorTriadMask (root) | pcBit (root + 11))).size == 2;
            shapes += classify (embed (pcBit (root))).shape == Shape::Point && classify (embed (pcBit (root) | pcBit (root + 7))).shape == Shape::Edge;
        }
        check (shapes == 48, "triads are triangles (major up, minor down), maj7 and m7 rhombi of two triangles, a note a point, a fifth an edge");

        // The previous chord picks the copy: C major after a chord far out on the plane lands next to it.
        const Embedding far = [] { Embedding e = embed (majorTriadMask (7)); for (int i = 0; i < e.size; ++i) e.at[(size_t) i] = e.at[(size_t) i] + torusPeriodA * 3; return e; }();
        const Embedding next = embed (majorTriadMask (0), &far);
        int apart = 0;
        for (int i = 0; i < next.size; ++i)
            apart = std::max (apart, latticeDistance (next.nodeOf (0), far.nodeOf (7)));
        check (apart == 1 && next.cost == 3 && classify (next).shape == Shape::Triangle, "after G major moved three periods away, C major is placed beside it");
        const Embedding home = embed (majorTriadMask (0));
        check (home.nodeOf (0) == Vec {} && home.nodeOf (7) == Vec { 1, 0 } && home.nodeOf (4) == Vec { 0, 1 }, "with no previous chord, C major sits at the origin");

        // Every pitch-class set embeds within the search budget, quickly.
        const auto t0 = std::chrono::steady_clock::now();
        int bad = 0;
        double worstMs = 0.0;
        for (unsigned m = 1; m < 4096; ++m)
        {
            const auto s = std::chrono::steady_clock::now();
            const Embedding e = embed (m);
            worstMs = std::max (worstMs, std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - s).count());
            bad += ! isConsistent (e) || e.mask() != m;
        }
        const double totalMs = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
        check (bad == 0, "all 4095 pitch-class sets embed consistently (" + std::to_string ((int) totalMs) + " ms in all, worst "
                             + std::to_string (worstMs).substr (0, 5) + " ms)");
    }

    void testRoots()
    {
        // chordRoot over its own bass: the root of the plain chord types, in every key.
        const char* rooted[] = { "maj", "min", "dim", "aug", "sus4", "5", "maj7", "m7", "7", "m7b5", "mMaj7", "6", "m6", "7sus4",
                                 "add9", "9", "maj9", "m9", "7b9", "7#9", "13" };
        int wrong = 0, moved = 0;
        std::string first;
        for (const auto& type : fixture::chordTypes())
        {
            bool expectRoot = false;
            for (const char* n : rooted) expectRoot = expectRoot || type.name == n;
            const int root0 = chordRoot (type.maskAt (0), 0);
            for (int r = 0; r < 12; ++r)
            {
                const int got = chordRoot (type.maskAt (r), r);
                moved += got != mod12 (root0 + r);     // the root moves with the chord
                if (expectRoot && got != r && wrong++ == 0)
                    first = " first: " + type.name + " on " + std::to_string (r) + " -> " + std::to_string (got);
            }
        }
        check (wrong == 0, "chordRoot finds the root of 21 plain chord types over their root, in all 12 keys" + first);
        check (moved == 0, "chordRoot is transposition-invariant for all 30 types");
        check (nameChord (majorTriadMask (0) | pcBit (11), 0, makeSpelling (0, Scale::Major)).text() == "Cmaj7"
                   && nameChord (minorTriadMask (9), 9, makeSpelling (0, Scale::Major)).text() == "Am"
                   && nameChord (minorTriadMask (9), 0, makeSpelling (0, Scale::Major)).text() == "C6"
                   && nameChord (majorTriadMask (6), 6, makeSpelling (1, Scale::Major)).text() == "Gb",
               "names: Cmaj7, Am, A C E over C is C6 (ties go to the bass, as in ChordMonkey), Gb in Db major");
    }
}

int main()
{
    testTorus();
    testSpelling();
    testEmbeddings();
    testRoots();
    return testutil::finish ("test_lattice");
}
