// Capture and export (PLAN.md §3.5, §7.1 test_capture; Phase 5):
//
//   queue      the SPSC queue keeps order and refuses when full, counting what it dropped; the
//              model then ends every open note and ignores the note-offs still to come, and
//              captures normally again afterwards
//   stamps     playing: a note's beat is the host's ppq at its sample (from ppq 0: equal), over
//              32-2048-sample blocks, 44.1/48/96 kHz and a tempo change; stopped: the tape runs
//              on at the host's tempo; a start, a locate and a loop wrap move it on to the
//              host's place in the bar, never back; whole-bar loops unroll with no gap
//   streams    generated notes (the generated channel) always; source keys only with
//              captureSource, never operator keys, whatever Mix sends, and never twice when
//              Mix sends them on the generated channel; nothing new while disabled, but every
//              captured note still ends
//   model      the last 64 bars (older notes go), the take (arming, notes sounding at the
//              arming, cut at disarming, kept until the next arming), `current`
//   engine     the real LatticeEngine (Mirror P L, Walk with operator keys): the As played file
//              equals what the engine sent, note for note, to the tick (G6 offline)
//   renderings As played (exact) and Chords (a hand-spread chord is one chord; snapped to the
//              sixteenth; overlaps cut; one block chord per change)
//   .mid       round trip: type 1, 960 ppq, tempo, meter, key signature (all 24 keys), track
//              names, notes; a source track only when there are source notes
//   allocation none in CaptureWriter::process (libmalloc's logger hook)
#include "TestUtil.h"
#include "core/engine/LatticeEngine.h"
#include "core/midiio/CaptureModel.h"
#include "core/midiio/CaptureQueue.h"
#include "core/midiio/MidiFile.h"
#include <atomic>
#include <cmath>
#include <memory>
#include <pthread.h>
#include <sstream>
#include <vector>

using namespace tonnetz;
using testutil::check;

extern "C" void (*malloc_logger) (uint32_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uint32_t);

namespace
{
    std::atomic<bool> watching { false };
    std::atomic<pthread_t> watched { nullptr };
    std::atomic<int> allocations { 0 };
    void* volatile sink = nullptr;

    void logger (uint32_t type, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uint32_t)
    {
        if ((type & 2) != 0 && watching.load (std::memory_order_relaxed) && watched.load (std::memory_order_relaxed) == pthread_self())
            allocations.fetch_add (1, std::memory_order_relaxed);
    }

    std::string str (double x)
    {
        std::ostringstream o;
        o.precision (9);
        o << x;
        return o.str();
    }

    // A writer and model with a hand-driven transport.
    struct Cap
    {
        std::unique_ptr<CaptureQueue> queue = std::make_unique<CaptureQueue>();
        std::unique_ptr<CaptureWriter> writer = std::make_unique<CaptureWriter> (*queue);
        CaptureModel model;
        Transport t;
        CaptureSettings s;

        Cap() { t.sampleRate = 48000.0; t.bpm = 120.0; }

        // One block: `out` as the engine's output, `in` as its input; advances ppq if playing.
        void block (int n, std::vector<MidiEvent> out = {}, std::vector<MidiEvent> in = {}, bool drain = true)
        {
            writer->process (t, n, in.data(), (int) in.size(), out.data(), (int) out.size(), s);
            if (t.playing)
                t.ppq += n * t.bpm / (60.0 * t.sampleRate);
            if (drain)
                model.drain (*queue);
        }
    };

