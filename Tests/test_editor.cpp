// The engine side of the lattice editor (PLAN.md §4, §5, §3.6; Phase 4):
//
//   fifo       EditorCommands.h's SPSC FIFO keeps order, refuses when full, and a producer and a
//              consumer thread pass 200 000 commands through it with nothing lost or reordered
//   operator   a ghost click (`editorOperator`) does what the operator key does, on the next
//              block's first sample: Walk and Explore append (P P cancels), Orbit takes Home and
//              Undo, Mirror ignores it; it works with the operator zone off
//   setWalk    the word bar's W replaces Walk's W and retunes the held chord; ignored in Mirror
//   chords     `sourceChord` and `generatedChord` are the chord and W(chord), with W's anchor,
//              under every Mix (Source only too); one op from `generatedChord` is what the
//              operator key gives (the ghost neighbours are exact)
//   generated  `forEachGenerated` lists the sounding generated notes and their velocities
//   releases   the counter moves on stop and Enable off
//   numerals   romanNumeral: every diatonic triad of all 24 keys, the plan's ♭VI, ♯ and °/+
#include "TestUtil.h"
#include "core/engine/EditorCommands.h"
#include "core/engine/LatticeEngine.h"
#include "core/theory/RomanNumeral.h"
#include <set>
#include <thread>
#include <vector>

using namespace tonnetz;
using testutil::check;

namespace
{
    Word word (const char* text)
    {
        Word w;
        parseWord (text, w);
        return w;
    }

    std::string text (const Word& w) { return printWord (w).c_str(); }

    // One engine, stopped transport, blocks of 64 at 48 kHz, gather 0.
    struct Rig
    {
        std::unique_ptr<LatticeEngine> engine = std::make_unique<LatticeEngine>();
        EngineSettings s;
        Transport t;
        BlockEvents out;

        Rig() { t.sampleRate = 48000.0; s.gatherMs = 0; }

        void block (std::initializer_list<MidiEvent> in = {})
        {
            std::vector<MidiEvent> v (in);
            out.clear();
            engine->process (t, v.data(), (int) v.size(), 64, s, out);
        }

        void chord (std::initializer_list<int> notes, int velocity = 100)
        {
            std::vector<MidiEvent> v;
            for (int n : notes)
                v.push_back (MidiEvent::noteOn (0, n, velocity, 0));
            out.clear();
            engine->process (t, v.data(), (int) v.size(), 64, s, out);
        }

        std::set<int> generated() const
        {
            std::set<int> g;
            engine->forEachGenerated ([&g] (int p, int) { g.insert (p); });
            return g;
        }
    };

    std::set<int> pcs (unsigned mask)
    {
        std::set<int> s;
        for (int i = 0; i < 12; ++i)
            if (hasPc (mask, i))
                s.insert (i);
        return s;
    }

    void testFifo()
    {
        std::cout << "fifo\n";
        SpscFifo<int, 8> f;
        bool ok = true;
        for (int i = 0; i < 8; ++i)
            ok = ok && f.push (i);
        check (ok && ! f.push (99) && f.size() == 8, "takes 8, refuses a 9th");
        bool order = true;
        for (int i = 0; i < 8; ++i)
        {
            order = order && f.peek() != nullptr && *f.peek() == i;
            f.pop();
        }
        check (order && f.peek() == nullptr, "gives them back in order, then nothing");

        auto big = std::make_unique<SpscFifo<int, 64>>();
        constexpr int total = 200000;
        std::thread producer ([&]
        {
            for (int i = 0; i < total; )
                if (big->push (i))
                    ++i;
                else
                    std::this_thread::yield();
        });
        int expected = 0;
        bool inOrder = true;
        while (expected < total)
        {
            if (const int* v = big->peek())
            {
                inOrder = inOrder && *v == expected;
                big->pop();
                ++expected;
            }
            else
                std::this_thread::yield();
        }
        producer.join();
        check (inOrder && big->size() == 0, "200 000 commands between two threads, none lost or reordered");

        const auto c = EditorCommand::noteOn (200, 0);
        check (c.note == (200 & 127) && c.velocity == 1 && EditorCommand::setWalk (word ("P L")).word == word ("P L"),
               "commands: note and velocity kept in range, a word carried whole");
    }

