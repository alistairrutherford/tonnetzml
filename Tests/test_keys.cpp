// Key guessing (PLAN.md §3.6, §7.1 test_keys).
//
// Part 1 is ChordMonkey's key test (its Tests/test_keys.cpp, ChordChimp's KEY_DETECTION_PLAN.md
// K6.1), copied with its corpus unchanged, run on TonnetzML's copy of the listener: the
// profiles, the corpus of tunes and progressions in every key, ChordMonkey's own chords,
// modulation, silence, ambiguous input, weighting, hysteresis, fuzz. Two adaptations, both
// because TonnetzML has no ChordMonkey engine or scale set: "ChordMonkey's own chords" are the
// notes its engine played, recorded once in Tests/fixtures/chordmonkey_own_chords.txt
// (Tools/gen_key_fixture.cpp) and replayed here on the same samples and blocks; the scale
// mapping checks the two scales TonnetzML has (ChordMonkey's checks its 13). ChordMonkey's
// engine tests (Analyse mode) are replaced by part 2.
//
// Part 2 is TonnetzML's engine (LatticeEngine.h): the guess is the same with Mix on Generated or
// Source + Generated, in any mode and under any word (only source notes are heard); operator keys
// are not heard; Hold freezes the guess; Listen after Hold starts empty; Off forgets; *In key*
// marks exactly the diatonic neighbours (WordEditor.h's opsInKey) for all 24 keys.
#include "TestUtil.h"
#include "FakeHost.h"
#include "core/engine/KeyListener.h"
#include "core/engine/LatticeEngine.h"
#include "core/theory/KeyProfiles.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace tonnetz;

using testutil::check;

namespace
{
    struct Note
    {
        double start = 0.0, length = 0.0;   // seconds
        int pitch = 60, velocity = 100;
    };

    using Notes = std::vector<Note>;

    // A melody from (pitch, beats) pairs at `bpm`, each note held for 90% of its beats; pitch
    // -1 is a rest.
    Notes melody (std::initializer_list<std::pair<int, double>> tune, double bpm = 100.0, double start = 0.0)
    {
        Notes n;
        const double beat = 60.0 / bpm;
        double t = start;
        for (const auto& [pitch, beats] : tune)
        {
            if (pitch >= 0)
                n.push_back ({ t, 0.9 * beats * beat, pitch, 100 });
            t += beats * beat;
        }
        return n;
    }

    // Block chords, one per `beats`, each a list of pitches.
    Notes chords (std::initializer_list<std::pair<std::vector<int>, double>> seq, double bpm = 100.0, double start = 0.0)
    {
        Notes n;
        const double beat = 60.0 / bpm;
        double t = start;
        for (const auto& [pitches, beats] : seq)
        {
            for (const int p : pitches)
                n.push_back ({ t, 0.95 * beats * beat, p, 90 });
            t += beats * beat;
        }
        return n;
    }

    Notes transpose (Notes n, int semitones)
    {
        for (auto& x : n)
            x.pitch += semitones;
        return n;
    }

    Notes append (Notes a, const Notes& b, double offset)
    {
        for (auto x : b)
        {
            x.start += offset;
            a.push_back (x);
        }
        return a;
    }

    double endOf (const Notes& n)
    {
        double e = 0.0;
        for (const auto& x : n)
            e = std::max (e, x.start + x.length);
        return e;
    }

    struct Change
    {
        double time;
        KeyGuess guess;
    };

    // Feeds the notes to `l` in time order (note-offs first at one time), advancing every 10 ms
    // until `until`; returns each change of the key shown (tonic or mode), and when it came.
    std::vector<Change> play (KeyListener& l, const Notes& notes, double until)
    {
        struct Ev { double t; bool on; int pitch, velocity; };
        std::vector<Ev> ev;
        for (const auto& n : notes)
        {
            ev.push_back ({ n.start, true, n.pitch, n.velocity });
            ev.push_back ({ n.start + n.length, false, n.pitch, 0 });
        }
        std::stable_sort (ev.begin(), ev.end(), [] (const Ev& a, const Ev& b) { return a.t < b.t || (a.t == b.t && ! a.on && b.on); });

        std::vector<Change> changes;
        KeyGuess last;
        size_t i = 0;
        for (double t = 0.0; t <= until + 1.0e-9; t += 0.01)
        {
            for (; i < ev.size() && ev[i].t <= t; ++i)
                ev[i].on ? l.noteOn (ev[i].pitch, ev[i].velocity, ev[i].t) : l.noteOff (ev[i].pitch, ev[i].t);
            l.advance (t);
            const KeyGuess g = l.guess();
            if (g.has() && (! last.has() || g.tonic != last.tonic || g.minor != last.minor))
                changes.push_back ({ t, g });
            last = g.has() ? g : last;
        }
        return changes;
    }

    std::string keyText (int tonic, bool minor)
    {
        return tonic < 0 ? std::string ("none") : std::string (guessKeyName (tonic, minor)) + (minor ? " minor" : " major");
    }

    std::string describe (const KeyGuess& g)
    {
        if (! g.has())
            return "no guess";
        return keyText (g.tonic, g.minor) + " " + std::to_string (g.fit) + "%" + (g.state == GuessState::Confident ? "" : "?")
             + " (next " + keyText (g.runnerUpTonic, g.runnerUpMinor) + " " + std::to_string (g.runnerUpFit) + "%)";
    }

    // The corpus, written in C (or A for minor) and transposed by the tests.
    enum class Expect { Major, Minor, Record };

    struct Case
    {
        const char* name;
        Notes notes;
        int tonic;                        // in the written key
        Expect expect;
    };

