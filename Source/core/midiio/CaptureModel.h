// The capture's message-thread half (PLAN.md §3.5): the notes the CaptureQueue delivers, kept
// two ways, and turned into chords and MIDI files.
//
//   the last 64 bars   always on (retrospective): every captured note that ended less than 64
//                      bars (of the current meter) before the tape's present, and every note
//                      still sounding
//   the take           while `capture` is armed, every note from the arming on (notes already
//                      sounding join it at the arming), open-ended; disarming ends it (notes
//                      still sounding are cut there) and keeps it until the next arming starts
//                      a new one
// `current` is what the strip shows and the drag-out exports: the take if there is one, else
// the last 64 bars.
//
// Renderings (MidiSong, MidiFile.h; 960 ppq, the tape's tempo and meter, the key in force):
//   As played  every note exactly as captured (to the tick), the file starting at the bar
//              line at or before its first note
//   Chords     one block chord per chord change (`chordsOf`), each snapped to the grid (a
//              sixteenth): every note of a chord starts and ends together; a chord that would
//              overlap the next is cut at the next's start; one that snaps to nothing is dropped
// Generated notes go on one track and, if any were captured, source notes on a second.
//
// Chords (`chordsOf`): the stream's sounding pitch set, piece by piece. A piece shorter than
// `minChord` (1/16 beat: a glide passing through) gives its time to the piece it touches after
// it (or before it); so does a piece shorter than a sixteenth whose notes are all in the piece
// it touches (a chord spread by hand, or let go raggedly); touching pieces with the same pitches
// are one chord; silence ends a chord.
//
// Plain C++ (std::vector: message thread only).
#pragma once

#include "CaptureQueue.h"
#include "MidiFile.h"
#include "../lattice/Lattice.h"
#include <algorithm>
#include <array>
#include <bitset>
#include <cmath>
#include <string>
#include <vector>

namespace tonnetz
{
    struct CapturedNote
    {
        double start = 0.0, end = 0.0;     // tape beats (end: where it is now, while open)
        uint64_t id = 0;
        uint8_t pitch = 60, velocity = 100, channel = 0;
        CaptureStream stream = CaptureStream::Generated;
        bool open = false;
    };

    struct CapturedChord
    {
        double start = 0.0, end = 0.0;
        CaptureStream stream = CaptureStream::Generated;
        uint8_t channel = 0;
        std::array<uint8_t, 128> velocity {};   // 0 = not in the chord
        int count = 0;

        PcMask mask() const noexcept
        {
            unsigned m = 0;
            for (int n = 0; n < 128; ++n)
                if (velocity[(size_t) n] != 0)
                    m |= pcBit (n);
            return (PcMask) m;
        }
        int lowest() const noexcept
        {
            for (int n = 0; n < 128; ++n)
                if (velocity[(size_t) n] != 0)
                    return n;
            return -1;
        }
        std::vector<int> pitches() const
        {
            std::vector<int> p;
            for (int n = 0; n < 128; ++n)
                if (velocity[(size_t) n] != 0)
                    p.push_back (n);
            return p;
        }
        bool subsetOf (const CapturedChord& o) const noexcept
        {
            for (int n = 0; n < 128; ++n)
                if (velocity[(size_t) n] != 0 && o.velocity[(size_t) n] == 0)
                    return false;
            return true;
        }
        bool samePitches (const CapturedChord& o) const noexcept
        {
            for (int n = 0; n < 128; ++n)
                if ((velocity[(size_t) n] != 0) != (o.velocity[(size_t) n] != 0))
                    return false;
            return true;
        }
    };

    enum class CaptureRendering : uint8_t { AsPlayed, Chords };

    constexpr double captureGrid = 0.25;          // Chords: a sixteenth
    constexpr double captureMinChord = 1.0 / 16.0; // beats: shorter pieces aren't chords of their own

