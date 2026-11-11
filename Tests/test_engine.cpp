// The block processor (PLAN.md §3.2, §3.3, §7.1 test_engine): LatticeEngine under the fake host,
// each scenario swept over 44.1/48/96 kHz, 32-2048-sample blocks and three tempos (FakeHost.h).
//
//   same sample    gather 0: a chord on one sample comes out transformed on that sample
//   window         5-80 ms spreads inside the window: the chord sounds when the window closes
//   late notes     a note after the window retunes the held chord: only the voices that change
//                  get a note-off and note-on, on the sample the new window closes
//   release        each key releases its own image and nothing else; velocities carry across
//   mix/channels   Source + Generated (source at once, generated at the window), Source only,
//                  both on one channel (a common tone sounds once), any channels
//   zone           operator keys never sound
//   changes        a new word or voicing while a chord is held retunes it; a Mix or channel
//                  change sounds it again; stop and Enable off end everything; Enable on again
//                  ends what was passed through
//   Mirror words   P, L, R, N, PL on I-V-vi-IV (the Live check's scenario) against the voicer
//   Walk           operator keys edit W (P, L, +5, Undo, Home), keys on one sample retune once,
//                  Relative / Reset, Explore as Walk, Mirror ignores them, a full W refuses ops
//   Latch          a tapped chord sounds on until the next one's window closes or latch goes off;
//                  stop ends it and the transport starting sounds it again; mode changes resound;
//                  a restored W and latched chord
#include "FakeHost.h"
#include "core/engine/LatticeEngine.h"
#include <map>
#include <set>

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

    struct Out
    {
        int64_t sample;
        MidiEvent e;
    };

    // A fake host around one engine. `before` runs before each block (settings changes by ppq).
    struct Run
    {
        std::unique_ptr<LatticeEngine> engine = std::make_unique<LatticeEngine>();
        EngineSettings s;
        Host h;
        std::vector<Out> events;
        std::function<void (const Transport&, EngineSettings&)> before;

        explicit Run (const Config& c) : h (c.rate, c.bpm)
        {
            s.gatherMs = 0;
            h.process = [this] (const Transport& t, const MidiEvent* in, int n, int len, BlockEvents& out)
            {
                if (before)
                    before (t, s);
                engine->process (t, in, n, len, s, out);
                for (const auto& e : out)
                    events.push_back ({ h.pos + e.sample, e });
            };
        }

        int64_t window() const { return gatherSamples (s.gatherMs, h.sampleRate); }

        // Keys down at `beats` for `length` beats.
        void chord (double beats, double length, std::initializer_list<int> notes, int velocity = 100)
        {
            for (int n : notes)
            {
                h.key (beats, n, true, velocity);
                h.key (beats + length, n, false);
            }
        }

        // Plays to `beats` (plus a little), then stops.
        void play (double beats, int block)
        {
            h.run (h.at (beats) + 1, block);
            h.stop (block);
        }

        bool allOnChannel (int channel, bool notesOnly = true) const
        {
            for (const auto& o : events)
                if ((! notesOnly || o.e.isNoteOn() || o.e.isNoteOff()) && o.e.channel() != channel)
                    return false;
            return true;
        }
    };

    void sameSample()
    {
        sweep ("gather 0: C E G on one sample comes out as P(C E G) = C Eb G on that sample, channel 2", [] (const Config& c, int64_t& worst)
        {
            Run r (c);
            r.s.word = word ("P");
            r.chord (1.0, 1.0, { 60, 64, 67 });
            r.chord (2.5, 0.75, { 62, 65, 69 }, 77);
            r.play (4.0, c.block);
            Expect e (r.h.spq());
            e.note (1.0, 2.0, 60); e.note (1.0, 2.0, 63); e.note (1.0, 2.0, 67);
            // D minor under P: D F# A
            e.note (2.5, 3.25, 62, 77); e.note (2.5, 3.25, 66, 77); e.note (2.5, 3.25, 69, 77);
            std::string why = finish (r.h, e, worst, 0);
            if (! r.allOnChannel (1)) why += " not all on channel 2;";
            return why;
        });
    }

    void windows()
    {
        for (int ms : { 5, 20, 40, 80 })
            sweep ("gather " + std::to_string (ms) + " ms: keys spread over the window sound together when it closes", [ms] (const Config& c, int64_t& worst)
            {
                Run r (c);
                r.s.gatherMs = ms;
                r.s.word = word ("L");
                const int64_t w = r.window();
                const int64_t t0 = r.h.at (1.0);
                // Spread across the window: at 0, a third and the last sample of it.
                r.h.keyAt (t0, 67, true, 90);
                r.h.keyAt (t0 + w / 3, 60, true, 80);
                r.h.keyAt (t0 + w - 1, 64, true, 70);
                const int64_t up = r.h.at (2.0);
                for (int n : { 60, 64, 67 }) r.h.keyAt (up, n, false);
                r.play (3.0, c.block);
                Expect e (r.h.spq());
                // L on C major: E minor (B E G); C (velocity 80) goes to B.
                e.onAt (t0 + w, 59, 80); e.onAt (t0 + w, 64, 70); e.onAt (t0 + w, 67, 90);
                for (int n : { 59, 64, 67 }) e.offAt (up, n);
                return finish (r.h, e, worst, 0);
            });

        sweep ("gather 20 ms: a key down and up inside the window never sounds", [] (const Config& c, int64_t& worst)
        {
            Run r (c);
            r.s.gatherMs = 20;
            r.s.word = word ("P");
            const int64_t t0 = r.h.at (1.0), w = r.window();
            r.h.keyAt (t0, 60, true);
            r.h.keyAt (t0 + 1, 72, true);
            r.h.keyAt (t0 + w / 2, 72, false);
            r.h.keyAt (t0 + 2, 64, true);
            r.h.keyAt (t0 + 3, 67, true);
            for (int n : { 60, 64, 67 }) r.h.key (2.0, n, false);
            r.play (3.0, c.block);
            Expect e (r.h.spq());
            for (int n : { 60, 63, 67 }) { e.onAt (t0 + w, n); e.off (2.0, n); }
            return finish (r.h, e, worst, 0);
        });
    }

    void lateNotes()
    {
        sweep ("late note: C E, then G after the window: only E retunes (E off, Eb on), C stays, G sounds", [] (const Config& c, int64_t& worst)
        {
            Run r (c);
            r.s.gatherMs = 20;
            r.s.word = word ("P");
            const int64_t w = r.window();
            r.h.key (1.0, 60, true, 100);
            r.h.key (1.0, 64, true, 90);
            r.h.key (1.5, 67, true, 80);
            for (int n : { 60, 64, 67 }) r.h.key (3.0, n, false);
            r.play (4.0, c.block);
            Expect e (r.h.spq());
            const int64_t a = r.h.at (1.0) + w, b = r.h.at (1.5) + w;
            // C E: P needs a fifth; unavailable, the dyad goes through.
            e.onAt (a, 60, 100); e.onAt (a, 64, 90);
            e.offAt (b, 64); e.onAt (b, 63, 90); e.onAt (b, 67, 80);
            for (int n : { 60, 63, 67 }) e.off (3.0, n);
            return finish (r.h, e, worst, 0);
        });

        sweep ("late note at gather 0: C E G, then Bb: C7 under P (C Eb G A): Bb sounds as A, nothing else moves", [] (const Config& c, int64_t& worst)
        {
            Run r (c);
            r.s.word = word ("P");
            r.chord (1.0, 2.0, { 60, 64, 67 });
            r.h.key (2.0, 70, true, 50);
            r.h.key (3.0, 70, false);
            r.play (4.0, c.block);
            Expect e (r.h.spq());
            e.note (1.0, 3.0, 60); e.note (1.0, 3.0, 63); e.note (1.0, 3.0, 67);
            e.note (2.0, 3.0, 69, 50);
            return finish (r.h, e, worst, 0);
        });
    }

    void release()
    {
        sweep ("a key let go on the very sample its window closes: its image sounds and ends there, in that order", [] (const Config& c, int64_t& worst)
        {
            Run r (c);
            r.s.gatherMs = 20;
            r.s.word = word ("P");
            const int64_t t0 = r.h.at (1.0), w = r.window();
            for (int n : { 60, 64, 67 }) r.h.keyAt (t0, n, true);
            r.h.keyAt (t0 + w, 64, false);
            for (int n : { 60, 67 }) r.h.key (2.0, n, false);
            r.play (3.0, c.block);
            Expect e (r.h.spq());
            for (int n : { 60, 67 }) { e.onAt (t0 + w, n); e.off (2.0, n); }
            e.onAt (t0 + w, 63); e.offAt (t0 + w, 63);
            return finish (r.h, e, worst, 0);
        });

        sweep ("per-voice release: each key ends its own image; velocities carry", [] (const Config& c, int64_t& worst)
        {
            Run r (c);
            r.s.word = word ("R");
            r.h.key (1.0, 60, true, 30);
            r.h.key (1.0, 64, true, 60);
            r.h.key (1.0, 67, true, 120);
            r.h.key (1.5, 67, false);       // G's image is A
            r.h.key (2.0, 60, false);
            r.h.key (2.5, 64, false);
            r.play (3.0, c.block);
            Expect e (r.h.spq());
            e.note (1.0, 2.0, 60, 30); e.note (1.0, 2.5, 64, 60); e.note (1.0, 1.5, 69, 120);
            return finish (r.h, e, worst, 0);
        });
    }

    void mixAndChannels()
    {
        sweep ("Source + Generated: source at once on channel 1, generated at the window on channel 2", [] (const Config& c, int64_t& worst)
        {
            Run r (c);
            r.s.gatherMs = 20;
            r.s.mix = Mix::SourceAndGenerated;
            r.s.word = word ("P");
            r.chord (1.0, 1.0, { 60, 64, 67 });
            r.play (3.0, c.block);
            const int64_t a = r.h.at (1.0);
            Expect e (r.h.spq());
            for (int n : { 60, 64, 67 }) { e.onAt (a, n); e.off (2.0, n); }
            for (int n : { 60, 63, 67 }) { e.onAt (a + r.window(), n); e.off (2.0, n); }
            std::string why = finish (r.h, e, worst, 0);
            for (const auto& o : r.events)
                if ((o.e.isNoteOn() && o.sample == a && o.e.channel() != 0) || (o.e.isNoteOn() && o.sample > a && o.e.channel() != 1))
                    why += " wrong channel;";
            return why;
        });

        sweep ("Source only: the source on channel 1 at once, nothing generated", [] (const Config& c, int64_t& worst)
        {
            Run r (c);
            r.s.gatherMs = 20;
            r.s.mix = Mix::SourceOnly;
            r.s.word = word ("P");
            r.chord (1.0, 1.0, { 60, 64, 67 });
            r.play (3.0, c.block);
            Expect e (r.h.spq());
            for (int n : { 60, 64, 67 }) e.note (1.0, 2.0, n);
            std::string why = finish (r.h, e, worst, 0);
            if (! r.allOnChannel (0)) why += " not all on channel 1;";
            return why;
        });

        sweep ("Source + Generated on one channel (5): the common tones C and G sound once", [] (const Config& c, int64_t& worst)
        {
            Run r (c);
            r.s.mix = Mix::SourceAndGenerated;
            r.s.sourceChannel = r.s.genChannel = 4;
            r.s.word = word ("P");
            r.chord (1.0, 1.0, { 60, 64, 67 });
            r.play (3.0, c.block);
            Expect e (r.h.spq());
            for (int n : { 60, 63, 64, 67 }) e.note (1.0, 2.0, n);
            std::string why = finish (r.h, e, worst, 0);
            if (! r.allOnChannel (4)) why += " not all on channel 5;";
            return why;
        });

        sweep ("channels 16 and 9; controllers and pitch bend pass through untouched", [] (const Config& c, int64_t& worst)
        {
            Run r (c);
            r.s.mix = Mix::SourceAndGenerated;
            r.s.sourceChannel = 15;
            r.s.genChannel = 8;
            r.s.word = word ("L");
            r.chord (1.0, 1.0, { 60, 64, 67 });
            r.h.input.push_back ({ r.h.at (1.25), MidiEvent::controller (3, 64, 127, 0) });
            r.play (3.0, c.block);
            Expect e (r.h.spq());
            for (int n : { 60, 64, 67 }) e.note (1.0, 2.0, n);
            for (int n : { 59, 64, 67 }) e.note (1.0, 2.0, n);
            std::string why = finish (r.h, e, worst, 0);
            int cc = 0;
            std::multiset<std::pair<int, int>> ons;
            for (const auto& o : r.events)
            {
                if (o.e.isController (64) && o.e.channel() == 3 && o.e.data[2] == 127 && o.sample == r.h.at (1.25)) ++cc;
                if (o.e.isNoteOn()) ons.insert ({ o.e.channel(), o.e.note() });
            }
            if (cc != 1) why += " the controller didn't pass;";
            if (ons != std::multiset<std::pair<int, int>> { { 15, 60 }, { 15, 64 }, { 15, 67 }, { 8, 59 }, { 8, 64 }, { 8, 67 } })
                why += " wrong channels;";
            return why;
        });
    }

    void zone()
    {
        sweep ("operator zone (C0-B0 here): its keys never sound, the chord is still voiced", [] (const Config& c, int64_t& worst)
        {
            Run r (c);
            r.s.zone.on = true;
            r.s.mix = Mix::SourceAndGenerated;
            r.s.word = word ("P");
            r.chord (1.0, 1.0, { 26, 60, 64, 67 });
            r.chord (1.5, 0.25, { 24, 35 });
            r.play (3.0, c.block);
            Expect e (r.h.spq());
            for (int n : { 60, 64, 67 }) e.note (1.0, 2.0, n);
            for (int n : { 60, 63, 67 }) e.note (1.0, 2.0, n);
            return finish (r.h, e, worst, 0);
        });
    }

    void changes()
    {
        sweep ("the word changes while a chord is held (P to PL): only the voice that moves retunes, on the block's first sample", [] (const Config& c, int64_t& worst)
        {
            Run r (c);
            r.s.word = word ("P");
            int64_t changedAt = -1;
            r.before = [&r, &changedAt] (const Transport& t, EngineSettings& s)
            {
                if (t.ppq >= 2.0 && changedAt < 0)
                {
                    s.word = word ("PL");
                    changedAt = r.h.pos;
                }
            };
            r.chord (1.0, 2.0, { 60, 64, 67 });
            r.play (4.0, c.block);
            Expect e (r.h.spq());
            e.note (1.0, 3.0, 60); e.note (1.0, 3.0, 63);
            e.on (1.0, 67); e.offAt (changedAt, 67); e.onAt (changedAt, 68); e.off (3.0, 68);
            return finish (r.h, e, worst, 0);
        });

        sweep ("Mix changes while held (Generated to Source + Generated): everything ends and the chord sounds again", [] (const Config& c, int64_t& worst)
        {
            Run r (c);
            r.s.word = word ("P");
            int64_t changedAt = -1;
            r.before = [&r, &changedAt] (const Transport& t, EngineSettings& s)
            {
                if (t.ppq >= 2.0 && changedAt < 0)
                {
                    s.mix = Mix::SourceAndGenerated;
                    changedAt = r.h.pos;
                }
            };
            r.chord (1.0, 2.0, { 60, 64, 67 });
            r.play (4.0, c.block);
            Expect e (r.h.spq());
            for (int n : { 60, 63, 67 }) { e.on (1.0, n); e.offAt (changedAt, n); e.onAt (changedAt, n); e.off (3.0, n); }
            for (int n : { 60, 64, 67 }) { e.onAt (changedAt, n); e.off (3.0, n); }
            return finish (r.h, e, worst, 0);
        });

        sweep ("stop ends everything; the held keys' note-offs are ignored", [] (const Config& c, int64_t& worst)
        {
            Run r (c);
            r.s.mix = Mix::SourceAndGenerated;
            r.s.word = word ("N");
            r.chord (1.0, 8.0, { 60, 64, 67 });
            r.h.run (r.h.at (2.0), c.block);
            const int64_t stopAt = r.h.pos;
            r.h.stop (c.block);
            r.h.playing = true;
            r.h.run (r.h.at (10.0), c.block);
            Expect e (r.h.spq());
            for (int n : { 60, 64, 67 }) { e.on (1.0, n); e.offAt (stopAt, n); }
            for (int n : { 60, 65, 68 }) { e.on (1.0, n); e.offAt (stopAt, n); }
            return finish (r.h, e, worst, 0);
        });

        sweep ("Enable off ends what sounds and passes MIDI through; Enable on again ends what passed", [] (const Config& c, int64_t& worst)
        {
            Run r (c);
            r.s.word = word ("P");
            int64_t offAt = -1, onAt = -1;
            r.before = [&r, &offAt, &onAt] (const Transport& t, EngineSettings& s)
            {
                if (t.ppq >= 2.0 && offAt < 0) { s.enabled = false; offAt = r.h.pos; }
                if (t.ppq >= 3.0 && onAt < 0) { s.enabled = true; onAt = r.h.pos; }
            };
            r.chord (1.0, 1.5, { 60, 64, 67 });    // up at 2.5, while off: no stray note-offs
            r.chord (2.75, 2.0, { 62, 65 });       // down while off, up after: ended at Enable on
            r.play (6.0, c.block);
            Expect e (r.h.spq());
            for (int n : { 60, 63, 67 }) { e.on (1.0, n); e.offAt (offAt, n); }
            for (int n : { 62, 65 }) { e.on (2.75, n); e.offAt (onAt, n); }
            return finish (r.h, e, worst, 0);
        });
    }

    // The Live check's scenario (PLAN.md §6 row 2): I-V-vi-IV in C, one bar each, from beat 4;
    // every slot word against the voicer.
    void mirrorWords()
    {
        const std::vector<std::vector<int>> chords { { 60, 64, 67 }, { 55, 59, 62 }, { 57, 60, 64 }, { 53, 57, 60 } };
        for (const char* w : { "P", "L", "R", "N", "PL", "+5", "S H" })
            sweep (std::string ("Mirror ") + w + " on I-V-vi-IV equals the voicer, every chord", [w, &chords] (const Config& c, int64_t& worst)
            {
                Run r (c);
                r.s.word = word (w);
                for (size_t i = 0; i < chords.size(); ++i)
                    for (int n : chords[i])
                    {
                        r.h.key (4.0 + 4.0 * (double) i, n, true, 100 - (int) i * 10);
                        r.h.key (4.0 + 4.0 * (double) i + 3.5, n, false);
                    }
                r.play (21.0, c.block);
                Expect e (r.h.spq());
                for (size_t i = 0; i < chords.size(); ++i)
                {
                    Voicing in, out;
                    for (int n : chords[i]) in.push (n, n);
                    voiceWord (r.s.word, inputChord (in), in, {}, out);
                    for (const auto& v : out)
                        e.note (4.0 + 4.0 * (double) i, 4.0 + 4.0 * (double) i + 3.5, v.pitch, 100 - (int) i * 10);
                }
                return finish (r.h, e, worst, 0);
            });
    }

    // Phase 3 (PLAN.md §3.1): Walk, latch, modes.
    std::map<int, int> voicingOf (const char* w, std::initializer_list<int> keys, Mode mode = Mode::Walk)
    {
        (void) mode;
        Voicing in, out;
        for (int n : keys)
            in.push (n, n);
        voiceWord (word (w), inputChord (in), in, {}, out);
        std::map<int, int> m;
        for (const auto& v : out)
            m[v.owner] = v.pitch;
        return m;
    }

    // A chord whose voicing changes at the given samples: changed voices end and start there.
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

    void opKey (Run& r, double beats, int index) { r.h.key (beats, 24 + index, true); r.h.key (beats + 0.1, 24 + index, false); }

    void walk()
    {
        sweep ("Walk: operator keys P, L, +5, Undo, Home on a held C E G: each retunes only the voices that move, on its sample", [] (const Config& c, int64_t& worst)
        {
            Run r (c);
            r.s.mode = Mode::Walk;
            r.s.zone.on = true;
            r.chord (1.0, 8.0, { 60, 64, 67 });
            opKey (r, 2.0, 0);            // P
            opKey (r, 3.0, 1);            // L
            opKey (r, 4.0, 6);            // +5
            opKey (r, 5.0, opkey::undo);  // back to P L
            opKey (r, 6.0, opkey::home);  // back to the chord as played
            r.play (10.0, c.block);
            const std::initializer_list<int> k { 60, 64, 67 };
            Expect e (r.h.spq());
            expectSteps (e, { { r.h.at (1.0), voicingOf ("", k) }, { r.h.at (2.0), voicingOf ("P", k) }, { r.h.at (3.0), voicingOf ("P L", k) },
                              { r.h.at (4.0), voicingOf ("P L +5", k) }, { r.h.at (5.0), voicingOf ("P L", k) }, { r.h.at (6.0), voicingOf ("", k) } },
                         r.h.at (9.0));
            std::string why = finish (r.h, e, worst, 0);
            if (! r.engine->walkState().word.empty())
                why += " W not empty after Home;";
            return why;
        });
        sweep ("Walk: P and L pressed on one sample give one retune (no zero-length notes); P twice cancels", [] (const Config& c, int64_t& worst)
        {
            Run r (c);
            r.s.mode = Mode::Walk;
            r.s.zone.on = true;
            r.chord (1.0, 4.0, { 60, 64, 67 });
            r.h.key (2.0, 24, true);
            r.h.key (2.0, 25, true);
            r.h.key (3.0, 25, true);      // L again: cancels L (W = P)
            r.play (6.0, c.block);
            const std::initializer_list<int> k { 60, 64, 67 };
            Expect e (r.h.spq());
            expectSteps (e, { { r.h.at (1.0), voicingOf ("", k) }, { r.h.at (2.0), voicingOf ("P L", k) }, { r.h.at (3.0), voicingOf ("P", k) } }, r.h.at (5.0));
            return finish (r.h, e, worst, 0);
        });
        for (const bool reset : { false, true })
            sweep (std::string ("Walk input ") + (reset ? "Reset: a new chord clears W" : "Relative: a new chord keeps W") + "; a late note does not count as new",
                   [reset] (const Config& c, int64_t& worst)
                   {
                       Run r (c);
                       r.s.mode = Mode::Walk;
                       r.s.zone.on = true;
                       r.s.walkInput = reset ? WalkInput::Reset : WalkInput::Relative;
                       r.chord (1.0, 2.0, { 60, 64 });
                       opKey (r, 1.25, 0);                    // P
                       r.chord (1.5, 1.5, { 67 });            // late: still the first chord
                       r.chord (4.0, 2.0, { 65, 69, 72 });    // new
                       r.play (7.0, c.block);
                       Expect e (r.h.spq());
                       expectSteps (e, { { r.h.at (1.0), voicingOf ("", { 60, 64 }) }, { r.h.at (1.25), voicingOf ("P", { 60, 64 }) },
                                         { r.h.at (1.5), voicingOf ("P", { 60, 64, 67 }) } }, r.h.at (3.0));
                       expectSteps (e, { { r.h.at (4.0), voicingOf (reset ? "" : "P", { 65, 69, 72 }) } }, r.h.at (6.0));
                       return finish (r.h, e, worst, 0);
                   });
        sweep ("Explore takes the operator keys as Walk does; Mirror ignores them", [] (const Config& c, int64_t& worst)
        {
            Run r (c);
            r.s.mode = Mode::Explore;
            r.s.zone.on = true;
            r.s.word = word ("R");
            r.chord (1.0, 2.0, { 60, 64, 67 });
            opKey (r, 1.5, 2);                // R
            bool switched = false;
            r.before = [&switched] (const Transport& t, EngineSettings& s)
            {
                if (t.ppq >= 3.5 && ! switched) { s.mode = Mode::Mirror; switched = true; }
            };
            r.chord (4.0, 2.0, { 62, 65, 69 });
            opKey (r, 5.0, 0);                // P: nothing in Mirror
            r.play (7.0, c.block);
            Expect e (r.h.spq());
            expectSteps (e, { { r.h.at (1.0), voicingOf ("", { 60, 64, 67 }) }, { r.h.at (1.5), voicingOf ("R", { 60, 64, 67 }) } }, r.h.at (3.0));
            expectSteps (e, { { r.h.at (4.0), voicingOf ("R", { 62, 65, 69 }) } }, r.h.at (6.0));
            return finish (r.h, e, worst, 0);
        });
        {
            Run r (configs()[0]);
            r.s.mode = Mode::Walk;
            r.s.zone.on = true;
            for (int i = 0; i < 40; ++i)
                opKey (r, 0.1 + 0.2 * i, 6);  // +5, 40 times
            r.play (10.0, 64);
            check (r.engine->walkState().word.size == maxWordOps && r.h.clean(), "a full W (32 ops) takes no more ops: 40 presses of +5 leave " + std::to_string (r.engine->walkState().word.size));
        }
    }

    void latch()
    {
        sweep ("Latch: a tapped chord sounds on (source and generated) until the next chord's window closes; latch off ends it", [] (const Config& c, int64_t& worst)
        {
            Run r (c);
            r.s.latch = true;
            r.s.gatherMs = 20;
            r.s.mix = Mix::SourceAndGenerated;
            r.s.word = word ("P");
            const int64_t w = gatherSamples (20, c.rate);
            r.chord (1.0, 0.25, { 60, 64, 67 });
            r.chord (3.0, 0.25, { 62, 65, 69 });
            int64_t offAt = -1;
            r.before = [&r, &offAt] (const Transport& t, EngineSettings& s)
            {
                if (t.ppq >= 5.0 && offAt < 0) { s.latch = false; offAt = r.h.pos; }
            };
            r.play (7.0, c.block);
            Expect e (r.h.spq());
            // Source at once; generated when the window closes; the first chord's notes go when the second's window closes.
            for (int n : { 60, 64, 67 }) { e.on (1.0, n); e.offAt (r.h.at (3.0) + w, n); }
            for (const auto& [o, p] : voicingOf ("P", { 60, 64, 67 })) { e.onAt (r.h.at (1.0) + w, p); e.offAt (r.h.at (3.0) + w, p); }
            for (int n : { 62, 65, 69 }) { e.on (3.0, n); e.offAt (offAt, n); }
            for (const auto& [o, p] : voicingOf ("P", { 62, 65, 69 })) { e.onAt (r.h.at (3.0) + w, p); e.offAt (offAt, p); }
            return finish (r.h, e, worst, 0);
        });
        sweep ("Latch: stop ends the latched chord, the transport starting again sounds it; operator keys walk it", [] (const Config& c, int64_t& worst)
        {
            Run r (c);
            r.s.latch = true;
            r.s.mode = Mode::Walk;
            r.s.zone.on = true;
            r.chord (1.0, 0.5, { 60, 64, 67 });
            opKey (r, 2.0, 1);                // L on the latched chord
            r.h.run (r.h.at (3.0), c.block);
            const int64_t stopAt = r.h.pos;
            r.h.stop (c.block);
            r.h.run (r.h.pos + 5 * c.block, c.block);    // stopped a while
            r.h.playing = true;
            const int64_t startAt = r.h.pos;
            r.h.run (startAt + r.h.at (1.0), c.block);
            r.s.latch = false;
            const int64_t endAt = r.h.pos;
            r.h.run (endAt + c.block, c.block);
            r.h.stop (c.block);
            const std::initializer_list<int> k { 60, 64, 67 };
            Expect e (r.h.spq());
            expectSteps (e, { { r.h.at (1.0), voicingOf ("", k) }, { r.h.at (2.0), voicingOf ("L", k) } }, stopAt);
            expectSteps (e, { { startAt, voicingOf ("L", k) } }, endAt);
            return finish (r.h, e, worst, 0);
        });
        sweep ("mode change while held (Mirror P to Walk): everything ends and the chord sounds again under the new W", [] (const Config& c, int64_t& worst)
        {
            Run r (c);
            r.s.word = word ("P");
            int64_t changedAt = -1;
            r.before = [&r, &changedAt] (const Transport& t, EngineSettings& s)
            {
                if (t.ppq >= 2.0 && changedAt < 0) { s.mode = Mode::Walk; changedAt = r.h.pos; }
            };
            r.chord (1.0, 2.0, { 60, 64, 67 });
            r.play (4.0, c.block);
            Expect e (r.h.spq());
            for (int n : { 60, 63, 67 }) { e.on (1.0, n); e.offAt (changedAt, n); }
            for (int n : { 60, 64, 67 }) { e.onAt (changedAt, n); e.off (3.0, n); }
            return finish (r.h, e, worst, 0);
        });
        sweep ("restored state: Walk's W and a latched chord come back; the chord sounds when the transport starts", [] (const Config& c, int64_t& worst)
        {
            Run r (c);
            r.s.mode = Mode::Walk;
            r.s.latch = true;
            WalkState w;
            parseWord ("P L", w.word);
            w.latched[57] = 90;
            w.latched[60] = 90;
            w.latched[64] = 90;
            BlockEvents none;
            r.h.playing = false;
            r.h.run (4 * c.block, c.block);
            r.engine->restore (w, none);
            r.h.run (8 * c.block, c.block);
            const bool silentWhileStopped = r.h.hits.empty();
            r.h.playing = true;
            const int64_t startAt = r.h.pos;
            r.h.run (startAt + r.h.at (1.0), c.block);
            r.h.stop (c.block);
            Expect e (r.h.spq());
            expectSteps (e, { { startAt, voicingOf ("P L", { 57, 60, 64 }) } }, startAt + r.h.at (1.0), 90);
            std::string why = finish (r.h, e, worst, 0);
            if (! silentWhileStopped) why += " sounded while stopped;";
            const auto back = r.engine->walkState();
            if (printWord (back.word).c_str() != std::string ("P L") || back.latched[57] != 90 || back.latched[60] != 90 || back.latched[64] != 90)
                why += " state not kept;";
            return why;
        });
    }

}

int main()
{
    std::cout << "test_engine\n";
    sameSample();
    windows();
    lateNotes();
    release();
    mixAndChannels();
    zone();
    changes();
    mirrorWords();
    walk();
    latch();
    return testutil::finish ("test_engine");
}