    // C4 = 60.
    std::vector<Case> corpus()
    {
        std::vector<Case> c;
        c.push_back ({ "Twinkle Twinkle (C major)", melody ({ { 60, 1 }, { 60, 1 }, { 67, 1 }, { 67, 1 }, { 69, 1 }, { 69, 1 }, { 67, 2 },
                                                              { 65, 1 }, { 65, 1 }, { 64, 1 }, { 64, 1 }, { 62, 1 }, { 62, 1 }, { 60, 2 },
                                                              { 67, 1 }, { 67, 1 }, { 65, 1 }, { 65, 1 }, { 64, 1 }, { 64, 1 }, { 62, 2 },
                                                              { 67, 1 }, { 67, 1 }, { 65, 1 }, { 65, 1 }, { 64, 1 }, { 64, 1 }, { 62, 2 },
                                                              { 60, 1 }, { 60, 1 }, { 67, 1 }, { 67, 1 }, { 69, 1 }, { 69, 1 }, { 67, 2 },
                                                              { 65, 1 }, { 65, 1 }, { 64, 1 }, { 64, 1 }, { 62, 1 }, { 62, 1 }, { 60, 2 } }),
                       0, Expect::Major });
        c.push_back ({ "Ode to Joy (C major)", melody ({ { 64, 1 }, { 64, 1 }, { 65, 1 }, { 67, 1 }, { 67, 1 }, { 65, 1 }, { 64, 1 }, { 62, 1 },
                                                         { 60, 1 }, { 60, 1 }, { 62, 1 }, { 64, 1 }, { 64, 1.5 }, { 62, 0.5 }, { 62, 2 },
                                                         { 64, 1 }, { 64, 1 }, { 65, 1 }, { 67, 1 }, { 67, 1 }, { 65, 1 }, { 64, 1 }, { 62, 1 },
                                                         { 60, 1 }, { 60, 1 }, { 62, 1 }, { 64, 1 }, { 62, 1.5 }, { 60, 0.5 }, { 60, 2 } }),
                       0, Expect::Major });
        c.push_back ({ "Frere Jacques (C major)", melody ({ { 60, 1 }, { 62, 1 }, { 64, 1 }, { 60, 1 }, { 60, 1 }, { 62, 1 }, { 64, 1 }, { 60, 1 },
                                                            { 64, 1 }, { 65, 1 }, { 67, 2 }, { 64, 1 }, { 65, 1 }, { 67, 2 },
                                                            { 67, 0.5 }, { 69, 0.5 }, { 67, 0.5 }, { 65, 0.5 }, { 64, 1 }, { 60, 1 },
                                                            { 67, 0.5 }, { 69, 0.5 }, { 67, 0.5 }, { 65, 0.5 }, { 64, 1 }, { 60, 1 },
                                                            { 60, 1 }, { 55, 1 }, { 60, 2 }, { 60, 1 }, { 55, 1 }, { 60, 2 } }),
                       0, Expect::Major });
        c.push_back ({ "Amazing Grace (C major, opening)", melody ({ { 55, 1 }, { 60, 2 }, { 64, 0.5 }, { 60, 0.5 }, { 64, 2 }, { 62, 1 },
                                                                     { 60, 2 }, { 57, 1 }, { 55, 2 }, { 55, 1 }, { 60, 2 }, { 64, 0.5 },
                                                                     { 60, 0.5 }, { 64, 2 }, { 62, 1 }, { 67, 5 },
                                                                     { 64, 1 }, { 67, 1.5 }, { 64, 0.5 }, { 67, 0.5 }, { 64, 0.5 }, { 60, 2 },
                                                                     { 55, 1 }, { 57, 1.5 }, { 60, 0.5 }, { 60, 0.5 }, { 57, 0.5 }, { 55, 2 },
                                                                     { 55, 1 }, { 60, 2 }, { 64, 0.5 }, { 60, 0.5 }, { 64, 2 }, { 62, 1 }, { 60, 5 } }),
                       0, Expect::Major });
        c.push_back ({ "Greensleeves (A minor, opening)", melody ({ { 57, 1 }, { 60, 2 }, { 62, 1 }, { 64, 1.5 }, { 65, 0.5 }, { 64, 1 },
                                                                    { 62, 2 }, { 59, 1 }, { 55, 1.5 }, { 57, 0.5 }, { 59, 1 },
                                                                    { 60, 2 }, { 57, 1 }, { 57, 1.5 }, { 56, 0.5 }, { 57, 1 },
                                                                    { 59, 2 }, { 56, 1 }, { 52, 2 }, { 57, 1 },
                                                                    { 60, 2 }, { 62, 1 }, { 64, 1.5 }, { 65, 0.5 }, { 64, 1 },
                                                                    { 62, 2 }, { 59, 1 }, { 55, 1.5 }, { 57, 0.5 }, { 59, 1 },
                                                                    { 60, 1.5 }, { 59, 0.5 }, { 57, 1 }, { 56, 1.5 }, { 54, 0.5 }, { 56, 1 }, { 57, 3 } }),
                       9, Expect::Minor });
        c.push_back ({ "Frere Jacques in minor (A minor, as Mahler's)", melody ({ { 57, 1 }, { 59, 1 }, { 60, 1 }, { 57, 1 }, { 57, 1 }, { 59, 1 }, { 60, 1 }, { 57, 1 },
                                                                                  { 60, 1 }, { 62, 1 }, { 64, 2 }, { 60, 1 }, { 62, 1 }, { 64, 2 },
                                                                                  { 64, 0.5 }, { 65, 0.5 }, { 64, 0.5 }, { 62, 0.5 }, { 60, 1 }, { 57, 1 },
                                                                                  { 64, 0.5 }, { 65, 0.5 }, { 64, 0.5 }, { 62, 0.5 }, { 60, 1 }, { 57, 1 },
                                                                                  { 57, 1 }, { 52, 1 }, { 57, 2 }, { 57, 1 }, { 52, 1 }, { 57, 2 } }),
                       9, Expect::Minor });
        c.push_back ({ "God Rest Ye Merry Gentlemen (A minor, opening)", melody ({ { 57, 1 }, { 57, 1 }, { 64, 1 }, { 64, 1 }, { 62, 1 }, { 60, 1 }, { 59, 1 }, { 57, 1 },
                                                                                   { 55, 1 }, { 57, 1 }, { 59, 1 }, { 60, 1 }, { 62, 1 }, { 64, 3 },
                                                                                   { 57, 1 }, { 57, 1 }, { 64, 1 }, { 64, 1 }, { 62, 1 }, { 60, 1 }, { 59, 1 }, { 57, 1 },
                                                                                   { 55, 1 }, { 57, 1 }, { 59, 1 }, { 60, 1 }, { 62, 1 }, { 64, 3 },
                                                                                   { 64, 1 }, { 65, 1 }, { 64, 1 }, { 62, 1 }, { 60, 1 }, { 59, 1 }, { 57, 1 }, { 55, 1 },
                                                                                   { 57, 1 }, { 59, 1 }, { 60, 1 }, { 59, 1 }, { 57, 1 }, { 56, 1 }, { 57, 4 } }),
                       9, Expect::Minor });
        c.push_back ({ "Scarborough Fair (D Dorian)", melody ({ { 62, 2 }, { 62, 1 }, { 69, 2 }, { 69, 1 }, { 64, 1.5 }, { 65, 0.5 }, { 64, 1 }, { 62, 3 },
                                                                { 69, 1 }, { 72, 2 }, { 74, 1 }, { 72, 2 }, { 69, 1 }, { 71, 1 }, { 67, 1 }, { 69, 4 },
                                                                { 74, 2 }, { 74, 1 }, { 74, 2 }, { 72, 1 }, { 69, 2 }, { 69, 1 }, { 67, 1 }, { 65, 2 },
                                                                { 64, 1 }, { 60, 2 }, { 62, 2 }, { 69, 1 }, { 67, 2 }, { 65, 1 }, { 64, 2 }, { 62, 1 },
                                                                { 60, 2 }, { 62, 4 } }),
                       2, Expect::Record });
        c.push_back ({ "Drunken Sailor (D Dorian)", melody ({ { 69, 0.5 }, { 69, 0.5 }, { 69, 0.5 }, { 69, 0.5 }, { 69, 0.5 }, { 69, 0.5 }, { 69, 0.5 }, { 62, 0.5 },
                                                              { 65, 0.5 }, { 69, 0.5 }, { 67, 0.5 }, { 67, 0.5 }, { 67, 0.5 }, { 67, 0.5 }, { 67, 0.5 }, { 67, 0.5 },
                                                              { 67, 0.5 }, { 60, 0.5 }, { 64, 0.5 }, { 67, 0.5 }, { 69, 0.5 }, { 69, 0.5 }, { 69, 0.5 }, { 69, 0.5 },
                                                              { 69, 0.5 }, { 69, 0.5 }, { 69, 0.5 }, { 71, 0.5 }, { 72, 0.5 }, { 74, 0.5 }, { 72, 0.5 }, { 69, 0.5 },
                                                              { 67, 0.5 }, { 64, 0.5 }, { 62, 1 }, { 62, 2 } }),
                       2, Expect::Record });

        // Progressions: block chords, a bar each, the tonic held two bars at the end.
        const std::vector<int> C { 48, 60, 64, 67 }, F { 53, 65, 69, 72 }, G { 55, 62, 67, 71 }, Am { 45, 60, 64, 69 },
                               G7 { 55, 65, 67, 71 }, Dm7 { 50, 60, 65, 69 }, Cmaj7 { 48, 64, 67, 71 }, C7 { 48, 64, 67, 70 },
                               F7 { 53, 63, 65, 69 };
        // In A minor: i, iv, V (raised 7th), bVII, bVI.
        const std::vector<int> aAm { 45, 60, 64, 69 }, aDm { 50, 62, 65, 69 }, aE { 52, 64, 68, 71 }, aG { 43, 62, 67, 71 }, aF { 41, 60, 65, 69 };
        c.push_back ({ "I-V-vi-IV (C)", chords ({ { C, 4 }, { G, 4 }, { Am, 4 }, { F, 4 }, { C, 4 }, { G, 4 }, { Am, 4 }, { F, 4 }, { C, 8 } }), 0, Expect::Major });
        c.push_back ({ "I-vi-IV-V (C)", chords ({ { C, 4 }, { Am, 4 }, { F, 4 }, { G, 4 }, { C, 4 }, { Am, 4 }, { F, 4 }, { G, 4 }, { C, 8 } }), 0, Expect::Major });
        c.push_back ({ "ii-V-I (C)", chords ({ { Dm7, 4 }, { G7, 4 }, { Cmaj7, 8 }, { Dm7, 4 }, { G7, 4 }, { Cmaj7, 8 } }), 0, Expect::Major });
        c.push_back ({ "I-IV-V-I (C)", chords ({ { C, 4 }, { F, 4 }, { G, 4 }, { C, 4 }, { F, 4 }, { G, 4 }, { C, 8 } }), 0, Expect::Major });
        c.push_back ({ "i-iv-V-i (A minor)", chords ({ { aAm, 4 }, { aDm, 4 }, { aE, 4 }, { aAm, 4 }, { aDm, 4 }, { aE, 4 }, { aAm, 8 } }), 9, Expect::Minor });
        c.push_back ({ "i-bVII-bVI-V (A minor)", chords ({ { aAm, 4 }, { aG, 4 }, { aF, 4 }, { aE, 4 }, { aAm, 4 }, { aG, 4 }, { aF, 4 }, { aE, 4 }, { aAm, 8 } }), 9, Expect::Minor });
        c.push_back ({ "12-bar blues in sevenths (C)", chords ({ { C7, 4 }, { C7, 4 }, { C7, 4 }, { C7, 4 }, { F7, 4 }, { F7, 4 }, { C7, 4 }, { C7, 4 },
                                                                 { G7, 4 }, { F7, 4 }, { C7, 4 }, { G7, 4 }, { C7, 8 } }), 0, Expect::Record });
        return c;
    }