    // The chords of one stream's notes (sorted by start), oldest first.
    inline std::vector<CapturedChord> chordsOf (const std::vector<CapturedNote>& notes, CaptureStream stream,
                                                double minChord = captureMinChord, double partOf = captureGrid)
    {
        struct Edge { double at; bool on; uint8_t pitch, velocity, channel; };
        std::vector<Edge> edges;
        for (const auto& n : notes)
            if (n.stream == stream && n.end > n.start)     // a note of no length sounds nothing
            {
                edges.push_back ({ n.start, true, n.pitch, n.velocity, n.channel });
                edges.push_back ({ n.end, false, n.pitch, n.velocity, n.channel });
            }
        std::stable_sort (edges.begin(), edges.end(), [] (const Edge& a, const Edge& b)
        {
            if (a.at != b.at)
                return a.at < b.at;
            return ! a.on && b.on;          // offs first: a retune is one change
        });

        // The pieces: a constant pitch set between consecutive edge times.
        std::vector<CapturedChord> pieces;
        std::array<int, 128> count {};
        std::array<uint8_t, 128> vel {};
        uint8_t channel = 0;
        int sounding = 0;
        for (size_t i = 0; i < edges.size();)
        {
            const double at = edges[i].at;
            for (; i < edges.size() && edges[i].at == at; ++i)
            {
                const auto& e = edges[i];
                auto& c = count[e.pitch];
                if (e.on)
                {
                    if (c++ == 0)
                        ++sounding;
                    vel[e.pitch] = std::max (vel[e.pitch], e.velocity);
                    channel = e.channel;
                }
                else if (c > 0 && --c == 0)
                {
                    --sounding;
                    vel[e.pitch] = 0;
                }
            }
            if (! pieces.empty() && pieces.back().end < 0.0)
                pieces.back().end = at;
            if (sounding > 0 && i < edges.size())
            {
                CapturedChord p;
                p.start = at;
                p.end = -1.0;
                p.stream = stream;
                p.channel = channel;
                for (int n = 0; n < 128; ++n)
                    if (count[(size_t) n] > 0)
                    {
                        p.velocity[(size_t) n] = std::max<uint8_t> (1, vel[(size_t) n]);
                        ++p.count;
                    }
                pieces.push_back (p);
            }
        }
        if (! pieces.empty() && pieces.back().end < 0.0)
            pieces.pop_back();

        // Short pieces give their time to a neighbour they touch: a passing piece (shorter than
        // `minChord`) to either, a piece that is part of its neighbour (a chord building up as
        // a hand spreads it, or dying away as it lets go raggedly) to that neighbour if it is
        // shorter than `partOf`.
        std::vector<CapturedChord> kept;
        for (size_t i = 0; i < pieces.size(); ++i)
        {
            auto p = pieces[i];
            const double length = p.end - p.start;
            const bool touchesNext = i + 1 < pieces.size() && pieces[i + 1].start == p.end;
            const bool touchesPrev = ! kept.empty() && kept.back().end == p.start;
            const bool inNext = touchesNext && p.subsetOf (pieces[i + 1]);
            const bool inPrev = touchesPrev && p.subsetOf (kept.back());
            if (touchesNext && (inNext ? length < partOf : length < minChord && ! inPrev))
            {
                pieces[i + 1].start = p.start;
                continue;
            }
            if (touchesPrev && (inPrev ? length < partOf : length < minChord))
            {
                kept.back().end = p.end;
                continue;
            }
            kept.push_back (p);
        }

        // Touching pieces with the same pitches are one chord.
        std::vector<CapturedChord> chords;
        for (const auto& p : kept)
        {
            if (! chords.empty() && chords.back().end == p.start && chords.back().samePitches (p))
            {
                chords.back().end = p.end;
                for (int n = 0; n < 128; ++n)
                    chords.back().velocity[(size_t) n] = std::max (chords.back().velocity[(size_t) n], p.velocity[(size_t) n]);
                continue;
            }
            chords.push_back (p);
        }
        return chords;
    }

    // Both streams' chords, in time order (a source chord before the generated one it gives).
    inline std::vector<CapturedChord> chordsOf (const std::vector<CapturedNote>& notes)
    {
        auto all = chordsOf (notes, CaptureStream::Source);      // a source chord first when both start together
        const auto g = chordsOf (notes, CaptureStream::Generated);
        all.insert (all.end(), g.begin(), g.end());
        std::stable_sort (all.begin(), all.end(), [] (const CapturedChord& a, const CapturedChord& b) { return a.start < b.start; });
        return all;
    }

    struct CaptureFileInfo
    {
        double bpm = 120.0;
        int numerator = 4, denominator = 4;
        int keyTonic = -1;                 // the key in force, -1 = none
        bool keyMinor = false;
        std::string name = "TonnetzML";    // the song's and the generated track's name
        std::string sourceName = "TonnetzML source";

        double barLength() const noexcept { return numerator > 0 && denominator > 0 ? numerator * 4.0 / denominator : 4.0; }
    };

    // The key signature of a key: sharps (negative: flats) as written (Lattice.h's spelling of
    // the tonic), and the mode.
    inline MidiKeySignature keySignatureOf (int tonic, bool minor) noexcept
    {
        MidiKeySignature k;
        if (tonic < 0 || tonic > 11)
            return k;
        k.present = true;
        k.minor = minor;
        k.sharps = tonicFifthsIndex (tonic, minor) - (minor ? 3 : 0);
        return k;
    }