    //==========================================================================
    void testQueue()
    {
        Cap c;
        c.t.playing = true;
        // Fill the queue without draining: 16 384 note-ons fit, the rest are dropped and counted.
        int pushed = 0;
        for (int b = 0; b < 140; ++b)
        {
            std::vector<MidiEvent> out;
            for (int i = 0; i < 128; ++i)
                out.push_back (MidiEvent::noteOn (1, i, 100, i));
            std::vector<MidiEvent> offs;
            for (int i = 0; i < 128; ++i)
                offs.push_back (MidiEvent::noteOff (1, i, 200 + i));
            out.insert (out.end(), offs.begin(), offs.end());
            c.block (512, out, {}, false);
            pushed += 256;
        }
        // 64 blocks fill it; after that each block's 128 note-ons are dropped (and their note-offs,
        // whose note-ons were never captured, aren't tried).
        check (c.queue->dropped() == (uint32_t) ((140 - CaptureQueue::capacity / 256) * 128),
               "a full queue drops what doesn't fit and counts it (" + std::to_string (c.queue->dropped()) + " of " + std::to_string (pushed) + " events)");

        // An open note when the queue overflows: it ends, its late note-off is ignored.
        c.model.drain (*c.queue);
        check (c.model.lossCount() == 1, "the model sees the loss once");
        int open = 0, total = 0;
        for (const auto& n : c.model.retrospective())
        {
            ++total;
            open += n.open ? 1 : 0;
        }
        check (open == 0 && total == CaptureQueue::capacity / 2,
               "after a loss nothing is left open (" + std::to_string (total) + " notes kept, " + std::to_string (open) + " open)");

        // Order is kept, and capture works normally afterwards.
        Cap d;
        d.t.playing = true;
        d.block (480, { MidiEvent::noteOn (1, 60, 90, 10) });
        d.block (480);
        d.block (480, { MidiEvent::noteOff (1, 60, 20) });
        auto notes = d.model.retrospective();
        check (notes.size() == 1 && ! notes[0].open && notes[0].velocity == 90 && notes[0].pitch == 60,
               "a note-on and its note-off two blocks later make one note");

        // Overflow with a note open across it: its note-off later finds nothing.
        Cap e;
        e.t.playing = true;
        e.block (64, { MidiEvent::noteOn (1, 40, 100, 0) });
        for (int b = 0; b < 70; ++b)
        {
            std::vector<MidiEvent> out;
            for (int i = 0; i < 128; ++i)
                if (i != 40 && i != 72)
                    out.push_back (MidiEvent::noteOn (1, i, 100, 0));
            for (int i = 0; i < 128; ++i)
                if (i != 40 && i != 72)
                    out.push_back (MidiEvent::noteOff (1, i, 1));
            e.block (64, out, {}, false);
        }
        e.model.drain (*e.queue);
        e.block (64, { MidiEvent::noteOff (1, 40, 0) });
        e.block (64, { MidiEvent::noteOn (1, 72, 100, 0) });
        e.block (64, { MidiEvent::noteOff (1, 72, 0) });
        int openAfter = 0;
        bool found72 = false;
        for (const auto& n : e.model.retrospective())
        {
            openAfter += n.open ? 1 : 0;
            found72 = found72 || (n.pitch == 72 && ! n.open && n.end > n.start);
        }
        check (openAfter == 0 && found72 && e.writer->openNotes() == 0, "after an overflow the next notes are captured whole, nothing stuck");
    }

    //==========================================================================
    // Stamps: one note-on per block at a fixed offset; its beat must be the host's ppq there.
    void testStampsPlaying()
    {
        double worst = 0.0;
        int configs = 0;
        for (double rate : { 44100.0, 48000.0, 96000.0 })
            for (int blockSize : { 32, 64, 256, 1000, 2048 })
            {
                Cap c;
                c.t.sampleRate = rate;
                c.t.playing = true;
                c.t.bpm = 97.0;
                std::vector<double> expected;
                for (int b = 0; b < 400; ++b)
                {
                    if (b == 200)
                        c.t.bpm = 151.0;         // a tempo change between blocks
                    const int at = (b * 7) % blockSize;
                    expected.push_back (c.t.ppq + at * c.t.bpm / (60.0 * rate));
                    c.block (blockSize, { MidiEvent::noteOn (1, 60 + b % 12, 100, at), MidiEvent::noteOff (1, 60 + b % 12, at) });
                }
                const auto notes = c.model.retrospective();
                if (notes.size() != expected.size())
                {
                    check (false, "stamps: every note captured at " + str (rate) + " Hz, " + std::to_string (blockSize));
                    continue;
                }
                for (size_t i = 0; i < notes.size(); ++i)
                    worst = std::max (worst, std::abs (notes[i].start - expected[i]));
                ++configs;
            }
        check (configs == 15 && worst < 1.0e-9, "playing from ppq 0: each stamp is the host's ppq at its sample, tempo change included, 15 rate x block configurations (worst " + str (worst) + " beat)");

        // Started mid-bar: the tape takes the host's place in the bar.
        Cap c;
        c.t.playing = true;
        c.t.ppq = 22.5;              // bar 6, beat 2.5
        c.t.hasBarStart = true;
        c.t.barStartPpq = 20.0;
        c.block (480, { MidiEvent::noteOn (1, 60, 100, 0) });
        c.block (480, { MidiEvent::noteOff (1, 60, 0) });
        const auto n = c.model.retrospective();
        check (n.size() == 1 && std::abs (n[0].start - 2.5) < 1.0e-9 && std::abs (n[0].end - (2.5 + 480.0 / 24000.0)) < 1.0e-9,
               "a first start at 22.5 (bar 6 + 2.5) lands at tape 2.5: the same place in the bar (" + (n.empty() ? std::string ("-") : str (n[0].start)) + ")");
    }

