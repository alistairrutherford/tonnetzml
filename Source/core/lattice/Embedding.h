// A chord as a shape on the lattice (PLAN.md §2): for each tone of a pitch-class set, the copy
// of its node that makes the whole shape most compact (the smallest sum of lattice distances
// over all pairs of tones). Triads come out as triangles, maj7 and m7 as rhombi (two triangles
// sharing an edge), everything else as a looser but still definite shape.
//
// Plain C++17, no JUCE, no allocation. `embed` is a bounded search (worst case, all twelve
// tones, well under a millisecond): cheap enough anywhere, but the engine does not need it (the
// ops work on pitch classes); it is for whoever draws the chord.
//
// Ties, in order:
//   - with a previous chord: the embedding nearest it (the smallest sum of distances from the new
//     nodes to the previous ones), which also picks which copy of the shape on the repeating
//     plane is meant;
//   - the canonical shape (so a chord type has one shape in every transposition);
//   - with no previous chord: the copy nearest the origin.
#pragma once

#include "Lattice.h"

namespace tonnetz
{
    constexpr int maxChordTones = 12;

    struct Embedding
    {
        int size = 0;
        std::array<int8_t, maxChordTones> pc {};     // ascending pitch class
        std::array<Vec, maxChordTones> at {};        // the node of each
        int cost = 0;                                // sum of lattice distances over all pairs

        PcMask mask() const noexcept
        {
            unsigned m = 0;
            for (int i = 0; i < size; ++i)
                m |= 1u << pc[(size_t) i];
            return (PcMask) m;
        }

        int indexOf (int pitchClassWanted) const noexcept
        {
            pitchClassWanted = mod12 (pitchClassWanted);
            for (int i = 0; i < size; ++i)
                if (pc[(size_t) i] == pitchClassWanted)
                    return i;
            return -1;
        }

        bool contains (int pitchClassWanted) const noexcept { return indexOf (pitchClassWanted) >= 0; }

        // The node of a tone of the chord (the home node if it is not in the chord).
        Vec nodeOf (int pitchClassWanted) const noexcept
        {
            const int i = indexOf (pitchClassWanted);
            return i >= 0 ? at[(size_t) i] : homeNode (pitchClassWanted);
        }

        // Twice the centroid would need fractions: the sum of the nodes and the count instead.
        Vec sum() const noexcept
        {
            Vec s;
            for (int i = 0; i < size; ++i)
                s = s + at[(size_t) i];
            return s;
        }
    };

    inline int pairwiseCost (const Vec* nodes, int n) noexcept
    {
        int c = 0;
        for (int i = 0; i < n; ++i)
            for (int j = 0; j < i; ++j)
                c += latticeDistance (nodes[i], nodes[j]);
        return c;
    }

    // True if every node carries the pitch class it claims and the cost is right.
    inline bool isConsistent (const Embedding& e) noexcept
    {
        for (int i = 0; i < e.size; ++i)
            if (pitchClass (e.at[(size_t) i]) != e.pc[(size_t) i] || (i > 0 && e.pc[(size_t) i] <= e.pc[(size_t) i - 1]))
                return false;
        return e.cost == pairwiseCost (e.at.data(), e.size);
    }

    namespace embedding_detail
    {
        constexpr int searchRadius = 4;         // no tone of a most compact shape is further from another
        constexpr int maxCopies = 8;
        constexpr int maxOptima = 64;
        constexpr int nodeBudget = 300000;      // never reached by a real chord: see test_lattice

        // For each interval 1..11 above a tone at the origin: the nodes within the search radius
        // that carry it, nearest first.
        struct Copies
        {
            Vec node[12][maxCopies] {};
            int count[12] {};
        };

        constexpr Copies makeCopies() noexcept
        {
            Copies c;
            for (int norm = 1; norm <= searchRadius; ++norm)
                for (int a = -searchRadius; a <= searchRadius; ++a)
                    for (int b = -searchRadius; b <= searchRadius; ++b)
                    {
                        const Vec v { a, b };
                        const int pc = pitchClass (v);
                        if (latticeNorm (v) == norm && pc != 0 && c.count[pc] < maxCopies)
                            c.node[pc][c.count[pc]++] = v;
                    }
            return c;
        }

        inline constexpr Copies copies = makeCopies();

