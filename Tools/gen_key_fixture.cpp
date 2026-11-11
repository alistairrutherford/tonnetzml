// Writes Tests/fixtures/chordmonkey_own_chords.txt: the notes ChordMonkey's engine plays for the
// 192 cases of its key test's "own chords" corpus (ChordMonkey's Tests/test_keys.cpp, ownOutput:
// 12 keys x Major / Natural Minor x Triads / Sevenths x Pop / Fifties / TwoFiveOne / Canon; the
// progression's first four keys twice, a bar each, then the tonic's key for two bars, at 100 bpm,
// 48 kHz, 480-sample blocks). TonnetzML's test_keys feeds them to its copy of the listener, so the
// corpus runs unchanged without TonnetzML depending on ChordMonkey's engine.
//
// Built against the sibling project's sources, read only (never modified):
//
//   clang++ -std=c++17 -O2 -I ../chordmonkey/Source Tools/gen_key_fixture.cpp -o /tmp/gen_key_fixture
//   /tmp/gen_key_fixture > Tests/fixtures/chordmonkey_own_chords.txt
//
// Format: `case KEY MINOR GROUP PROGRESSION` (GROUP, PROGRESSION: ChordMonkey's names), then
// `KIND SAMPLE NOTE VELOCITY` lines (KIND 1 = note-on, 0 = note-off; samples from 0 at 48 kHz),
// and `end SAMPLES` (the last block's end).
#include "core/engine/ChordEngine.h"
#include "core/engine/KeyRouter.h"
#include "core/theory/Progressions.h"
#include <cstdio>
#include <memory>
#include <vector>

using namespace chordmonkey;

int main()
{
    std::printf ("# ChordMonkey's own chords for its key corpus (Tools/gen_key_fixture.cpp). 48 kHz.\n");
    const Progression progressions[] = { Progression::Pop, Progression::Fifties, Progression::TwoFiveOne, Progression::Canon };
    for (int key = 0; key < 12; ++key)
        for (const Scale scale : { Scale::Major, Scale::NaturalMinor })
            for (const ChordGroup group : { ChordGroup::Triads, ChordGroup::Sevenths })
                for (const Progression prog : progressions)
                {
                    auto engine = std::make_unique<ChordEngine>();
                    EngineSettings s;
                    s.key = key;
                    s.scale = scale;
                    s.group = group;
                    s.progression = prog;
                    Transport t;
                    t.sampleRate = 48000.0;
                    const auto degrees = progressionDegrees (prog);
                    int tonicSlot = 0;
                    for (int n = 0; n < numWhiteSlots; ++n)
                        tonicSlot = degrees[(size_t) n] == 0 ? n : tonicSlot;
                    std::vector<int> order { 0, 1, 2, 3, 0, 1, 2, 3, tonicSlot, tonicSlot };

                    std::printf ("case %d %d %s %s\n", key, scale == Scale::NaturalMinor ? 1 : 0, chordGroupInfo (group).name, progressionInfo (prog).name);
                    const int blockSize = 480, barBlocks = 48000 * 24 / 10 / blockSize;
                    BlockEvents out;
                    long long pos = 0;
                    for (size_t step = 0; step < order.size(); ++step)
                    {
                        const int note = KeyRouter::keyForSlot (order[step]);
                        for (int b = 0; b < barBlocks; ++b)
                        {
                            std::vector<MidiEvent> in;
                            if (b == 0)
                                in.push_back (MidiEvent::noteOn (0, note, 100, 0));
                            if (b == barBlocks - 1)
                                in.push_back (MidiEvent::noteOff (0, note, blockSize - 1));
                            out.clear();
                            engine->process (s, t, in.data(), (int) in.size(), blockSize, out);
                            for (const auto& e : out)
                                if (e.isNoteOn() || e.isNoteOff())
                                    std::printf ("%d %lld %d %d\n", e.isNoteOn() ? 1 : 0, pos + e.sample, e.note(), e.isNoteOn() ? e.velocity() : 0);
                            pos += blockSize;
                        }
                    }
                    std::printf ("end %lld\n", pos);
                }
    return 0;
}
