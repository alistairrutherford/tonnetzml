// The keys the editor holds down (PLAN.md §5, direct play and the keyboard strip): the lattice,
// the mouse on the keyboard strip and the computer keyboard each hold notes; a note sounds while
// any of them holds it. Presses and releases go to the engine as commands (EditorCommands.h),
// so they are source notes like the host's. Message thread.
#pragma once

#include "../plugin/TonnetzProcessor.h"
#include <array>

namespace tonnetz::ui
{
    class EditorNotes
    {
    public:
        enum Holder : uint8_t { lattice = 1, mouseKeys = 2, computerKeys = 4 };

        explicit EditorNotes (TonnetzProcessor& p) : processor (p) {}
        ~EditorNotes() { releaseAll(); }

        void press (int note, int velocity, Holder h)
        {
            if (note < 0 || note > 127)
                return;
            auto& o = owners[(size_t) note];
            if (o == 0)
                processor.sendEditorCommand (EditorCommand::noteOn (note, velocity), &pressSeq[(size_t) note]);
            o = (uint8_t) (o | h);
        }

        void release (int note, Holder h)
        {
            if (note < 0 || note > 127)
                return;
            auto& o = owners[(size_t) note];
            if ((o & h) == 0)
                return;
            o = (uint8_t) (o & ~h);
            if (o == 0)
                processor.sendEditorCommand (EditorCommand::noteOff (note));
        }

        void releaseHolder (Holder h)
        {
            for (int n = 0; n < 128; ++n)
                release (n, h);
        }

        void releaseAll()
        {
            for (int n = 0; n < 128; ++n)
                if (owners[(size_t) n] != 0)
                {
                    owners[(size_t) n] = 0;
                    processor.sendEditorCommand (EditorCommand::noteOff (n));
                }
        }

        // The engine let go of everything (stop, Enable off): it ignores the note-offs that
        // would follow, so the editor just forgets.
        void forget() { owners.fill (0); }

        // The engine let go of everything up to editor note `through` (EngineStatus::
        // releasedThrough): forgets the notes pressed then or before. A note pressed after it
        // (it reached the engine after the release, or is still queued) still sounds and is
        // kept, so its release is still sent. (A press the queue refused, seq 0, is forgotten.)
        void forgetThrough (uint32_t through)
        {
            for (size_t n = 0; n < owners.size(); ++n)
                if (owners[n] != 0 && (pressSeq[n] == 0 || (int32_t) (pressSeq[n] - through) <= 0))
                    owners[n] = 0;
        }

        bool holds (int note, Holder h) const noexcept { return note >= 0 && note < 128 && (owners[(size_t) note] & h) != 0; }
        bool holds (int note) const noexcept { return note >= 0 && note < 128 && owners[(size_t) note] != 0; }
        bool any (Holder h) const noexcept
        {
            for (auto o : owners)
                if ((o & h) != 0)
                    return true;
            return false;
        }

    private:
        TonnetzProcessor& processor;
        std::array<uint8_t, 128> owners {};
        std::array<uint32_t, 128> pressSeq {};       // the seq of each held note's note-on
    };
}