    // The bar line at or before a beat (the file's start).
    inline double captureOrigin (double firstBeat, double bar) noexcept
    {
        bar = bar > 0.0 ? bar : 4.0;
        return std::floor (firstBeat / bar + 1.0e-9) * bar;
    }

    // The notes (any order) as a MIDI song.
    inline MidiSong renderCapture (const std::vector<CapturedNote>& notes, const CaptureFileInfo& info,
                                   CaptureRendering rendering, int ppq = 960)
    {
        MidiSong song;
        song.ppq = ppq;
        song.bpm = info.bpm;
        song.numerator = info.numerator;
        song.denominator = info.denominator;
        song.key = keySignatureOf (info.keyTonic, info.keyMinor);
        song.name = info.name;
        if (notes.empty())
            return song;

        double first = notes.front().start;
        for (const auto& n : notes)
            first = std::min (first, n.start);
        const double origin = captureOrigin (first, info.barLength());
        const auto tick = [&] (double beat) { return (int64_t) std::llround ((beat - origin) * ppq); };

        for (const auto stream : { CaptureStream::Generated, CaptureStream::Source })
        {
            MidiTrack track;
            track.name = stream == CaptureStream::Generated ? info.name : info.sourceName;
            if (rendering == CaptureRendering::AsPlayed)
            {
                for (const auto& n : notes)
                    if (n.stream == stream)
                    {
                        MidiNote m { tick (n.start), tick (n.end), n.pitch, n.velocity, n.channel };
                        if (m.end <= m.start)
                            m.end = m.start + 1;
                        track.notes.push_back (m);
                    }
            }
            else
            {
                const auto snap = [] (double beat) { return std::round (beat / captureGrid) * captureGrid; };
                auto chords = chordsOf (notes, stream);
                std::vector<std::pair<double, double>> at;
                for (const auto& c : chords)
                {
                    const double s = snap (c.start);
                    double e = snap (c.end);
                    if (e <= s)
                        e = s + captureGrid;
                    at.push_back ({ s, e });
                }
                for (size_t i = 0; i < chords.size(); ++i)
                {
                    if (i + 1 < chords.size() && at[i + 1].first < at[i].second)
                        at[i].second = at[i + 1].first;
                    if (at[i].second <= at[i].first)
                        continue;           // two chords on one grid point: the later one stays
                    for (int p = 0; p < 128; ++p)
                        if (chords[i].velocity[(size_t) p] != 0)
                            track.notes.push_back ({ tick (at[i].first), tick (at[i].second), (uint8_t) p,
                                                     chords[i].velocity[(size_t) p], chords[i].channel });
                }
            }
            if (track.notes.empty())
                continue;
            smf::sortNotes (track.notes);
            song.tracks.push_back (std::move (track));
        }
        return song;
    }

    class CaptureModel
    {
    public:
        static constexpr int retroBars = 64;
        static constexpr size_t maxTakeNotes = 1u << 20;   // a take's ceiling (about 25 MB)

        // Takes everything waiting in the queue. True if anything changed.
        bool drain (CaptureQueue& q)
        {
            bool changed = false;
            const uint32_t dropped = q.dropped();
            CaptureEvent e;
            while (q.pop (e))
            {
                apply (e);
                changed = true;
            }
            // After what was queued: a note whose note-off was dropped is in it.
            if (dropped != seenDropped)
            {
                seenDropped = dropped;
                lose();
                changed = true;
            }
            const auto& c = q.clock();
            if (c.blocks != clockInfo.blocks)
            {
                clockInfo = c;
                now = std::max (now, c.now);
                prune();
                if (anyOpen)
                    changed = true;         // open notes grow
            }
            if (changed)
                ++rev;
            return changed;
        }