    void testStampsStoppedAndJumps()
    {
        Cap c;
        c.t.playing = true;
        // Play bars 0-3 (16 beats) in 500-sample blocks at 120 BPM, 48 kHz: 1/48 beat per... 500/24000.
        while (c.t.ppq < 13.2 - 1.0e-9)
            c.block (480);
        const double stopAt = c.t.ppq;    // 13.2
        c.t.playing = false;
        // Stopped: half a second of free-running tape (1 beat at 120 BPM), a note in it.
        c.block (480, { MidiEvent::noteOn (1, 50, 100, 240) });
        for (int i = 0; i < 49; ++i)
            c.block (480);
        c.block (480, { MidiEvent::noteOff (1, 50, 0) });
        auto notes = c.model.retrospective();
        check (notes.size() == 1 && std::abs (notes[0].start - (stopAt + 0.01)) < 1.0e-9 && std::abs (notes[0].end - (stopAt + 1.0)) < 1.0e-9,
               "stopped: the tape runs on from the stop at the host's tempo (note at " + (notes.empty() ? std::string ("-") : str (notes[0].start) + " - " + str (notes[0].end)) + ")");

        // Start again at 8.5 (bar 3, beat 0.5): the tape moves on to the next 0.5 into a bar.
        c.t.playing = true;
        c.t.ppq = 8.5;
        c.block (480, { MidiEvent::noteOn (1, 52, 100, 0) });
        c.block (480, { MidiEvent::noteOff (1, 52, 0) });
        notes = c.model.retrospective();
        check (notes.size() == 2 && std::abs (notes[1].start - 16.5) < 1.0e-9,
               "a restart at 8.5 after the tape reached 14.22 continues at 16.5, never back (" + (notes.size() < 2 ? std::string ("-") : str (notes[1].start)) + ")");

        // A locate back to 0 while playing: on to 20.0.
        c.t.ppq = 0.0;
        c.block (480, { MidiEvent::noteOn (1, 53, 100, 0) });
        c.block (480, { MidiEvent::noteOff (1, 53, 0) });
        notes = c.model.retrospective();
        check (notes.size() == 3 && std::abs (notes[2].start - 20.0) < 1.0e-9,
               "a locate to 0 at tape 16.54 continues at 20.0, the next bar line (" + (notes.size() < 3 ? std::string ("-") : str (notes[2].start)) + ")");

        // A whole-bar loop (4..12) that the host wraps inside a block: unrolled with no gap.
        Cap l;
        l.t.playing = true;
        l.t.ppq = 4.0;
        double unrolled = 0.0, worst = 0.0;     // a first start at a bar line starts the tape at 0
        int count = 0;
        for (int b = 0; b < 300; ++b)
        {
            const int at = 100;
            l.block (300, { MidiEvent::noteOn (1, 60, 100, at), MidiEvent::noteOff (1, 60, at + 1) });
            const double expect = unrolled + at / 24000.0;
            const auto ns = l.model.retrospective();
            if (! ns.empty())
                worst = std::max (worst, std::abs (ns.back().start - expect));
            ++count;
            unrolled += 300.0 / 24000.0;
            if (l.t.ppq >= 12.0)
                l.t.ppq -= 8.0;          // the wrap, as a host that doesn't split blocks
        }
        check (count == 300 && worst < 1.0e-6, "a 2-bar loop wrapped by the host unrolls on the tape with no gap (worst " + str (worst) + " beat over 300 blocks, 3.75 laps)");
    }

