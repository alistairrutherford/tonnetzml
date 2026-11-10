// The test rig (PLAN.md §7.3, Source/core/engine/TestRig.h): the Keys.txt parser, and the
// player's placement of keys on the sample from the host's ppq, swept over sample rates, block
// sizes and tempos, with loops, a locate, a stop and the rig switched off.
#include "FakeHost.h"
#include "core/engine/TestRig.h"
#include <cstring>

using namespace tonnetz;
using fakehost::Config;
using fakehost::Expect;
using fakehost::Host;
using fakehost::LoopStyle;
using testutil::check;

namespace
{
    RigKeys parse (const std::string& text) { return parseRigText (text.data(), text.size()); }

    // What the plug-in does in Phase 0: the host's MIDI through, plus the rig's keys.
    struct Rig
    {
        RigKeys keys;
        RigPlayer player;
        bool on = true;

        void attach (Host& h)
        {
            h.process = [this] (const Transport& t, const MidiEvent* in, int numIn, int n, BlockEvents& out)
            {
                for (int i = 0; i < numIn; ++i)
                    out.push (in[i]);
                BlockEvents rig;
                player.generate (keys, on, t, n, rig);
                for (const auto& e : rig)
                    out.push (e);
                out.sortBySample();
            };
        }
    };

    // I-V-vi-IV in C, one chord a bar, with a few keys off the grid.
    const char* const progression =
        "# beat note velocity length\n"
        "0 60 100 3.5\n0 64 90 3.5\n0 67 80 3.5\n"
        "4 55 100 3.5\n4 59 90 3.5\n4 62 80 3.5\n"
        "8 57 100 3.5\n8 60 90 3.5\n8 64 80 3.5\n"
        "12 53 100 3.5\n12 57 90 3.5\n12 60 80 3.5\n"
        "1.3333 72 64 0.25\n6.75 74 1 0.0625\n10.001 76 127 2.999\n";

    void expectAll (const RigKeys& k, Expect& e, double shift = 0.0)
    {
        for (int i = 0; i < k.count; ++i)
            e.note (k.keys[(size_t) i].from + shift, k.keys[(size_t) i].to + shift, k.keys[(size_t) i].note, k.keys[(size_t) i].velocity);
    }

    void testParser()
    {
        const auto k = parse (progression);
        check (k.count == 15 && k.rejected == 0 && k.dropped == 0, "parser: 15 keys, the comment skipped");
        check (k.keys[0].from == 0.0 && k.keys[0].to == 3.5 && k.keys[0].note == 60 && k.keys[0].velocity == 100, "parser: beat note velocity length");
        check (std::abs (k.keys[12].from - 1.3333) < 1.0e-12 && std::abs (k.keys[12].to - 1.5833) < 1.0e-12, "parser: fractions");

        const auto odd = parse ("\n   \n\t# indented comment\r\n 2.5\t61  7 .5 # trailing comment\r\n-1 0 1 1\n+3,62,127,0.25\n4 60 100 1");
        check (odd.count == 4 && odd.rejected == 0, "parser: blank lines, CRLF, tabs, commas, a trailing comment, no final newline");
        check (odd.keys[0].from == 2.5 && odd.keys[0].to == 3.0 && odd.keys[0].note == 61 && odd.keys[0].velocity == 7, "parser: leading-dot length");
        check (odd.keys[1].from == -1.0 && odd.keys[1].note == 0, "parser: a negative beat (the count-in) is kept");

        const auto bad = parse ("0 60 100\n0 128 100 1\n0 60 0 1\n0 60 128 1\n0 60 100 0\n0 60 100 -1\n0 60.5 100 1\n"
                                "0 60 100 1 9\nx 60 100 1\n0 60 1e2 1\n0 60 100 1\n");
        check (bad.count == 1 && bad.rejected == 10, "parser: 10 malformed lines rejected, the good one kept ("
                                                         + std::to_string (bad.count) + ", " + std::to_string (bad.rejected) + ")");

        std::string many;
        for (int i = 0; i < 600; ++i)
            many += std::to_string (i) + " " + std::to_string (i % 128) + " 100 0.5\n";
        const auto full = parse (many);
        check (full.count == 512 && full.dropped == 88 && full.keys[511].from == 511.0, "parser: 512 keys kept, 88 dropped");
        check (parse ("").count == 0, "parser: an empty file");
    }

    void testPlacement()
    {
        fakehost::sweep ("placement: every key on its sample, host MIDI through", [] (const Config& c, int64_t& worst)
        {
            Host h (c.rate, c.bpm);
            Rig rig;
            rig.keys = parse (progression);
            rig.attach (h);
            h.key (0.5, 84, true, 33);       // the host's own keys pass through
            h.key (2.25, 84, false);
            h.run (h.at (17.0), c.block);
            h.stop (c.block);

            Expect e (h.spq());
            expectAll (rig.keys, e);
            e.note (0.5, 2.25, 84, 33);
            return fakehost::finish (h, e, worst);
        });

        fakehost::sweep ("placement: started mid-song, earlier keys are not played", [] (const Config& c, int64_t& worst)
        {
            Host h (c.rate, c.bpm);
            Rig rig;
            rig.keys = parse (progression);
            rig.attach (h);
            h.ppq = 6.0;
            h.run (h.at (4.0), c.block);     // song beats 6-10
            h.stop (c.block);

            Expect e (h.spq());
            e.note (0.75, 0.8125, 74, 1);
            for (int n : { 57, 60, 64 })
                e.note (2.0, 4.0, n, n == 57 ? 100 : (n == 60 ? 90 : 80));   // cut by the stop
            return fakehost::finish (h, e, worst);
        });
    }