    KeyGuess finalGuess (const Notes& n, KeyProfiles set = KeyListener::defaultProfiles)
    {
        KeyListener l (set);
        play (l, n, endOf (n) + 0.5);
        return l.guess();
    }
}

//==============================================================================
static void testProfiles()
{
    std::cout << "== profiles ==\n";
    bool own = true;
    for (const auto set : { KeyProfiles::KrumhanslKessler, KeyProfiles::Temperley })
    for (int m = 0; m < 2; ++m)
        for (int tonic = 0; tonic < 12; ++tonic)
        {
            PitchWeights w {};
            for (int i = 0; i < 12; ++i)
                w[(size_t) ((i + tonic) % 12)] = keyprofiles::profile (m == 1, set)[i];
            const auto e = estimateKey (w, set);
            own &= e.valid && e.best.tonic == tonic && e.best.minor == (m == 1) && std::abs (e.best.fit - 1.0) < 1.0e-9;
        }
    check (own, "each of the 24 profiles (Krumhansl-Kessler and Temperley), as a histogram, is its own key with fit 1");
    const auto kk = KeyProfiles::KrumhanslKessler;

    PitchWeights flat {};
    const bool none = ! estimateKey (flat, kk).valid;
    flat.fill (3.0);
    check (none && ! estimateKey (flat, kk).valid && keyFit (flat, 0, false, kk) == 0.0, "nothing, or all 12 pitch classes alike, is no estimate");

    PitchWeights nan {};
    nan[0] = std::nan ("");
    check (! estimateKey (nan, kk).valid, "a non-finite weight is no estimate");

    KeyGuess g { 9, true, GuessState::Confident, 82, 0, false, 76, 41 };
    const KeyGuess back = KeyGuess::unpack (g.pack());
    check (back == g && back.tonic == 9 && back.minor && back.state == GuessState::Confident && back.fit == 82
               && back.runnerUpTonic == 0 && ! back.runnerUpMinor && back.runnerUpFit == 76 && back.notes == 41
               && ! KeyGuess::unpack (KeyGuess().pack()).has(),
           "a guess packs into 64 bits and back");
}