    void testOperator()
    {
        std::cout << "operator\n";
        Rig r;
        r.s.mode = Mode::Walk;
        r.s.zone.on = false;
        r.chord ({ 60, 64, 67 });
        r.engine->editorOperator (1);                 // L
        r.block();
        int ops = 0;
        check (text (r.engine->currentWord (ops)) == "L" && r.generated() == std::set<int> { 59, 64, 67 },
               "Walk: a ghost click on L appends L (zone off) and E minor sounds (B E G)");
        r.engine->editorOperator (1);
        r.engine->editorOperator (6);                 // L again cancels, then +5
        r.block();
        check (text (r.engine->currentWord (ops)) == "+5", "two clicks in one block: L cancels L, then +5 (" + text (r.engine->currentWord (ops)) + ")");
        r.engine->editorOperator (opkey::home);
        r.block();
        check (r.engine->currentWord (ops).empty() && r.generated() == std::set<int> { 60, 64, 67 }, "Home clears W");

        r.s.mode = Mode::Explore;
        r.block();
        r.engine->editorOperator (0);
        r.block();
        check (text (r.engine->currentWord (ops)) == "P", "Explore: a click appends P");

        r.s.mode = Mode::Mirror;
        r.s.word = word ("R");
        r.block();
        r.engine->editorOperator (2);
        r.block();
        check (text (r.engine->walkState().word) == "P" && text (r.engine->currentWord (ops)) == "R", "Mirror ignores it (W is the slot's)");

        r.s.mode = Mode::Orbit;
        r.block();
        auto w = r.engine->walkState();
        w.orbitStep = 3;
        r.engine->restore (w, r.out);
        r.chord ({ 60, 64, 67 });
        r.engine->editorOperator (opkey::undo);
        r.block();
        check (r.engine->orbitStep() == 2, "Orbit: Undo steps the cycle back (3 to 2)");
        r.engine->editorOperator (opkey::home);
        r.block();
        check (r.engine->orbitStep() == 0, "Orbit: Home restarts it");

        Rig off;
        off.s.mode = Mode::Walk;
        off.s.enabled = false;
        off.block();
        off.engine->editorOperator (0);
        off.block();
        off.s.enabled = true;
        off.block();
        check (off.engine->walkState().word.empty(), "Enable off: a click is dropped, not kept for later");
    }

    void testSetWalk()
    {
        std::cout << "setWalk\n";
        Rig r;
        r.s.mode = Mode::Walk;
        r.chord ({ 60, 64, 67 });
        r.engine->editorSetWalk (word ("R L"));
        r.block();
        int ops = 0;
        const auto want = pcs (applyWord (word ("R L"), makeChord (majorTriadMask (0), 0)).chord.mask);
        std::set<int> got;
        for (int p : r.generated())
            got.insert (p % 12);
        check (text (r.engine->currentWord (ops)) == "R L" && got == want, "Walk: W set from the word bar (R L) and the chord retuned");
        r.s.mode = Mode::Mirror;
        r.block();
        r.engine->editorSetWalk (word ("P"));
        r.block();
        check (text (r.engine->walkState().word) == "R L", "Mirror: ignored");
    }

    void testChords()
    {
        std::cout << "chords\n";
        int exact = 0, total = 0;
        for (int mixI = 0; mixI < 3; ++mixI)
            for (int root = 0; root < 12; ++root)
                for (int minor = 0; minor < 2; ++minor)
                {
                    Rig r;
                    r.s.mode = Mode::Walk;
                    r.s.mix = (Mix) mixI;
                    const int third = minor ? 3 : 4;
                    r.chord ({ 48 + root, 48 + root + third, 48 + root + 7 });
                    r.engine->editorSetWalk (word ("P L"));
                    r.block();
                    const Chord src = r.engine->sourceChord();
                    const Chord gen = r.engine->generatedChord();
                    bool ok = src == makeChord (minor ? minorTriadMask (root) : majorTriadMask (root), root)
                           && gen == applyWord (word ("P L"), src).chord;
                    // Each ghost: one op from the generated chord is what the key gives.
                    for (int k = 0; k < numOperatorOps; ++k)
                    {
                        Op op;
                        operatorOp (k, op);
                        const Step ghost = apply (op, gen);
                        Rig q;
                        q.s = r.s;
                        q.chord ({ 48 + root, 48 + root + third, 48 + root + 7 });
                        q.engine->editorSetWalk (word ("P L"));
                        q.block();
                        q.engine->editorOperator (k);
                        q.block();
                        ok = ok && q.engine->generatedChord().mask == ghost.chord.mask;
                    }
                    exact += ok ? 1 : 0;
                    ++total;
                }
        check (exact == total, "source and W(chord) with W's anchor, and all ten ghosts equal the key's result, for 24 triads x 3 mixes ("
               + std::to_string (exact) + "/" + std::to_string (total) + ")");

        Rig r;
        r.s.mix = Mix::SourceOnly;
        r.s.word = word ("P");
        r.chord ({ 60, 64, 67 });
        check (r.generated().empty() && r.engine->generatedChord().mask == minorTriadMask (0), "Source only: nothing generated sounds, but W(chord) is there for the lattice");
    }

