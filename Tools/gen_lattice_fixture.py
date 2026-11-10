#!/usr/bin/env python3
"""Writes Tests/fixtures/lattice_reference.json (PLAN.md §7.1): the expected results of the
lattice operations, embeddings, spellings and cycles, worked out independently of the C++.

It uses plain pitch-class arithmetic and a breadth-first search over the lattice graph: no code
is shared with Source/core/lattice, only the rules as PLAN.md §2 states them. The output is
committed, so the tests need no Python.

    python3 Tools/gen_lattice_fixture.py            # writes the fixture
    python3 Tools/gen_lattice_fixture.py --check    # exits 1 if the committed one differs

The rules (PLAN.md §2, "Choosing the edge"):

  A chord is a state (X, a, s): its pitch-class set X, an anchor a in X (the root when the
  chord comes in) and a parity s (0 when it comes in).

  P, R, L reflect through an edge of the chord of 7, 4, 3 semitones: a pair (p, p + k) of
  chord tones, x -> 2p + k - x. Among the chord's edges of that size the one used is the
  first by
      1. it contains the anchor;
      2. its midpoint (p + k/2) is nearest the anchor around the pitch-class circle;
      3. (p - a) mod 12 is smallest if s = 0, (a - p - k) mod 12 if s = 1.
  No such edge: the op is unavailable and leaves the state as it is.

  Any reflection x -> c - x maps the anchor too (a -> c - a) and flips the parity. A
  translation by t semitones moves the anchor by t. Grow and Shrink leave both alone.

  N = R L P, S = L P R, H = L P L (applied left to right); available when all three steps
  are, otherwise the whole op is a no-op.
  T(a, b) moves by 7a + 4b semitones. Flip(i, j) reflects through the chord tones a + i and
  a + j, Pivot(i) through the chord tone a + i, Grow(i, j) adds the vertex across the edge
  (a + i, a + j) from the chord's triangle on it; each is unavailable if its tones aren't in
  the chord (Grow: if the edge isn't a third or a fifth, or not exactly one of the two
  triangles on it has its third tone in the chord). Shrink removes the tone added by the
  latest Grow not yet shrunk (followed through the ops since); after an unavailable Grow,
  nothing.
"""
import json
import os
import sys
from collections import deque

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "Tests", "fixtures", "lattice_reference.json")

CHORD_TYPES = [
    ("maj", [0, 4, 7]), ("min", [0, 3, 7]), ("dim", [0, 3, 6]), ("aug", [0, 4, 8]),
    ("sus2", [0, 2, 7]), ("sus4", [0, 5, 7]), ("5", [0, 7]), ("note", [0]),
    ("M3", [0, 4]), ("m3", [0, 3]),
    ("maj7", [0, 4, 7, 11]), ("m7", [0, 3, 7, 10]), ("7", [0, 4, 7, 10]), ("m7b5", [0, 3, 6, 10]),
    ("dim7", [0, 3, 6, 9]), ("mMaj7", [0, 3, 7, 11]), ("maj7#5", [0, 4, 8, 11]), ("7#5", [0, 4, 8, 10]),
    ("7b5", [0, 4, 6, 10]), ("6", [0, 4, 7, 9]), ("m6", [0, 3, 7, 9]), ("7sus4", [0, 5, 7, 10]),
    ("add9", [0, 2, 4, 7]), ("madd9", [0, 2, 3, 7]), ("9", [0, 2, 4, 7, 10]), ("maj9", [0, 2, 4, 7, 11]),
    ("m9", [0, 2, 3, 7, 10]), ("7b9", [0, 1, 4, 7, 10]), ("7#9", [0, 3, 4, 7, 10]), ("13", [0, 2, 4, 7, 9, 10]),
]

OPS = ["P", "L", "R", "N", "S", "H", "+5", "-5", "+M3", "-M3", "+m3", "-m3", "T(2,-1)",
       "Flip(0,4)", "Flip(4,7)", "Flip(0,10)", "Pivot(0)", "Pivot(7)", "Pivot(3)",
       "Grow(4,7)", "Grow(0,4)", "Grow(0,7)", "Grow(3,7)"]