    void testSamePitch()
    {
        fakehost::sweep ("same pitch: back-to-back keys restrike, in either file order", [] (const Config& c, int64_t& worst)
        {
            Host h (c.rate, c.bpm);
            Rig rig;
            rig.keys = parse ("1 60 90 1\n0 60 100 1\n2 60 80 0.5\n");
            rig.attach (h);
            h.run (h.at (4.0), c.block);
            h.stop (c.block);
            Expect e (h.spq());
            e.note (0.0, 1.0, 60, 100);
            e.note (1.0, 2.0, 60, 90);
            e.note (2.0, 2.5, 60, 80);
            return fakehost::finish (h, e, worst);
        });

        fakehost::sweep ("same pitch: overlapping keys restrike and end with the last", [] (const Config& c, int64_t& worst)
        {
            Host h (c.rate, c.bpm);
            Rig rig;
            rig.keys = parse ("0 60 100 2\n1 60 90 2\n");
            rig.attach (h);
            h.run (h.at (4.0), c.block);
            h.stop (c.block);
            Expect e (h.spq());
            e.note (0.0, 1.0, 60, 100);
            e.note (1.0, 3.0, 60, 90);
            return fakehost::finish (h, e, worst);
        });
    }

    void testStopsAndJumps()
    {
        fakehost::sweep ("stop: held keys end at the stop, and play again from the start", [] (const Config& c, int64_t& worst)
        {
            Host h (c.rate, c.bpm);
            Rig rig;
            rig.keys = parse ("0 60 100 8\n1 64 100 0.5\n");
            rig.attach (h);
            h.run (h.at (2.0), c.block);
            h.stop (c.block);
            const int64_t stopped = h.pos - c.block, restart = h.pos;
            h.playing = true;
            h.ppq = 0.0;
            h.run (restart + h.at (2.0), c.block);
            h.stop (c.block);

            Expect e (h.spq());
            e.on (0.0, 60); e.offAt (stopped, 60);
            e.note (1.0, 1.5, 64);
            e.onAt (restart, 60); e.offAt (restart + h.at (2.0), 60);
            e.onAt (restart + h.at (1.0), 64); e.offAt (restart + h.at (1.5), 64);
            return fakehost::finish (h, e, worst);
        });

        fakehost::sweep ("locate: held keys end at the jump, keys from the new position play", [] (const Config& c, int64_t& worst)
        {
            Host h (c.rate, c.bpm);
            Rig rig;
            rig.keys = parse ("0 60 100 8\n9 64 100 0.5\n8 62 100 4\n");
            rig.attach (h);
            h.run (h.at (2.0), c.block);
            const int64_t jump = h.pos;      // a block boundary at or after beat 2
            h.ppq = 8.0;
            h.run (jump + h.at (3.0), c.block);
            h.stop (c.block);

            Expect e (h.spq());
            e.on (0.0, 60); e.offAt (jump, 60);
            e.onAt (jump, 62); e.offAt (jump + h.at (3.0), 62);
            e.onAt (jump + h.at (1.0), 64); e.offAt (jump + h.at (1.5), 64);
            return fakehost::finish (h, e, worst);
        });

        fakehost::sweep ("rig off: held keys end, nothing more plays", [] (const Config& c, int64_t& worst)
        {
            Host h (c.rate, c.bpm);
            Rig rig;
            rig.keys = parse ("0 60 100 8\n3 64 100 0.5\n");
            rig.attach (h);
            h.run (h.at (2.0), c.block);
            const int64_t off = h.pos;
            rig.on = false;
            h.run (off + h.at (2.0), c.block);
            h.stop (c.block);
            Expect e (h.spq());
            e.on (0.0, 60); e.offAt (off, 60);
            return fakehost::finish (h, e, worst);
        });
    }

    void testLoop()
    {
        for (const auto style : { LoopStyle::Wrap, LoopStyle::Split, LoopStyle::SplitDropOvershoot })
        {
            const char* name = style == LoopStyle::Wrap ? "wrap" : (style == LoopStyle::Split ? "split" : "split, overshoot dropped");
            // A host that drops the overshoot starts each pass up to a sample late.
            const int64_t tolerance = style == LoopStyle::SplitDropOvershoot ? 4 : 1;
            fakehost::sweep (std::string ("loop (") + name + "): three passes, keys cut at the loop end",
                             [style, tolerance] (const Config& c, int64_t& worst)
            {
                Host h (c.rate, c.bpm);
                Rig rig;
                rig.keys = parse ("0 60 100 1\n3.5 67 90 1\n2 64 80 0.5\n4 72 100 1\n");   // beat 4 is never reached
                rig.attach (h);
                h.looping = true;
                h.loopStart = 0.0;
                h.loopEnd = 4.0;
                h.style = style;
                h.run (h.at (11.0), c.block);   // stops in the third pass, before its last key
                h.stop (c.block);

                Expect e (h.spq());
                for (int pass = 0; pass < 3; ++pass)
                {
                    const double b = pass * 4.0;
                    e.note (b, b + 1.0, 60, 100);
                    e.note (b + 2.0, b + 2.5, 64, 80);
                    if (pass < 2)
                        e.note (b + 3.5, b + 4.0, 67, 90);
                }
                return fakehost::finish (h, e, worst, tolerance);
            });
        }
    }
}

int main()
{
    testParser();
    testPlacement();
    testSamePitch();
    testStopsAndJumps();
    testLoop();
    return testutil::finish ("test_rig");
}