    void testGenerated()
    {
        std::cout << "generated\n";
        Rig r;
        r.s.word = word ("P");
        r.chord ({ 60, 64, 67 }, 90);
        std::vector<std::pair<int, int>> g;
        r.engine->forEachGenerated ([&g] (int p, int v) { g.push_back ({ p, v }); });
        std::sort (g.begin(), g.end());
        check (g == std::vector<std::pair<int, int>> { { 60, 90 }, { 63, 90 }, { 67, 90 } }, "C Eb G at velocity 90");
        const auto before = r.engine->releases();
        r.t.playing = true;
        r.block();
        r.t.playing = false;
        r.block();
        const auto afterStop = r.engine->releases();
        r.s.enabled = false;
        r.block();
        check (afterStop > before && r.engine->releases() > afterStop && r.generated().empty(), "the release counter moves on stop and on Enable off");
    }

    void testNumerals()
    {
        std::cout << "numerals\n";
        static const char* majorWant[7] = { "I", "ii", "iii", "IV", "V", "vi", "vii\xc2\xb0" };
        static const char* minorWant[7] = { "i", "ii\xc2\xb0", "III", "iv", "v", "VI", "VII" };
        int good = 0;
        for (int tonic = 0; tonic < 12; ++tonic)
            for (int minor = 0; minor < 2; ++minor)
            {
                const Scale sc = minor ? Scale::NaturalMinor : Scale::Major;
                bool ok = true;
                for (int d = 0; d < 7; ++d)
                {
                    const int root = mod12 (tonic + scaleOffset (sc, d));
                    const unsigned m = pcBit (root) | pcBit (tonic + scaleOffset (sc, d + 2)) | pcBit (tonic + scaleOffset (sc, d + 4));
                    ok = ok && romanNumeral (root, m, tonic, minor) == (minor ? minorWant : majorWant)[d];
                }
                good += ok ? 1 : 0;
            }
        check (good == 24, "the seven diatonic triads of all 24 keys (" + std::to_string (good) + "/24)");
        check (romanNumeral (8, majorTriadMask (8) | pcBit (7), 0, false) == "\xe2\x99\xadVI", "Abmaj7 in C major: bVI (the plan's example)");
        check (romanNumeral (0, majorTriadMask (0) | pcBit (11), 0, false) == "I", "Cmaj7 in C major: I");
        check (romanNumeral (6, (unsigned) (pcBit (6) | pcBit (9) | pcBit (0)), 0, false) == "\xe2\x99\xaf" "iv\xc2\xb0", "F# dim in C major: #iv°");
        check (romanNumeral (0, (unsigned) (pcBit (0) | pcBit (4) | pcBit (8)), 0, false) == "I+", "C aug: I+");
        check (romanNumeral (11, majorTriadMask (11), 6, false) == "IV", "Cb (B) major in Gb major: IV, spelled in the key");
        check (romanNumeral (1, majorTriadMask (1), 0, false, false) == "bII" && romanNumeral (-1, 1, 0, false).empty() && romanNumeral (0, 1, -1, false).empty(),
               "ASCII bII; no root or no key: nothing");
    }
}

int main()
{
    testFifo();
    testOperator();
    testSetWalk();
    testChords();
    testGenerated();
    testNumerals();
    return testutil::finish ("test_editor");
}