WORDS = ["P L", "P L R", "N S H", "P +5 L", "R -M3 Pivot(0) L", "Grow(4,7) P Shrink",
         "Flip(0,7) +m3 R", "L R L R L R", "Grow(0,4) Grow(4,7) L Shrink R Shrink",
         "H -5 N T(1,1) S", "Shrink P", "Grow(0,4) Shrink Shrink"]

CYCLE_WORDS = ["P L", "P R", "L R", "P L R"]
CYCLE_TYPES = ["maj", "min", "maj7", "m7", "7", "m7b5", "dim7", "aug", "sus4"]

EDGE = {"P": 7, "R": 4, "L": 3}
TRANSLATIONS = {"+5": (1, 0), "-5": (-1, 0), "+M3": (0, 1), "-M3": (0, -1), "+m3": (1, -1), "-m3": (-1, 1)}
COMPOSITES = {"N": "RLP", "S": "LPR", "H": "LPL"}


# ---- ops on states (X, anchor, parity); a step also returns its pitch-class map -----------

def reflect_state(state, c):
    X, a, s = state
    return (frozenset((c - x) % 12 for x in X), (c - a) % 12, 1 - s), ("reflect", c % 12)


def edge_for(state, k):
    X, a, s = state
    cands = [p for p in range(12) if p in X and (p + k) % 12 in X]
    if not cands:
        return None

    def key(p):
        contains = 0 if a in (p, (p + k) % 12) else 1
        d = (2 * p + k - 2 * a) % 24
        d = min(d, 24 - d)
        u = (p - a) % 12 if s == 0 else (a - p - k) % 12
        return (contains, d, u)

    return min(cands, key=key)


def parse_args(tok):
    inner = tok[tok.index("(") + 1: tok.index(")")]
    return [int(v) for v in inner.split(",")]


def apply_op(tok, state):
    """(available, new state, [maps], added pc or None)"""
    X, a, s = state
    if tok in EDGE:
        p = edge_for(state, EDGE[tok])
        if p is None:
            return False, state, [], None
        ns, m = reflect_state(state, 2 * p + EDGE[tok])
        return True, ns, [m], None
    if tok in COMPOSITES:
        cur, maps = state, []
        for t in COMPOSITES[tok]:
            ok, cur, ms, _ = apply_op(t, cur)
            if not ok:
                return False, state, [], None
            maps += ms
        return True, cur, maps, None
    if tok in TRANSLATIONS or tok.startswith("T("):
        va, vb = TRANSLATIONS[tok] if tok in TRANSLATIONS else parse_args(tok)
        t = (7 * va + 4 * vb) % 12
        return True, (frozenset((x + t) % 12 for x in X), (a + t) % 12, s), [("move", t)], None
    if tok.startswith("Flip("):
        i, j = parse_args(tok)
        p, q = (a + i) % 12, (a + j) % 12
        if p not in X or q not in X:
            return False, state, [], None
        ns, m = reflect_state(state, p + q)
        return True, ns, [m], None
    if tok.startswith("Pivot("):
        (i,) = parse_args(tok)
        p = (a + i) % 12
        if p not in X:
            return False, state, [], None
        ns, m = reflect_state(state, 2 * p)
        return True, ns, [m], None
    if tok.startswith("Grow("):
        i, j = parse_args(tok)
        p, q = (a + i) % 12, (a + j) % 12
        if p not in X or q not in X:
            return False, state, [], None
        d = (q - p) % 12
        # The third tones of the two triads (major or minor) that contain p and q.
        thirds = [r for r in range(12) if r not in (p, q)
                  and any(sorted(((x - root) % 12) for x in (p, q, r)) in ([0, 4, 7], [0, 3, 7])
                          for root in (p, q, r))]
        if d not in (3, 4, 5, 7, 8, 9) or len(thirds) != 2:
            return False, state, [], None
        inside = [r for r in thirds if r in X]
        if len(inside) != 1:
            return False, state, [], None
        new = [r for r in thirds if r not in X][0]
        return True, (X | {new}, a, s), [], new
    raise ValueError(tok)


