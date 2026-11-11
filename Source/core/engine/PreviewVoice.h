// Preview tone (PLAN.md §3.7, Phase 6): a small built-in polyphonic voice so the keyboard and the
// lattice can be tried with nothing routed. It plays what TonnetzML sends (every note-on and
// note-off of the block's output, on the sample) plus the editor's auditions (a ghost
// neighbour hovered, heard but never sent as MIDI).
//
// Sixteen voices of a soft electric-piano-like tone: a sine with a quieter octave and a touch of
// the twelfth, a 4 ms attack, a decay to 60 % over 1.2 s and a 220 ms release; velocity sets the
// level. A seventeenth note takes the voice that has been releasing longest, else the oldest.
// The same pitch from two sources is one voice (a second note-on restrikes it; a note-off ends
// it, as a MIDI instrument would).
//
// Plain C++, no JUCE, no allocation, no locks: render() is for the audio thread.
#pragma once

#include "MidiEvents.h"
#include <array>
#include <cmath>
#include <cstdint>

namespace tonnetz
{
    class PreviewVoice
    {
    public:
        static constexpr int numVoices = 16;
        static constexpr float level = 0.16f;            // one voice at velocity 127

        void prepare (double sampleRate) noexcept
        {
            rate = sampleRate > 0.0 && std::isfinite (sampleRate) ? sampleRate : 44100.0;
            attackStep = (float) (1.0 / (0.004 * rate));
            decayCoeff = (float) std::exp (-1.0 / (1.2 * rate));
            releaseCoeff = (float) std::exp (-1.0 / (0.024 * rate));  // ~220 ms to -80 dB
        }

        // Ends every note at once (bypass, the parameter going off, reset).
        void silence() noexcept
        {
            for (auto& v : voices)
                v = {};
        }

        bool isSilent() const noexcept
        {
            for (const auto& v : voices)
                if (v.stage != Stage::Off)
                    return false;
            return true;
        }

        int activeVoices() const noexcept
        {
            int n = 0;
            for (const auto& v : voices)
                n += v.stage != Stage::Off ? 1 : 0;
            return n;
        }

        // Adds the block's events (any order within the block is fine as long as `events` is
        // sorted by sample) into `left` and `right` (mixed in, not overwritten).
        void render (const MidiEvent* events, int numEvents, float* left, float* right, int numSamples) noexcept
        {
            int e = 0;
            for (int s = 0; s < numSamples; ++s)
            {
                for (; e < numEvents && events[e].sample <= s; ++e)
                    handle (events[e]);
                float out = 0.0f;
                for (auto& v : voices)
                    if (v.stage != Stage::Off)
                        out += next (v);
                if (left != nullptr)
                    left[s] += out;
                if (right != nullptr)
                    right[s] += out;
            }
            for (; e < numEvents; ++e)
                handle (events[e]);
        }

        void noteOn (int note, int velocity) noexcept
        {
            if (note < 0 || note > 127)
                return;
            Voice* target = nullptr;
            for (auto& v : voices)
                if (v.stage != Stage::Off && v.note == note)
                    target = &v;                          // restrike
            if (target == nullptr)
                for (auto& v : voices)
                    if (v.stage == Stage::Off)
                    {
                        target = &v;
                        break;
                    }
            if (target == nullptr)
            {
                // Steal: the longest releasing, else the oldest.
                for (auto& v : voices)
                    if (target == nullptr || score (v) > score (*target))
                        target = &v;
            }
            Voice& v = *target;
            const bool restrike = v.stage != Stage::Off && v.note == note;
            v.note = note;
            v.gain = level * (float) (velocity < 1 ? 1 : (velocity > 127 ? 127 : velocity)) / 127.0f;
            v.stage = Stage::Attack;
            if (! restrike)
            {
                v.env = 0.0f;
                v.phase = 0.0;
            }
            v.age = ++clock;
            v.inc = 2.0 * 3.14159265358979323846 * 440.0 * std::pow (2.0, (note - 69) / 12.0) / rate;
        }

        void noteOff (int note) noexcept
        {
            for (auto& v : voices)
                if (v.stage != Stage::Off && v.stage != Stage::Release && v.note == note)
                {
                    v.stage = Stage::Release;
                    v.age = ++clock;
                }
        }

    private:
        enum class Stage : uint8_t { Off, Attack, Decay, Release };

        struct Voice
        {
            Stage stage = Stage::Off;
            int note = -1;
            float gain = 0.0f, env = 0.0f;
            double phase = 0.0, inc = 0.0;
            uint64_t age = 0;
        };

        void handle (const MidiEvent& e) noexcept
        {
            if (e.isNoteOn())
                noteOn (e.note(), e.velocity());
            else if (e.isNoteOff())
                noteOff (e.note());
            else if (e.isAllNotesOff())
                for (auto& v : voices)
                    if (v.stage != Stage::Off)
                        v.stage = Stage::Release;
        }

        // Higher = better to steal.
        int64_t score (const Voice& v) const noexcept
        {
            const int64_t since = (int64_t) (clock - v.age);
            return v.stage == Stage::Release ? (int64_t) 1 << 40 | since : since;
        }

        float next (Voice& v) noexcept
        {
            switch (v.stage)
            {
                case Stage::Attack:
                    v.env += attackStep;
                    if (v.env >= 1.0f)
                    {
                        v.env = 1.0f;
                        v.stage = Stage::Decay;
                    }
                    break;
                case Stage::Decay:
                    v.env = 0.6f + (v.env - 0.6f) * decayCoeff;
                    break;
                case Stage::Release:
                    v.env *= releaseCoeff;
                    if (v.env < 1.0e-4f)
                    {
                        v = {};
                        return 0.0f;
                    }
                    break;
                case Stage::Off:
                    return 0.0f;
            }
            const double p = v.phase;
            v.phase += v.inc;
            if (v.phase > 2.0 * 3.14159265358979323846)
                v.phase -= 2.0 * 3.14159265358979323846;
            const float tone = (float) (std::sin (p) + 0.28 * std::sin (2.0 * p) + 0.08 * std::sin (3.0 * p));
            return tone * v.env * v.gain;
        }

        std::array<Voice, numVoices> voices {};
        double rate = 44100.0;
        float attackStep = 1.0f / 176.4f, decayCoeff = 0.99998f, releaseCoeff = 0.9995f;
        uint64_t clock = 0;
    };
}
