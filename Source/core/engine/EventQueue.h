// The scheduler's queue (Chordism's PLAN.md §4.5, "Scheduling"): future note-ons and note-offs,
// kept across blocks.
//
// Fixed capacity, allocated with its owner (prepareToPlay), never resized. An event is timed
// either in musical time (PPQ; the rhythmic effects) or in absolute samples (Strum while
// stopped). A musical event's sample is worked out from the clock in the block it falls in, so
// a tempo change moves every musical event to its new sample by itself. On a loop wrap or a
// locate the owner moves the musical events (`forEach`), see ChordFx.h.
//
// Events due at the same sample come out in `order` (lower first: note-offs before note-ons,
// so a pitch that ends and starts on one sample is restruck), then in the order they were
// pushed. Plain C++, real-time safe; the queue is short, so a linear scan is fine.
// Copied from ChordMonkey (namespace tonnetz); TonnetzML schedules with it from Phase 3 (Orbit).
#pragma once

#include "Clock.h"
#include <array>
#include <cmath>
#include <cstdint>

namespace tonnetz
{
    struct TimedEvent
    {
        enum class Base : uint8_t { Musical, Samples };

        double ppq = 0.0;                 // Musical
        int64_t sample = 0;               // Samples: absolute sample (Clock::blockStart based)
        Base base = Base::Musical;
        uint8_t type = 0;                 // the owner's meaning
        uint8_t order = 0;                // ties at one sample: lower first
        uint16_t owner = 0;
        uint32_t tag = 0;                 // e.g. the owner's generation when pushed
        uint32_t arg = 0;
        uint32_t seq = 0;                 // set by push

        static TimedEvent musical (double ppq, uint8_t type, uint8_t order, uint16_t owner, uint32_t tag, uint32_t arg) noexcept
        {
            TimedEvent e;
            e.ppq = ppq;
            e.base = Base::Musical;
            e.type = type;
            e.order = order;
            e.owner = owner;
            e.tag = tag;
            e.arg = arg;
            return e;
        }

        static TimedEvent atSample (int64_t sample, uint8_t type, uint8_t order, uint16_t owner, uint32_t tag, uint32_t arg) noexcept
        {
            TimedEvent e = musical (0.0, type, order, owner, tag, arg);
            e.base = Base::Samples;
            e.sample = sample;
            return e;
        }
    };

    template <int Capacity>
    class EventQueue
    {
    public:
        static constexpr int capacity = Capacity;

        bool push (TimedEvent e) noexcept
        {
            if (count >= Capacity)
                return false;
            e.seq = nextSeq++;
            events[(size_t) count++] = e;
            return true;
        }

        void clear() noexcept { count = 0; }
        int size() const noexcept { return count; }
        bool isEmpty() const noexcept { return count == 0; }
        const TimedEvent& operator[] (int i) const noexcept { return events[(size_t) i]; }

        // The sample of the block (in segment `seg`) where `e` falls, never before `floor`.
        static int blockSample (const TimedEvent& e, const Clock& clock, int seg, int floor) noexcept
        {
            double s = e.base == TimedEvent::Base::Musical ? clock.sampleAt (seg, e.ppq)
                                                           : (double) (e.sample - clock.blockStart());
            s = std::round (s);
            if (s < floor)
                return floor;
            return s > 2.0e9 ? 2000000000 : (int) s;
        }

        // The next event due in segment `seg` before block sample `limit`: its index, and its
        // sample in `at` (at least `floor`). -1 if none. A musical event at or past the
        // segment's musical end (a loop end) isn't due: it belongs after the wrap.
        int earliestDue (const Clock& clock, int seg, int floor, int limit, int& at) const noexcept
        {
            int best = -1, bestAt = 0;
            const double endPpq = clock.segment (seg).endPpq;
            for (int i = 0; i < count; ++i)
            {
                const auto& e = events[(size_t) i];
                const int s = blockSample (e, clock, seg, floor);
                if (s >= limit || (e.base == TimedEvent::Base::Musical && e.ppq >= endPpq - 1.0e-9))
                    continue;
                if (best < 0 || s < bestAt
                    || (s == bestAt && (e.order < events[(size_t) best].order
                                        || (e.order == events[(size_t) best].order && e.seq < events[(size_t) best].seq))))
                {
                    best = i;
                    bestAt = s;
                }
            }
            at = bestAt;
            return best;
        }

        // Removes event i and returns it.
        TimedEvent take (int i) noexcept
        {
            const TimedEvent e = events[(size_t) i];
            events[(size_t) i] = events[(size_t) --count];
            return e;
        }

        template <class Pred>
        void removeIf (Pred pred) noexcept
        {
            for (int i = 0; i < count;)
            {
                if (pred (events[(size_t) i]))
                    events[(size_t) i] = events[(size_t) --count];
                else
                    ++i;
            }
        }

        template <class Fn>
        void forEach (Fn fn) noexcept
        {
            for (int i = 0; i < count; ++i)
                fn (events[(size_t) i]);
        }

    private:
        std::array<TimedEvent, Capacity> events {};
        int count = 0;
        uint32_t nextSeq = 0;
    };
}