def map_pc(maps, x):
    for kind, v in maps:
        x = (v - x) % 12 if kind == "reflect" else (x + v) % 12
    return x


def apply_word(tokens, state):
    grown = []                 # pc added by each Grow not yet shrunk, or None
    for tok in tokens:
        if tok == "Shrink":
            if grown:
                g = grown.pop()
                if g is not None:
                    X, a, s = state
                    state = (X - {g}, a, s)
            continue
        ok, state, maps, added = apply_op(tok, state)
        grown = [None if g is None else map_pc(maps, g) for g in grown]
        if tok.startswith("Grow("):
            grown.append(added if ok else None)
    return state


def mask_of(X):
    return sum(1 << x for x in X)


def encode(state, ok=True):
    X, a, s = state
    return [mask_of(X), a, s, 1 if ok else 0]


# ---- the lattice graph, for embeddings ---------------------------------------------------

STEPS = [(1, 0), (0, 1), (1, -1), (-1, 0), (0, -1), (-1, 1)]


def pc_of(node):
    return (7 * node[0] + 4 * node[1]) % 12


def bfs(radius):
    dist = {(0, 0): 0}
    q = deque([(0, 0)])
    while q:
        n = q.popleft()
        if dist[n] == radius:
            continue
        for s in STEPS:
            m = (n[0] + s[0], n[1] + s[1])
            if m not in dist:
                dist[m] = dist[n] + 1
                q.append(m)
    return dist


DIST = bfs(16)


def lattice_distance(u, v):
    return DIST[(v[0] - u[0], v[1] - u[1])]


def embedding_cost(pcs, radius=6):
    """The least sum of pairwise lattice distances, by brute force: the first tone on its
    node (C at the origin), each other tone at any copy within `radius` steps of it."""
    pcs = sorted(pcs)
    first = next(n for n in DIST if DIST[n] <= 2 and pc_of(n) == pcs[0])
    near = [n for n in DIST if DIST[n] <= radius]
    copies = [[(first[0] + n[0], first[1] + n[1]) for n in near if pc_of((first[0] + n[0], first[1] + n[1])) == pc]
              for pc in pcs[1:]]
    best = [10 ** 9]

    def go(i, placed, cost):
        if cost >= best[0]:
            return
        if i == len(copies):
            best[0] = cost
            return
        for c in copies[i]:
            go(i + 1, placed + [c], cost + sum(lattice_distance(c, p) for p in placed))

    go(0, [first], 0)
    return best[0]


# ---- spelling ----------------------------------------------------------------------------

LETTERS = "CDEFGAB"
NATURAL = [0, 2, 4, 5, 7, 9, 11]


def name_of_fifths(i):
    letter = "FCGDAEB"[(i + 1) % 7]
    acc = (i + 1) // 7
    return letter + ("#" * acc if acc > 0 else "b" * -acc)


def fifths_of(letter, acc):
    return [-1 + 7 * acc + "FCGDAEB".index(letter)][0]


def key_spelling(tonic, minor):
    # The tonic: the key signature with the fewer accidentals (major: index = sharps; minor:
    # index - 3), a tie going to sharps in major and flats in minor.
    cands = [i for i in range(-14, 15) if (7 * i) % 12 == tonic]
    sig = (lambda i: i) if not minor else (lambda i: i - 3)
    t = min(cands, key=lambda i: (abs(sig(i)), -sig(i) if not minor else sig(i)))
    tonic_letter = name_of_fifths(t)[0]
    steps = [0, 2, 3, 5, 7, 8, 10] if minor else [0, 2, 4, 5, 7, 9, 11]
    names = [None] * 12
    for deg, st in enumerate(steps):
        letter = LETTERS[(LETTERS.index(tonic_letter) + deg) % 7]
        pc = (tonic + st) % 12
        acc = (pc - NATURAL[LETTERS.index(letter)] + 6) % 12 - 6
        names[pc] = letter + ("#" * acc if acc > 0 else "b" * -acc)
    for pc in range(12):
        if names[pc] is not None:
            continue
        options = []
        for letter in LETTERS:
            acc = (pc - NATURAL[LETTERS.index(letter)] + 6) % 12 - 6
            if abs(acc) <= 2:
                options.append((letter, acc))
        def rank(o):
            f = fifths_of(o[0], o[1])
            return (abs(o[1]), 0 if t - 5 <= f <= t + 6 else 1, abs(f - t))
        letter, acc = min(options, key=rank)
        names[pc] = letter + ("#" * acc if acc > 0 else "b" * -acc)
    return names


