// Short MIDI messages with a sample position, and a fixed-capacity buffer for them.
// Plain C++, no JUCE, no allocation after construction.
#pragma once

#include <array>
#include <cstdint>

namespace tonnetz
{
    struct MidiEvent
    {
        int32_t sample = 0;               // offset in the current block
        uint8_t data[3] {};
        uint8_t size = 0;

        static MidiEvent noteOn (int channel, int note, int velocity, int sample) noexcept
        {
            return { sample, { (uint8_t) (0x90 | (channel & 15)), (uint8_t) (note & 127), (uint8_t) (velocity & 127) }, 3 };
        }

        static MidiEvent noteOff (int channel, int note, int sample) noexcept
        {
            return { sample, { (uint8_t) (0x80 | (channel & 15)), (uint8_t) (note & 127), 0 }, 3 };
        }

        static MidiEvent controller (int channel, int cc, int value, int sample) noexcept
        {
            return { sample, { (uint8_t) (0xb0 | (channel & 15)), (uint8_t) (cc & 127), (uint8_t) (value & 127) }, 3 };
        }

        int status() const noexcept { return data[0] & 0xf0; }
        int channel() const noexcept { return data[0] & 0x0f; }
        int note() const noexcept { return data[1]; }
        int velocity() const noexcept { return data[2]; }

        bool isNoteOn() const noexcept { return size == 3 && status() == 0x90 && data[2] > 0; }
        bool isNoteOff() const noexcept { return size == 3 && (status() == 0x80 || (status() == 0x90 && data[2] == 0)); }
        bool isController (int cc) const noexcept { return size == 3 && status() == 0xb0 && data[1] == cc; }
        bool isAllNotesOff() const noexcept { return isController (123) || isController (120); }   // also All Sound Off
    };

    template <int Capacity>
    class EventBuffer
    {
    public:
        static constexpr int capacity = Capacity;

        bool push (const MidiEvent& e) noexcept
        {
            if (count >= Capacity)
                return false;
            events[(size_t) count++] = e;
            return true;
        }

        void clear() noexcept { count = 0; }
        int size() const noexcept { return count; }
        int space() const noexcept { return Capacity - count; }

        // Stable insertion sort by sample (a few events merged into a sorted block). With
        // `offsFirst`, note-offs come before the other events of their sample, so a pitch that
        // ends and starts on one sample is restruck.
        void sortBySample (bool offsFirst = false) noexcept
        {
            const auto rank = [offsFirst] (const MidiEvent& e) noexcept
            {
                return (int64_t) e.sample * 2 + (offsFirst && ! e.isNoteOff() ? 1 : 0);
            };
            for (int i = 1; i < count; ++i)
            {
                const MidiEvent e = events[(size_t) i];
                int j = i;
                for (; j > 0 && rank (events[(size_t) j - 1]) > rank (e); --j)
                    events[(size_t) j] = events[(size_t) j - 1];
                events[(size_t) j] = e;
            }
        }
        const MidiEvent& operator[] (int i) const noexcept { return events[(size_t) i]; }
        const MidiEvent* begin() const noexcept { return events.data(); }
        const MidiEvent* end() const noexcept { return events.data() + count; }

    private:
        std::array<MidiEvent, Capacity> events {};
        int count = 0;
    };

    using BlockEvents = EventBuffer<4096>;
}