static void testScaleMapping()
{
    std::cout << "== scale mapping ==\n";
    // ChordMonkey checks its 13 scales here; TonnetzML has Major and Natural Minor (Scales.h).
    check (scaleFamily (Scale::Major) == ScaleFamily::Major && scaleFamily (Scale::NaturalMinor) == ScaleFamily::Minor
               && scaleForGuess (Scale::Major, false) == Scale::Major && scaleForGuess (Scale::Major, true) == Scale::NaturalMinor
               && scaleForGuess (Scale::NaturalMinor, true) == Scale::NaturalMinor && scaleForGuess (Scale::NaturalMinor, false) == Scale::Major,
           "Major is the major family and Natural Minor the minor; a guess sets one or the other");
    check (std::string (guessKeyName (1, false)) == "Db" && std::string (guessKeyName (1, true)) == "C#" && std::string (guessKeyName (8, true)) == "G#"
               && std::string (guessKeyName (6, false)) == "F#",
           "guesses are spelled by key signature (Db major, C# minor, G# minor, F# major)");
}

static void testCorpus()
{
    std::cout << "== corpus ==\n";
    for (const auto& c : corpus())
    {
        bool invariant = true;
        KeyGuess first;
        for (int t = 0; t < 12; ++t)
        {
            const KeyGuess g = finalGuess (transpose (c.notes, t));
            if (t == 0)
                first = g;
            invariant &= g.has() == first.has() && g.fit == first.fit && g.state == first.state && g.minor == first.minor
                      && (! g.has() || g.tonic == (first.tonic + t) % 12);
        }

        // When the guess first shows, and when it first shows the right key confidently.
        KeyListener l;
        const auto changes = play (l, c.notes, endOf (c.notes) + 0.5);
        const bool wantMinor = c.expect == Expect::Minor;
        const bool right = first.has() && first.tonic == c.tonic && first.minor == wantMinor;
        if (c.expect == Expect::Record)
        {
            check (invariant, std::string (c.name) + ": " + describe (first) + " (recorded, not required)");
            continue;
        }
        // When the right key first shows.
        double settled = -1.0;
        for (const auto& ch : changes)
            if (settled < 0.0 && ch.guess.tonic == c.tonic && ch.guess.minor == wantMinor)
                settled = ch.time;
        const double eightBars = 8 * 4 * 60.0 / 100.0;
        check (right && first.state == GuessState::Confident && invariant && settled >= 0.0 && settled <= eightBars,
               std::string (c.name) + ": " + describe (first) + ", shown from " + std::to_string (settled).substr (0, 5) + " s, "
                   + std::to_string (changes.size()) + " change(s); the same in all 12 keys");
    }
}

// ChordMonkey's own chords (source C, as a corpus): a progression's first four white keys
// twice, a bar each, then the tonic's key for two bars, at 100 bpm.
struct OwnResult { int cases = 0, right = 0, knownWrong = 0; std::set<std::string> wrong; };

// The notes ChordMonkey's engine played for each case (Tools/gen_key_fixture.cpp), fed to the
// listener as ChordMonkey's test fed them: in 480-sample blocks at 48 kHz, each block's events in
// order, then the listener advanced to the block's end.
struct OwnCase
{
    int key = 0;
    bool minor = false;
    std::string group, progression;
    struct Ev { long long sample; bool on; int note, velocity; };
    std::vector<Ev> events;
    long long end = 0;
};

static const std::vector<OwnCase>& ownCases()
{
    static std::vector<OwnCase> cases;
    if (! cases.empty())
        return cases;
    const char* dir = std::getenv ("TONNETZML_FIXTURES");
    std::ifstream f (std::string (dir != nullptr ? dir : "Tests/fixtures") + "/chordmonkey_own_chords.txt");
    std::string line;
    while (std::getline (f, line))
    {
        if (line.empty() || line[0] == '#')
            continue;
        std::istringstream in (line);
        std::string word;
        in >> word;
        if (word == "case")
        {
            OwnCase c;
            int minor = 0;
            in >> c.key >> minor >> c.group;
            std::getline (in, c.progression);
            c.progression.erase (0, c.progression.find_first_not_of (' '));
            c.minor = minor != 0;
            cases.push_back (c);
        }
        else if (word == "end" && ! cases.empty())
            in >> cases.back().end;
        else if (! cases.empty())
        {
            OwnCase::Ev e;
            in >> e.sample >> e.note >> e.velocity;
            e.on = word == "1";
            cases.back().events.push_back (e);
        }
    }
    return cases;
}

static OwnResult ownOutput (KeyProfiles set)
{
    OwnResult r;
    for (const auto& c : ownCases())
    {
        KeyListener l (set);
        const int blockSize = 480;
        size_t i = 0;
        for (long long pos = 0; pos < c.end; pos += blockSize)
        {
            for (; i < c.events.size() && c.events[i].sample < pos + blockSize; ++i)
            {
                const double at = (double) c.events[i].sample / 48000.0;
                if (c.events[i].on) l.noteOn (c.events[i].note, c.events[i].velocity, at);
                else l.noteOff (c.events[i].note, at);
            }
            l.advance ((double) (pos + blockSize) / 48000.0);
        }
        l.advance ((double) c.end / 48000.0 + 0.5);
        const auto g = l.guess();
        ++r.cases;
        const bool ok = g.has() && g.tonic == c.key && g.minor == c.minor;
        r.right += ok ? 1 : 0;
        // Canon's first four in natural minor with sevenths, i7 v7 VImaj7 IIImaj7, is the
        // relative major's vi iii IV I: heard as that, in every key.
        const bool known = c.minor && c.group == "Sevenths" && c.progression.rfind ("Canon", 0) == 0;
        r.knownWrong += ! ok && known ? 1 : 0;
        if (! ok && ! known)
            r.wrong.insert (keyText (c.key, c.minor) + " " + c.group + " " + c.progression + ": " + describe (g));
    }
    return r;
}

