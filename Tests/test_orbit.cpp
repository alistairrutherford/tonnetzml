// Orbit (PLAN.md §3.1, §3.3, §7.1 test_orbit): a held chord stepped through a cycle word every
// Rate, its steps on the grid of the host's ppq across loops (three loop styles), locates, tempo
// changes and block boundaries; each scenario swept over 44.1/48/96 kHz, 32-2048-sample blocks
// and three tempos (FakeHost.h).
//
// The expected notes come from the voicer (W = the cycle's first k ops, k mod the cycle length)
// and from where the grid lines fall, worked out here from the host's ppq at every block
// (FakeHost records it), independently of OrbitClock.h: a line g in a block that runs straight
// from ppq p at sample s falls on round (s + (g - p) · samples per quarter); after a loop wrap
// or a locate, a line at the new position falls on the first sample after it.
#include "FakeHost.h"
#include "core/engine/LatticeEngine.h"
#include <map>

using namespace tonnetz;
using namespace fakehost;
using testutil::check;

namespace
{
    Word word (const char* text)
    {
        Word w;
        parseWord (text, w);
        return w;
    }

    struct Run
    {
        std::unique_ptr<LatticeEngine> engine = std::make_unique<LatticeEngine>();
        EngineSettings s;
        Host h;
        std::function<void (const Transport&, EngineSettings&)> before;

        explicit Run (const Config& c) : h (c.rate, c.bpm)
        {
            s.gatherMs = 0;
            s.mode = Mode::Orbit;
            s.orbitCycle = OrbitCycle::PL;
            s.orbitRate = 2;                   // 1 bar
            h.process = [this] (const Transport& t, const MidiEvent* in, int n, int len, BlockEvents& out)
            {
                if (before)
                    before (t, s);
                engine->process (t, in, n, len, s, out);
            };
        }

        void chord (double beats, double length, std::initializer_list<int> notes, int velocity = 100)
        {
            for (int n : notes)
            {
                h.key (beats, n, true, velocity);
                h.key (beats + length, n, false);
            }
        }
    };

    // The voicing of `keys` under the cycle's first `k` ops (mod its length on the chord).
    std::map<int, int> voicingAt (const Word& cycle, std::initializer_list<int> keys, int k, const VoicerSettings& vs = {})
    {
        Voicing in, out;
        for (int n : keys)
            in.push (n, n);
        const Chord start = inputChord (in);
        const int period = cycleOf (cycle, start).steps;
        const int r = period > 0 ? k % period : k;
        voiceOps (r, [&cycle] (int i) { return cycle[i % cycle.size]; }, start, in, vs, out, nullptr);
        std::map<int, int> m;
        for (const auto& v : out)
            m[v.owner] = v.pitch;
        return m;
    }

    // Expected hits for a chord whose voicing changes at the given samples: voices whose pitch
    // changes end and start there; everything ends at `end`.
    void expectSteps (Expect& e, const std::vector<std::pair<int64_t, std::map<int, int>>>& steps, int64_t end, int velocity = 100)
    {
        std::map<int, int> cur;
        for (const auto& [at, v] : steps)
        {
            for (const auto& [o, p] : cur)
                if (! v.count (o) || v.at (o) != p)
                    e.offAt (at, p);
            for (const auto& [o, p] : v)
                if (! cur.count (o) || cur.at (o) != p)
                    e.onAt (at, p, velocity);
            cur = v;
        }
        for (const auto& [o, p] : cur)
            e.offAt (end, p);
    }

