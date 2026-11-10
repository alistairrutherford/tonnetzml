// What an instrument receiving the engine's output would hear: which notes are on, and whether
// the stream ever doubled a note-on, sent a stray note-off, or went backwards in time.
#pragma once

#include "core/engine/MidiEvents.h"
#include <algorithm>
#include <set>
#include <sstream>
#include <string>
#include <utility>

namespace testutil
{
    struct Receiver
    {
        std::set<std::pair<int, int>> on;     // (channel, note)
        int noteOns = 0, noteOffs = 0;
        int doubleOns = 0, strayOffs = 0, outOfOrder = 0, outOfBlock = 0;

        void feed (const tonnetz::BlockEvents& out, int numSamples)
        {
            int last = -1;
            for (const auto& e : out)
            {
                if (e.sample < last) ++outOfOrder;
                if (e.sample < 0 || e.sample >= std::max (1, numSamples)) ++outOfBlock;
                last = e.sample;

                const auto k = std::make_pair (e.channel(), e.note());
                if (e.isNoteOn())
                {
                    ++noteOns;
                    if (! on.insert (k).second) ++doubleOns;
                }
                else if (e.isNoteOff())
                {
                    ++noteOffs;
                    if (on.erase (k) == 0) ++strayOffs;
                }
                else if (e.isAllNotesOff())
                {
                    on.clear();
                }
            }
        }

        bool clean() const { return doubleOns == 0 && strayOffs == 0 && outOfOrder == 0 && outOfBlock == 0; }

        std::string describe() const
        {
            std::ostringstream o;
            o << on.size() << " sounding, " << noteOns << " on / " << noteOffs << " off, " << doubleOns
              << " double on, " << strayOffs << " stray off, " << outOfOrder << " out of order, " << outOfBlock
              << " outside the block";
            return o.str();
        }
    };
}