        struct Search
        {
            int n = 0;
            int rel[maxChordTones] {};          // interval of each tone above the first
            int remaining[maxChordTones + 1] {};// the least the pairs involving tones i.. can cost
            Vec place[maxChordTones] {};
            int best = 1 << 30;
            int visited = 0;
            int numOptima = 0;
            Vec optima[maxOptima][maxChordTones] {};

            void run (int i, int partial) noexcept
            {
                if (i == n)
                {
                    if (partial < best)
                    {
                        best = partial;
                        numOptima = 0;
                    }
                    if (numOptima < maxOptima)
                    {
                        for (int k = 0; k < n; ++k)
                            optima[numOptima][k] = place[k];
                        ++numOptima;
                    }
                    return;
                }
                const int interval = rel[i];
                for (int c = 0; c < copies.count[interval]; ++c)
                {
                    if (++visited > nodeBudget)
                        return;
                    const Vec v = copies.node[interval][c];
                    int cost = partial;
                    for (int j = 0; j < i; ++j)
                        cost += latticeDistance (v, place[j]);
                    if (cost + remaining[i + 1] > best)
                        continue;
                    place[i] = v;
                    run (i + 1, cost);
                }
            }
        };

        // Sorted, with the first node moved to the origin: equal for two copies of one shape.
        inline void canonical (const Vec* nodes, int n, Vec* out) noexcept
        {
            for (int i = 0; i < n; ++i)
            {
                int j = i;
                for (; j > 0 && nodes[i] < out[j - 1]; --j)
                    out[j] = out[j - 1];
                out[j] = nodes[i];
            }
            const Vec first = n > 0 ? out[0] : Vec {};
            for (int i = 0; i < n; ++i)
                out[i] = out[i] - first;
        }

        inline int compare (const Vec* x, const Vec* y, int n) noexcept
        {
            for (int i = 0; i < n; ++i)
                if (x[i] != y[i])
                    return x[i] < y[i] ? -1 : 1;
            return 0;
        }
    }

    // The most compact embedding of the pitch classes in `mask` (see the top of the file).
    inline Embedding embed (unsigned mask, const Embedding* previous = nullptr) noexcept
    {
        using namespace embedding_detail;
        Embedding e;
        mask &= 0xfffu;
        for (int pc = 0; pc < 12; ++pc)
            if ((mask >> pc) & 1u)
                e.pc[(size_t) e.size++] = (int8_t) pc;
        const int n = e.size;
        if (n == 0)
            return e;
        if (previous != nullptr && previous->size == 0)
            previous = nullptr;

        Search s;
        s.n = n;
        for (int i = 0; i < n; ++i)
            s.rel[i] = e.pc[(size_t) i] - e.pc[0];
        for (int i = n - 1; i >= 0; --i)
        {
            int least = 0;
            for (int j = 0; j < i; ++j)
                least += torusDistance (s.rel[j], s.rel[i]);
            s.remaining[i] = s.remaining[i + 1] + least;
        }
        s.place[0] = {};
        s.run (1, 0);

        // Where the shape goes: its first tone's home node, moved by whole periods of the torus.
        const Vec origin {};
        const Vec* targets = previous != nullptr ? previous->at.data() : &origin;
        const int numTargets = previous != nullptr ? previous->size : 1;
        Vec targetSum;
        for (int i = 0; i < numTargets; ++i)
            targetSum = targetSum + targets[i];
        const Vec home = homeNode (e.pc[0]);

        bool have = false;
        int bestNear = 0;
        Vec bestShape[maxChordTones] {}, bestNodes[maxChordTones] {};

        for (int o = 0; o < s.numOptima; ++o)
        {
            const Vec* shape = s.optima[o];
            Vec shapeKey[maxChordTones] {};
            canonical (shape, n, shapeKey);

            Vec shapeSum;
            for (int i = 0; i < n; ++i)
                shapeSum = shapeSum + shape[i];
            // The period nearest (target centroid) - (shape centroid), then its neighbours.
            const Vec want { floorDiv (targetSum.a, numTargets) - home.a - floorDiv (shapeSum.a, n),
                             floorDiv (targetSum.b, numTargets) - home.b - floorDiv (shapeSum.b, n) };
            const int i0 = floorDiv (want.a + 2, 4);
            const int j0 = floorDiv (want.b + i0 + 1, 3);

            for (int pi = i0 - 2; pi <= i0 + 2; ++pi)
                for (int pj = j0 - 2; pj <= j0 + 2; ++pj)
                {
                    const Vec shift = home + torusPeriodA * pi + torusPeriodB * pj;
                    Vec nodes[maxChordTones] {};
                    int near = 0;
                    for (int i = 0; i < n; ++i)
                    {
                        nodes[i] = shape[i] + shift;
                        for (int t = 0; t < numTargets; ++t)
                            near += latticeDistance (nodes[i], targets[t]);
                    }

                    bool better = ! have;
                    if (have)
                    {
                        const int byShape = compare (shapeKey, bestShape, n);
                        const int byNear = near < bestNear ? -1 : (near > bestNear ? 1 : 0);
                        const int first = previous != nullptr ? byNear : byShape;
                        const int second = previous != nullptr ? byShape : byNear;
                        better = first != 0 ? first < 0
                               : second != 0 ? second < 0
                               : compare (nodes, bestNodes, n) < 0;
                    }
                    if (better)
                    {
                        have = true;
                        bestNear = near;
                        for (int i = 0; i < n; ++i)
                        {
                            bestShape[i] = shapeKey[i];
                            bestNodes[i] = nodes[i];
                        }
                    }
                }
        }

        for (int i = 0; i < n; ++i)
            e.at[(size_t) i] = bestNodes[i];
        e.cost = s.best;
        return e;
    }