    //==========================================================================
    void testStreams()
    {
        Cap c;
        c.t.playing = true;
        c.s.zone.on = true;
        c.s.zone.start = 24;
        c.s.genChannel = 1;
        c.s.sourceChannel = 0;
        // Generated only: the source keys aren't captured; other channels aren't.
        c.block (100, { MidiEvent::noteOn (1, 63, 100, 5), MidiEvent::noteOn (4, 70, 100, 5) },
                 { MidiEvent::noteOn (0, 60, 100, 5), MidiEvent::noteOn (0, 24, 100, 6) });
        auto n = c.model.retrospective();
        check (n.size() == 1 && n[0].pitch == 63 && n[0].stream == CaptureStream::Generated, "captureSource off: only the generated channel's notes");

        // captureSource on: the source key, not the operator key (C0), on the source channel.
        c.s.captureSource = true;
        c.block (100, {}, { MidiEvent::noteOn (0, 64, 80, 1), MidiEvent::noteOn (0, 26, 100, 1), MidiEvent::noteOff (0, 24, 2) });
        n = c.model.retrospective();
        bool ok = n.size() == 2 && n[1].pitch == 64 && n[1].stream == CaptureStream::Source && n[1].channel == 0 && n[1].velocity == 80;
        check (ok, "captureSource on: the source key E, not the operator keys");

        // Mix sends source on the generated channel: not captured twice.
        Cap d;
        d.t.playing = true;
        d.s.captureSource = true;
        d.s.sendsSource = true;
        d.s.sourceChannel = d.s.genChannel = 1;
        d.block (100, { MidiEvent::noteOn (1, 60, 100, 0), MidiEvent::noteOn (1, 63, 100, 0) }, { MidiEvent::noteOn (0, 60, 100, 0) });
        n = d.model.retrospective();
        check (n.size() == 2 && n[0].stream == CaptureStream::Generated && n[1].stream == CaptureStream::Generated,
               "source and generated on one channel: what the channel carries, once");

        // Disabled: nothing new; but a note captured before still ends (Enable off's note-offs).
        Cap e;
        e.t.playing = true;
        e.s.captureSource = true;
        e.block (100, { MidiEvent::noteOn (1, 67, 100, 0) }, { MidiEvent::noteOn (0, 64, 100, 0) });
        e.s.enabled = false;
        e.s.sendsGenerated = false;
        e.block (100, { MidiEvent::noteOff (1, 67, 0), MidiEvent::noteOn (1, 69, 100, 0) }, { MidiEvent::noteOn (0, 65, 100, 0), MidiEvent::noteOff (0, 64, 3) });
        n = e.model.retrospective();
        int open = 0;
        for (const auto& x : n)
            open += x.open ? 1 : 0;
        check (n.size() == 2 && open == 0 && e.writer->openNotes() == 0, "Enable off: nothing new is captured, every captured note ends");
    }

    //==========================================================================
    void testModel()
    {
        Cap c;
        c.t.playing = true;
        // 80 bars of one chord per bar (4/4, 120 BPM, 48 kHz: 96 000 samples a bar).
        for (int bar = 0; bar < 80; ++bar)
        {
            c.block (100, { MidiEvent::noteOn (1, 60 + bar % 12, 100, 0) });
            c.block (95800);
            c.block (100, { MidiEvent::noteOff (1, 60 + bar % 12, 0) });
        }
        auto r = c.model.retrospective();
        check (r.size() == 64 && std::abs (r.front().start - 16 * 4.0) < 1.0e-6,
               "the last 64 bars are kept, older notes go (" + std::to_string (r.size()) + " notes from beat " + (r.empty() ? std::string ("-") : str (r.front().start)) + ")");
        check (! c.model.showingTake() && c.model.current().size() == 64 && c.model.bars() == 64, "with no take, current() is the last 64 bars, 64 bars long");

        // Arm while a note sounds: it joins at the arming; disarm cuts what still sounds.
        c.block (100, { MidiEvent::noteOn (1, 72, 100, 0) });
        c.block (24000);
        const double armAt = c.t.ppq;
        c.s.capture = true;
        c.block (100);
        c.block (100, { MidiEvent::noteOn (1, 76, 90, 0) });
        c.block (24000, { MidiEvent::noteOff (1, 72, 0) });
        const double disarmAt = c.t.ppq;
        c.s.capture = false;
        c.block (100);
        c.block (100, { MidiEvent::noteOff (1, 76, 0) });
        c.block (100, { MidiEvent::noteOn (1, 79, 90, 0), MidiEvent::noteOff (1, 79, 50) });
        const auto take = c.model.takeNotes();
        check (c.model.showingTake() && ! c.model.isArmed() && take.size() == 2, "the take: the notes from the arming to the disarming, kept after it");
        if (take.size() == 2)
        {
            check (std::abs (take[0].start - armAt) < 1.0e-9 && take[0].pitch == 72, "a note sounding at the arming joins the take there");
            check (std::abs (take[1].end - disarmAt) < 1.0e-9 && ! take[1].open && take[1].pitch == 76, "a note sounding at the disarming is cut there");
        }
        // A new arming starts a new take.
        c.s.capture = true;
        c.block (100);
        check (c.model.isArmed() && c.model.takeNotes().empty() && c.model.current().empty(), "arming again starts an empty take");
        c.block (100, { MidiEvent::noteOn (1, 48, 100, 0) });
        auto cur = c.model.current();
        check (cur.size() == 1 && cur[0].open && cur[0].end >= cur[0].start, "an open note in the take reaches to the present");
    }

