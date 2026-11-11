// Where an incoming MIDI event goes (PLAN.md §3.2, §4): a source note (part of the chord the
// engine transforms), an operator key (an octave of keys that edit the word in Walk, Phase 3;
// they never sound), or anything else (controllers, pitch bend, …: passed through).
//
// Phase 2 has no operator-key parameters yet (`opZoneStart`, `opZoneOn` arrive in Phase 3), so
// the zone is off by default and every note is a source note. A note-off is routed by what its
// note-on was (the engine asks the gatherer whether the key is held as a source key), so moving
// the zone while keys are down never leaves one stuck.
//
// Plain C++, no JUCE, no allocation.
#pragma once

#include "MidiEvents.h"
#include <cstdint>

namespace tonnetz
{
    enum class Route : uint8_t
    {
        Source,               // a note of the source chord
        Operator,             // a key in the operator zone: never sounds
        Other                 // not a note: passed through
    };

    struct ZoneSettings
    {
        bool on = false;
        int start = 24;       // the zone's lowest key: C0 in Live's naming (C3 = 60)

        bool operator== (const ZoneSettings& o) const noexcept { return on == o.on && start == o.start; }
        bool operator!= (const ZoneSettings& o) const noexcept { return ! (*this == o); }

        bool contains (int note) const noexcept { return on && note >= start && note < start + 12; }

        // 0..11: which operator key (Phase 3 maps them to ops), or -1.
        int operatorIndex (int note) const noexcept { return contains (note) ? note - start : -1; }
    };

    // Routes a note-on, or anything that isn't a note. (Note-offs: see the top of the file.)
    inline Route route (const MidiEvent& e, const ZoneSettings& zone) noexcept
    {
        if (! e.isNoteOn() && ! e.isNoteOff())
            return Route::Other;
        return zone.contains (e.note()) ? Route::Operator : Route::Source;
    }
}