static void testOwnOutput()
{
    std::cout << "== ChordMonkey's own chords ==\n";
    const auto r = ownOutput (KeyListener::defaultProfiles);
    std::string kinds;
    for (const auto& w : r.wrong)
        kinds += (kinds.empty() ? "" : "; ") + w;
    check (r.cases == 192 && r.wrong.empty() && r.right + r.knownWrong == r.cases,
           std::to_string (r.right) + " of " + std::to_string (r.cases) + " Key x Scale x Chord Group x Progression played by ChordMonkey give the Key; "
               + std::to_string (r.knownWrong) + " are natural minor sevenths under Canon (i v VI III), heard as the relative major"
               + (kinds.empty() ? std::string() : " (also wrong: " + kinds + ")"));
}

// The plan's bake-off: Krumhansl-Kessler against Temperley on the corpus and ChordMonkey's own
// chords. The listener uses the one that gets more right.
static void testBakeOff()
{
    std::cout << "== profiles bake-off ==\n";
    int score[2] = {};
    std::string detail[2];
    for (int i = 0; i < 2; ++i)
    {
        const auto set = i == 0 ? KeyProfiles::KrumhanslKessler : KeyProfiles::Temperley;
        int corpusRight = 0, corpusCases = 0;
        for (const auto& c : corpus())
        {
            if (c.expect == Expect::Record)
                continue;
            const auto g = finalGuess (c.notes, set);
            ++corpusCases;
            corpusRight += g.has() && g.tonic == c.tonic && g.minor == (c.expect == Expect::Minor) ? 1 : 0;
        }
        const auto own = ownOutput (set);
        score[i] = corpusRight + own.right;
        detail[i] = std::to_string (corpusRight) + "/" + std::to_string (corpusCases) + " corpus, " + std::to_string (own.right) + "/192 own chords";
    }
    check (KeyListener::defaultProfiles == KeyProfiles::Temperley && score[1] > score[0],
           "Temperley (" + detail[1] + ") beats Krumhansl-Kessler (" + detail[0] + "), so the listener uses Temperley's profiles");
}

static void testModulation()
{
    std::cout << "== modulation ==\n";
    const auto cases = corpus();
    const auto& majorTune = cases[2].notes;   // Frere Jacques
    const auto& minorTune = cases[5].notes;   // Frere Jacques in A minor
    const auto part = [] (const Notes& tune, double length)
    {
        Notes n;
        const double e = endOf (tune) + 0.6;
        for (double o = 0.0; o < length; o += e)
            for (auto x : tune)
                if (x.start + o < length)
                {
                    x.start += o;
                    x.length = std::min (x.length, length - x.start);
                    n.push_back (x);
                }
        return n;
    };

    // 24 s in one key, then 24 s in the other. While the evidence is a mix of both, one key
    // between them may show.
    constexpr double partLength = 24.0, bound = 20.0;   // close keys cross over after about a half-life
    struct Mod { const char* name; Notes a, b; int fromTonic, toTonic; bool minor; };
    const Mod mods[] = {
        { "C major -> G major", part (majorTune, partLength), transpose (part (majorTune, partLength), 7), 0, 7, false },
        { "A minor -> C minor", part (minorTune, partLength), transpose (part (minorTune, partLength), 3), 9, 0, true },
    };
    for (const auto& m : mods)
    {
        KeyListener l;
        const auto changes = play (l, append (m.a, m.b, partLength), 2.0 * partLength + 0.5);
        const bool first = ! changes.empty() && changes.front().guess.tonic == m.fromTonic && changes.front().guess.minor == m.minor
                        && (changes.size() < 2 || changes[1].time > partLength);
        const auto& last = changes.back().guess;
        const bool arrived = last.tonic == m.toTonic && last.minor == m.minor && changes.size() >= 2 && changes.size() <= 3;
        const double after = changes.size() >= 2 ? changes.back().time - partLength : -1.0;
        std::string keys;
        for (const auto& c : changes)
            keys += (keys.empty() ? "" : ", ") + keyText (c.guess.tonic, c.guess.minor);
        check (first && arrived && after > 0.0 && after <= bound,
               std::string (m.name) + ": " + keys + "; the new key " + std::to_string (after).substr (0, 5)
                   + " s after the modulation (at most " + std::to_string ((int) bound) + " s), then " + describe (l.guess()));
    }
}

