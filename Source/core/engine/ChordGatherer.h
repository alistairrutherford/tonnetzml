// Finding the chord in the source notes (PLAN.md §3.2): which keys are held, and the gather
// window.
//
// A note-on with no window open opens one; note-ons until it closes are one chord. When it
// closes, the engine evaluates every key held at that moment: new keys sound, and keys that
// were already sounding are retuned only if their image changed. So a note added to a held
// chord after the window (a late note) opens a window of its own and retunes the chord when it
// closes.
//
// The window is `gatherMs` long, in samples. At 0 it closes at the sample it opened, after
// every input event on that sample: exactly the notes of one sample (a quantized clip) are one
// chord, with no delay. A longer window closes before any input event on its closing sample
// (the window is [open, open + length)).
//
// Times are absolute samples (the engine's Clock counts them), so a window can span blocks.
// Plain C++, no JUCE, no allocation.
#pragma once

#include "../lattice/Lattice.h"
#include <array>
#include <cstdint>

namespace tonnetz
{
    constexpr int maxGatherMs = 80;

    inline int64_t gatherSamples (int gatherMs, double sampleRate) noexcept
    {
        gatherMs = gatherMs < 0 ? 0 : (gatherMs > maxGatherMs ? maxGatherMs : gatherMs);
        const double rate = sampleRate > 0.0 ? sampleRate : 44100.0;
        return (int64_t) (gatherMs * rate / 1000.0 + 0.5);
    }

    class ChordGatherer
    {
    public:
        // A key goes down at absolute sample `at`. False if it was already down (ignored).
        // `windowLength` is the gather window in samples.
        bool press (int note, int velocity, int64_t at, int64_t windowLength) noexcept
        {
            if (note < 0 || note > 127 || keys[(size_t) note].held)
                return false;
            auto& k = keys[(size_t) note];
            k.held = true;
            k.sounded = false;
            k.velocity = (uint8_t) (velocity < 1 ? 1 : (velocity > 127 ? 127 : velocity));
            ++heldCount;
            if (! open)
            {
                open = true;
                closeAt = at + (windowLength > 0 ? windowLength : 0);
                afterInputs = windowLength <= 0;
            }
            return true;
        }

        // A key comes up. False if it wasn't held as a source key. `wasSounded`: its image was
        // sounding (the window it came in had closed). The last key up cancels an open window.
        bool release (int note, bool& wasSounded) noexcept
        {
            wasSounded = false;
            if (note < 0 || note > 127 || ! keys[(size_t) note].held)
                return false;
            auto& k = keys[(size_t) note];
            wasSounded = k.sounded;
            k.held = false;
            k.sounded = false;
            if (--heldCount == 0)
                open = false;
            return true;
        }

        // Is the window due to close before an input event at absolute sample `at`?
        bool dueBefore (int64_t at) const noexcept
        {
            return open && (closeAt < at || (closeAt == at && ! afterInputs));
        }

        // Is it due by the end of a block that ends (exclusive) at absolute sample `end`?
        bool dueBy (int64_t end) const noexcept { return open && closeAt < end; }

        bool windowOpen() const noexcept { return open; }
        int64_t closesAt() const noexcept { return closeAt; }

        // The window closes: every held key now sounds.
        void close() noexcept
        {
            open = false;
            for (auto& k : keys)
                if (k.held)
                    k.sounded = true;
        }

        // Forget every key (stop, reset, All Notes Off). Later note-offs of these keys are ignored.
        void clear() noexcept
        {
            for (auto& k : keys)
                k = {};
            heldCount = 0;
            open = false;
        }

        bool isHeld (int note) const noexcept { return note >= 0 && note <= 127 && keys[(size_t) note].held; }
        bool isSounded (int note) const noexcept { return note >= 0 && note <= 127 && keys[(size_t) note].sounded; }
        int velocity (int note) const noexcept { return note >= 0 && note <= 127 ? keys[(size_t) note].velocity : 0; }
        int held() const noexcept { return heldCount; }

        PcMask heldMask() const noexcept
        {
            unsigned m = 0;
            for (int n = 0; n < 128; ++n)
                if (keys[(size_t) n].held)
                    m |= 1u << (n % 12);
            return (PcMask) m;
        }

        int lowestHeld() const noexcept
        {
            for (int n = 0; n < 128; ++n)
                if (keys[(size_t) n].held)
                    return n;
            return -1;
        }

    private:
        struct Key
        {
            bool held = false;
            bool sounded = false;
            uint8_t velocity = 0;
        };

        std::array<Key, 128> keys {};
        int heldCount = 0;
        bool open = false, afterInputs = false;
        int64_t closeAt = 0;
    };
}