    //==========================================================================
    // The real engine: what it sends is what the file says (G6, offline).
    struct EngineRun
    {
        std::unique_ptr<LatticeEngine> engine = std::make_unique<LatticeEngine>();
        Cap c;
        EngineSettings es;
        std::vector<CapturedNote> sent;          // from the engine's output, paired by hand
        std::array<int, 128> openAt {};
        std::array<int, 128> openVel {};

        EngineRun()
        {
            openAt.fill (-1);
            es.gatherMs = 0;
            es.zone.on = true;
            c.t.sampleRate = 44100.0;
            c.t.bpm = 120.0;
            c.t.playing = true;
            c.s.capture = true;
        }

        // Keys (beat, note, down) in time order; blocks of `blockSize`.
        void run (const std::vector<std::tuple<double, int, bool>>& keys, double until, int blockSize)
        {
            const double spb = c.t.sampleRate * 60.0 / c.t.bpm;
            size_t next = 0;
            int64_t pos = 0;
            BlockEvents out;
            while ((double) pos / spb < until)
            {
                std::vector<MidiEvent> in;
                while (next < keys.size() && (int64_t) std::llround (std::get<0> (keys[next]) * spb) < pos + blockSize)
                {
                    const auto [beat, note, down] = keys[next++];
                    const int at = (int) (std::llround (beat * spb) - pos);
                    in.push_back (down ? MidiEvent::noteOn (0, note, 100, at) : MidiEvent::noteOff (0, note, at));
                }
                out.clear();
                engine->process (c.t, in.data(), (int) in.size(), blockSize, es, out);
                for (const auto& e : out)
                {
                    if (e.channel() != es.genChannel)
                        continue;
                    const double beat = (double) (pos + e.sample) / spb;
                    if (e.isNoteOn())
                    {
                        openAt[(size_t) e.note()] = (int) sent.size();
                        sent.push_back ({ beat, beat, 0, (uint8_t) e.note(), (uint8_t) e.velocity(), (uint8_t) e.channel(), CaptureStream::Generated, false });
                    }
                    else if (e.isNoteOff() && openAt[(size_t) e.note()] >= 0)
                    {
                        sent[(size_t) openAt[(size_t) e.note()]].end = beat;
                        openAt[(size_t) e.note()] = -1;
                    }
                }
                c.s.sendsGenerated = es.hasGenerated();
                c.s.sendsSource = es.hasSource();
                c.s.genChannel = es.genChannel;
                c.s.sourceChannel = es.sourceChannel;
                c.s.zone = es.zone;
                c.writer->process (c.t, blockSize, in.data(), (int) in.size(), out.begin(), out.size(), c.s);
                c.t.ppq += blockSize / spb;
                pos += blockSize;
                c.model.drain (*c.queue);
            }
        }
    };

    bool sameAsSent (const MidiSong& song, const std::vector<CapturedNote>& sent, double origin, std::string& why)
    {
        std::vector<MidiNote> want;
        for (const auto& n : sent)
            want.push_back ({ (int64_t) std::llround ((n.start - origin) * 960), (int64_t) std::llround ((n.end - origin) * 960), n.pitch, n.velocity, n.channel });
        smf::sortNotes (want);
        if (song.tracks.size() != 1 || song.tracks[0].notes.size() != want.size())
        {
            why = "tracks " + std::to_string (song.tracks.size()) + ", notes " + std::to_string (song.tracks.empty() ? 0 : song.tracks[0].notes.size()) + " of " + std::to_string (want.size());
            return false;
        }
        for (size_t i = 0; i < want.size(); ++i)
        {
            const auto& a = song.tracks[0].notes[i];
            const auto& b = want[i];
            if (a.start != b.start || a.end != b.end || a.pitch != b.pitch || a.velocity != b.velocity || a.channel != b.channel)
            {
                why = "note " + std::to_string (i) + ": " + std::to_string (a.pitch) + "@" + std::to_string (a.start) + "-" + std::to_string (a.end)
                    + " vs " + std::to_string (b.pitch) + "@" + std::to_string (b.start) + "-" + std::to_string (b.end);
                return false;
            }
        }
        return true;
    }

