// A fake host for the core's timing tests (PLAN.md §7.1), adapted from ChordMonkey's: plays a
// block processor block by block under a transport that can loop (with or without splitting
// blocks at the loop end), change tempo and stop, feeds it keys at absolute samples, and records
// what comes out. `sweep` runs a scenario at 32-2048-sample blocks, 44.1/48/96 kHz and three
// tempos; `compare` pairs the output with the expected notes within a tolerance in samples.
//
// The processor is `Host::process` (transport, input events, count, block length, output): in
// Phase 0 the test rig's player; from Phase 2 the LatticeEngine.
#pragma once

#include "TestUtil.h"
#include "Receiver.h"
#include "core/engine/MidiEvents.h"
#include "core/engine/Transport.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <sstream>
#include <vector>

namespace fakehost
{
    using namespace tonnetz;

    struct Hit
    {
        int64_t sample = 0;
        int note = 0;
        bool on = false;
        int velocity = 0;
    };

    enum class LoopStyle
    {
        Wrap,              // the host doesn't split blocks; its next block starts after the wrap
        Split,             // the host ends a block at the loop end
        SplitDropOvershoot // …and starts the next exactly at the loop start
    };

    using Process = std::function<void (const Transport&, const MidiEvent*, int, int, BlockEvents&)>;

    // A fake host: plays the processor block by block with a transport that can loop, change tempo
    // at block boundaries and stop; feeds it key events at absolute sample positions.
    struct Host
    {
        Process process;
        double sampleRate = 48000.0, bpm = 120.0;
        bool playing = true;
        double ppq = 0.0;
        int numerator = 4, denominator = 4;
        bool looping = false;
        double loopStart = 0.0, loopEnd = 0.0;
        LoopStyle style = LoopStyle::Wrap;
        bool reportBarStart = false;
        std::function<void (const Transport&, int, const BlockEvents&)> observe;   // after each block

        std::vector<std::pair<int64_t, MidiEvent>> input;
        std::vector<std::pair<int64_t, double>> tempo;   // (absolute sample, bpm), from the first block at or after it
        int64_t pos = 0;
        size_t nextInput = 0, nextTempo = 0;

        std::vector<Hit> hits;
        testutil::Receiver rx;
        std::vector<std::pair<int64_t, double>> blockStarts;   // (absolute sample, ppq) of each block

        Host (double rate, double tempoBpm) : sampleRate (rate), bpm (tempoBpm) {}

        double spq() const { return sampleRate * 60.0 / bpm; }
        int64_t at (double beats) const { return (int64_t) std::llround (beats * spq()); }

        void keyAt (int64_t sample, int note, bool down, int velocity = 100)
        {
            input.push_back ({ sample, down ? MidiEvent::noteOn (0, note, velocity, 0) : MidiEvent::noteOff (0, note, 0) });
        }

        // A key at `beats` (constant tempo from ppq 0 at sample 0).
        void key (double beats, int note, bool down, int velocity = 100) { keyAt (at (beats), note, down, velocity); }

        void run (int64_t until, int blockSize)
        {
            std::stable_sort (input.begin() + (long) nextInput, input.end(),
                              [] (const auto& a, const auto& b) { return a.first < b.first; });
            BlockEvents out;
            std::vector<MidiEvent> in;
            while (pos < until)
            {
                while (nextTempo < tempo.size() && tempo[nextTempo].first <= pos)
                    bpm = tempo[nextTempo++].second;

                int n = (int) std::min<int64_t> (blockSize, until - pos);
                const double samplesPerQuarter = spq();
                if (playing && looping && style != LoopStyle::Wrap && ppq < loopEnd - 1.0e-12)
                {
                    const int split = (int) std::ceil ((loopEnd - ppq) * samplesPerQuarter - 1.0e-6);
                    if (split >= 1 && split < n)
                        n = split;
                }

                Transport t { sampleRate, playing, ppq, bpm, numerator, denominator };
                t.looping = looping;
                t.loopStart = loopStart;
                t.loopEnd = loopEnd;
                if (reportBarStart)
                {
                    t.hasBarStart = true;
                    t.barStartPpq = std::floor (ppq / t.barLength() + 1.0e-9) * t.barLength();
                }

                in.clear();
                while (nextInput < input.size() && input[nextInput].first < pos + n)
                {
                    MidiEvent e = input[nextInput].second;
                    e.sample = (int) (input[nextInput].first - pos);
                    in.push_back (e);
                    ++nextInput;
                }

                blockStarts.push_back ({ pos, ppq });
                out.clear();
                process (t, in.data(), (int) in.size(), n, out);
                if (observe)
                    observe (t, n, out);
                rx.feed (out, n);
                for (const auto& e : out)
                    if (e.isNoteOn() || e.isNoteOff())
                        hits.push_back ({ pos + e.sample, e.note(), e.isNoteOn(), e.isNoteOn() ? e.velocity() : 0 });

                if (playing)
                {
                    double next = ppq + n / samplesPerQuarter;
                    if (looping && ppq < loopEnd && next >= loopEnd - 1.0e-9)
                        next = style == LoopStyle::SplitDropOvershoot ? loopStart : next - (loopEnd - loopStart);
                    ppq = next;
                }
                pos += n;
            }
        }