static void testSilenceAndAmbiguity()
{
    std::cout << "== silence and ambiguous input ==\n";
    {
        KeyListener l;
        l.advance (0.0);
        l.advance (30.0);
        check (! l.guess().has(), "nothing heard: no guess");

        KeyListener fresh;
        auto& m = fresh;
        const auto tune = corpus()[1].notes;
        play (m, tune, endOf (tune) + KeyListener::tailSeconds + 0.5);
        const auto before = m.guess();
        const double listened = m.listeningTime();
        m.advance (endOf (tune) + 60.0);
        check (before.has() && m.guess() == before && m.listeningTime() == listened,
               "a tune, then 60 s of silence: the guess is unchanged (listening time stops 2 s after the last note)");
    }

    // noneOnly: no guess; else no guess or a tentative one, or one of `either`.
    struct Amb { const char* name; Notes notes; bool noneOnly; std::vector<std::pair<int, bool>> either = {}; };
    Notes repeated, fifth, triad, chromatic, wholeTone, diatonic;
    for (int i = 0; i < 40; ++i)
    {
        repeated.push_back ({ i * 0.5, 0.4, 60, 100 });
        fifth.push_back ({ i * 0.5, 0.4, i % 2 == 0 ? 60 : 67, 100 });
        chromatic.push_back ({ i * 0.25, 0.2, 60 + i % 12, 100 });
        wholeTone.push_back ({ i * 0.25, 0.2, 60 + 2 * (i % 6), 100 });
    }
    for (const int p : { 60, 64, 67 })
        triad.push_back ({ 0.0, 8.0, p, 100 });
    for (int bar = 0; bar < 14; ++bar)
        for (const int step : { 0, 2, 4 })
        {
            const int degree = bar % 7;
            const int idx = degree + step;
            diatonic.push_back ({ bar * 1.2, 1.1, 60 + scaleOffset (Scale::Major, idx), 100 });
        }
    const Amb ambs[] = {
        { "one note repeated", repeated, true },
        { "a fifth (C G)", fifth, true },
        { "a C major triad held", triad, false },
        { "all 12 pitch classes evenly", chromatic, true },
        { "a whole-tone scale", wholeTone, false },
        { "the 7 diatonic triads of C, evenly", diatonic, false, { { 0, false }, { 9, true } } },
        { "Am F C G (i-bVI-bIII-bVII, or vi-IV-I-V in C)",
          chords ({ { { 45, 60, 64, 69 }, 4 }, { { 41, 60, 65, 69 }, 4 }, { { 48, 60, 64, 67 }, 4 }, { { 43, 62, 67, 71 }, 4 },
                    { { 45, 60, 64, 69 }, 4 }, { { 41, 60, 65, 69 }, 4 }, { { 48, 60, 64, 67 }, 4 }, { { 43, 62, 67, 71 }, 4 }, { { 45, 60, 64, 69 }, 8 } }),
          false, { { 0, false }, { 9, true } } },
    };
    for (const auto& a : ambs)
    {
        const auto g = finalGuess (a.notes);
        bool either = false;
        for (const auto& [tonic, minor] : a.either)
            either |= g.has() && g.tonic == tonic && g.minor == minor;
        const bool ok = a.noneOnly ? ! g.has() : (! g.has() || g.state == GuessState::Tentative || either);
        check (ok, std::string (a.name) + ": " + describe (g)
                       + (a.noneOnly ? " (no guess expected)" : a.either.empty() ? " (no guess or tentative expected)" : " (C major or A minor, or tentative)"));
    }
}

static void testWeighting()
{
    std::cout << "== weighting ==\n";
    const auto tune = corpus()[0].notes;
    Notes ghost = tune;
    for (size_t i = 1; i < ghost.size(); i += 2)
        ghost[i].velocity = 20;
    const auto a = finalGuess (tune), b = finalGuess (ghost);
    check (a.has() && b.has() && a.tonic == b.tonic && a.minor == b.minor, "every other note at velocity 20: the same guess (" + describe (b) + ")");

    Notes held = tune;
    held.push_back ({ 0.0, endOf (tune), 67, 100 });
    const auto c = finalGuess (held);
    check (c.has() && c.tonic == a.tonic && c.minor == a.minor, "with G held throughout: the same guess (" + describe (c) + ")");

    // One note held 30 s counts as its onset and 2 s of holding, decayed.
    KeyListener l;
    l.noteOn (60, 127, 0.0);
    l.advance (30.0);
    const double k = std::pow (2.0, -1.0 / KeyListener::halfLife);
    double expect = KeyListener::onsetSeconds * std::pow (k, 30.0);
    expect += (std::pow (k, 28.0) - std::pow (k, 30.0)) / -std::log (k);   // the integral of k^(30-s) over 0..2
    check (std::abs (l.getWeights()[0] - expect) < 0.01 * expect, "a note held 30 s counts 2 s of holding ("
                                                                 + std::to_string (l.getWeights()[0]) + " vs " + std::to_string (expect) + ")");
}

static void testHysteresis()
{
    std::cout << "== hysteresis ==\n";
    // Random notes of the C major scale, evenly, 4 a second for 60 s: C major and A minor (and
    // their neighbours) stay close all along.
    std::mt19937 rng (7);
    Notes n;
    for (int i = 0; i < 240; ++i)
        n.push_back ({ i * 0.25, 0.22, 60 + scaleOffset (Scale::Major, (int) (rng() % 7)), 100 });
    KeyListener l;
    const auto changes = play (l, n, 60.5);
    std::string keys;
    for (const auto& c : changes)
        keys += (keys.empty() ? "" : ", ") + keyText (c.guess.tonic, c.guess.minor);
    check (changes.size() <= 2, "60 s of random C major scale notes: " + std::to_string (changes.size()) + " key(s) shown (" + keys + ")");
}

static void testFuzz()
{
    std::cout << "== fuzz ==\n";
    std::mt19937 rng (11);
    KeyListener l;
    double t = 0.0;
    bool sane = true;
    for (int i = 0; i < 200000; ++i)
    {
        t += (rng() % 1000) / 20000.0;
        const int note = (int) (rng() % 140) - 6;
        switch (rng() % 10)
        {
            case 0: case 1: case 2: case 3: l.noteOn (note, (int) (rng() % 140) - 6, t); break;
            case 4: case 5: case 6: l.noteOff (note, t); break;
            case 7: l.allUp (t); break;
            case 8: l.advance (t - 1.0); break;   // backwards: ignored
            default: if (rng() % 500 == 0) l.reset(); else l.advance (t); break;
        }
        for (const double w : l.getWeights())
            sane &= std::isfinite (w) && w >= 0.0;
        const auto g = l.guess();
        sane &= g.fit >= 0 && g.fit <= 100 && g.tonic >= -1 && g.tonic <= 11 && (int) g.state <= 2;
    }
    check (sane, "200,000 random notes, velocities, key-ups, resets and times (10 minutes and more): weights finite and >= 0, guesses in range");
}

//==============================================================================
// Part 2: TonnetzML's engine.
namespace
{
    struct KeyRun
    {
        std::unique_ptr<LatticeEngine> engine = std::make_unique<LatticeEngine>();
        EngineSettings s;
        Transport t { 48000.0, true, 0.0, 120.0, 4, 4 };
        BlockEvents out;
        testutil::Receiver rx;
        int64_t pos = 0;
        int block = 480;

        KeyRun() { s.gatherMs = 0; }

