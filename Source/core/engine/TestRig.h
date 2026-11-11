// The test rig (PLAN.md §7.3): keys from a text file, played as if they came from the host while
// the transport runs, so a check in Live can feed the plug-in known notes and record what comes
// out.
//
//   RigKeys    the file's keys, in a fixed array (512), trivially copyable: parsed on the
//              message thread (`parseRigText`), handed to the audio thread through a
//              SnapshotExchange.
//   RigPlayer  audio thread: this block's key-downs and key-ups, placed on the sample from the
//              host's ppq. Follows loops and locates, and ends every key it holds when the
//              transport stops, jumps or the rig is turned off.
//
// The file: one key per line, `beat note velocity length` (beats are quarter notes from the
// start of the song; length in beats). Blank lines and lines starting with `#` are skipped;
// anything else that doesn't parse is counted as rejected.
//
// Plain C++, no JUCE. RigPlayer does not allocate or lock.
#pragma once

#include "MidiEvents.h"
#include "Transport.h"
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace tonnetz
{
    struct RigKey
    {
        double from = 0.0, to = 0.0;      // beats (ppq)
        uint8_t note = 0, velocity = 0;
    };

    struct RigKeys
    {
        static constexpr int capacity = 512;
        std::array<RigKey, capacity> keys {};
        int count = 0;
        int rejected = 0;                 // lines that weren't a key, a comment or blank
        int dropped = 0;                  // keys past the capacity
    };

    namespace rigparse
    {
        inline bool isSpace (char c) noexcept { return c == ' ' || c == '\t' || c == '\r' || c == ','; }

        // A plain decimal number (no exponent), whatever the host's locale. Moves `p` past it.
        inline bool number (const char*& p, const char* end, double& out) noexcept
        {
            while (p < end && isSpace (*p)) ++p;
            const char* start = p;
            bool negative = false;
            if (p < end && (*p == '-' || *p == '+')) negative = *p++ == '-';
            double v = 0.0, scale = 1.0;
            int digits = 0;
            for (; p < end && *p >= '0' && *p <= '9'; ++p, ++digits) v = v * 10.0 + (*p - '0');
            if (p < end && *p == '.')
                for (++p; p < end && *p >= '0' && *p <= '9'; ++p, ++digits) v += (*p - '0') * (scale *= 0.1);
            if (digits == 0 || (p < end && ! isSpace (*p))) { p = start; return false; }
            out = negative ? -v : v;
            return true;
        }
    }

    // One line (without its newline). Returns 1 for a key, 0 for a blank line or a comment,
    // -1 for anything else.
    inline int parseRigLine (const char* p, const char* end, RigKey& key) noexcept
    {
        while (p < end && rigparse::isSpace (*p)) ++p;
        if (p == end || *p == '#')
            return 0;
        double beat = 0.0, note = 0.0, velocity = 0.0, length = 0.0;
        if (! (rigparse::number (p, end, beat) && rigparse::number (p, end, note)
               && rigparse::number (p, end, velocity) && rigparse::number (p, end, length)))
            return -1;
        while (p < end && rigparse::isSpace (*p)) ++p;
        if (p != end && *p != '#')
            return -1;
        if (note < 0.0 || note > 127.0 || note != std::floor (note) || velocity < 1.0 || velocity > 127.0
            || velocity != std::floor (velocity) || ! (length > 0.0) || beat > 1.0e9 || beat < -1.0e9 || length > 1.0e9)
            return -1;
        key = { beat, beat + length, (uint8_t) note, (uint8_t) velocity };
        return 1;
    }

    // The whole file. Message thread.
    inline RigKeys parseRigText (const char* text, size_t length) noexcept
    {
        RigKeys k;
        const char* p = text;
        const char* const end = text + length;
        while (p < end)
        {
            const char* eol = p;
            while (eol < end && *eol != '\n') ++eol;
            RigKey key;
            const int r = parseRigLine (p, eol, key);
            if (r < 0)
                ++k.rejected;
            else if (r > 0)
            {
                if (k.count < RigKeys::capacity) k.keys[(size_t) k.count++] = key;
                else ++k.dropped;
            }
            p = eol < end ? eol + 1 : end;
        }
        return k;
    }

    class RigPlayer
    {
    public:
        static constexpr int channel = 0;   // MIDI channel 1

        // Adds this block's events to `out`, in sample order (key-ups before key-downs on one
        // sample). `on` is the testRig parameter (and Enable).
        template <class Out>
        void generate (const RigKeys& k, bool on, const Transport& t, int numSamples, Out& out) noexcept
        {
            if (numSamples <= 0)
                return;
            if (! on || ! t.playing)
            {
                releaseAll (0, out);
                hasEnd = false;
                return;
            }

            const double spb = t.samplesPerBeat();
            double from = t.ppq;
            // The host's ppq carries on from the last block (within two samples: hosts round):
            // take up exactly where that block ended, so no key falls between blocks or twice.
            if (hasEnd && std::abs (t.ppq - endPpq) <= 2.0 / spb)
                from = endPpq;
            else
                releaseAll (0, out);       // the start, a locate, or a loop wrap between blocks

            double to = t.ppq + numSamples / spb;
            if (t.looping && t.loopEnd > t.loopStart && t.ppq < t.loopEnd && to > t.loopEnd)
            {
                // The loop wraps inside this block: up to the loop end, then from its start.
                segment (k, from, t.loopEnd, t.ppq, spb, 0, numSamples, out);
                const int wrap = sampleOf (t.loopEnd, t.ppq, spb, 0, numSamples);
                releaseAll (wrap, out);
                to = t.loopStart + (to - t.loopEnd);
                segment (k, t.loopStart, to, t.loopStart, spb, wrap, numSamples, out);
            }
            else
            {
                segment (k, from, to, t.ppq, spb, 0, numSamples, out);
            }
            endPpq = to;
            hasEnd = true;
        }

        // Key-ups for everything held (stop, bypass, reset).
        template <class Out>
        void releaseAll (int sample, Out& out) noexcept
        {
            if (held == 0)
                return;
            for (int n = 0; n < 128; ++n)
                if (down[(size_t) n] > 0 && out.push (MidiEvent::noteOff (channel, n, sample)))
                {
                    down[(size_t) n] = 0;
                    --held;
                    ++noteOffs;
                }
        }

        void forgetPosition() noexcept { hasEnd = false; }

        int heldCount() const noexcept { return held; }
        uint32_t noteOnCount() const noexcept { return noteOns; }
        uint32_t noteOffCount() const noexcept { return noteOffs; }

    private:
        static int sampleOf (double ppq, double basePpq, double spb, int offset, int numSamples) noexcept
        {
            const double s = std::round ((ppq - basePpq) * spb) + offset;
            return s < offset ? offset : (s > numSamples - 1 ? numSamples - 1 : (int) s);
        }

        // The keys that start or end in [from, to), less half a sample (so each lands on its
        // nearest sample: one in a block's last half-sample is the next block's first sample, not
        // this block's last; Phase 3, so a chord on an Orbit line lands on the line's sample):
        // gathered, put in sample order (ups first), then played through the held-key counts, so
        // two keys on one pitch restrike rather than cut each other off, whatever their order in
        // the file.
        template <class Out>
        void segment (const RigKeys& k, double from, double to, double basePpq, double spb, int offset, int numSamples, Out& out) noexcept
        {
            due.clear();
            const double half = 0.5 / spb;
            from -= half;
            to -= half;
            for (int i = 0; i < k.count; ++i)
            {
                const auto& key = k.keys[(size_t) i];
                if (key.to >= from && key.to < to)
                    due.push (MidiEvent::noteOff (channel, key.note, sampleOf (key.to, basePpq, spb, offset, numSamples)));
                if (key.from >= from && key.from < to)
                    due.push (MidiEvent::noteOn (channel, key.note, key.velocity, sampleOf (key.from, basePpq, spb, offset, numSamples)));
            }
            due.sortBySample (true);

            for (const auto& e : due)
            {
                auto& count = down[(size_t) e.note()];
                if (e.isNoteOn())
                {
                    if (count > 0 && ! out.push (MidiEvent::noteOff (channel, e.note(), e.sample)))
                        continue;
                    if (count > 0)
                        ++noteOffs;
                    if (out.push (e))
                    {
                        held += count == 0 ? 1 : 0;
                        ++count;
                        ++noteOns;
                    }
                    else if (count > 0)
                    {
                        count = 0;        // its restrike's key-up went out, the key-down didn't
                        --held;
                    }
                }
                else if (count > 1)
                {
                    --count;              // another key still holds this pitch
                }
                else if (count == 1 && out.push (e))
                {
                    count = 0;
                    --held;
                    ++noteOffs;
                }
            }
        }

        EventBuffer<RigKeys::capacity * 2> due;
        std::array<uint16_t, 128> down {};  // keys holding each pitch
        int held = 0;                       // pitches sounding
        uint32_t noteOns = 0, noteOffs = 0;
        bool hasEnd = false;
        double endPpq = 0.0;                // where the last block ended, in ppq
    };
}