    // ---- Shapes ----------------------------------------------------------------------------
    enum class Shape : uint8_t
    {
        Empty,
        Point,       // one tone
        Edge,        // two tones a fifth or a third apart
        Triangle,    // a major or minor triad
        Rhombus,     // two triangles sharing an edge: maj7, m7 (and their inversions, 6 and m6... as sets)
        Other        // anything looser: draw its nodes, edges and whatever triangles it has
    };

    struct ShapeInfo
    {
        Shape shape = Shape::Empty;
        bool major = false;          // Triangle: an upward triangle (a major triad)
        int adjacentPairs = 0;       // pairs of tones that are lattice neighbours (edges to draw)
    };

    inline ShapeInfo classify (const Embedding& e) noexcept
    {
        ShapeInfo info;
        for (int i = 0; i < e.size; ++i)
            for (int j = 0; j < i; ++j)
                info.adjacentPairs += (int) adjacent (e.at[(size_t) i], e.at[(size_t) j]);

        if (e.size == 0)       info.shape = Shape::Empty;
        else if (e.size == 1)  info.shape = Shape::Point;
        else if (e.size == 2)  info.shape = info.adjacentPairs == 1 ? Shape::Edge : Shape::Other;
        else if (e.size == 3 && info.adjacentPairs == 3)
        {
            info.shape = Shape::Triangle;
            // An upward triangle has two nodes on its lower row.
            int lowest = e.at[0].b, onLowest = 0;
            for (int i = 1; i < 3; ++i)
                lowest = e.at[(size_t) i].b < lowest ? e.at[(size_t) i].b : lowest;
            for (int i = 0; i < 3; ++i)
                onLowest += (int) (e.at[(size_t) i].b == lowest);
            info.major = onLowest == 2;
        }
        else if (e.size == 4 && info.adjacentPairs == 5) info.shape = Shape::Rhombus;
        else info.shape = Shape::Other;
        return info;
    }

    // The unit triangles among an embedding's nodes (what the editor fills), as indices into it.
    constexpr int maxShapeTriangles = 32;

    struct ShapeTriangles
    {
        int size = 0;
        std::array<std::array<int8_t, 3>, maxShapeTriangles> tones {};
    };

    inline ShapeTriangles trianglesOf (const Embedding& e) noexcept
    {
        ShapeTriangles t;
        for (int i = 0; i < e.size; ++i)
            for (int j = i + 1; j < e.size; ++j)
                if (adjacent (e.at[(size_t) i], e.at[(size_t) j]))
                    for (int k = j + 1; k < e.size; ++k)
                        if (adjacent (e.at[(size_t) i], e.at[(size_t) k]) && adjacent (e.at[(size_t) j], e.at[(size_t) k])
                            && t.size < maxShapeTriangles)
                            t.tones[(size_t) t.size++] = { { (int8_t) i, (int8_t) j, (int8_t) k } };
        return t;
    }
}