def literal_name(a, b):
    f = a + 4 * b
    while f > 19:
        f -= 12
    while f < -15:
        f += 12
    return name_of_fifths(f)


# ---- build -------------------------------------------------------------------------------

def build():
    out = {"chordTypes": [{"name": n, "intervals": iv} for n, iv in CHORD_TYPES], "ops": OPS, "results": {}}
    for name, iv in CHORD_TYPES:
        per_op = {}
        for op in OPS:
            row = []
            for parity in (0, 1):
                for root in range(12):
                    state = (frozenset((root + x) % 12 for x in iv), root, parity)
                    ok, ns, _, _ = apply_op(op, state)
                    row.append(encode(ns, ok))
            per_op[op] = row
        out["results"][name] = per_op

    out["words"] = WORDS
    out["wordResults"] = {
        name: {w: [encode(apply_word(w.split(), (frozenset((r + x) % 12 for x in iv), r, 0))) for r in range(12)]
               for w in WORDS}
        for name, iv in CHORD_TYPES}

    out["embeddingCost"] = {name: embedding_cost([x for x in iv]) for name, iv in CHORD_TYPES}

    # The PLR group, as permutations of the 24 triads.
    triads = [frozenset({r, (r + 4) % 12, (r + 7) % 12}) for r in range(12)] + \
             [frozenset({r, (r + 3) % 12, (r + 7) % 12}) for r in range(12)]
    index = {t: i for i, t in enumerate(triads)}
    gens = []
    for op in "PLR":
        gens.append(tuple(index[apply_op(op, (t, min(t), 0))[1][0]] for t in triads))
    group = {tuple(range(24))}
    frontier = list(group)
    while frontier:
        g = frontier.pop()
        for h in gens:
            gh = tuple(h[g[i]] for i in range(24))
            if gh not in group:
                group.add(gh)
                frontier.append(gh)
    out["plrGroupOrder"] = len(group)

    cycles = []
    types = dict(CHORD_TYPES)
    for w in CYCLE_WORDS:
        toks = w.split()
        for name in CYCLE_TYPES:
            for root in range(12):
                start = (frozenset((root + x) % 12 for x in types[name]), root, 0)
                state, steps, distinct = start, 0, set()
                while True:
                    state = apply_op(toks[steps % len(toks)], state)[1]
                    steps += 1
                    distinct.add(state[0])
                    if steps % len(toks) == 0 and state == start:
                        break
                cycles.append({"word": w, "type": name, "root": root, "length": steps, "chords": len(distinct)})
    out["cycles"] = cycles

    out["spelling"] = {
        "window": [{"a": a, "b": b, "literal": literal_name(a, b), "inC": key_spelling(0, False)[pc_of((a, b))]}
                   for b in range(3, -4, -1) for a in range(-4, 5)],
        "keys": [{"tonic": t, "minor": m, "names": key_spelling(t, m)} for m in (False, True) for t in range(12)],
    }
    return out


def dump(data):
    # One line per op row, so a diff of the fixture stays readable.
    lines = json.dumps(data, separators=(",", ":"), sort_keys=True)
    return lines + "\n"


def main():
    text = dump(build())
    if "--check" in sys.argv:
        with open(OUT) as f:
            same = f.read() == text
        print("fixture up to date" if same else "fixture differs: run Tools/gen_lattice_fixture.py")
        sys.exit(0 if same else 1)
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w") as f:
        f.write(text)
    print("wrote", os.path.relpath(OUT), "(%d bytes)" % len(text))


if __name__ == "__main__":
    main()