        void apply (const CaptureEvent& e)
        {
            now = std::max (now, e.beat);
            switch (e.kind)
            {
                case CaptureKind::TakeStart:
                    take.clear();
                    armed = true;
                    haveTake = true;
                    takeStart = e.beat;
                    for (const auto& n : retro)     // what sounds joins the take at the arming
                        if (n.open)
                        {
                            auto j = n;
                            j.start = e.beat;
                            take.push_back (j);
                        }
                    break;

                case CaptureKind::TakeEnd:
                    if (armed)
                        for (auto& n : take)
                            if (n.open)
                            {
                                n.open = false;
                                n.end = std::max (n.start, e.beat);
                            }
                    armed = false;
                    takeEnd = e.beat;
                    break;

                case CaptureKind::NoteOn:
                {
                    CapturedNote n;
                    n.start = n.end = e.beat;
                    n.id = ++lastId;
                    n.pitch = e.pitch;
                    n.velocity = e.velocity;
                    n.channel = e.channel;
                    n.stream = e.stream;
                    n.open = true;
                    retro.push_back (n);
                    if (armed && take.size() < maxTakeNotes)
                        take.push_back (n);
                    openIds[slotOf (e)].push_back (n.id);
                    anyOpen = true;
                    break;
                }

                case CaptureKind::NoteOff:
                {
                    auto& ids = openIds[slotOf (e)];
                    if (ids.empty())
                        break;              // never captured (or lost)
                    const uint64_t id = ids.front();
                    ids.erase (ids.begin());
                    close (retro, id, e.beat);
                    if (armed)
                        close (take, id, e.beat);
                    break;
                }
            }
        }

        // The queue overflowed: whatever was open is ended where the tape is, and note-offs
        // still to come for them are ignored.
        void lose()
        {
            for (auto* list : { &retro, &take })
                for (auto& n : *list)
                    if (n.open)
                    {
                        n.open = false;
                        n.end = std::max (n.start, now);
                    }
            for (auto& ids : openIds)
                ids.clear();
            anyOpen = false;
            ++losses;
        }

        void clear()
        {
            retro.clear();
            take.clear();
            haveTake = false;
            for (auto& ids : openIds)
                ids.clear();
            anyOpen = false;
            ++rev;
        }

        // ---- What is kept --------------------------------------------------------------------
        bool isArmed() const noexcept { return armed; }
        bool hasTake() const noexcept { return haveTake; }
        bool showingTake() const noexcept { return haveTake && (armed || ! take.empty()); }
        double tapeNow() const noexcept { return now; }
        const CaptureClockInfo& clock() const noexcept { return clockInfo; }
        uint32_t revision() const noexcept { return rev; }
        uint32_t lossCount() const noexcept { return losses; }

        // The notes with open ones ended at the present, sorted by start (then pitch).
        std::vector<CapturedNote> retrospective() const { return finished (retro); }
        std::vector<CapturedNote> takeNotes() const { return finished (take); }
        std::vector<CapturedNote> current() const { return showingTake() ? takeNotes() : retrospective(); }

        // How many bars `current` spans (from its first bar line to its last note's end).
        int bars() const
        {
            const auto n = current();
            if (n.empty())
                return 0;
            double first = n.front().start, last = n.front().end;
            for (const auto& x : n)
            {
                first = std::min (first, x.start);
                last = std::max (last, x.end);
            }
            const double bar = clockInfo.barLength();
            return std::max (1, (int) std::ceil ((last - captureOrigin (first, bar)) / bar - 1.0e-9));
        }

    private:
        static size_t slotOf (const CaptureEvent& e) noexcept
        {
            return (size_t) ((int) e.stream * 16 * 128 + (e.channel & 15) * 128 + (e.pitch & 127));
        }

        static void close (std::vector<CapturedNote>& list, uint64_t id, double beat)
        {
            for (auto it = list.rbegin(); it != list.rend(); ++it)
                if (it->id == id)
                {
                    it->open = false;
                    it->end = std::max (it->start, beat);
                    return;
                }
        }

        std::vector<CapturedNote> finished (const std::vector<CapturedNote>& list) const
        {
            auto out = list;
            for (auto& n : out)
                if (n.open)
                    n.end = std::max (n.start, now);
            std::stable_sort (out.begin(), out.end(), [] (const CapturedNote& a, const CapturedNote& b)
            {
                return a.start != b.start ? a.start < b.start : a.pitch < b.pitch;
            });
            return out;
        }

        // Drops what ended more than 64 bars ago.
        void prune()
        {
            const double from = now - retroBars * clockInfo.barLength();
            if (! retro.empty() && retro.front().start < from)
                retro.erase (std::remove_if (retro.begin(), retro.end(), [from] (const CapturedNote& n) { return ! n.open && n.end < from; }),
                             retro.end());
            anyOpen = false;
            for (const auto& ids : openIds)
                if (! ids.empty())
                {
                    anyOpen = true;
                    break;
                }
        }

        std::vector<CapturedNote> retro, take;
        std::array<std::vector<uint64_t>, 2 * 16 * 128> openIds;
        CaptureClockInfo clockInfo;
        double now = 0.0, takeStart = 0.0, takeEnd = 0.0;
        uint64_t lastId = 0;
        uint32_t seenDropped = 0, rev = 0, losses = 0;
        bool armed = false, haveTake = false, anyOpen = false;
    };
}