    void testThroughEngine()
    {
        // Mirror, slot word P L, I-V-vi-IV from beat 4, one bar each, at 4 block sizes.
        for (int blockSize : { 64, 256, 1024, 2048 })
        {
            EngineRun r;
            parseWord ("P L", r.es.word);
            std::vector<std::tuple<double, int, bool>> keys;
            const int chords[4][3] = { { 60, 64, 67 }, { 55, 59, 62 }, { 57, 60, 64 }, { 53, 57, 60 } };
            for (int i = 0; i < 4; ++i)
            {
                for (int n : chords[i]) keys.push_back ({ 4.0 + 4 * i, n, true });
                for (int n : chords[i]) keys.push_back ({ 4.0 + 4 * i + 3.5, n, false });
            }
            r.run (keys, 24.0, blockSize);
            CaptureFileInfo info;
            const auto song = renderCapture (r.c.model.current(), info, CaptureRendering::AsPlayed);
            std::string why;
            const bool same = sameAsSent (song, r.sent, 4.0, why);
            check (same && r.sent.size() == 12, "Mirror P L, I-V-vi-IV, blocks of " + std::to_string (blockSize) + ": the As played file is what the engine sent, to the tick, from bar 2 (" + (same ? std::to_string (r.sent.size()) + " notes" : why) + ")");
        }

        // Walk: C E G held, P, L, +5, Undo on the operator keys (C0 = P, C#0 = L, E0 = +5, A#0 = Undo).
        {
            EngineRun r;
            r.es.mode = Mode::Walk;
            std::vector<std::tuple<double, int, bool>> keys {
                { 4.0, 60, true }, { 4.0, 64, true }, { 4.0, 67, true },
                { 5.0, 24, true }, { 5.25, 24, false },
                { 6.0, 25, true }, { 6.25, 25, false },
                { 7.0, 30, true }, { 7.25, 30, false },
                { 8.0, 34, true }, { 8.25, 34, false },
                { 10.0, 60, false }, { 10.0, 64, false }, { 10.0, 67, false } };
            r.run (keys, 12.0, 512);
            CaptureFileInfo info;
            const auto song = renderCapture (r.c.model.current(), info, CaptureRendering::AsPlayed);
            std::string why;
            const bool same = sameAsSent (song, r.sent, 4.0, why);
            check (same && r.sent.size() >= 6, "Walk with P, L, +5, Undo: the As played file is what the engine sent (" + (same ? std::to_string (r.sent.size()) + " notes" : why) + ")");

            const auto chords = chordsOf (r.c.model.current(), CaptureStream::Generated);
            std::string names;
            for (const auto& ch : chords)
            {
                names += "[";
                for (int p : ch.pitches())
                    names += std::to_string (p) + " ";
                names += "]";
            }
            check (chords.size() == 5, "the walk is five chords: C, Cm, Ab, Eb, Ab (" + names + ")");
            const auto blocks = renderCapture (r.c.model.current(), info, CaptureRendering::Chords);
            bool onGrid = ! blocks.tracks.empty();
            for (const auto& n : blocks.tracks.empty() ? std::vector<MidiNote> {} : blocks.tracks[0].notes)
                onGrid = onGrid && n.start % 240 == 0 && n.end % 240 == 0;
            check (onGrid, "the Chords rendering of the walk is on the sixteenth grid");
        }
    }

    //==========================================================================
    CapturedNote note (double start, double end, int pitch, int velocity = 100, CaptureStream stream = CaptureStream::Generated, int channel = 1)
    {
        CapturedNote n;
        n.start = start;
        n.end = end;
        n.pitch = (uint8_t) pitch;
        n.velocity = (uint8_t) velocity;
        n.stream = stream;
        n.channel = (uint8_t) channel;
        return n;
    }

