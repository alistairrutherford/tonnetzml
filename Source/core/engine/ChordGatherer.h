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
// Latch (PLAN.md §3.1): a key let go with latch on stays in the chord (latched) until a new chord
// replaces it: `down` counts the keys physically down, `chordSize` the keys in the chord (down or
// latched). With latch on, letting go of every key inside an open window doesn't cancel it: the
// chord latches when it closes.
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
        // `windowLength` is the gather window in samples. `wasLatched`: it was in the chord,
        // latched, and is now down again (still in the chord, at the new velocity).
        bool press (int note, int velocity, int64_t at, int64_t windowLength, bool& wasLatched) noexcept
        {
            wasLatched = false;
            if (note < 0 || note > 127 || keys[(size_t) note].held)
                return false;
            auto& k = keys[(size_t) note];
            wasLatched = k.latched;
            if (! k.latched)
                ++chordCount;
            k.held = true;
            k.latched = false;
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

        // A key comes up. False if it wasn't down as a source key. `wasSounded`: its image was
        // sounding (the window it came in had closed). With `latch` the key stays in the chord;
        // without, the last key up cancels an open window.
        bool release (int note, bool& wasSounded, bool latch = false) noexcept
        {
            wasSounded = false;
            if (note < 0 || note > 127 || ! keys[(size_t) note].held)
                return false;
            auto& k = keys[(size_t) note];
            wasSounded = k.sounded;
            k.held = false;
            --heldCount;
            if (latch)
            {
                k.latched = true;
                return true;
            }
            k.sounded = false;
            --chordCount;
            if (chordCount == 0)
                open = false;
            return true;
        }

        // A key goes into the chord latched (restoring a latched chord), sounding.
        void addLatched (int note, int velocity) noexcept
        {
            if (note < 0 || note > 127 || keys[(size_t) note].held || keys[(size_t) note].latched)
                return;
            auto& k = keys[(size_t) note];
            k.latched = true;
            k.sounded = true;
            k.velocity = (uint8_t) (velocity < 1 ? 1 : (velocity > 127 ? 127 : velocity));
            ++chordCount;
        }

        // The latched keys leave the chord (a new chord replaces them, or latch goes off).
        // Calls `fn (note)` for each.
        template <class Fn>
        void dropLatched (Fn fn) noexcept
        {
            for (int n = 0; n < 128; ++n)
            {
                auto& k = keys[(size_t) n];
                if (! k.latched)
                    continue;
                k = {};
                --chordCount;
                fn (n);
            }
            if (chordCount == 0)
                open = false;
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

        // The window closes: every key in the chord now sounds.
        void close() noexcept
        {
            open = false;
            for (auto& k : keys)
                if (k.held || k.latched)
                    k.sounded = true;
        }

        // Forget every key, latched or down (stop, reset, All Notes Off). Later note-offs of
        // these keys are ignored.
        void clear() noexcept
        {
            for (auto& k : keys)
                k = {};
            heldCount = 0;
            chordCount = 0;
            open = false;
        }

        // In the chord: down, or latched.
        bool isHeld (int note) const noexcept { return note >= 0 && note <= 127 && (keys[(size_t) note].held || keys[(size_t) note].latched); }
        bool isDown (int note) const noexcept { return note >= 0 && note <= 127 && keys[(size_t) note].held; }
        bool isLatched (int note) const noexcept { return note >= 0 && note <= 127 && keys[(size_t) note].latched; }
        bool isSounded (int note) const noexcept { return note >= 0 && note <= 127 && keys[(size_t) note].sounded; }
        int velocity (int note) const noexcept { return note >= 0 && note <= 127 ? keys[(size_t) note].velocity : 0; }
        int held() const noexcept { return chordCount; }           // keys in the chord
        int down() const noexcept { return heldCount; }            // keys physically down

        // A chord is sounding: some key of it has had its window close.
        bool sounding() const noexcept
        {
            if (chordCount == 0)
                return false;
            for (const auto& k : keys)
                if (k.sounded)
                    return true;
            return false;
        }

        PcMask heldMask() const noexcept
        {
            unsigned m = 0;
            for (int n = 0; n < 128; ++n)
                if (isHeld (n))
                    m |= 1u << (n % 12);
            return (PcMask) m;
        }

        int lowestHeld() const noexcept
        {
            for (int n = 0; n < 128; ++n)
                if (isHeld (n))
                    return n;
            return -1;
        }

    private:
        struct Key
        {
            bool held = false;            // physically down
            bool latched = false;         // let go with latch on: still in the chord
            bool sounded = false;
            uint8_t velocity = 0;
        };

        std::array<Key, 128> keys {};
        int heldCount = 0, chordCount = 0;
        bool open = false, afterInputs = false;
        int64_t closeAt = 0;
    };
}