        // Plays `notes` (seconds) plus `extra` events (samples from now) for `until` seconds.
        void play (const Notes& notes, double until, std::vector<std::pair<int64_t, MidiEvent>> extra = {})
        {
            struct Ev { int64_t sample; MidiEvent e; };
            std::vector<Ev> ev;
            const double rate = t.sampleRate;
            const int64_t origin = pos;
            for (const auto& n : notes)
            {
                ev.push_back ({ origin + (int64_t) std::llround (n.start * rate), MidiEvent::noteOn (0, n.pitch, n.velocity, 0) });
                ev.push_back ({ origin + (int64_t) std::llround ((n.start + n.length) * rate), MidiEvent::noteOff (0, n.pitch, 0) });
            }
            for (const auto& [at, e] : extra)
                ev.push_back ({ origin + at, e });
            std::stable_sort (ev.begin(), ev.end(), [] (const Ev& a, const Ev& b)
                              { return a.sample < b.sample || (a.sample == b.sample && a.e.isNoteOff() && ! b.e.isNoteOff()); });
            size_t i = 0;
            const auto end = origin + (int64_t) std::llround (until * rate);
            std::vector<MidiEvent> in;
            for (; pos < end; pos += block)
            {
                in.clear();
                for (; i < ev.size() && ev[i].sample < pos + block; ++i)
                {
                    MidiEvent e = ev[i].e;
                    e.sample = (int) (ev[i].sample - pos);
                    in.push_back (e);
                }
                t.ppq = (double) pos / t.samplesPerBeat();
                out.clear();
                engine->process (t, in.data(), (int) in.size(), block, s, out);
                rx.feed (out, block);
            }
        }
    };

    // What the listener alone hears of `notes`, fed as the engine feeds it (sample times, block ends).
    KeyGuess direct (const Notes& notes, double until, int block = 480, double rate = 48000.0)
    {
        struct Ev { int64_t sample; bool on; int pitch, velocity; };
        std::vector<Ev> ev;
        for (const auto& n : notes)
        {
            ev.push_back ({ (int64_t) std::llround (n.start * rate), true, n.pitch, n.velocity });
            ev.push_back ({ (int64_t) std::llround ((n.start + n.length) * rate), false, n.pitch, 0 });
        }
        std::stable_sort (ev.begin(), ev.end(), [] (const Ev& a, const Ev& b) { return a.sample < b.sample || (a.sample == b.sample && ! a.on && b.on); });
        KeyListener l;
        size_t i = 0;
        const auto end = (int64_t) std::llround (until * rate);
        for (int64_t pos = 0; pos < end; pos += block)
        {
            for (; i < ev.size() && ev[i].sample < pos + block; ++i)
                ev[i].on ? l.noteOn (ev[i].pitch, ev[i].velocity, (double) ev[i].sample / rate) : l.noteOff (ev[i].pitch, (double) ev[i].sample / rate);
            l.advance ((double) (pos + block) / rate);
        }
        return l.guess();
    }

    Word wordOf (const char* text)
    {
        Word w;
        parseWord (text, w);
        return w;
    }
}

static void testEngineHearsSourceOnly()
{
    std::cout << "== engine: only the source notes are heard ==\n";
    const auto cases = corpus();
    const Notes dMajor = transpose (cases[0].notes, 2);          // Twinkle in D major
    const Notes fMinor = transpose (cases[4].notes, -4);         // Greensleeves in F minor
    struct Tune { const char* name; Notes notes; int tonic; bool minor; };
    for (const auto& tune : { Tune { "D major (Twinkle)", dMajor, 2, false }, Tune { "F minor (Greensleeves)", fMinor, 5, true } })
    {
        const double until = endOf (tune.notes) + 0.5;
        const KeyGuess want = direct (tune.notes, until);
        int same = 0, runs = 0, sounded = 0;
        std::string firstDiff;
        const char* words[] = { "", "P", "PL", "N S H", "+5 -M3", "LR" };
        for (int mode = 0; mode < 4; ++mode)
            for (const Mix mix : { Mix::Generated, Mix::SourceAndGenerated })
                for (const char* w : words)
                {
                    KeyRun r;
                    r.s.mode = (Mode) mode;
                    r.s.mix = mix;
                    r.s.word = wordOf (w);
                    r.s.orbitCycle = OrbitCycle::Slot;
                    r.s.orbitRate = 0;
                    r.s.latch = mode == 3;
                    r.s.zone.on = true;
                    // Walk and Explore: the word's P L R N S H walked in with operator keys first.
                    std::vector<std::pair<int64_t, MidiEvent>> ops;
                    if (mode == 1 || mode == 3)
                        for (const char* p = w; *p != 0; ++p)
                        {
                            const int idx = *p == 'P' ? 0 : *p == 'L' ? 1 : *p == 'R' ? 2 : *p == 'N' ? 3 : *p == 'S' ? 4 : *p == 'H' ? 5 : -1;
                            if (idx >= 0)
                                ops.push_back ({ 100 + (int64_t) ops.size(), MidiEvent::noteOn (0, 24 + idx, 100, 0) });
                        }
                    r.play (tune.notes, until, ops);
                    const auto g = r.engine->keyGuess();
                    ++runs;
                    sounded += r.rx.noteOns > 0 ? 1 : 0;
                    if (g == want)
                        ++same;
                    else if (firstDiff.empty())
                        firstDiff = std::string (" first difference: ") + modeNames[mode] + " " + w + ": " + describe (g);
                }
        check (want.has() && want.tonic == tune.tonic && want.minor == tune.minor && want.state == GuessState::Confident && same == runs && sounded == runs,
               std::string (tune.name) + ": the listener alone says " + describe (want) + "; the engine says the same in " + std::to_string (same) + "/"
                   + std::to_string (runs) + " runs (4 modes x Generated / Source + Generated x 6 words), generated chords sounding" + firstDiff);
    }
}