    // Grid lines every `step` quarters from ppq 0, found from the blocks the host played (its
    // (sample, ppq) at each block start), between absolute samples [from, to).
    std::vector<int64_t> gridLines (const Host& h, double step, double barLen, int64_t from, int64_t to, double spq,
                                    const std::vector<std::pair<int64_t, double>>* bpmAt = nullptr)
    {
        (void) barLen;
        std::vector<int64_t> lines;
        const auto& b = h.blockStarts;
        double prevEnd = 0.0, lastG = -1.0e300;    // the last line taken, in continuous musical time
        for (size_t i = 0; i < b.size(); ++i)
        {
            const int64_t s0 = b[i].first, s1 = i + 1 < b.size() ? b[i + 1].first : h.pos;
            double p = b[i].second;
            double rate = spq;
            if (bpmAt != nullptr)
                for (const auto& [at, bpm] : *bpmAt)
                    if (at <= s0)
                        rate = h.sampleRate * 60.0 / bpm;
            // The block runs straight from p, except where it reaches the loop end.
            const bool jumped = i == 0 || std::abs (prevEnd - p) * rate > 0.5;
            int64_t start = s0;
            bool fresh = jumped;
            while (start < s1)
            {
                int64_t end = s1;
                double endP = 1.0e300;
                if (h.looping && p < h.loopEnd - 1.0e-9)
                {
                    const auto wrap = (int64_t) std::ceil ((double) start + (h.loopEnd - p) * rate - 1.0e-6);
                    if (wrap < s1)
                    {
                        end = wrap;
                        endP = h.loopEnd;
                    }
                    else if (wrap == s1)
                        endP = h.loopEnd;
                }
                const double tol = 1.0 / rate;     // a line up to a sample before a block or segment start is taken there
                if (fresh)
                    lastG = -1.0e300;
                for (double g = std::ceil ((p - tol) / step - 1.0e-9) * step; g < endP - 1.0e-9; g += step)
                {
                    auto at = (int64_t) std::llround ((double) start + (g - p) * rate);
                    at = std::max (at, start);
                    if (at >= end)
                        break;
                    if (fresh)
                        lastG = -1.0e300;
                    fresh = false;
                    if (g <= lastG + 1.0e-7)
                        continue;
                    lastG = g;
                    if (at >= from && at < to)
                        lines.push_back (at);
                }
                if (end >= s1)
                    break;
                p = h.loopStart + (p + (double) (end - start) / rate - h.loopEnd);
                start = end;
                fresh = true;
            }
            prevEnd = p + (double) (s1 - start) / rate;
        }
        std::sort (lines.begin(), lines.end());
        lines.erase (std::unique (lines.begin(), lines.end()), lines.end());
        return lines;
    }

    // A chord held from `on` to `off` (samples) steps at every grid line strictly inside.
    std::string checkHeld (Run& r, const Word& cycle, std::initializer_list<int> keys, int64_t on, int64_t off, double stepQuarters,
                           int64_t& worst, const std::vector<std::pair<int64_t, double>>* bpmAt = nullptr, int64_t tolerance = 1)
    {
        const auto lines = gridLines (r.h, stepQuarters, 4.0, on + 1, off, r.h.spq(), bpmAt);
        std::vector<std::pair<int64_t, std::map<int, int>>> steps;
        steps.push_back ({ on, voicingAt (cycle, keys, 0, r.s.voicing) });
        for (size_t i = 0; i < lines.size(); ++i)
            steps.push_back ({ lines[i], voicingAt (cycle, keys, (int) i + 1, r.s.voicing) });
        Expect e (r.h.spq());
        expectSteps (e, steps, off);
        std::string why = finish (r.h, e, worst, tolerance);
        if (lines.size() < 2)
            why += " too few grid lines (" + std::to_string (lines.size()) + ");";
        return why;
    }

