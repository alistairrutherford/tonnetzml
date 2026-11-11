// Preview tone (PLAN.md §3.7, Phase 6; core/engine/PreviewVoice.h): silent with nothing played;
// a note sounds at its pitch from its sample and dies away after its note-off; velocity sets
// the level; the same pitch twice is one voice; a seventeenth note steals; All Notes Off
// releases; silence() stops at once; never louder than its level allows; no allocation.
#include "TestUtil.h"
#include "core/engine/PreviewVoice.h"
#include <array>
#include <atomic>
#include <cmath>
#include <pthread.h>
#include <string>
#include <vector>

using namespace tonnetz;
using testutil::check;

extern "C" void (*malloc_logger) (uint32_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uint32_t);

namespace
{
    std::atomic<bool> watching { false };
    std::atomic<int> allocations { 0 };
    std::atomic<pthread_t> watched { nullptr };

    void logger (uint32_t type, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uint32_t)
    {
        if ((type & 2) != 0 && watching.load() && watched.load() == pthread_self())
            allocations.fetch_add (1);
    }

    struct Run
    {
        PreviewVoice v;
        std::vector<float> l, r;
        Run() { v.prepare (48000.0); }
        float block (std::vector<MidiEvent> e, int n = 480)
        {
            l.assign ((size_t) n, 0.0f);
            r.assign ((size_t) n, 0.0f);
            v.render (e.data(), (int) e.size(), l.data(), r.data(), n);
            float peak = 0.0f;
            for (float x : l)
                peak = std::max (peak, std::abs (x));
            return peak;
        }
    };

    // Zero crossings upward per second over the buffer: the fundamental for this tone.
    double pitchHz (const std::vector<float>& x, double rate)
    {
        int ups = 0, first = -1, last = -1;
        for (size_t i = 1; i < x.size(); ++i)
            if (x[i - 1] <= 0.0f && x[i] > 0.0f)
            {
                ++ups;
                if (first < 0) first = (int) i;
                last = (int) i;
            }
        return ups > 1 ? (ups - 1) * rate / (last - first) : 0.0;
    }
}

int main()
{
    std::cout << "test_preview\n";
    {
        Run t;
        check (t.block ({}) == 0.0f && t.v.isSilent(), "nothing played: silent");
        t.block ({ MidiEvent::noteOn (0, 69, 127, 240) });
        bool before = true;
        for (int i = 0; i < 240; ++i)
            before = before && t.l[(size_t) i] == 0.0f;
        check (before && t.l[300] != 0.0f, "a note starts on its sample");
        t.block ({}, 9600);
        const double hz = pitchHz (t.l, 48000.0);
        check (std::abs (hz - 440.0) < 3.0, "A4 sounds at 440 Hz (" + std::to_string (hz) + ")");
        check (t.l == t.r, "both channels carry it");
        const float loud = t.block ({});
        check (loud > 0.05f && loud <= PreviewVoice::level * 1.4f, "at velocity 127 its peak is within its level (" + std::to_string (loud) + ")");
        t.block ({ MidiEvent::noteOff (0, 69, 0) });
        float tail = 1.0f;
        for (int i = 0; i < 30; ++i)
            tail = t.block ({});
        check (tail == 0.0f && t.v.isSilent(), "a note-off dies away within 300 ms");
    }
    {
        Run a, b;
        a.block ({ MidiEvent::noteOn (0, 60, 127, 0) });
        b.block ({ MidiEvent::noteOn (0, 60, 32, 0) });
        const float pa = a.block ({}), pb = b.block ({});
        check (pb > 0.0f && std::abs (pa / pb - 127.0f / 32.0f) < 0.05f, "velocity sets the level");
        a.block ({ MidiEvent::noteOn (0, 60, 127, 0) });
        check (a.v.activeVoices() == 1, "the same pitch again restrikes one voice");
    }
    {
        Run t;
        std::vector<MidiEvent> on;
        for (int i = 0; i < 20; ++i)
            on.push_back (MidiEvent::noteOn (0, 40 + i, 100, i));
        t.block (on);
        check (t.v.activeVoices() == PreviewVoice::numVoices, "20 notes: 16 voices, the oldest taken");
        t.block ({ MidiEvent::controller (0, 123, 0, 0) });
        for (int i = 0; i < 30; ++i)
            t.block ({});
        check (t.v.isSilent(), "All Notes Off releases everything");
        t.block ({ MidiEvent::noteOn (0, 50, 100, 0) });
        t.v.silence();
        check (t.block ({}) == 0.0f, "silence() stops at once");
    }
    {
        Run t;
        std::array<MidiEvent, 2> ev {};
        std::vector<float> l (480), r (480);
        watched = pthread_self();
        malloc_logger = logger;
        const int before = allocations.load();
        watching = true;
        for (int b = 0; b < 500; ++b)
        {
            ev[0] = MidiEvent::noteOn (0, 30 + b % 60, 100, b % 400);
            ev[1] = MidiEvent::noteOff (0, 30 + (b + 30) % 60, 420);
            t.v.render (ev.data(), 2, l.data(), r.data(), 480);
        }
        watching = false;
        check (allocations.load() - before == 0, "no allocation in render (500 blocks, a note in and out each)");
        malloc_logger = nullptr;
    }
    return testutil::finish ("test_preview");
}
