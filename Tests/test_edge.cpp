// Fuzz (PLAN.md §7.1 test_edge): random blocks of notes, operator keys, controllers and parameter
// changes (every mode, latch, Orbit's cycle and rate, the key settings, the zone), loops, locates,
// stops, Enable flips, releaseAll (bypass, reset) and state restores, under hosts with random block sizes and
// sample rates. Every run must keep the output clean (no double note-on, stray note-off or event
// outside its block), leave nothing sounding once the keys are up and the transport stops, and
// never allocate inside the engine (libmalloc's logger hook counts every allocation made while
// the engine runs).
//
// Then floods: 2000 input events in one block, all 128 keys held under a long word with the bass
// and Source + Generated on, an output buffer that fills: still clean.
#include "FakeHost.h"
#include "core/engine/LatticeEngine.h"
#include <atomic>
#include <pthread.h>
#include <random>
#include <set>

using namespace tonnetz;
using namespace fakehost;
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

    const char* const wordTexts[] = { "", "P", "L", "R", "N", "S", "H", "PL", "PR", "LR", "P L R N S H P L", "+5", "-M3 +m3",
                                      "T(3,-2)", "Flip(0,7) Pivot(4)", "Grow(0,4) P Grow(4,7) L Shrink", "Grow(4,7)",
                                      "P L P L P L P L P L P L P L P L P L P L P L P L P L P L P L P L" };

    Word randomWord (std::mt19937& rng)
    {
        Word w;
        parseWord (wordTexts[rng() % (sizeof (wordTexts) / sizeof (*wordTexts))], w);
        return w;
    }

    void randomSettings (std::mt19937& rng, EngineSettings& s)
    {
        switch (rng() % 19)
        {
            case 10: s.mode = (Mode) (rng() % 4); break;
            case 11: s.latch = (rng() % 2) != 0; break;
            case 12: s.walkInput = (WalkInput) (rng() % 2); break;
            case 13: s.orbitCycle = (OrbitCycle) (rng() % 5); break;
            case 14: s.orbitRate = (int) (rng() % numOrbitRates); break;
            case 15: s.key = (KeyMode) (rng() % 3); break;
            case 16: s.keyTonic = (int) (rng() % 12); s.keyMinor = (rng() % 2) != 0; break;
            case 17: s.inKey = (rng() % 2) != 0; break;
            case 18: s.zone.on = true; s.zone.start = 24 + (int) (rng() % 4) * 12; break;
            case 0: s.word = randomWord (rng); break;
            case 1: s.mix = (Mix) (rng() % 3); break;
            case 2: s.sourceChannel = (int) (rng() % 16); break;
            case 3: s.genChannel = (int) (rng() % 16); break;
            case 4: s.gatherMs = (int) (rng() % 81); break;
            case 5: s.voicing.reg = (Register) (rng() % 2); break;
            case 6: s.voicing.bass = (BassMode) (rng() % 2); break;
            case 7: s.voicing.octave = (int) (rng() % 5) - 2; break;
            case 8: s.enabled = (rng() % 3) != 0; break;
            case 9: s.zone.on = (rng() % 2) != 0; s.zone.start = (int) (rng() % 120); break;
        }
    }

    struct Fuzz
    {
        std::unique_ptr<LatticeEngine> engine = std::make_unique<LatticeEngine>();
        EngineSettings s;
        Host h;
        bool releaseNext = false, restoreNext = false;
        WalkState restoreTo;
        bool trace = std::getenv ("TONNETZ_TRACE") != nullptr;
        std::vector<std::pair<int64_t, MidiEvent>> traced;
        int64_t maxOut = 0;

        Fuzz (double rate, double bpm) : h (rate, bpm)
        {
            h.input.reserve (1 << 16);
            h.process = [this] (const Transport& t, const MidiEvent* in, int n, int len, BlockEvents& out)
            {
                watched = pthread_self();
                watching = true;
                if (releaseNext)
                {
                    engine->releaseAll (0, out);
                    releaseNext = false;
                }
                if (restoreNext)
                {
                    engine->restore (restoreTo, out);
                    restoreNext = false;
                }
                engine->process (t, in, n, len, s, out);
                watching = false;
                maxOut = std::max<int64_t> (maxOut, out.size());
                if (trace)
                    for (const auto& e : out)
                        traced.push_back ({ h.pos + e.sample, e });
            };
        }
    };

    void fuzz()
    {
        std::mt19937 rng (777);
        int runs = 0, unclean = 0, sounding = 0, leftovers = 0;
        int64_t blocks = 0, events = 0;
        std::string first;
        const int before = allocations.load();
        for (int run = 0; run < 1500; ++run)
        {
            const double rates[] = { 8000.0, 22050.0, 44100.0, 48000.0, 96000.0, 192000.0 };
            Fuzz f (rates[rng() % 6], 40.0 + (double) (rng() % 200));
            for (int i = 0; i < 3; ++i) randomSettings (rng, f.s);
            f.restoreTo.word = randomWord (rng);
            f.restoreTo.orbitStep = rng() % 100;
            for (int k = 0, n = (int) (rng() % 6); k < n; ++k)
                f.restoreTo.latched[(size_t) (rng() % 128)] = (uint8_t) (1 + rng() % 127);
            if (rng() % 3 == 0)
            {
                f.h.looping = true;
                f.h.loopStart = (double) (rng() % 8);
                f.h.loopEnd = f.h.loopStart + 0.25 + (double) (rng() % 8);
                f.h.style = (LoopStyle) (rng() % 3);
            }

            // Keys: mostly well formed (down, then up), with garbage mixed in.
            const int64_t span = f.h.at (16.0);
            for (int k = 0, n = 20 + (int) (rng() % 200); k < n; ++k)
            {
                const int64_t at = (int64_t) (rng() % (uint64_t) span);
                const int note = (int) (rng() % 128), ch = (rng() % 8) == 0 ? (int) (rng() % 16) : 0;
                const int kind = (int) (rng() % 20);
                if (kind < 15)
                {
                    const int64_t len = 1 + (int64_t) (rng() % (uint64_t) f.h.at (2.0));
                    f.h.input.push_back ({ at, MidiEvent::noteOn (ch, note, 1 + (int) (rng() % 127), 0) });
                    f.h.input.push_back ({ at + len, (rng() % 4) == 0 ? MidiEvent { 0, { (uint8_t) (0x90 | ch), (uint8_t) note, 0 }, 3 }
                                                                       : MidiEvent::noteOff (ch, note, 0) });
                }
                else if (kind < 17) f.h.input.push_back ({ at, MidiEvent::noteOn (ch, note, 100, 0) });     // a key never released…
                else if (kind < 18) f.h.input.push_back ({ at, MidiEvent::noteOff (ch, note, 0) });         // …a stray release
                else if (kind < 19) f.h.input.push_back ({ at, MidiEvent::controller (ch, (rng() % 6) == 0 ? 123 : 64, 127, 0) });
                else f.h.input.push_back ({ at, MidiEvent { 0, { (uint8_t) (0xe0 | ch), 0, 64 }, 3 } });
            }

            // Play in chunks of random block sizes, changing settings, stopping, releasing.
            while (f.h.pos < span)
            {
                const int block = (rng() % 4) == 0 ? 1 + (int) (rng() % 16) : 1 + (int) (rng() % 4096);
                const int64_t to = std::min (span, f.h.pos + (int64_t) block * (1 + (int64_t) (rng() % 8)));
                f.h.run (to, block);
                blocks += (to - f.h.pos) / block + 1;
                const int r = (int) (rng() % 12);
                if (r < 5) randomSettings (rng, f.s);
                else if (r == 5) f.h.playing = ! f.h.playing;
                else if (r == 6) f.releaseNext = true;
                else if (r == 7) f.h.ppq = (double) (rng() % 64);   // a locate
                else if (r == 8) f.restoreNext = true;               // a state restore (W, Orbit's step, a latched chord)
                else if (r == 9)
                {
                    // A burst of operator keys (wherever the zone is).
                    for (int k = 0; k < 6; ++k)
                    {
                        const int64_t at = f.h.pos + (int64_t) (rng() % 4096);
                        const int key = std::min (127, f.s.zone.start + (int) (rng() % 12));
                        if (at + 200 >= span)
                            continue;
                        f.h.input.push_back ({ at, MidiEvent::noteOn (0, key, 100, 0) });
                        f.h.input.push_back ({ at + 1 + (int64_t) (rng() % 200), MidiEvent::noteOff (0, key, 0) });
                    }
                    std::stable_sort (f.h.input.begin() + (long) f.h.nextInput, f.h.input.end(), [] (const auto& a, const auto& b) { return a.first < b.first; });
                }
            }

            // Every key up on every channel, Enable on, then the transport stops.
            for (int ch = 0; ch < 16; ++ch)
                for (int n = 0; n < 128; ++n)
                    f.h.input.push_back ({ f.h.pos + (n % 7), MidiEvent::noteOff (ch, n, 0) });
            f.h.run (f.h.pos + 64, 64);
            f.s.enabled = true;
            f.h.playing = true;
            f.h.run (f.h.pos + 64, 64);
            f.h.stop (64);

            ++runs;
            events += f.h.rx.noteOns + f.h.rx.noteOffs;
            const bool clean = f.h.rx.clean(), silent = f.h.rx.on.empty(), empty = f.engine->isSilent() && f.engine->voices().isEmpty();
            unclean += clean ? 0 : 1;
            sounding += silent ? 0 : 1;
            leftovers += empty ? 0 : 1;
            if ((! clean || ! silent || ! empty) && first.empty() && f.trace)
            {
                std::set<std::pair<int, int>> on;
                for (const auto& [at, e] : f.traced)
                {
                    const auto k = std::make_pair (e.channel(), e.note());
                    const char* what = e.isNoteOn() ? "on " : e.isNoteOff() ? "off" : "cc ";
                    bool bad = false;
                    if (e.isNoteOn()) bad = ! on.insert (k).second;
                    else if (e.isNoteOff()) bad = on.erase (k) == 0;
                    else if (e.isAllNotesOff()) on.clear();
                    if (bad || (e.isNoteOn() == false && e.isNoteOff() == false && e.isAllNotesOff()))
                        std::cout << (bad ? "BAD " : "    ") << at << " " << what << " ch" << e.channel() << " " << e.note() << "\n";
                }
                for (auto [ch, n] : on) std::cout << "left on ch" << ch << " " << n << "\n";
            }
            if ((! clean || ! silent || ! empty) && first.empty())
                first = " first: run " + std::to_string (run) + ": " + f.h.rx.describe() + (empty ? "" : ", the engine still holds notes");
        }
        const int allocated = allocations.load() - before;
        check (unclean == 0 && sounding == 0 && leftovers == 0,
               "fuzz: " + std::to_string (runs) + " runs, " + std::to_string (blocks) + " blocks, " + std::to_string (events)
                   + " notes out: clean, nothing left sounding" + first);
        check (allocated == 0, "fuzz: no allocation inside the engine (" + std::to_string (allocated) + ")");
    }

    void floods()
    {
        const int before = allocations.load();
        Fuzz f (48000.0, 120.0);
        f.s.mix = Mix::SourceAndGenerated;
        f.s.voicing.bass = BassMode::RootBelow;
        parseWord ("Grow(0,4) P Grow(4,7) L +5 N Shrink S H", f.s.word);
        f.s.gatherMs = 0;
        // 2000 events in one 512-sample block: every key down, then flapping.
        for (int i = 0; i < 2000; ++i)
        {
            const int n = i % 128;
            f.h.input.push_back ({ i % 512, (i / 128) % 2 == 0 ? MidiEvent::noteOn (0, n, 100, 0) : MidiEvent::noteOff (0, n, 0) });
        }
        for (int n = 0; n < 128; ++n)
            f.h.input.push_back ({ 600, MidiEvent::noteOn (0, n, 90, 0) });
        f.h.run (2048, 512);
        const bool full = f.engine->voices().soundingNotes() > 100;
        for (int n = 0; n < 128; ++n)
            f.h.input.push_back ({ 3000 + n, MidiEvent::noteOff (0, n, 0) });
        f.h.run (4096, 512);
        f.h.stop (512);
        check (f.h.rx.clean() && f.h.rx.on.empty() && f.engine->isSilent() && full && f.maxOut <= BlockEvents::capacity,
               "flood: 2000 events in a block, all 128 keys under a long word with bass and source: clean, all ended ("
                   + f.h.rx.describe() + ", most out in a block " + std::to_string (f.maxOut) + ")");
        // A buffer that fills: every key on two channels in one block with Enable off, then on.
        Fuzz g (44100.0, 120.0);
        g.s.enabled = false;
        for (int ch = 0; ch < 16; ++ch)
            for (int n = 0; n < 128; ++n)
                g.h.input.push_back ({ 5, MidiEvent::noteOn (ch, n, 100, 0) });
        g.h.run (64, 64);
        g.s.enabled = true;
        g.h.run (128, 64);
        g.h.stop (64);
        check (g.h.rx.clean() && g.h.rx.on.empty() && g.engine->isSilent(),
               "a full buffer with Enable off (2048 note-ons in one block): what didn't fit is dropped whole, the rest ends (" + g.h.rx.describe() + ")");
        check (allocations.load() - before == 0, "floods: no allocation inside the engine");
    }

    void hookWorks()
    {
        const int before = allocations.load();
        watched = pthread_self();
        watching = true;
        sink = std::malloc (1000);
        std::free (sink);
        watching = false;
        check (allocations.load() - before >= 1, "the malloc hook sees allocations");
    }
}

int main()
{
    malloc_logger = logger;
    std::cout << "test_edge\n";
    hookWorks();
    fuzz();
    floods();
    malloc_logger = nullptr;
    return testutil::finish ("test_edge");
}