        // Stops the transport for one block: everything still sounding ends at `pos`.
        void stop (int blockSize = 64)
        {
            playing = false;
            run (pos + blockSize, blockSize);
        }

        bool clean() const { return rx.clean() && rx.on.empty() && rx.noteOns == rx.noteOffs; }
    };

    // The expected note list, from musical times through a beats → samples mapping.
    struct Expect
    {
        std::function<double (double)> toSample;
        std::vector<Hit> hits;

        explicit Expect (double spq) : toSample ([spq] (double beats) { return beats * spq; }) {}
        explicit Expect (std::function<double (double)> f) : toSample (std::move (f)) {}

        int64_t s (double beats) const { return (int64_t) std::llround (toSample (beats)); }

        void on (double beats, int note, int velocity = 100) { hits.push_back ({ s (beats), note, true, velocity }); }
        void off (double beats, int note) { hits.push_back ({ s (beats), note, false, 0 }); }
        void note (double from, double to, int note, int velocity = 100) { on (from, note, velocity); off (to, note); }
        void onAt (int64_t sample, int note, int velocity = 100) { hits.push_back ({ sample, note, true, velocity }); }
        void offAt (int64_t sample, int note) { hits.push_back ({ sample, note, false, 0 }); }
    };

    // Pairs every expected hit with a recorded one (same note, on/off and velocity, within
    // `tolerance` samples). Returns "" if all match, else what didn't.
    inline std::string compare (const std::vector<Hit>& got, const std::vector<Hit>& want, int64_t& worst, int64_t tolerance = 1)
    {
        std::vector<bool> used (got.size(), false);
        std::ostringstream why;
        int missing = 0;
        worst = 0;
        for (const auto& w : want)
        {
            int best = -1;
            int64_t bestD = tolerance + 1;
            for (size_t i = 0; i < got.size(); ++i)
            {
                const auto& g = got[i];
                if (used[i] || g.note != w.note || g.on != w.on || (w.on && g.velocity != w.velocity))
                    continue;
                const int64_t d = std::llabs (g.sample - w.sample);
                if (d < bestD)
                {
                    bestD = d;
                    best = (int) i;
                }
            }
            if (best < 0)
            {
                if (missing++ < 4)
                    why << " missing " << (w.on ? "on " : "off ") << w.note << " v" << w.velocity << " @" << w.sample << ";";
                continue;
            }
            used[(size_t) best] = true;
            worst = std::max (worst, bestD);
        }
        int extra = 0;
        for (size_t i = 0; i < got.size(); ++i)
            if (! used[i] && extra++ < 4)
                why << " extra " << (got[i].on ? "on " : "off ") << got[i].note << " v" << got[i].velocity << " @" << got[i].sample << ";";
        if (missing > 0 || extra > 0)
            why << " (" << missing << " missing, " << extra << " extra of " << want.size() << ")";
        return why.str();
    }

    struct Config
    {
        double rate, bpm;
        int block;
    };

    inline std::vector<Config> configs()
    {
        std::vector<Config> c;
        for (double rate : { 44100.0, 48000.0, 96000.0 })
            for (int block : { 32, 64, 128, 333, 512, 1024, 2048 })
                c.push_back ({ rate, 120.0, block });
        for (int block : { 32, 333, 2048 })
            c.push_back ({ 44100.0, 97.0, block });
        for (int block : { 64, 512 })
            c.push_back ({ 96000.0, 174.0, block });
        return c;
    }

    inline std::string describe (const Config& c)
    {
        std::ostringstream o;
        o << c.rate / 1000.0 << "k/" << c.bpm << "/" << c.block;
        return o.str();
    }

    // Runs `scenario` in every configuration; one check line for all of them.
    inline void sweep (const std::string& name, const std::function<std::string (const Config&, int64_t& worst)>& scenario)
    {
        int failed = 0;
        int64_t worst = 0;
        std::string first;
        const auto all = configs();
        for (const auto& c : all)
        {
            int64_t w = 0;
            const std::string why = scenario (c, w);
            worst = std::max (worst, w);
            if (! why.empty())
                if (failed++ == 0)
                    first = " first failure " + describe (c) + ":" + why;
        }
        testutil::check (failed == 0, name + ": " + std::to_string (all.size() - (size_t) failed) + "/" + std::to_string (all.size())
                                + " configurations, worst " + std::to_string (worst) + " sample(s)" + first);
    }

    // Finishes a scenario: compare, and check the stream is clean and nothing is left sounding.
    inline std::string finish (Host& h, const Expect& e, int64_t& worst, int64_t tolerance = 1)
    {
        std::string why = compare (h.hits, e.hits, worst, tolerance);
        if (! h.clean())
            why += " not clean: " + h.rx.describe();
        return why;
    }
}