    void testRenderings()
    {
        // A hand-spread chord (C, then E 15 ms later, then G), released raggedly, then Am a bar on.
        std::vector<CapturedNote> notes {
            note (4.02, 7.40, 60, 90), note (4.05, 7.55, 64, 80), note (4.06, 7.47, 67, 70),
            note (7.96, 11.9, 57, 100), note (7.98, 11.9, 60, 100), note (7.99, 11.9, 64, 100) };
        const auto chords = chordsOf (notes, CaptureStream::Generated);
        check (chords.size() == 2 && chords[0].count == 3 && chords[1].count == 3,
               "a spread chord and a ragged release make one chord each (" + std::to_string (chords.size()) + " chords)");

        CaptureFileInfo info;
        const auto song = renderCapture (notes, info, CaptureRendering::Chords);
        bool ok = song.tracks.size() == 1 && song.tracks[0].notes.size() == 6;
        if (ok)
        {
            const auto& t = song.tracks[0].notes;
            // Origin bar 1 (beat 4). C major 4.02-7.55 → 4.0-7.5, Am 7.96-11.9 → 8.0-12.0.
            for (int i = 0; i < 3; ++i)
                ok = ok && t[(size_t) i].start == 0 && t[(size_t) i].end == 3360;
            for (int i = 3; i < 6; ++i)
                ok = ok && t[(size_t) i].start == 4 * 960 && t[(size_t) i].end == 8 * 960;
            ok = ok && t[0].pitch == 60 && t[0].velocity == 90 && t[2].pitch == 67 && t[2].velocity == 70;
        }
        check (ok, "Chords: one block chord per change, snapped to the sixteenth, velocities kept");

        const auto exact = renderCapture (notes, info, CaptureRendering::AsPlayed);
        ok = exact.tracks.size() == 1 && exact.tracks[0].notes.size() == 6 && exact.tracks[0].notes[0].start == (int64_t) std::llround (0.02 * 960)
          && exact.tracks[0].notes[1].start == (int64_t) std::llround (0.05 * 960) && exact.tracks[0].notes[1].end == (int64_t) std::llround (3.55 * 960);
        check (ok, "As played: exact, from the bar line before the first note");

        // Overlap: the second chord snaps before the first's end: cut there. Two chords on one
        // grid point: the later stays.
        std::vector<CapturedNote> fast { note (0.0, 1.1, 60), note (1.06, 2.0, 62), note (2.0, 2.05, 64), note (2.05, 3.0, 65) };
        const auto f = renderCapture (fast, info, CaptureRendering::Chords);
        std::string desc;
        for (const auto& n : f.tracks.empty() ? std::vector<MidiNote> {} : f.tracks[0].notes)
            desc += std::to_string (n.pitch) + "@" + std::to_string (n.start) + "-" + std::to_string (n.end) + " ";
        ok = f.tracks.size() == 1 && f.tracks[0].notes.size() == 3
          && f.tracks[0].notes[0].pitch == 60 && f.tracks[0].notes[0].end == 960
          && f.tracks[0].notes[1].pitch == 62 && f.tracks[0].notes[1].start == 960 && f.tracks[0].notes[1].end == 1920
          && f.tracks[0].notes[2].pitch == 65 && f.tracks[0].notes[2].start == 1920;
        check (ok, "Chords: an overlap is cut at the next chord, a chord too short for the grid merges into the next (" + desc + ")");

        // Source notes make a second track; channels kept.
        std::vector<CapturedNote> both { note (0.0, 1.0, 60, 100, CaptureStream::Source, 0), note (0.0, 1.0, 63, 100) };
        info.name = "TZ gen";
        info.sourceName = "TZ src";
        const auto b = renderCapture (both, info, CaptureRendering::AsPlayed);
        check (b.tracks.size() == 2 && b.tracks[0].name == "TZ gen" && b.tracks[1].name == "TZ src" && b.tracks[1].notes[0].channel == 0 && b.tracks[0].notes[0].channel == 1,
               "source notes go on their own named track, on their own channel");
        const auto g = renderCapture ({ note (0.0, 1.0, 63, 100) }, info, CaptureRendering::AsPlayed);
        check (g.tracks.size() == 1, "no source notes: one note track");
    }