    void plainSteps()
    {
        for (const auto& [name, cycle] : { std::make_pair ("PL", OrbitCycle::PL), std::make_pair ("LR", OrbitCycle::LR),
                                           std::make_pair ("PR", OrbitCycle::PR), std::make_pair ("PLR", OrbitCycle::PLR) })
            sweep (std::string ("Orbit ") + name + " at 1 bar: C E G held from bar 2 for 7 bars steps on every bar line, only moving voices retuned",
                   [cyc = cycle, text = name] (const Config& c, int64_t& worst)
                   {
                       Run r (c);
                       r.s.orbitCycle = cyc;
                       r.chord (4.0, 28.0, { 60, 64, 67 });
                       r.h.run (r.h.at (34.0), c.block);
                       r.h.stop (c.block);
                       return checkHeld (r, word (text), { 60, 64, 67 }, r.h.at (4.0), r.h.at (32.0), 4.0, worst);
                   });

        for (int rate : { 0, 1, 3, 4 })
            sweep (std::string ("Orbit PL at ") + orbitRateNames[rate] + ": steps on its grid", [rate] (const Config& c, int64_t& worst)
            {
                Run r (c);
                r.s.orbitRate = rate;
                r.chord (4.0, 36.0, { 57, 60, 64 });
                r.h.run (r.h.at (42.0), c.block);
                r.h.stop (c.block);
                return checkHeld (r, word ("PL"), { 57, 60, 64 }, r.h.at (4.0), r.h.at (40.0), orbitRateBars[rate] * 4.0, worst);
            });

        sweep ("a chord played off the grid steps on the next line, not a Rate after it", [] (const Config& c, int64_t& worst)
        {
            Run r (c);
            r.chord (5.3, 10.0, { 62, 66, 69 });
            r.h.run (r.h.at (17.0), c.block);
            r.h.stop (c.block);
            return checkHeld (r, word ("PL"), { 62, 66, 69 }, r.h.at (5.3), r.h.at (15.3), 4.0, worst);
        });

        sweep ("3/4 time: 1 bar = 3 quarters, lines at the host's bar starts", [] (const Config& c, int64_t& worst)
        {
            Run r (c);
            r.h.numerator = 3;
            r.h.reportBarStart = true;
            r.chord (3.0, 15.0, { 60, 64, 67 });
            r.h.run (r.h.at (20.0), c.block);
            r.h.stop (c.block);
            return checkHeld (r, word ("PL"), { 60, 64, 67 }, r.h.at (3.0), r.h.at (18.0), 3.0, worst);
        });
    }

    void loops()
    {
        const LoopStyle styles[] = { LoopStyle::Wrap, LoopStyle::Split, LoopStyle::SplitDropOvershoot };
        const char* names[] = { "the host doesn't split blocks", "split at the loop end", "split, next block exactly at the loop start" };
        for (int st = 0; st < 3; ++st)
        {
            sweep (std::string ("loop of 2 bars (beats 4-12), ") + names[st] + ": a chord held over four passes steps at every bar line and at the loop start, once each",
                   [st, &styles] (const Config& c, int64_t& worst)
                   {
                       Run r (c);
                       r.h.looping = true;
                       r.h.loopStart = 4.0;
                       r.h.loopEnd = 12.0;
                       r.h.style = styles[st];
                       // Down at beat 4 (the first pass), up 30 beats of song time later.
                       const int64_t on = r.h.at (4.0), off = on + r.h.at (30.0);
                       for (int n : { 60, 64, 67 })
                       {
                           r.h.keyAt (on, n, true);
                           r.h.keyAt (off, n, false);
                       }
                       r.h.run (off + r.h.at (1.0), c.block);
                       r.h.stop (c.block);
                       return checkHeld (r, word ("PL"), { 60, 64, 67 }, on, off, 4.0, worst);
                   });
            sweep (std::string ("loop of 3.5 beats (5.25-8.75) at 1/4 bar, ") + names[st] + ": lines inside the loop only",
                   [st, &styles] (const Config& c, int64_t& worst)
                   {
                       Run r (c);
                       r.s.orbitRate = 0;
                       r.h.looping = true;
                       r.h.loopStart = 5.25;
                       r.h.loopEnd = 8.75;
                       r.h.style = styles[st];
                       const int64_t on = r.h.at (5.5), off = on + r.h.at (20.0);
                       for (int n : { 60, 63, 67 })
                       {
                           r.h.keyAt (on, n, true);
                           r.h.keyAt (off, n, false);
                       }
                       r.h.run (off + r.h.at (1.0), c.block);
                       r.h.stop (c.block);
                       return checkHeld (r, word ("PL"), { 60, 63, 67 }, on, off, 1.0, worst);
                   });
        }
    }

