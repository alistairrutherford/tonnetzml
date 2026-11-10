// Hands snapshots from one thread to another without locks or allocation (Chordism's PLAN.md §5,
// "Real-time safety rules"): the audio thread publishes what it played for the editor to show.
//
// Double-buffering with an atomic swap, in its safe form: three buffers. The writer fills its
// back buffer and swaps it with the shared middle one; the reader swaps its front buffer with
// the middle one when a new snapshot is waiting. The writer never touches the buffer the reader
// holds, however often it publishes, and neither side ever waits. One writer thread and one
// reader thread at a time.
//
// Plain C++, header-only.
#pragma once

#include <array>
#include <atomic>
#include <cstdint>

namespace tonnetz
{
    template <class T>
    class SnapshotExchange
    {
    public:
        SnapshotExchange() = default;
        explicit SnapshotExchange (const T& initial) { slots.fill (initial); }

        // Writer: makes `value` the latest snapshot.
        void publish (const T& value) noexcept
        {
            slots[back] = value;
            back = (uint8_t) (middle.exchange ((uint8_t) (back | fresh), std::memory_order_acq_rel) & indexMask);
        }

        // Reader: the latest snapshot. The reference stays valid until the reader's next call.
        const T& read() noexcept
        {
            if ((middle.load (std::memory_order_relaxed) & fresh) != 0)
                front = (uint8_t) (middle.exchange (front, std::memory_order_acq_rel) & indexMask);
            return slots[front];
        }

        // Reader: true if a snapshot newer than the last read() is waiting.
        bool hasNew() const noexcept { return (middle.load (std::memory_order_acquire) & fresh) != 0; }

    private:
        static constexpr uint8_t indexMask = 3, fresh = 4;

        std::array<T, 3> slots {};
        std::atomic<uint8_t> middle { 1 };
        uint8_t back = 0;                 // writer's
        uint8_t front = 2;                // reader's
    };
}