    //==========================================================================
    void testMidiFile()
    {
        // Round trip with a key signature, a meter, a tempo and two tracks.
        std::vector<CapturedNote> notes;
        for (int i = 0; i < 40; ++i)
            notes.push_back (note (3.0 + i * 0.37, 3.0 + i * 0.37 + 0.123 + (i % 5) * 0.4, 40 + (i * 7) % 50, 1 + (i * 13) % 127,
                                   i % 3 == 0 ? CaptureStream::Source : CaptureStream::Generated, i % 3 == 0 ? 0 : 1));
        CaptureFileInfo info;
        info.bpm = 97.5;
        info.numerator = 3;
        info.denominator = 4;
        info.keyTonic = 3;      // E♭ minor: 6 flats
        info.keyMinor = true;
        info.name = "TonnetzML Eb minor 18 bars";
        const auto song = renderCapture (notes, info, CaptureRendering::AsPlayed);
        const auto bytes = writeMidiFile (song);
        MidiSong back;
        std::string error;
        const bool read = readMidiFile (bytes.data(), bytes.size(), back, error);
        check (read && bytes.size() > 14 && bytes[9] == 1 && bytes[11] == 3 && back.ppq == 960, "type 1, 960 ppq, a conductor track and two note tracks");
        check (std::abs (back.bpm - 97.5) < 0.001 && back.numerator == 3 && back.denominator == 4, "tempo 97.5 and 3/4 come back");
        check (back.key.present && back.key.sharps == -6 && back.key.minor, "the key signature comes back: E♭ minor, 6 flats");
        check (back.tracks.size() == 2 && back.tracks[0].name == info.name && back.tracks[1].name == "TonnetzML source" && back.name == info.name,
               "the track names come back");
        bool same = back.tracks.size() == 2;
        for (size_t t = 0; same && t < 2; ++t)
        {
            same = back.tracks[t].notes.size() == song.tracks[t].notes.size();
            for (size_t i = 0; same && i < song.tracks[t].notes.size(); ++i)
            {
                const auto& a = back.tracks[t].notes[i];
                const auto& b = song.tracks[t].notes[i];
                same = a.start == b.start && a.end == b.end && a.pitch == b.pitch && a.velocity == b.velocity && a.channel == b.channel;
            }
        }
        check (same, "every note comes back: start, end, pitch, velocity, channel");
        // And in beats, against the capture itself, within 0.001 beat (G6's tolerance).
        double worst = 0.0;
        const double origin = captureOrigin (notes.front().start, info.barLength());
        for (const auto& n : back.notes)
        {
            double best = 1.0e9;
            for (const auto& c : notes)
                if (c.pitch == n.pitch && c.velocity == n.velocity)
                    best = std::min (best, std::abs ((double) n.start / 960 - (c.start - origin)) + std::abs ((double) n.end / 960 - (c.end - origin)));
            worst = std::max (worst, best);
        }
        check (back.notes.size() == 40 && worst < 0.001, "the file's notes are the captured notes within 0.001 beat (worst " + str (worst) + ")");

        // Every key's signature: sharps and flats as the key is written.
        const int majors[12] = { 0, -5, 2, -3, 4, -1, 6, 1, -4, 3, -2, 5 };
        const int minors[12] = { -3, 4, -1, -6, 1, -4, 3, -2, 5, 0, -5, 2 };
        bool keys = true;
        for (int t = 0; t < 12; ++t)
        {
            keys = keys && keySignatureOf (t, false).sharps == majors[t] && keySignatureOf (t, true).sharps == minors[t];
            MidiSong s;
            s.key = keySignatureOf (t, t % 2 == 1);
            s.tracks.push_back ({ "x", { { 0, 10, 60, 100, 0 } } });
            const auto b = writeMidiFile (s);
            MidiSong r;
            keys = keys && readMidiFile (b.data(), b.size(), r, error) && r.key.present && r.key.sharps == s.key.sharps && r.key.minor == (t % 2 == 1);
        }
        check (keys, "all 24 keys write their signature (D♭ major 5 flats, F♯ major 6 sharps, G♯ minor 5 sharps, E♭ minor 6 flats …) and read back");
        check (! keySignatureOf (-1, false).present, "no key in force: no key signature");
    }

    //==========================================================================
    void testNoAllocation()
    {
        Cap c;
        c.t.playing = true;
        c.s.captureSource = true;
        std::vector<MidiEvent> in, out;
        for (int i = 0; i < 64; ++i)
        {
            in.push_back (MidiEvent::noteOn (0, 40 + i, 100, i));
            out.push_back (MidiEvent::noteOn (1, 40 + i, 100, i));
            in.push_back (MidiEvent::noteOff (0, 40 + i, 100 + i));
            out.push_back (MidiEvent::noteOff (1, 40 + i, 100 + i));
        }
        const int before = allocations.load();
        watched = pthread_self();
        watching = true;
        for (int b = 0; b < 400; ++b)
        {
            c.s.capture = (b / 50) % 2 == 1;
            if (b == 200)
                c.t.playing = false;
            if (b == 300)
            {
                c.t.playing = true;
                c.t.ppq = 3.0;
            }
            c.writer->process (c.t, 256, in.data(), (int) in.size(), out.data(), (int) out.size(), c.s);
            if (c.t.playing)
                c.t.ppq += 256 / 24000.0;
        }
        watching = false;
        check (allocations.load() - before == 0, "no allocation in CaptureWriter::process (400 blocks, 256 events each, a full queue, arming, stop, start)");

        // The hook works.
        const int b2 = allocations.load();
        watching = true;
        sink = std::malloc (100);
        std::free (sink);
        watching = false;
        check (allocations.load() - b2 >= 1, "the malloc hook sees allocations");
    }
}

int main()
{
    malloc_logger = logger;
    std::cout << "test_capture\n";
    testQueue();
    testStampsPlaying();
    testStampsStoppedAndJumps();
    testStreams();
    testModel();
    testThroughEngine();
    testRenderings();
    testMidiFile();
    testNoAllocation();
    malloc_logger = nullptr;
    return testutil::finish ("test_capture");
}