    void locatesAndTempo()
    {
        sweep ("a locate while held (beat 9 to 18.5): the grid follows the new position", [] (const Config& c, int64_t& worst)
        {
            Run r (c);
            const int64_t on = r.h.at (4.0);
            for (int n : { 60, 64, 67 })
                r.h.keyAt (on, n, true);
            r.h.run (r.h.at (9.0), c.block);
            r.h.ppq = 18.5;
            const int64_t off = r.h.pos + r.h.at (14.0);
            for (int n : { 60, 64, 67 })
                r.h.keyAt (off, n, false);
            r.h.run (off + r.h.at (1.0), c.block);
            r.h.stop (c.block);
            return checkHeld (r, word ("PL"), { 60, 64, 67 }, on, off, 4.0, worst);
        });

        sweep ("tempo changes while held (to 90, then 150 BPM): every step still on its bar line", [] (const Config& c, int64_t& worst)
        {
            Run r (c);
            const double spq0 = r.h.spq();
            const int64_t on = r.h.at (4.0);
            const std::vector<std::pair<int64_t, double>> bpm { { 0, c.bpm }, { (int64_t) (9.3 * spq0), 90.0 }, { (int64_t) (17.7 * spq0), 150.0 } };
            r.h.tempo = { bpm[1], bpm[2] };
            for (int n : { 60, 64, 67 })
                r.h.keyAt (on, n, true);
            const int64_t off = (int64_t) (34.0 * spq0);
            for (int n : { 60, 64, 67 })
                r.h.keyAt (off, n, false);
            r.h.run (off + 1000, c.block);
            r.h.stop (c.block);
            // Tempo changes land on block boundaries: the reference finds each block's tempo from
            // where the host changed it.
            std::vector<std::pair<int64_t, double>> applied;
            for (const auto& [at, b] : bpm)
            {
                int64_t first = -1;
                for (const auto& bs : r.h.blockStarts)
                    if (bs.first >= at)
                    {
                        first = bs.first;
                        break;
                    }
                if (first >= 0)
                    applied.push_back ({ first, b });
            }
            return checkHeld (r, word ("PL"), { 60, 64, 67 }, on, off, 4.0, worst, &applied);
        });
    }

