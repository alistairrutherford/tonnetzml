// What the editor asks of the engine (PLAN.md §4: "the editor reads snapshots and writes
// commands, never engine state"): a single-producer, single-consumer FIFO of fixed size, written
// on the message thread and read on the audio thread at the start of each block. No locks, no
// allocation after construction.
//
//   NoteOn / NoteOff   a source key from the lattice (a triangle, a glide, a node set) or the
//                      keyboard strip: the processor puts it into the block's input as if the
//                      host had sent it, so it is gathered, voiced, heard by the key listener
//                      and, on the operator zone, taken as an operator key, like any other
//   Operator           operator key `index` (0..11: P L R N S H +5 -5 +M3 -M3 Undo Home), as a
//                      ghost-neighbour click: what the key would do, whether or not the zone is on
//   SetWalk            Walk / Explore's W replaced (the word bar: a chip dragged or dropped off)
//
// Plain C++, no JUCE.
#pragma once

#include "../lattice/Word.h"
#include <array>
#include <atomic>
#include <cstdint>

namespace tonnetz
{
    enum class EditorCommandType : uint8_t { NoteOn, NoteOff, Operator, SetWalk };

    struct EditorCommand
    {
        EditorCommandType type = EditorCommandType::NoteOn;
        uint8_t note = 0;          // NoteOn, NoteOff
        uint8_t velocity = 0;      // NoteOn
        int8_t index = 0;          // Operator
        Word word;                 // SetWalk

        static EditorCommand noteOn (int note, int velocity) noexcept
        {
            EditorCommand c;
            c.type = EditorCommandType::NoteOn;
            c.note = (uint8_t) (note & 127);
            c.velocity = (uint8_t) (velocity < 1 ? 1 : (velocity > 127 ? 127 : velocity));
            return c;
        }
        static EditorCommand noteOff (int note) noexcept
        {
            EditorCommand c;
            c.type = EditorCommandType::NoteOff;
            c.note = (uint8_t) (note & 127);
            return c;
        }
        static EditorCommand operatorKey (int index) noexcept
        {
            EditorCommand c;
            c.type = EditorCommandType::Operator;
            c.index = (int8_t) index;
            return c;
        }
        static EditorCommand setWalk (const Word& w) noexcept
        {
            EditorCommand c;
            c.type = EditorCommandType::SetWalk;
            c.word = w;
            return c;
        }
    };

    template <class T, int Capacity>
    class SpscFifo
    {
        static_assert ((Capacity & (Capacity - 1)) == 0, "a power of two");

    public:
        static constexpr int capacity = Capacity;

        // Producer. False (and nothing queued) when full.
        bool push (const T& value) noexcept
        {
            const uint32_t w = writeAt.load (std::memory_order_relaxed);
            if (w - readAt.load (std::memory_order_acquire) >= (uint32_t) Capacity)
                return false;
            items[(size_t) (w & (Capacity - 1))] = value;
            writeAt.store (w + 1, std::memory_order_release);
            return true;
        }

        // Consumer: the oldest item, or nothing.
        const T* peek() const noexcept
        {
            const uint32_t r = readAt.load (std::memory_order_relaxed);
            if (r == writeAt.load (std::memory_order_acquire))
                return nullptr;
            return &items[(size_t) (r & (Capacity - 1))];
        }
        void pop() noexcept { readAt.store (readAt.load (std::memory_order_relaxed) + 1, std::memory_order_release); }

        int size() const noexcept { return (int) (writeAt.load (std::memory_order_acquire) - readAt.load (std::memory_order_acquire)); }

    private:
        std::array<T, Capacity> items {};
        std::atomic<uint32_t> writeAt { 0 }, readAt { 0 };
    };

    using EditorCommandFifo = SpscFifo<EditorCommand, 512>;
}