static void testOperatorKeysUnheard()
{
    std::cout << "== engine: operator keys are not heard ==\n";
    const auto tune = transpose (corpus()[0].notes, 2);
    const double until = endOf (tune) + 0.5;
    const KeyGuess want = direct (tune, until);
    // Operator keys (C0-B0) all through the tune, in Walk.
    std::vector<std::pair<int64_t, MidiEvent>> ops;
    std::mt19937 rng (5);
    for (int i = 0; i < 200; ++i)
    {
        const auto at = (int64_t) (rng() % (uint64_t) (until * 48000.0));
        const int key = 24 + (int) (rng() % 12);
        ops.push_back ({ at, MidiEvent::noteOn (0, key, 100, 0) });
        ops.push_back ({ at + 2000, MidiEvent::noteOff (0, key, 0) });
    }
    KeyRun r;
    r.s.mode = Mode::Walk;
    r.s.zone.on = true;
    r.play (tune, until, ops);
    const auto g = r.engine->keyGuess();
    check (g == want && g.notes == (int) tune.size(), "200 operator-key presses through a D major tune in Walk: the same guess (" + describe (g)
                                                            + "), " + std::to_string (g.notes) + " notes heard (the tune's " + std::to_string (tune.size()) + ")");
}

static void testHoldAndListen()
{
    std::cout << "== engine: Off, Listen, Hold ==\n";
    const auto cases = corpus();
    const Notes dMajor = transpose (cases[0].notes, 2), fMinor = transpose (cases[4].notes, -4);
    KeyRun r;
    r.play (dMajor, endOf (dMajor) + 0.5);
    const auto heard = r.engine->keyGuess();
    const auto keyHeard = r.engine->currentKey();

    r.s.key = KeyMode::Hold;
    r.s.keyTonic = heard.tonic;          // as the plug-in copies the guess into keyTonic / keyMinor
    r.s.keyMinor = heard.minor;
    r.play (fMinor, endOf (fMinor) + 0.5);
    const auto held = r.engine->keyGuess();
    const auto keyHeld = r.engine->currentKey();
    check (heard.has() && heard.tonic == 2 && ! heard.minor && keyHeard.tonic == 2 && held == heard && keyHeld.tonic == 2 && ! keyHeld.minor,
           "Hold freezes the guess: D major heard, then an F minor tune under Hold: still " + describe (held));

    r.s.keyTonic = 9;
    r.s.keyMinor = true;
    r.play ({}, 0.1);
    check (r.engine->currentKey().tonic == 9 && r.engine->currentKey().minor, "under Hold the key is keyTonic / keyMinor (set by hand: A minor)");

    r.s.key = KeyMode::Listen;
    r.play ({}, 0.05);
    const auto fresh = r.engine->keyGuess();
    check (! fresh.has() && fresh.notes == 0 && ! r.engine->currentKey().known(), "Listen after Hold starts empty: no guess, no notes");
    r.play (fMinor, endOf (fMinor) + 0.5);
    const auto again = r.engine->keyGuess();
    check (again.has() && again.tonic == 5 && again.minor, "…and the F minor tune then gives F minor (" + describe (again) + ")");

    r.s.key = KeyMode::Off;
    r.play (dMajor, endOf (dMajor) + 0.5);
    check (! r.engine->keyGuess().has() && ! r.engine->currentKey().known(), "Off: nothing is heard, no key");
    r.s.key = KeyMode::Listen;
    r.play ({}, 0.05);
    check (! r.engine->keyGuess().has() && r.engine->keyGuess().notes == 0, "Listen after Off starts empty");
}

static void testInKey()
{
    std::cout << "== In key: diatonic neighbours ==\n";
    // The keys' notes counted independently: a major key is seven fifths from its subdominant; a
    // minor key has its relative major's.
    const auto diatonic = [] (int tonic, bool minor)
    {
        const int major = minor ? (tonic + 3) % 12 : tonic;
        unsigned m = 0;
        for (int k = -1; k <= 5; ++k)
            m |= 1u << (((major + 7 * k) % 12 + 12) % 12);
        return m;
    };
    // Chords: triads, sevenths, sus, aug, dim, dyads, at every root and both parities.
    const unsigned shapes[] = { 0x091, 0x089, 0x049, 0x111, 0x891, 0x489, 0x491, 0x249, 0x0a1, 0x085, 0x081, 0x011, 0x881, 0x291 };
    int checked = 0, wrong = 0, marked = 0, bands = 0;
    for (int key = 0; key < 24; ++key)
    {
        const int tonic = key % 12;
        const bool minor = key >= 12;
        const unsigned d = diatonic (tonic, minor);
        const KeyRef ref { tonic, minor };
        bands += keyMask (tonic, minor) == d ? 1 : 0;
        for (const unsigned shape : shapes)
            for (int root = 0; root < 12; ++root)
                for (int parity = 0; parity < 2; ++parity)
                {
                    unsigned mask = 0;
                    for (int pc = 0; pc < 12; ++pc)
                        if ((shape >> pc) & 1u)
                            mask |= 1u << ((pc + root) % 12);
                    Chord c = makeChord (mask, chordRoot (mask, root));
                    c.parity = (int8_t) parity;
                    const uint16_t bits = opsInKey (c, ref);
                    for (int i = 0; i < numOperatorOps; ++i)
                    {
                        Op op;
                        operatorOp (i, op);
                        const Step s = apply (op, c);
                        const bool want = s.available && (s.chord.mask & ~d & 0xfffu) == 0;
                        wrong += (((bits >> i) & 1u) != 0) == want ? 0 : 1;
                        marked += want ? 1 : 0;
                        ++checked;
                    }
                }
    }
    check (bands == 24, "the 24 keys' notes (KeyBand.h) are their seven fifths");
    check (wrong == 0, "opsInKey marks exactly the diatonic results: " + std::to_string (checked) + " op x chord x key cases (24 keys, 14 shapes x 12 roots x 2 parities), "
                           + std::to_string (marked) + " marked, " + std::to_string (wrong) + " wrong");
    const Chord cMajor = makeChord (0x091, 0);
    const uint16_t c = opsInKey (cMajor, KeyRef { 0, false });
    check (c == ((1u << 1) | (1u << 2) | (1u << 6) | (1u << 7)), "C major in C major: L (E minor), R (A minor), +5 (G), -5 (F) and nothing else");
    check (opsInKey (cMajor, KeyRef {}) == 0, "no key: nothing marked");
}

int main()
{
    testProfiles();
    testScaleMapping();
    testCorpus();
    testOwnOutput();
    testBakeOff();
    testModulation();
    testSilenceAndAmbiguity();
    testWeighting();
    testHysteresis();
    testFuzz();
    testEngineHearsSourceOnly();
    testOperatorKeysUnheard();
    testHoldAndListen();
    testInKey();
    return testutil::finish ("test_keys");
}