    void inputs()
    {
        sweep ("Relative: a new chord on the bar line comes in at the step the old one reached", [] (const Config& c, int64_t& worst)
        {
            Run r (c);
            r.chord (4.0, 8.0, { 60, 64, 67 });      // steps at 8; ends at 12
            r.chord (12.0, 8.0, { 65, 69, 72 });     // comes in at k = 2 (the line at 12 stepped), steps at 16
            r.h.run (r.h.at (22.0), c.block);
            r.h.stop (c.block);
            const Word pl = word ("PL");
            Expect e (r.h.spq());
            expectSteps (e, { { r.h.at (4.0), voicingAt (pl, { 60, 64, 67 }, 0) }, { r.h.at (8.0), voicingAt (pl, { 60, 64, 67 }, 1) } }, r.h.at (12.0));
            expectSteps (e, { { r.h.at (12.0), voicingAt (pl, { 65, 69, 72 }, 2) }, { r.h.at (16.0), voicingAt (pl, { 65, 69, 72 }, 3) } }, r.h.at (20.0));
            return finish (r.h, e, worst);
        });
        sweep ("Reset: each new chord starts the cycle again", [] (const Config& c, int64_t& worst)
        {
            Run r (c);
            r.s.walkInput = WalkInput::Reset;
            r.chord (4.0, 8.0, { 60, 64, 67 });
            r.chord (12.0, 8.0, { 65, 69, 72 });
            r.h.run (r.h.at (22.0), c.block);
            r.h.stop (c.block);
            const Word pl = word ("PL");
            Expect e (r.h.spq());
            expectSteps (e, { { r.h.at (4.0), voicingAt (pl, { 60, 64, 67 }, 0) }, { r.h.at (8.0), voicingAt (pl, { 60, 64, 67 }, 1) } }, r.h.at (12.0));
            expectSteps (e, { { r.h.at (12.0), voicingAt (pl, { 65, 69, 72 }, 0) }, { r.h.at (16.0), voicingAt (pl, { 65, 69, 72 }, 1) } }, r.h.at (20.0));
            return finish (r.h, e, worst);
        });
        sweep ("Latch: a chord tapped once orbits on; the next chord replaces it when its window closes", [] (const Config& c, int64_t& worst)
        {
            Run r (c);
            r.s.latch = true;
            r.s.gatherMs = 20;
            r.chord (4.0, 0.5, { 60, 64, 67 });      // tapped; steps at 8, 12
            r.chord (13.0, 0.5, { 62, 65, 69 });     // replaces it at 13 + 20 ms, at k = 2; steps at 16
            r.h.run (r.h.at (18.0), c.block);
            r.h.stop (c.block);
            const Word pl = word ("PL");
            const int64_t w = gatherSamples (20, c.rate);
            const auto at = [&r, w] (double beats) { return r.h.at (beats) + w; };
            Expect e (r.h.spq());
            expectSteps (e, { { at (4.0), voicingAt (pl, { 60, 64, 67 }, 0) }, { r.h.at (8.0), voicingAt (pl, { 60, 64, 67 }, 1) },
                              { r.h.at (12.0), voicingAt (pl, { 60, 64, 67 }, 2) } }, at (13.0));
            expectSteps (e, { { at (13.0), voicingAt (pl, { 62, 65, 69 }, 2) }, { r.h.at (16.0), voicingAt (pl, { 62, 65, 69 }, 3) } }, r.h.at (18.0));
            return finish (r.h, e, worst);
        });
        sweep ("stopped: a chord orbits on the internal clock, its first step a Rate after it was played", [] (const Config& c, int64_t& worst)
        {
            Run r (c);
            r.h.playing = false;
            const int64_t on = 777;
            for (int n : { 60, 64, 67 })
            {
                r.h.keyAt (on, n, true);
                r.h.keyAt (on + r.h.at (10.0), n, false);
            }
            r.h.run (on + r.h.at (11.0), c.block);
            const Word pl = word ("PL");
            const auto bar = [&] (int n) { return on + (int64_t) std::llround (n * 4.0 * r.h.spq()); };
            Expect e (r.h.spq());
            expectSteps (e, { { on, voicingAt (pl, { 60, 64, 67 }, 0) }, { bar (1), voicingAt (pl, { 60, 64, 67 }, 1) },
                              { bar (2), voicingAt (pl, { 60, 64, 67 }, 2) } }, on + r.h.at (10.0));
            return finish (r.h, e, worst);
        });
        sweep ("operator keys in Orbit: Home restarts the cycle, Undo steps back; the others do nothing", [] (const Config& c, int64_t& worst)
        {
            Run r (c);
            r.s.zone.on = true;
            r.chord (4.0, 18.0, { 60, 64, 67 });      // steps at 8, 12 (k = 2)
            r.h.key (13.0, 24 + opkey::undo, true);   // k = 1
            r.h.key (13.5, 24 + 0, true);             // P: nothing
            r.h.key (14.0, 24 + opkey::home, true);   // k = 0; steps at 16, 20 (k = 2)
            r.h.run (r.h.at (23.0), c.block);
            r.h.stop (c.block);
            const Word pl = word ("PL");
            const auto v = [&pl] (int k) { return voicingAt (pl, { 60, 64, 67 }, k); };
            Expect e (r.h.spq());
            expectSteps (e, { { r.h.at (4.0), v (0) }, { r.h.at (8.0), v (1) }, { r.h.at (12.0), v (2) }, { r.h.at (13.0), v (1) },
                              { r.h.at (14.0), v (0) }, { r.h.at (16.0), v (1) }, { r.h.at (20.0), v (2) } }, r.h.at (22.0));
            return finish (r.h, e, worst);
        });
        sweep ("the slot's word as the cycle (R +5): its own period on the chord", [] (const Config& c, int64_t& worst)
        {
            Run r (c);
            r.s.orbitCycle = OrbitCycle::Slot;
            r.s.word = word ("R +5");
            r.chord (4.0, 40.0, { 60, 64, 67 });
            r.h.run (r.h.at (46.0), c.block);
            r.h.stop (c.block);
            return checkHeld (r, word ("R +5"), { 60, 64, 67 }, r.h.at (4.0), r.h.at (44.0), 4.0, worst);
        });
    }

    // In key: the cycle's chords in order, with those outside the key passed over.
    void inKey()
    {
        const auto chordsSounded = [] (Run& r)
        {
            std::vector<PcMask> seq;
            std::set<int> on;
            for (const auto& hit : r.h.hits)
            {
                if (hit.on) on.insert (hit.note);
                else on.erase (hit.note);
                unsigned m = 0;
                for (int n : on) m |= pcBit (n);
                if (on.size() == 3 && (seq.empty() || seq.back() != (PcMask) m))
                    seq.push_back ((PcMask) m);
            }
            return seq;
        };
        for (const bool minor : { false, true })
            sweep (std::string ("In key (") + (minor ? "A minor" : "C major") + ", Hold): Orbit LR from C major passes over the chords outside the key",
                   [minor, chordsSounded] (const Config& c, int64_t&)
                   {
                       Run r (c);
                       r.s.orbitCycle = OrbitCycle::LR;
                       r.s.inKey = true;
                       r.s.key = KeyMode::Hold;
                       r.s.keyTonic = minor ? 9 : 0;
                       r.s.keyMinor = minor;
                       r.chord (4.0, 40.0, { 60, 64, 67 });
                       r.h.run (r.h.at (46.0), c.block);
                       r.h.stop (c.block);
                       const auto seq = chordsSounded (r);
                       // The cycle walked op by op from C major, kept where it's in the key.
                       const KeyRef key { minor ? 9 : 0, minor };
                       std::vector<PcMask> want { (PcMask) 0x091 };
                       WordRunner w (makeChord (0x091, 0));
                       const Word lr = word ("LR");
                       for (int i = 0; want.size() < seq.size() && i < 200; ++i)
                       {
                           w.step (lr[i % 2]);
                           if (key.contains (w.chord().mask) && w.chord().mask != want.back())
                               want.push_back (w.chord().mask);
                       }
                       std::string why;
                       if (seq.size() < 9 || seq != want)
                           why = " got " + std::to_string (seq.size()) + " chords, not the key's walk;";
                       for (const auto m : seq)
                           if (! key.contains (m))
                               why += " a chord outside the key;";
                       if (! r.h.clean())
                           why += " not clean: " + r.h.rx.describe();
                       return why;
                   });
        sweep ("In key with Listen and no guess yet: nothing is passed over", [] (const Config& c, int64_t& worst)
        {
            Run r (c);
            r.s.orbitCycle = OrbitCycle::LR;
            r.s.inKey = true;
            r.chord (4.0, 20.0, { 60, 64, 67 });
            r.h.run (r.h.at (26.0), c.block);
            r.h.stop (c.block);
            return checkHeld (r, word ("LR"), { 60, 64, 67 }, r.h.at (4.0), r.h.at (24.0), 4.0, worst);
        });
    }

    void longRun()
    {
        sweep ("Orbit LR for 60 bars (more than 32 ops): W wraps with the 24-chord cycle, nothing drifts", [] (const Config& c, int64_t& worst)
        {
            Run r (c);
            r.s.orbitCycle = OrbitCycle::LR;
            r.chord (4.0, 240.0, { 60, 64, 67 });
            r.h.run (r.h.at (246.0), c.block);
            r.h.stop (c.block);
            std::string why = checkHeld (r, word ("LR"), { 60, 64, 67 }, r.h.at (4.0), r.h.at (244.0), 4.0, worst);
            // 59 steps while it sounds, and one on the line it is let go on (it was sounding just before).
            if (r.engine->orbitStep() != 60)
                why += " step " + std::to_string (r.engine->orbitStep()) + ", not 60;";
            return why;
        });
    }
}

int main()
{
    std::cout << "test_orbit\n";
    plainSteps();
    loops();
    locatesAndTempo();
    inputs();
    inKey();
    longRun();
    return testutil::finish ("test_orbit");
}
