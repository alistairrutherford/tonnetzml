// Editor snapshot and host test (PLAN.md §7.2). Runs the real processor and editor without a
// host. Checks (Phase 0, extended in Phase 2):
//
//   1. editor   the stub editor opens at its smallest size (760×520) and a large one, and
//               renders; each image is compared with Tests/fixtures/snapshot/*.png (at most 0.5%
//               of pixels may differ by more than 24 levels). Its word field shows the current
//               slot's word, stores a word typed into it, refuses text that isn't one, and
//               follows the slot parameter
//   2. through  bypassed and Enable off, MIDI in comes out byte for byte; enabled, everything
//               that isn't a note still does; the audio is silent
//   3. rig      with Test rig on, Keys.txt (in a temporary rig folder) plays on the sample from
//               the play head, merged with the host's MIDI (Mix: Source only shows it as it
//               came); Status.txt says so; a stop, Enable off and bypass end the held keys; an
//               edited Keys.txt is picked up
//   4. mirror   (Phase 2) the rig and host chords come out as W(chord) on the generated channel,
//               after the gather window, for each factory slot; Mix and channels; Status.txt
//               names the slot, word and chords
//   5. params   the 21 parameters in order (Phase 3's appended after Octave), with their defaults
//   8. walk     (Phase 3) operator keys from the host and the rig walk a held chord and never
//               sound; Orbit steps on the beat from the rig; a D major tune from the rig gives a
//               confident D major in Status.txt with generated chords sounding; Hold copies the
//               guess into Key tonic / Key minor; no allocation while walking or orbiting
//   6. realtime no allocation in processBlock while the rig plays through the engine (a malloc
//               hook counts every one on the audio thread)
//   7. state    a saved state restores the parameters and the slot words into a new instance;
//               a state without slot words, or with a bad one, gets the factory words; garbage
//               is ignored; (Phase 3) the current W and the latched chord come back, the chord
//               silent until the transport starts; a state without them (Phase 2) has neither
//
//   9. lattice  (Phase 4) the lattice editor: reference images of fixed chords and words (Mirror
//               P L on C; Walk P L on Cmaj7 in C major with In key; Orbit LR; empty) in both
//               themes at up to three sizes; no stock combo boxes, sliders or buttons; the
//               header's sentence spelled and numbered in the key
//  10. play     a click at the centre of each of the 24 triangles plays that triad; a drag
//               glides (one voice moves); shift-click builds a set; Sticky; a ghost click
//               appends its op in Walk and not in Mirror; the keyboard strip and its operator
//               zone; a stop makes the editor forget what it held
//  11. controls every parameter has a control, visible, inside the window, not overlapping
//               another, and a click changes it (at 760 x 520 and 1400 x 900)
//  12. word bar text entry, the palette, dragging chips to reorder and off to delete, Undo and
//               Home, the slot's word in Mirror, W in Walk, the cycle read only in Orbit
//  13. key      spelling in the key (E♭ minor writes C♭), numerals, at most double accidentals,
//               In key marks exactly opsInKey's ghosts
//  14. realtime no allocation in processBlock while the editor sends notes, ghost clicks and
//               words; the command queue's limit; a press and release in one block keep order
//
//   TonnetzMLSnapshot <fixtures dir> <output dir> [--update | --no-pixels]
//
// --update writes new reference images. --no-pixels renders without comparing. The images of
// this run are always written to the output dir.
#include <juce_audio_processors/juce_audio_processors.h>
#include "../../Source/plugin/TonnetzProcessor.h"
#include "../../Source/ui/TonnetzEditor.h"
#include <iostream>
#include <map>
#include <set>
#include <tuple>
#include <pthread.h>

using namespace tonnetz;

// Every allocation the watched thread makes (malloc, realloc, new: libmalloc's logger hook sees
// them all). Only a pthread_t compare inside: the hook runs within malloc.
extern "C" void (*malloc_logger) (uint32_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uint32_t);

namespace
{
    std::atomic<pthread_t> allocWatched { nullptr };
    std::atomic<int> allocCount { 0 };

    void allocLogger (uint32_t type, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uint32_t)
    {
        if ((type & 2) != 0 && allocWatched.load (std::memory_order_relaxed) == pthread_self())   // 2: an allocation
            allocCount.fetch_add (1, std::memory_order_relaxed);
    }

    struct AllocWatch
    {
        AllocWatch() { malloc_logger = allocLogger; allocWatched = pthread_self(); }
        ~AllocWatch() { allocWatched = nullptr; }
    };

    int failures = 0;
    bool comparePixels = true, updateImages = false;

    void check (bool ok, const juce::String& what)
    {
        std::cout << (ok ? "  PASS  " : "  FAIL  ") << what << std::endl;
        failures += ok ? 0 : 1;
    }

    void set (TonnetzProcessor& p, const char* id, bool on)
    {
        if (auto* param = p.getState().getParameter (id))
            param->setValueNotifyingHost (on ? 1.0f : 0.0f);
    }

    // A parameter to a plain value (a choice's index, an int's value).
    void setPlain (TonnetzProcessor& p, const char* id, float value)
    {
        if (auto* param = p.getState().getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (value));
    }

    float plain (TonnetzProcessor& p, const char* id)
    {
        auto* param = p.getState().getParameter (id);
        return param != nullptr ? param->convertFrom0to1 (param->getValue()) : -999.0f;
    }

    struct PlayHead : juce::AudioPlayHead
    {
        bool playing = true;
        double ppq = 0.0, bpm = 120.0;
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setIsPlaying (playing);
            info.setIsLooping (false);
            info.setPpqPosition (ppq);
            info.setBpm (bpm);
            info.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
            return info;
        }
    };

    struct Note
    {
        juce::int64 sample;
        int note, velocity;                 // velocity 0: a note-off
        int channel = 0;                    // 1..16; 0: not compared
        bool operator== (const Note& o) const
        {
            return std::abs (sample - o.sample) <= 1 && note == o.note && velocity == o.velocity
                && (channel == 0 || o.channel == 0 || channel == o.channel);
        }
    };

    // A little host: one processor, blocks of 256 at 48 kHz, a play head.
    struct MiniHost
    {
        static constexpr int block = 256;
        static constexpr double rate = 48000.0;
        std::unique_ptr<TonnetzProcessor> p = std::make_unique<TonnetzProcessor>();
        PlayHead head;
        juce::AudioBuffer<float> audio { 2, block };
        juce::MidiBuffer midi;
        juce::int64 pos = 0;
        std::vector<Note> notes;
        float peak = 0.0f;
        bool bypassed = false;

        MiniHost()
        {
            midi.ensureSize (1 << 16);
            notes.reserve (4096);           // the host's own bookkeeping must not allocate under the hook
            p->setPlayHead (&head);
            p->prepareToPlay (rate, block);
        }

        static juce::int64 at (double beats, double bpm = 120.0) { return (juce::int64) std::llround (beats * rate * 60.0 / bpm); }

        // One block; `in` (positions within the block) is the host's MIDI.
        void process (const juce::MidiBuffer& in = {})
        {
            midi.clear();
            midi.addEvents (in, 0, -1, 0);
            for (int ch = 0; ch < audio.getNumChannels(); ++ch)
                juce::FloatVectorOperations::fill (audio.getWritePointer (ch), 0.25f, block);   // a host's dirty buffer
            if (bypassed) p->processBlockBypassed (audio, midi);
            else          p->processBlock (audio, midi);
            peak = juce::jmax (peak, audio.getMagnitude (0, block));
            for (const auto meta : midi)
            {
                const auto m = meta.getMessage();
                if (m.isNoteOnOrOff())
                    notes.push_back ({ pos + meta.samplePosition, m.getNoteNumber(), m.isNoteOn() ? (int) m.getVelocity() : 0, m.getChannel() });
            }
            if (head.playing)
                head.ppq += block * head.bpm / (60.0 * rate);
            pos += block;
        }

        void runTo (juce::int64 sample) { while (pos < sample) process(); }
        int sounding() const
        {
            std::map<int, int> on;
            for (const auto& n : notes) on[n.note] += n.velocity > 0 ? 1 : -1;
            int count = 0;
            for (const auto& [note, c] : on) count += c != 0 ? 1 : 0;
            return count;
        }
    };

    bool sameNotes (std::vector<Note> got, std::vector<Note> want, juce::String& why)
    {
        const auto order = [] (const Note& a, const Note& b) { return std::tie (a.note, a.sample, a.velocity) < std::tie (b.note, b.sample, b.velocity); };
        std::sort (got.begin(), got.end(), order);
        std::sort (want.begin(), want.end(), order);
        if (got.size() != want.size())
        {
            why = juce::String (got.size()) + " events, expected " + juce::String (want.size());
            return false;
        }
        for (size_t i = 0; i < got.size(); ++i)
            if (! (got[i] == want[i]))
            {
                why = "note " + juce::String (got[i].note) + " v" + juce::String (got[i].velocity) + " @" + juce::String (got[i].sample)
                      + ", expected " + juce::String (want[i].note) + " v" + juce::String (want[i].velocity) + " @" + juce::String (want[i].sample);
                return false;
            }
        return true;
    }

    //==========================================================================
    // A reference image: written to the output dir; compared with Tests/fixtures/snapshot (at most
    // 0.5% of pixels may differ by more than 24 levels), or written there with --update.
    void compareImage (const juce::Image& image, const juce::String& name, const juce::File& fixtures, const juce::File& output)
    {
        juce::PNGImageFormat png;
        output.createDirectory();
        const auto outFile = output.getChildFile (name + ".png");
        outFile.deleteFile();
        if (juce::FileOutputStream out (outFile); out.openedOk())
            png.writeImageToStream (image, out);

        std::set<juce::uint32> colours;
        for (int y = 0; y < image.getHeight(); y += 2)
            for (int x = 0; x < image.getWidth(); x += 2)
                colours.insert (image.getPixelAt (x, y).getARGB());
        check (colours.size() > 8, name + ": rendered (" + juce::String ((int) colours.size()) + " colours)");

        const auto refFile = fixtures.getChildFile (name + ".png");
        if (updateImages)
        {
            fixtures.createDirectory();
            outFile.copyFileTo (refFile);
            std::cout << "  wrote " << refFile.getFullPathName() << std::endl;
        }
        else if (comparePixels)
        {
            const auto ref = juce::ImageFileFormat::loadFrom (refFile);
            if (! ref.isValid() || ref.getBounds() != image.getBounds())
            {
                check (false, name + ": no reference image of this size (run with --update)");
                return;
            }
            int differing = 0;
            for (int y = 0; y < image.getHeight(); ++y)
                for (int x = 0; x < image.getWidth(); ++x)
                {
                    const auto a = image.getPixelAt (x, y), b = ref.getPixelAt (x, y);
                    if (std::abs (a.getRed() - b.getRed()) > 24 || std::abs (a.getGreen() - b.getGreen()) > 24 || std::abs (a.getBlue() - b.getBlue()) > 24)
                        ++differing;
                }
            const double percent = 100.0 * differing / (image.getWidth() * image.getHeight());
            check (percent <= 0.5, name + ": matches the reference (" + juce::String (percent, 3) + "% of pixels differ)");
        }
    }

    void testThrough()
    {
        std::cout << "through" << std::endl;
        juce::MidiBuffer in;
        in.addEvent (juce::MidiMessage::noteOn (3, 60, (juce::uint8) 101), 0);
        in.addEvent (juce::MidiMessage::controllerEvent (1, 1, 64), 7);
        in.addEvent (juce::MidiMessage::pitchWheel (2, 9000), 7);
        in.addEvent (juce::MidiMessage::noteOff (3, 60, (juce::uint8) 40), 200);
        const juce::uint8 sysex[] = { 0x7d, 1, 2, 3 };
        in.addEvent (juce::MidiMessage::createSysExMessage (sysex, 4), 255);

        const auto same = [&in] (const juce::MidiBuffer& out)
        {
            auto a = in.begin(), b = out.begin();
            for (; a != in.end() && b != out.end(); ++a, ++b)
                if ((*a).samplePosition != (*b).samplePosition || (*a).numBytes != (*b).numBytes
                    || std::memcmp ((*a).data, (*b).data, (size_t) (*a).numBytes) != 0)
                    return false;
            return a == in.end() && b == out.end();
        };

        for (const char* mode : { "bypassed", "Enable off" })
        {
            MiniHost h;
            const juce::String m (mode);
            h.head.playing = m != "stopped";
            h.bypassed = m == "bypassed";
            if (m == "Enable off") set (*h.p, params::enable, false);
            if (m.startsWith ("Test rig"))
            {
                h.p->setRigFolder (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("TonnetzML-no-such-rig"));
                set (*h.p, params::testRig, true);
            }
            bool ok = true;
            for (int i = 0; i < 4; ++i)
            {
                h.process (in);
                ok = ok && same (h.midi);
            }
            check (ok && h.peak == 0.0f, juce::String ("MIDI through unchanged, audio silent: ") + mode);
        }

        // Enabled: everything that isn't a note goes through byte for byte. (The lone note is
        // let go inside the gather window, so it never sounds.)
        juce::MidiBuffer others;
        for (const auto meta : in)
            if (! meta.getMessage().isNoteOnOrOff())
                others.addEvent (meta.getMessage(), meta.samplePosition);
        const auto sameOthers = [&others] (const juce::MidiBuffer& out)
        {
            auto a = others.begin(), b = out.begin();
            for (; a != others.end() && b != out.end(); ++a, ++b)
                if ((*a).samplePosition != (*b).samplePosition || (*a).numBytes != (*b).numBytes
                    || std::memcmp ((*a).data, (*b).data, (size_t) (*a).numBytes) != 0)
                    return false;
            return a == others.end() && b == out.end();
        };
        for (const char* mode : { "playing", "stopped", "Test rig on, no keys" })
        {
            MiniHost h;
            const juce::String m (mode);
            h.head.playing = m != "stopped";
            if (m.startsWith ("Test rig"))
            {
                h.p->setRigFolder (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("TonnetzML-no-such-rig"));
                set (*h.p, params::testRig, true);
            }
            bool ok = true;
            for (int i = 0; i < 4; ++i)
            {
                h.process (in);
                ok = ok && sameOthers (h.midi);
            }
            check (ok && h.peak == 0.0f, juce::String ("enabled: controllers, pitch bend and SysEx through unchanged, audio silent: ") + mode);
        }
    }

    void testRig (const juce::File& output)
    {
        std::cout << "rig" << std::endl;
        const auto folder = output.getChildFile ("rig");
        folder.deleteRecursively();
        folder.createDirectory();
        const auto keysFile = folder.getChildFile ("Keys.txt");
        const auto statusFile = folder.getChildFile ("Status.txt");
        keysFile.replaceWithText ("# C, G, Am\n0 60 100 1\n0 64 90 1\n0 67 80 1\n"
                                  "1.5 55 100 0.5\n1.5 59 100 0.5\n1.5 62 100 0.5\n"
                                  "2.3333 57 70 1\n2.3333 60 70 1\n2.3333 64 70 8\nnot a key\n");
        const auto status = [&] { return statusFile.loadFileAsString(); };

        {
            MiniHost h;
            h.p->setRigFolder (folder);
            h.runTo (MiniHost::at (0.25));
            check (h.notes.empty(), "Test rig off: nothing plays");
            check (! statusFile.existsAsFile(), "Test rig off: no Status.txt");
        }

        MiniHost h;
        h.p->setRigFolder (folder);
        set (*h.p, params::testRig, true);
        setPlain (*h.p, params::mix, 2.0f);           // Source only: the keys as they came, on channel 1
        h.p->pollTestRig();
        check (status().contains ("keys: 9\n") && status().contains ("rejectedLines: 1\n") && status().contains ("keysFile: found"),
               "Status.txt: 9 keys, 1 rejected line");

        // The host plays a note of its own in the first block.
        juce::MidiBuffer in;
        in.addEvent (juce::MidiMessage::noteOn (1, 100, (juce::uint8) 11), 5);
        in.addEvent (juce::MidiMessage::noteOff (1, 100), 250);
        h.process (in);
        int allocations = 0;
        {
            AllocWatch watch;
            const int before = allocCount.load();
            h.runTo (MiniHost::at (3.0));
            allocations = allocCount.load() - before;
        }
        std::vector<Note> want { { 5, 100, 11 }, { 250, 100, 0 } };
        const auto key = [&want] (double beat, int note, int velocity, double length)
        {
            want.push_back ({ MiniHost::at (beat), note, velocity });
            want.push_back ({ MiniHost::at (beat + length), note, 0 });
        };
        key (0, 60, 100, 1); key (0, 64, 90, 1); key (0, 67, 80, 1);
        key (1.5, 55, 100, 0.5); key (1.5, 59, 100, 0.5); key (1.5, 62, 100, 0.5);
        want.push_back ({ MiniHost::at (2.3333), 57, 70 });
        want.push_back ({ MiniHost::at (2.3333), 60, 70 });
        want.push_back ({ MiniHost::at (2.3333), 64, 70 });
        juce::String why;
        check (sameNotes (h.notes, want, why), "the keys play on their samples, with the host's note " + why);
        check (allocations == 0, "no allocation in processBlock while the rig plays through the engine (" + juce::String (allocations) + ")");

        // Stop: the three held keys end.
        h.head.playing = false;
        h.process();
        check (h.sounding() == 0 && h.notes.size() == want.size() + 3, "stop ends the held keys");
        juce::Thread::sleep (300);
        h.p->pollTestRig();
        check (status().contains ("noteOns: 9\n") && status().contains ("noteOffs: 9\n") && status().contains ("held: 0\n")
                   && status().contains ("playing: no"), "Status.txt: 9 on, 9 off, none held, stopped");

        // An edited file is picked up; Enable off and bypass end what is held.
        keysFile.replaceWithText ("0 72 127 4\n");
        h.p->pollTestRig();
        for (const char* how : { "Enable off", "bypass" })
        {
            h.notes.clear();
            h.head.playing = true;
            h.head.ppq = 0.0;
            h.runTo (h.pos + MiniHost::at (1.0));
            const bool playing = h.notes.size() == 1 && h.notes[0].note == 72 && h.notes[0].velocity == 127;
            if (juce::String (how) == "bypass") h.bypassed = true;
            else set (*h.p, params::enable, false);
            h.process();
            check (playing && h.notes.size() == 2 && h.sounding() == 0, juce::String ("the edited Keys.txt plays; ") + how + " ends the held key");
            h.runTo (h.pos + MiniHost::at (1.0));
            check (h.notes.size() == 2, juce::String (how) + ": nothing more plays");
            h.bypassed = false;
            set (*h.p, params::enable, true);
            h.head.playing = false;
            h.process();
        }

        // Rig off: the status file is left alone, and the keys are forgotten.
        set (*h.p, params::testRig, false);
        h.p->pollTestRig();
        h.notes.clear();
        h.head.playing = true;
        h.head.ppq = 0.0;
        h.runTo (h.pos + MiniHost::at (1.0));
        check (h.notes.empty(), "Test rig off again: nothing plays");
    }

    void testParams()
    {
        std::cout << "params" << std::endl;
        TonnetzProcessor p;
        juce::StringArray ids;
        for (auto* param : p.getParameters())
            if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (param))
                ids.add (withId->getParameterID());
        check (ids.joinIntoString (" ") == "enable testRig slot gatherMs mix sourceChannel genChannel register bass octave "
                                           "mode latch walkInput orbitCycle orbitRate opZoneStart opZoneOn key keyTonic keyMinor inKey",
               juce::String (ids.size()) + " parameters (at most 28), Phase 2's and Phase 3's appended in turn: " + ids.joinIntoString (" "));
        check (plain (p, params::mode) == 0.0f && plain (p, params::latch) == 0.0f && plain (p, params::walkInput) == 0.0f
                   && plain (p, params::orbitCycle) == 0.0f && plain (p, params::orbitRate) == 2.0f && plain (p, params::opZoneStart) == 24.0f
                   && plain (p, params::opZoneOn) == 1.0f && plain (p, params::key) == 1.0f && plain (p, params::keyTonic) == 0.0f
                   && plain (p, params::keyMinor) == 0.0f && plain (p, params::inKey) == 0.0f,
               "Phase 3 defaults: Mirror, latch off, Relative, PL, 1 bar, operator keys on from C0 (24), Key Listen, C, major, In key off");
        auto* zone = p.getState().getParameter (params::opZoneStart);
        auto* rate = p.getState().getParameter (params::orbitRate);
        auto* mode = p.getState().getParameter (params::mode);
        check (zone->getText (zone->convertTo0to1 (24.0f), 8) == "C0" && zone->getText (zone->convertTo0to1 (61.0f), 8) == "C#3"
                   && rate->getText (rate->convertTo0to1 (0.0f), 16) == "1/4 bar" && mode->getText (mode->convertTo0to1 (2.0f), 16) == "Orbit",
               "operator keys named as Live names them (24 = C0, 61 = C#3); Orbit rate 1/4 bar; Mode Orbit");
        check (plain (p, params::slot) == 0.0f && plain (p, params::gatherMs) == 20.0f && plain (p, params::mix) == 0.0f
                   && plain (p, params::sourceChannel) == 1.0f && plain (p, params::genChannel) == 2.0f && plain (p, params::registerMode) == 0.0f
                   && plain (p, params::bass) == 0.0f && plain (p, params::octave) == 0.0f,
               "defaults: slot A, gather 20 ms, Mix Generated, source channel 1, generated channel 2, Register Stay, Bass Off, Octave 0");
        juce::StringArray words;
        for (int i = 0; i < numSlots; ++i)
            words.add (TonnetzProcessor::slotName (i) + "=" + juce::String (printWord (p.getSlotWord (i)).c_str()));
        check (words.joinIntoString (" ") == "A=P B=L C=R D=N E=P L F=P R G=L R H=S", "factory slot words: " + words.joinIntoString (" "));
        auto* slot = p.getState().getParameter (params::slot);
        check (slot->getText (slot->convertTo0to1 (4.0f), 8) == "E" && slot->getNumSteps() == 8, "slot is a choice A..H (8 steps)");
    }

    // The engine through the real processor: host chords and rig keys come out as W(chord).
    void testMirror (const juce::File& output)
    {
        std::cout << "mirror" << std::endl;
        const auto chordAt = [] (juce::MidiBuffer& b, int sample, std::initializer_list<int> notes, bool on, int velocity = 100)
        {
            for (int n : notes)
                b.addEvent (on ? juce::MidiMessage::noteOn (1, n, (juce::uint8) velocity) : juce::MidiMessage::noteOff (1, n), sample);
        };

        // Every factory slot on C E G, gather 0: on the chord's sample, channel 2.
        const std::vector<std::vector<int>> expected { { 60, 63, 67 }, { 59, 64, 67 }, { 60, 64, 69 }, { 60, 65, 68 },
                                                       { 60, 63, 68 }, { 58, 63, 67 }, { 59, 62, 67 }, { 61, 64, 68 } };
        for (int slot = 0; slot < numSlots; ++slot)
        {
            MiniHost h;
            setPlain (*h.p, params::slot, (float) slot);
            setPlain (*h.p, params::gatherMs, 0.0f);
            juce::MidiBuffer in;
            chordAt (in, 10, { 60, 64, 67 }, true);
            h.process (in);
            juce::MidiBuffer up;
            chordAt (up, 100, { 60, 64, 67 }, false);
            h.process (up);
            std::vector<Note> want;
            for (int n : expected[(size_t) slot])
            {
                want.push_back ({ 10, n, 100, 2 });
                want.push_back ({ MiniHost::block + 100, n, 0, 2 });
            }
            juce::String why;
            check (sameNotes (h.notes, want, why), "slot " + TonnetzProcessor::slotName (slot) + " (" + printWord (h.p->getSlotWord (slot)).c_str()
                                                      + ") on C E G, gather 0: on the sample, channel 2 " + why);
        }

        // Gather 20 ms (the default): the chord sounds 960 samples later; Source + Generated on
        // channels 3 and 9; a late note retunes; Bass and Octave.
        {
            MiniHost h;
            setPlain (*h.p, params::mix, 1.0f);
            setPlain (*h.p, params::sourceChannel, 3.0f);
            setPlain (*h.p, params::genChannel, 9.0f);
            juce::MidiBuffer in;
            chordAt (in, 0, { 60, 64 }, true, 90);
            h.process (in);
            h.runTo (2048);
            juce::MidiBuffer late;
            chordAt (late, 0, { 67 }, true, 70);
            h.process (late);
            h.runTo (4096);
            std::vector<Note> want { { 0, 60, 90, 3 }, { 0, 64, 90, 3 }, { 960, 60, 90, 9 }, { 960, 64, 90, 9 },
                                     { 2048, 67, 70, 3 }, { 2048 + 960, 64, 0, 9 }, { 2048 + 960, 63, 90, 9 }, { 2048 + 960, 67, 70, 9 } };
            juce::String why;
            check (sameNotes (h.notes, want, why), "gather 20 ms, Source + Generated on channels 3 and 9: source at once, generated 960 samples on; "
                                                   "the late G retunes only E (to Eb) " + why);
        }
        {
            MiniHost h;
            setPlain (*h.p, params::gatherMs, 0.0f);
            setPlain (*h.p, params::bass, 1.0f);
            setPlain (*h.p, params::octave, -1.0f);
            setPlain (*h.p, params::slot, 2.0f);           // R: A minor
            juce::MidiBuffer in;
            chordAt (in, 3, { 60, 64, 67 }, true);
            h.process (in);
            std::vector<Note> want { { 3, 48, 100, 2 }, { 3, 52, 100, 2 }, { 3, 57, 100, 2 }, { 3, 33, 100, 2 } };
            juce::String why;
            check (sameNotes (h.notes, want, why), "R with Octave -1 and Bass Root below: A minor an octave down, A1 under it " + why);
        }

        // The rig (Keys.txt) through Mirror, as in the Live check: Status.txt names it all.
        const auto folder = output.getChildFile ("rig-mirror");
        folder.deleteRecursively();
        folder.createDirectory();
        folder.getChildFile ("Keys.txt").replaceWithText ("1 60 100 2\n1 64 100 2\n1 67 100 2\n");
        MiniHost h;
        h.p->setRigFolder (folder);
        set (*h.p, params::testRig, true);
        setPlain (*h.p, params::slot, 4.0f);               // E: P L
        setPlain (*h.p, params::gatherMs, 0.0f);
        h.p->pollTestRig();
        h.runTo (MiniHost::at (2.0));
        juce::Thread::sleep (300);
        h.p->pollTestRig();
        const auto status = folder.getChildFile ("Status.txt").loadFileAsString();
        check (status.contains ("slot: E\n") && status.contains ("word: P L\n") && status.contains ("source: C\n")
                   && status.contains ("generated: Ab/C\n") && status.contains ("sounding: 3\n"),
               "Status.txt: slot E, word P L, source C, generated Ab/C, 3 sounding");
        h.runTo (MiniHost::at (4.0));
        std::vector<Note> want;
        for (int n : { 60, 63, 68 })
        {
            want.push_back ({ MiniHost::at (1.0), n, 100, 2 });
            want.push_back ({ MiniHost::at (3.0), n, 0, 2 });
        }
        juce::String why;
        check (sameNotes (h.notes, want, why), "the rig's C E G under P L: C Eb Ab on channel 2, on the beat " + why);
    }

    // Phase 3 helpers: what sounds on a channel now, and a voicing's pitches.
    std::set<int> soundingOn (const MiniHost& h, int channel)
    {
        std::map<int, int> on;
        for (const auto& n : h.notes)
            if (n.channel == channel)
                on[n.note] += n.velocity > 0 ? 1 : -1;
        std::set<int> s;
        for (const auto& [note, c] : on)
            if (c > 0)
                s.insert (note);
        return s;
    }

    std::set<int> voiced (const char* w, std::initializer_list<int> keys)
    {
        Word word;
        parseWord (w, word);
        Voicing in, out;
        for (int n : keys)
            in.push (n, n);
        voiceWord (word, inputChord (in), in, {}, out);
        std::set<int> s;
        for (const auto& v : out)
            s.insert (v.pitch);
        return s;
    }

    juce::String text (const std::set<int>& s)
    {
        juce::StringArray a;
        for (int n : s)
            a.add (juce::String (n));
        return a.joinIntoString (" ");
    }

    // Keys.txt lines for a tune (C major / A minor as written, `shift` semitones up), from beat 4.
    juce::String tuneKeys (const std::vector<std::pair<int, double>>& tune, int shift)
    {
        juce::String s;
        double beat = 4.0;
        for (const auto& [pitch, beats] : tune)
        {
            s << juce::String (beat, 4) << " " << (pitch + shift) << " 100 " << juce::String (0.9 * beats, 4) << "\n";
            beat += beats;
        }
        return s;
    }

    const std::vector<std::pair<int, double>> twinkle { { 60, 1 }, { 60, 1 }, { 67, 1 }, { 67, 1 }, { 69, 1 }, { 69, 1 }, { 67, 2 },
                                                        { 65, 1 }, { 65, 1 }, { 64, 1 }, { 64, 1 }, { 62, 1 }, { 62, 1 }, { 60, 2 },
                                                        { 67, 1 }, { 67, 1 }, { 65, 1 }, { 65, 1 }, { 64, 1 }, { 64, 1 }, { 62, 2 },
                                                        { 67, 1 }, { 67, 1 }, { 65, 1 }, { 65, 1 }, { 64, 1 }, { 64, 1 }, { 62, 2 },
                                                        { 60, 1 }, { 60, 1 }, { 67, 1 }, { 67, 1 }, { 69, 1 }, { 69, 1 }, { 67, 2 },
                                                        { 65, 1 }, { 65, 1 }, { 64, 1 }, { 64, 1 }, { 62, 1 }, { 62, 1 }, { 60, 2 } };

    // Walk, Orbit and the key through the real processor (PLAN.md §6 row 3).
    void testWalkOrbitKey (const juce::File& output)
    {
        std::cout << "walk, orbit, key" << std::endl;
        const auto keyAt = [] (juce::MidiBuffer& b, int sample, std::initializer_list<int> notes, bool on)
        {
            for (int n : notes)
                b.addEvent (on ? juce::MidiMessage::noteOn (1, n, (juce::uint8) 100) : juce::MidiMessage::noteOff (1, n), sample);
        };

        // Walk from the host: operator keys (C0 = 24 up) walk a held C E G; keys pressed together
        // retune once; they never sound.
        {
            MiniHost h;
            setPlain (*h.p, params::mode, 1.0f);
            setPlain (*h.p, params::gatherMs, 0.0f);
            setPlain (*h.p, params::mix, 1.0f);            // Source + Generated: the source shows the op keys don't sound
            juce::MidiBuffer a, b, c, d;
            keyAt (a, 10, { 60, 64, 67 }, true);
            h.process (a);
            const auto first = soundingOn (h, 2);
            h.runTo (h.pos + 20 * MiniHost::block);       // the listener estimates every 50 ms of listening
            keyAt (b, 20, { 24 }, true);                   // P
            h.process (b);
            const auto afterP = soundingOn (h, 2);
            keyAt (c, 30, { 25, 30 }, true);               // L and +5 together
            h.process (c);
            const auto afterLFifth = soundingOn (h, 2);
            const size_t eventsBefore = h.notes.size();
            keyAt (d, 5, { 24, 25, 30 }, false);           // operator key-ups: nothing
            keyAt (d, 6, { 35 }, true);                    // Home
            h.process (d);
            const auto home = soundingOn (h, 2);
            bool noOpKeys = true;
            for (const auto& n : h.notes)
                noOpKeys = noOpKeys && (n.note < 24 || n.note > 35);
            check (first == voiced ("", { 60, 64, 67 }) && afterP == voiced ("P", { 60, 64, 67 }) && afterLFifth == voiced ("P L +5", { 60, 64, 67 })
                       && home == voiced ("", { 60, 64, 67 }) && noOpKeys && soundingOn (h, 1) == std::set<int> { 60, 64, 67 },
                   "Walk: P, then L and +5 together, then Home on a held C E G (" + text (afterP) + " / " + text (afterLFifth) + " / " + text (home)
                       + "); operator keys never sound");
            // L and +5 on one sample: each voice moved once (no note both started and ended on that sample).
            int zeroLength = 0;
            for (size_t i = 0; i < eventsBefore; ++i)
                for (size_t j = 0; j < eventsBefore; ++j)
                    zeroLength += h.notes[i].velocity > 0 && h.notes[j].velocity == 0 && h.notes[i].note == h.notes[j].note
                                  && h.notes[i].sample == h.notes[j].sample && h.notes[i].channel == 2 && h.notes[j].channel == 2 && j > i ? 1 : 0;
            check (zeroLength == 0, "two operator keys on one sample give one retune (no zero-length notes)");
            check (KeyGuess::unpack (h.p->getEngineStatus().keyGuess).notes == 3, "the key listener heard the 3 source notes, not the 4 operator keys");
        }

        // The rig: a held chord walked by operator keys in Keys.txt, then Orbit, with no allocation.
        const auto folder = output.getChildFile ("rig-walk");
        folder.deleteRecursively();
        folder.createDirectory();
        const auto statusFile = folder.getChildFile ("Status.txt");
        folder.getChildFile ("Keys.txt").replaceWithText ("4 60 100 12\n4 64 100 12\n4 67 100 12\n6 24 100 0.5\n8 25 100 0.5\n10 33 100 0.5\n");
        {
            MiniHost h;
            h.p->setRigFolder (folder);
            set (*h.p, params::testRig, true);
            setPlain (*h.p, params::mode, 1.0f);
            setPlain (*h.p, params::gatherMs, 0.0f);
            h.p->pollTestRig();
            h.process();
            int allocations = 0;
            {
                AllocWatch watch;
                const int before = allocCount.load();
                h.runTo (MiniHost::at (11.0));
                allocations = allocCount.load() - before;
            }
            juce::Thread::sleep (300);
            h.p->pollTestRig();
            const auto status = statusFile.loadFileAsString();
            check (soundingOn (h, 2) == voiced ("P L -M3", { 60, 64, 67 }) && status.contains ("mode: Walk\n") && status.contains ("word: P L -M3\n")
                       && status.contains ("walk: P L -M3\n") && status.contains ("wordOps: 3\n"),
                   "the rig walks C E G with P, L, -M3 from Keys.txt: " + text (soundingOn (h, 2)) + "; Status.txt: mode Walk, word P L -M3");
            check (allocations == 0, "no allocation in processBlock while the rig walks (" + juce::String (allocations) + ")");
        }
        folder.getChildFile ("Keys.txt").replaceWithText ("4 60 100 8\n4 64 100 8\n4 67 100 8\n");
        {
            MiniHost h;
            h.p->setRigFolder (folder);
            set (*h.p, params::testRig, true);
            setPlain (*h.p, params::mode, 2.0f);           // Orbit
            setPlain (*h.p, params::orbitCycle, 0.0f);     // PL
            setPlain (*h.p, params::orbitRate, 0.0f);      // 1/4 bar: every beat
            setPlain (*h.p, params::gatherMs, 0.0f);
            h.p->pollTestRig();
            h.process();
            int allocations = 0;
            {
                AllocWatch watch;
                const int before = allocCount.load();
                h.runTo (MiniHost::at (7.5));
                allocations = allocCount.load() - before;
            }
            juce::Thread::sleep (300);
            h.p->pollTestRig();
            const auto status = statusFile.loadFileAsString();
            // Steps at beats 5, 6 and 7: W = P L P.
            std::set<int> stepAt7;
            for (const auto& n : h.notes)
                if (n.velocity > 0 && n.sample == MiniHost::at (7.0))
                    stepAt7.insert (n.note);
            check (soundingOn (h, 2) == voiced ("P L P", { 60, 64, 67 }) && ! stepAt7.empty() && status.contains ("mode: Orbit\n")
                       && status.contains ("word: P L P\n") && status.contains ("orbitStep: 3\n"),
                   "Orbit PL at 1/4 bar from the rig: C E G stepped on beats 5, 6, 7 to " + text (soundingOn (h, 2)) + " (the step at 7 on its sample); Status.txt: orbitStep 3");
            check (allocations == 0, "no allocation in processBlock while Orbit steps (" + juce::String (allocations) + ")");
        }

        // The key: a tune in D major from the rig under Mirror PL; Status.txt shows the guess,
        // confident, with the generated chords sounding; Hold copies it into keyTonic / keyMinor.
        folder.getChildFile ("Keys.txt").replaceWithText (tuneKeys (twinkle, 2));
        {
            MiniHost h;
            h.p->setRigFolder (folder);
            set (*h.p, params::testRig, true);
            setPlain (*h.p, params::slot, 4.0f);           // E: P L
            setPlain (*h.p, params::gatherMs, 0.0f);
            h.p->pollTestRig();
            h.process();
            h.runTo (MiniHost::at (54.0));
            juce::Thread::sleep (300);
            h.p->pollTestRig();
            const auto status = statusFile.loadFileAsString();
            int generated = 0;
            for (const auto& n : h.notes)
                generated += n.channel == 2 && n.velocity > 0 ? 1 : 0;
            check (status.contains ("key: D major\n") && status.contains ("keyState: confident\n") && status.contains ("keyMode: Listen\n")
                       && status.contains ("keyInForce: D major\n") && status.contains ("keyNotes: 42\n") && generated >= 42,
                   "a D major tune from the rig under P L: Status.txt says D major, confident, from the 42 source notes; " + juce::String (generated) + " generated notes");
            setPlain (*h.p, params::key, 2.0f);
            h.process();
            h.p->followKeyMode();
            check (plain (*h.p, params::keyTonic) == 2.0f && plain (*h.p, params::keyMinor) == 0.0f, "Key Listen to Hold copies the guess into Key tonic (D) and Key minor (off)");
            h.process();
            juce::Thread::sleep (300);
            h.p->pollTestRig();
            check (statusFile.loadFileAsString().contains ("keyMode: Hold\n") && statusFile.loadFileAsString().contains ("keyInForce: D major\n"),
                   "Status.txt: Hold, D major in force");
        }
    }

    // Phase 3's parameters and the engine's state (the current W and Latch's chord).
    void testWalkState()
    {
        std::cout << "walk state" << std::endl;
        TonnetzProcessor a;
        const std::pair<const char*, float> values[] = { { params::mode, 3.0f }, { params::latch, 1.0f }, { params::walkInput, 1.0f },
                                                         { params::orbitCycle, 4.0f }, { params::orbitRate, 4.0f }, { params::opZoneStart, 96.0f },
                                                         { params::opZoneOn, 0.0f }, { params::key, 2.0f }, { params::keyTonic, 10.0f },
                                                         { params::keyMinor, 1.0f }, { params::inKey, 1.0f } };
        for (const auto& [id, v] : values)
            setPlain (a, id, v);
        juce::MemoryBlock saved;
        a.getStateInformation (saved);
        TonnetzProcessor b;
        b.setStateInformation (saved.getData(), (int) saved.getSize());
        bool all = true;
        for (const auto& [id, v] : values)
            all = all && plain (b, id) == v;
        check (all, "every Phase 3 parameter comes back in a new instance");
        b.followKeyMode();
        check (plain (b, params::keyTonic) == 10.0f, "a state saved in Hold keeps its key (no guess is copied over it)");

        // W and a latched chord: walked and latched in one instance, saved, restored in another.
        MiniHost h;
        setPlain (*h.p, params::mode, 1.0f);
        setPlain (*h.p, params::latch, 1.0f);
        setPlain (*h.p, params::gatherMs, 0.0f);
        juce::MidiBuffer on, op, off;
        for (int n : { 60, 64, 67 }) on.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 90), 3);
        op.addEvent (juce::MidiMessage::noteOn (1, 25, (juce::uint8) 100), 3);        // L
        for (int n : { 60, 64, 67 }) off.addEvent (juce::MidiMessage::noteOff (1, n), 3);
        h.process (on);
        h.process (op);
        h.process (off);
        h.process();
        const auto latchedSound = soundingOn (h, 2);
        h.p->getStateInformation (saved);
        const auto xml = juce::AudioProcessor::getXmlFromBinary (saved.getData(), (int) saved.getSize());
        const auto* walk = xml != nullptr ? xml->getChildByName ("WALK") : nullptr;
        check (latchedSound == voiced ("L", { 60, 64, 67 }) && walk != nullptr && walk->getStringAttribute ("word") == "L"
                   && walk->getStringAttribute ("latched") == "60:90 64:90 67:90",
               "a chord latched and walked by L sounds on after its keys are up (" + text (latchedSound) + "); the state has WALK word=L latched="
                   + (walk != nullptr ? walk->getStringAttribute ("latched") : juce::String()));

        MiniHost r;
        r.head.playing = false;
        r.p->setStateInformation (saved.getData(), (int) saved.getSize());
        const auto back = r.p->getWalkState();
        check (printWord (back.word).c_str() == std::string ("L") && back.latched[60] == 90 && back.latched[64] == 90 && back.latched[67] == 90,
               "restored into a new instance: W = L and the latched C E G, before any block");
        r.runTo (4 * MiniHost::block);
        const bool silent = r.notes.empty();
        r.head.playing = true;
        const auto startAt = r.pos;
        r.process();
        std::set<int> started;
        for (const auto& n : r.notes)
            if (n.velocity > 0 && n.sample == startAt && n.channel == 2)
                started.insert (n.note);
        check (silent && started == voiced ("L", { 60, 64, 67 }), "it is silent while stopped and sounds when the transport starts: " + text (started));
        r.head.playing = false;
        r.process();
        check (r.sounding() == 0, "stop ends it");

        // A Phase 2 state (no WALK): no W, nothing latched.
        auto old = juce::AudioProcessor::getXmlFromBinary (saved.getData(), (int) saved.getSize());
        old->removeChildElement (old->getChildByName ("WALK"), true);
        juce::MemoryBlock phase2;
        juce::AudioProcessor::copyXmlToBinary (*old, phase2);
        r.p->setStateInformation (phase2.getData(), (int) phase2.getSize());
        const auto none = r.p->getWalkState();
        bool nothingLatched = true;
        for (const auto v : none.latched)
            nothingLatched = nothingLatched && v == 0;
        check (none.word.empty() && none.orbitStep == 0 && nothingLatched, "a state from before Phase 3 has an empty W and nothing latched");
    }

    void testState()
    {
        std::cout << "state" << std::endl;
        TonnetzProcessor a;
        check (a.getState().getParameter (params::enable)->getValue() == 1.0f && a.getState().getParameter (params::testRig)->getValue() == 0.0f,
               "defaults: Enable on, Test rig off");
        set (a, params::enable, false);
        setPlain (a, params::slot, 6.0f);
        setPlain (a, params::gatherMs, 37.0f);
        setPlain (a, params::mix, 2.0f);
        setPlain (a, params::sourceChannel, 7.0f);
        setPlain (a, params::genChannel, 16.0f);
        setPlain (a, params::registerMode, 1.0f);
        setPlain (a, params::bass, 1.0f);
        setPlain (a, params::octave, -2.0f);
        check (a.setSlotWordText (0, "Grow(0,4) P +M3 Shrink") && a.setSlotWordText (7, "T(2,-1) Flip(0,7) Pivot(4) H"), "slot words set from text");
        juce::MemoryBlock saved;
        a.getStateInformation (saved);

        TonnetzProcessor b;
        b.setStateInformation (saved.getData(), (int) saved.getSize());
        check (b.getState().getParameter (params::enable)->getValue() == 0.0f && plain (b, params::slot) == 6.0f && plain (b, params::gatherMs) == 37.0f
                   && plain (b, params::mix) == 2.0f && plain (b, params::sourceChannel) == 7.0f && plain (b, params::genChannel) == 16.0f
                   && plain (b, params::registerMode) == 1.0f && plain (b, params::bass) == 1.0f && plain (b, params::octave) == -2.0f,
               "a saved state restores every parameter into a new instance");
        bool wordsBack = true;
        for (int i = 0; i < numSlots; ++i)
            wordsBack = wordsBack && b.getSlotWord (i) == a.getSlotWord (i);
        check (wordsBack && printWord (b.getSlotWord (0)).c_str() == std::string ("Grow(0,4) P +M3 Shrink"),
               "and the eight slot words (" + juce::String (printWord (b.getSlotWord (7)).c_str()) + " in H)");

        // A state from Phase 0 (no SLOTS), and one with a word that doesn't parse.
        auto xml = juce::AudioProcessor::getXmlFromBinary (saved.getData(), (int) saved.getSize());
        xml->removeChildElement (xml->getChildByName ("SLOTS"), true);
        juce::MemoryBlock old;
        juce::AudioProcessor::copyXmlToBinary (*xml, old);
        b.setStateInformation (old.getData(), (int) old.getSize());
        check (printWord (b.getSlotWord (0)).c_str() == std::string ("P") && plain (b, params::slot) == 6.0f,
               "a state without slot words (Phase 0) gets the factory words, parameters still restored");
        xml = juce::AudioProcessor::getXmlFromBinary (saved.getData(), (int) saved.getSize());
        xml->getChildByName ("SLOTS")->setAttribute ("A", "P Q nonsense");
        juce::MemoryBlock bad;
        juce::AudioProcessor::copyXmlToBinary (*xml, bad);
        b.setStateInformation (bad.getData(), (int) bad.getSize());
        check (printWord (b.getSlotWord (0)).c_str() == std::string ("P") && b.getSlotWord (7) == a.getSlotWord (7),
               "a slot word that doesn't parse gets its factory word; the others are kept");
        check (! b.getState().state.getChildWithName ("SLOTS").isValid(), "the slot words are not left in the parameter tree");

        check (! b.getState().state.getChildWithName ("WALK").isValid(), "nor are W and the latched chord");

        const char garbage[] = "not a state at all";
        b.setStateInformation (garbage, (int) sizeof (garbage));
        b.setStateInformation (nullptr, 0);
        check (b.getState().getParameter (params::enable)->getValue() == 0.0f && b.getSlotWord (7) == a.getSlotWord (7), "garbage is ignored");
    }
    //==========================================================================
    // The lattice editor (Phase 4, PLAN.md §5, §7.2).

    TonnetzEditor& editorOf (std::unique_ptr<juce::AudioProcessorEditor>& e) { return dynamic_cast<TonnetzEditor&> (*e); }

    // A synthetic mouse event on `c` at `pos` (its own coordinates).
    juce::MouseEvent mouseAt (juce::Component& c, juce::Point<float> pos, juce::ModifierKeys mods = {}, int clicks = 1, juce::Point<float> downAt = { -1.0f, -1.0f })
    {
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), pos, mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, now,
                                 downAt.x < 0.0f ? pos : downAt, now, clicks, false);
    }

    const auto leftButton = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier);

    // The editor's own snapshot (the editor at `w` × `h`, animation held still).
    juce::Image editorImage (TonnetzEditor& ed, int w, int h)
    {
        ed.setSize (w, h);
        ed.refreshNow();
        ed.latticeView().recentre();
        ed.refreshNow();
        return ed.createComponentSnapshot (ed.getLocalBounds(), false, 1.0f);
    }

    juce::MidiBuffer chordOn (std::initializer_list<int> notes, int sample = 0, int velocity = 100)
    {
        juce::MidiBuffer b;
        for (int n : notes)
            b.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) velocity), sample);
        return b;
    }

    std::set<int> pcsOf (const std::set<int>& notes)
    {
        std::set<int> s;
        for (int n : notes)
            s.insert (n % 12);
        return s;
    }

    std::set<int> pcsOfMask (unsigned m)
    {
        std::set<int> s;
        for (int i = 0; i < 12; ++i)
            if ((m >> i) & 1u)
                s.insert (i);
        return s;
    }

    void testEditor (const juce::File& fixtures, const juce::File& output)
    {
        std::cout << "editor" << std::endl;
        TonnetzProcessor p;
        check (p.hasEditor(), "the processor has an editor");
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
        check (editor != nullptr && editor->getWidth() == TonnetzEditor::minWidth && editor->getHeight() == TonnetzEditor::minHeight, "it opens at 760 x 520");
        editor->setSize (200, 100);
        check (editor->isResizable() && editor->getWidth() == 200, "resizable (the host is held to 760 x 520 by the constrainer)");
        if (auto* c = editor->getConstrainer())
            check (c->getMinimumWidth() == TonnetzEditor::minWidth && c->getMinimumHeight() == TonnetzEditor::minHeight, "smallest size 760 x 520");
        auto& ed = editorOf (editor);
        ed.freezeAnimation (true);
        ed.setSize (760, 520);

        // Nothing from the siblings' look: no combo boxes, sliders or text buttons anywhere.
        int stock = 0;
        std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
        {
            stock += dynamic_cast<juce::ComboBox*> (&c) != nullptr || dynamic_cast<juce::Slider*> (&c) != nullptr || dynamic_cast<juce::TextButton*> (&c) != nullptr;
            for (auto* child : c.getChildren())
                walk (*child);
        };
        walk (ed);
        check (stock == 0, "no combo boxes, sliders or text buttons (the controls are HUD labels and chips)");

        // The empty lattice in the paper theme at the smallest size.
        compareImage (editorImage (ed, 760, 520), "lattice_empty_paper_760x520", fixtures, output);
        check (ed.model().sentence.isEmpty() && ! ed.model().source.present, "with nothing playing there is no chord and no sentence");
    }

    // Reference images: fixed chords and words in both themes at three sizes (§7.2).
    void testLatticeImages (const juce::File& fixtures, const juce::File& output)
    {
        std::cout << "lattice images" << std::endl;
        const int sizes[3][2] = { { 760, 520 }, { 1200, 800 }, { 1800, 1100 } };

        // 1. Mirror, slot E (P L), C E G from the host: the generated chord is A♭ (C E♭ A♭).
        {
            MiniHost h;
            setPlain (*h.p, params::gatherMs, 0.0f);
            setPlain (*h.p, params::slot, 4.0f);
            h.p->getState().getParameter (params::key)->setValueNotifyingHost (0.0f);      // Key Off: plain C major spelling
            std::unique_ptr<juce::AudioProcessorEditor> e (h.p->createEditor());
            auto& ed = editorOf (e);
            ed.freezeAnimation (true);
            h.process (chordOn ({ 60, 64, 67 }));
            h.process();
            for (int theme = 0; theme < 2; ++theme)
            {
                ed.setDark (theme == 1);
                for (const auto& s : sizes)
                    compareImage (editorImage (ed, s[0], s[1]), juce::String ("lattice_mirror_PL_") + (theme ? "dark_" : "paper_") + juce::String (s[0]) + "x" + juce::String (s[1]),
                                  fixtures, output);
            }
            check (ed.model().sentence == juce::String::fromUTF8 ("C \xe2\x94\x80\xe2\x94\x80P\xc2\xb7L\xe2\x94\x80\xe2\x94\x80\xe2\x96\xba A\xe2\x99\xad"),
                   "the header's sentence: " + ed.model().sentence);
            check (ed.model().path.size() == 2 && ed.model().source.shape.size == 3 && ed.model().generated.present,
                   "W's path has two steps (P, then L) from the source triangle");
        }

        // 2. Walk, Cmaj7 held, W = P L from the word bar, key held at C major with In key: the
        //    plan's sentence, rhombi, ghosts solid in the key and faint outside it.
        {
            MiniHost h;
            setPlain (*h.p, params::gatherMs, 0.0f);
            setPlain (*h.p, params::mode, 1.0f);
            setPlain (*h.p, params::key, 2.0f);
            setPlain (*h.p, params::keyTonic, 0.0f);
            setPlain (*h.p, params::inKey, 1.0f);
            std::unique_ptr<juce::AudioProcessorEditor> e (h.p->createEditor());
            auto& ed = editorOf (e);
            ed.freezeAnimation (true);
            h.process (chordOn ({ 60, 64, 67, 71 }));
            ed.refreshNow();
            Word w;
            parseWord ("P L", w);
            ed.wordBar().setWord (w);
            h.process();
            h.process();
            for (int theme = 0; theme < 2; ++theme)
            {
                ed.setDark (theme == 1);
                for (int s = 0; s < 2; ++s)
                    compareImage (editorImage (ed, sizes[s][0], sizes[s][1]), juce::String ("lattice_walk_cmaj7_key_") + (theme ? "dark_" : "paper_") + juce::String (sizes[s][0]) + "x" + juce::String (sizes[s][1]),
                                  fixtures, output);
            }
            check (ed.model().sentence == juce::String::fromUTF8 ("Cmaj7 (I) \xe2\x94\x80\xe2\x94\x80P\xc2\xb7L\xe2\x94\x80\xe2\x94\x80\xe2\x96\xba A\xe2\x99\xadmaj7 (\xe2\x99\xadVI)"),
                   "the plan's sentence, spelled and numbered in C major: " + ed.model().sentence);
            check (classify (ed.model().source.shape).shape == Shape::Rhombus && classify (ed.model().generated.shape).shape == Shape::Rhombus,
                   "Cmaj7 and A\xe2\x99\xadmaj7 are rhombi");
        }

        // 3. Orbit LR on A minor: the cycle's loop through all 24 triads.
        {
            MiniHost h;
            setPlain (*h.p, params::gatherMs, 0.0f);
            setPlain (*h.p, params::mode, 2.0f);
            setPlain (*h.p, params::orbitCycle, 2.0f);
            setPlain (*h.p, params::orbitRate, 0.0f);
            h.p->getState().getParameter (params::key)->setValueNotifyingHost (0.0f);
            std::unique_ptr<juce::AudioProcessorEditor> e (h.p->createEditor());
            auto& ed = editorOf (e);
            ed.freezeAnimation (true);
            h.process (chordOn ({ 57, 60, 64 }));
            h.runTo (h.pos + MiniHost::at (3.1));          // three quarter-note steps
            compareImage (editorImage (ed, 1200, 800), "lattice_orbit_LR_paper_1200x800", fixtures, output);
            check (ed.model().cycle.size() == 25 && ed.model().cycleAt == 3, "Orbit LR: a loop of 24 triads drawn, the step reached marked ("
                   + juce::String ((int) ed.model().cycle.size()) + ", at " + juce::String (ed.model().cycleAt) + ")");
        }
    }

    // A click at the centre of each of the 24 triangles plays that triad (§7.2); a drag glides;
    // shift-click builds a set; a ghost click appends its op; the keyboard strip plays.
    void testDirectPlay()
    {
        std::cout << "direct play" << std::endl;
        MiniHost h;
        setPlain (*h.p, params::gatherMs, 0.0f);
        setPlain (*h.p, params::mix, 2.0f);                   // Source only: what was played comes out as it is
        std::unique_ptr<juce::AudioProcessorEditor> e (h.p->createEditor());
        auto& ed = editorOf (e);
        ed.freezeAnimation (true);
        ed.setSize (1200, 800);
        ed.setVisible (true);                                 // (no window: getComponentAt needs it)
        h.p->editorView().sticky = false;
        auto& lat = ed.latticeView();
        const auto& geo = lat.geometry();

        int good = 0;
        juce::StringArray bad;
        for (int root = 0; root < 12; ++root)
            for (const bool major : { true, false })
            {
                // A copy of the triangle in the open (not under a floating control).
                juce::Point<float> at;
                bool found = false;
                for (int b = -4; b <= 4 && ! found; ++b)
                    for (int a = -6; a <= 6 && ! found; ++a)
                    {
                        const Vec r { a, b };
                        if (pitchClass (r) != root)
                            continue;
                        const ui::Triangle t { r, major };
                        const auto c = geo.screenCentreOf (t);
                        const auto inEditor = ed.getLocalPoint (&lat, juce::Point<float> ((float) c.x, (float) c.y));
                        if (c.x > 20 && c.y > 20 && c.x < lat.getWidth() - 20 && c.y < lat.getHeight() - 90
                            && ed.getComponentAt (inEditor) == &lat)
                        {
                            at = { (float) c.x, (float) c.y };
                            found = true;
                        }
                    }
                const auto mask = major ? majorTriadMask (root) : minorTriadMask (root);
                const size_t before = h.notes.size();
                lat.mouseDown (mouseAt (lat, at, leftButton));
                h.process();
                const auto on = soundingOn (h, 1);
                const bool ok = found && geo.triangleAt (at.x, at.y).mask() == mask && pcsOf (on) == pcsOfMask (mask) && on.size() == 3;
                lat.mouseUp (mouseAt (lat, at));
                h.process();
                const bool off = soundingOn (h, 1).empty() && h.notes.size() == before + 6;
                good += ok && off ? 1 : 0;
                if (! (ok && off))
                    bad.add (juce::String (root) + (major ? "M" : "m") + " (" + text (on) + ")");
            }
        check (good == 24, "a click at each of the 24 triangles' centres plays its triad and lets go (" + juce::String (good) + "/24) " + bad.joinIntoString (", "));

        // A drag glides: C major to E minor (L) moves one voice, C down to B.
        {
            const ui::Triangle cMajor { { 0, 0 }, true }, eMinor { { 0, 1 }, false };
            const auto c1 = geo.screenCentreOf (cMajor), c2 = geo.screenCentreOf (eMinor);
            lat.mouseDown (mouseAt (lat, { (float) c1.x, (float) c1.y }, leftButton));
            h.process();
            const auto first = soundingOn (h, 1);
            const size_t before = h.notes.size();
            lat.mouseDrag (mouseAt (lat, { (float) c2.x, (float) c2.y }, leftButton, 1, { (float) c1.x, (float) c1.y }));
            h.process();
            const auto second = soundingOn (h, 1);
            check (first == std::set<int> { 60, 64, 67 } && second == std::set<int> { 59, 64, 67 } && h.notes.size() == before + 2,
                   "dragging from C major into E minor glides: one voice moves (C to B): " + text (first) + " / " + text (second));
            lat.mouseUp (mouseAt (lat, { (float) c2.x, (float) c2.y }));
            h.process();
            check (soundingOn (h, 1).empty(), "letting go ends it (Sticky off)");
        }

        // Shift-click four nodes: C, E, G, B♭ is C7; shift-clicking E again removes it.
        {
            for (const Vec v : { Vec { 0, 0 }, Vec { 0, 1 }, Vec { 1, 0 }, Vec { -2, 0 } })
            {
                const auto c = geo.toScreen (v);
                lat.mouseDown (mouseAt (lat, { (float) c.x + 2.0f, (float) c.y - 1.0f }, leftButton.withFlags (juce::ModifierKeys::shiftModifier)));
                lat.mouseUp (mouseAt (lat, { (float) c.x + 2.0f, (float) c.y - 1.0f }));
            }
            h.process();
            const auto set = pcsOf (soundingOn (h, 1));
            const auto e1 = geo.toScreen (Vec { 0, 1 });
            lat.mouseDown (mouseAt (lat, { (float) e1.x, (float) e1.y }, leftButton.withFlags (juce::ModifierKeys::shiftModifier)));
            h.process();
            check (set == std::set<int> { 0, 4, 7, 10 } && pcsOf (soundingOn (h, 1)) == std::set<int> { 0, 7, 10 },
                   "shift-clicking C, E, G and B\xe2\x99\xad builds C7; shift-clicking E again takes it out");
            lat.releaseLattice();
            h.process();
        }

        // Sticky: a triangle stays down after the click, and goes on a second click.
        {
            h.p->editorView().sticky = true;
            const auto c = geo.screenCentreOf ({ { 1, 0 }, true });     // G major
            lat.mouseDown (mouseAt (lat, { (float) c.x, (float) c.y }, leftButton));
            lat.mouseUp (mouseAt (lat, { (float) c.x, (float) c.y }));
            h.process();
            const auto held = pcsOf (soundingOn (h, 1));
            lat.mouseDown (mouseAt (lat, { (float) c.x, (float) c.y }, leftButton));
            lat.mouseUp (mouseAt (lat, { (float) c.x, (float) c.y }));
            h.process();
            check (held == std::set<int> { 7, 11, 2 } && soundingOn (h, 1).empty(), "Sticky: G major stays down after the click and goes on a second click");
        }

        // A ghost click (Walk): with C major held, the L chip appends L; E minor sounds.
        {
            setPlain (*h.p, params::mode, 1.0f);
            setPlain (*h.p, params::mix, 0.0f);
            setPlain (*h.p, params::opZoneOn, 0.0f);
            const auto c = geo.screenCentreOf ({ { 0, 0 }, true });
            lat.mouseDown (mouseAt (lat, { (float) c.x, (float) c.y }, leftButton));
            lat.mouseUp (mouseAt (lat, { (float) c.x, (float) c.y }));
            h.process();
            ed.refreshNow();
            const auto chip = lat.ghostChipCentre (1);                // L
            const int hit = lat.ghostAt (chip);
            lat.mouseDown (mouseAt (lat, chip, leftButton));
            lat.mouseUp (mouseAt (lat, chip));
            h.process();
            ed.refreshNow();
            const auto s = h.p->getEngineStatus();
            check (hit == 1 && printWord (s.walk.word).c_str() == std::string ("L") && pcsOf (soundingOn (h, 2)) == std::set<int> { 4, 7, 11 },
                   "Walk: clicking the L ghost (operator keys off) appends L and E minor sounds: " + text (soundingOn (h, 2)));
            check (ed.model().ghosts[1].description.startsWith (juce::String::fromUTF8 ("L \xe2\x86\x92 C major")),
                   "the L ghost of E minor names its result: " + ed.model().ghosts[1].description);
            setPlain (*h.p, params::mode, 0.0f);
            h.process();
            ed.refreshNow();
            const int count = (int) h.notes.size();
            lat.mouseDown (mouseAt (lat, lat.ghostChipCentre (0), leftButton));
            h.process();
            check (! lat.ghostsClickable() && printWord (h.p->getEngineStatus().walk.word).c_str() == std::string ("L") && (int) h.notes.size() >= count,
                   "Mirror: the ghosts are labels, not buttons (a click there plays the triangle instead)");
            lat.releaseLattice();
            h.process();
        }

        // The keyboard strip: a click plays a key (Sticky: it stays until clicked again); an
        // operator-zone key walks W and doesn't sound.
        {
            setPlain (*h.p, params::mode, 1.0f);
            setPlain (*h.p, params::mix, 1.0f);
            setPlain (*h.p, params::opZoneOn, 1.0f);
            auto& kb = ed.keyboardStrip();
            for (int n : { 60, 64, 67 })
                kb.mouseDown (mouseAt (kb, kb.keyBounds (n).getCentre(), leftButton));
            h.process();
            const auto held = soundingOn (h, 1);
            ed.refreshNow();
            const auto zone24 = kb.keyBounds (24).getCentre();
            kb.mouseDown (mouseAt (kb, zone24, leftButton));            // P
            kb.mouseDown (mouseAt (kb, zone24, leftButton));            // and up again (sticky)
            h.process();
            bool zoneSilent = true;
            for (const auto& n : h.notes)
                zoneSilent = zoneSilent && n.note != 24;
            check (held == std::set<int> { 60, 64, 67 } && zoneSilent && printWord (h.p->getEngineStatus().walk.word).c_str() == std::string ("L P"),
                   "keyboard strip: C E G clicked on, the P key (C0) walks W (L P) and never sounds");
            check (ui::KeyboardStrip::keyName (60) == "C3" && ui::KeyboardStrip::keyName (24) == "C0", "keys are named as Live names them (C3 = 60, C0 = 24)");
            for (int n : { 60, 64, 67 })
                kb.clickKey (n, 100);
            h.process();
            check (soundingOn (h, 1).empty(), "clicking held keys again lets them go");
        }

        // A stop: the engine lets go of everything and the editor forgets what it held.
        {
            setPlain (*h.p, params::mix, 2.0f);
            setPlain (*h.p, params::mode, 0.0f);              // (in Walk a ghost chip could sit on the triangle)
            h.process();
            ed.refreshNow();
            const auto c = geo.screenCentreOf ({ { 0, 0 }, true });
            lat.mouseDown (mouseAt (lat, { (float) c.x, (float) c.y }, leftButton));
            lat.mouseUp (mouseAt (lat, { (float) c.x, (float) c.y }));
            h.process();
            const bool wasHeld = ed.editorNotes().any (ui::EditorNotes::lattice);
            h.head.playing = true;
            h.process();
            h.head.playing = false;
            h.process();
            ed.refreshNow();
            check (wasHeld && ! ed.editorNotes().any (ui::EditorNotes::lattice) && ! lat.heldTriangle().has_value() && soundingOn (h, 1).empty(),
                   "a transport stop ends the clicked chord and the editor forgets it (" + juce::String ((int) wasHeld) + juce::String ((int) ed.editorNotes().any (ui::EditorNotes::lattice))
                       + juce::String ((int) lat.heldTriangle().has_value()) + " " + text (soundingOn (h, 1)) + ")");
        }
    }

    // Every parameter has a control on screen (§5, §7.2), and clicking it changes the parameter.
    void testControls()
    {
        std::cout << "controls" << std::endl;
        TonnetzProcessor p;
        std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditor());
        auto& ed = editorOf (e);
        ed.freezeAnimation (true);
        for (const auto& size : { std::make_pair (760, 520), std::make_pair (1400, 900) })
        {
            ed.setSize (size.first, size.second);
            ed.refreshNow();
            int found = 0, inside = 0, works = 0, total = 0;
            juce::StringArray missing, broken;
            for (auto* param : p.getParameters())
            {
                auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param);
                if (ranged == nullptr)
                    continue;
                ++total;
                const auto id = ranged->getParameterID();
                auto* c = ed.controlFor (id);
                if (c == nullptr)
                {
                    missing.add (id);
                    continue;
                }
                ++found;
                bool visible = true;
                for (juce::Component* x = c; x != nullptr && x != &ed; x = x->getParentComponent())
                    visible = visible && x->isVisible();
                const auto r = ed.getLocalArea (c, c->getLocalBounds());
                if (visible && ! r.isEmpty() && ed.getLocalBounds().contains (r) && r.getWidth() >= c->preferredWidth() - 1)
                    ++inside;
                else
                    broken.add (id + " (bounds)");
                const float before = ranged->getValue();
                c->activate (false);
                const float after = ranged->getValue();
                if (after != before)
                    ++works;
                else
                    broken.add (id + " (click)");
                ranged->setValueNotifyingHost (before);
                c->refresh();
            }
            check (found == total && total == 21, juce::String (size.first) + " x " + juce::String (size.second) + ": every parameter has a control ("
                   + juce::String (found) + "/" + juce::String (total) + ") " + missing.joinIntoString (" "));
            check (inside == total && works == total, "each is visible, inside the window at its full width, and a click changes it " + broken.joinIntoString (" "));

            // No two controls overlap.
            auto all = ed.controls();
            int overlaps = 0;
            for (int i = 0; i < all.size(); ++i)
                for (int j = 0; j < i; ++j)
                    overlaps += ed.getLocalArea (all[i], all[i]->getLocalBounds()).intersects (ed.getLocalArea (all[j], all[j]->getLocalBounds())) ? 1 : 0;
            check (overlaps == 0, "no two controls overlap (" + juce::String (overlaps) + ")");
        }

        // A row of chips is clicked on its option: Orbit in the mode row.
        auto* mode = dynamic_cast<ui::HudChoices*> (ed.controlFor (params::mode));
        mode->mouseUp (mouseAt (*mode, mode->optionBounds (2).getCentre()));
        check (plain (p, params::mode) == 2.0f, "clicking Orbit in the mode row selects it");
        // The key badge cycles Listen, Hold, Off.
        auto* key = ed.controlFor (params::key);
        key->activate (false);
        const float hold = plain (p, params::key);
        key->activate (false);
        const float off = plain (p, params::key);
        key->activate (false);
        check (hold == 2.0f && off == 0.0f && plain (p, params::key) == 1.0f, "the key badge cycles Listen, Hold, Off, Listen");
        // A number: Gather moves by 5 a click and wraps.
        auto* gather = ed.controlFor (params::gatherMs);
        gather->activate (false);
        check (plain (p, params::gatherMs) == 25.0f && gather->valueText() == "25 ms", "gather: a click adds 5 ms (" + gather->valueText() + ")");
    }

    // The word bar (§5): text entry, the palette, dragging chips, Mirror's slot word, Walk's W.
    void testWordBar()
    {
        std::cout << "word bar" << std::endl;
        MiniHost h;
        setPlain (*h.p, params::gatherMs, 0.0f);
        std::unique_ptr<juce::AudioProcessorEditor> e (h.p->createEditor());
        auto& ed = editorOf (e);
        ed.freezeAnimation (true);
        auto& wb = ed.wordBar();
        const auto slotWord = [&h] (int s) { return std::string (printWord (h.p->getSlotWord (s)).c_str()); };

        check (printWord (wb.shownWord()).c_str() == std::string ("P"), "Mirror: the bar shows slot A's word (P)");
        wb.beginTextEntry();
        wb.textField().setText (juce::String::fromUTF8 ("P\xc2\xb7L R"), false);
        check (wb.commitText() && slotWord (0) == "P L R", "a word typed in (P\xc2\xb7L R) is stored in the slot as P L R");
        wb.beginTextEntry();
        wb.textField().setText ("P Q", false);
        check (! wb.commitText() && slotWord (0) == "P L R" && wb.message().startsWith ("Not a word"), "text that isn't a word is refused; the slot keeps its word");
        wb.textField().setVisible (false);

        wb.openPalette();
        auto* pal = wb.palette();
        check (pal != nullptr && pal->isVisible() && pal->getParentComponent() == &ed, "+ opens the op palette over the lattice");
        pal->pick (0);                                       // +5
        check (slotWord (0) == "P L R +5" && ! pal->isVisible(), "picking +5 appends it and closes the palette");

        // Drag the first chip (P) after the third (R); then drag L off the bar to delete it.
        {
            const auto from = wb.chipBounds (0).getCentre(), to = wb.chipBounds (2).getCentre() + juce::Point<float> (4.0f, 0.0f);
            wb.mouseDown (mouseAt (wb, from, leftButton));
            wb.mouseDrag (mouseAt (wb, to, leftButton, 1, from));
            wb.mouseUp (mouseAt (wb, to));
            const auto afterMove = slotWord (0);
            const auto l = wb.chipBounds (0).getCentre();
            wb.mouseDown (mouseAt (wb, l, leftButton));
            wb.mouseDrag (mouseAt (wb, l + juce::Point<float> (0.0f, -60.0f), leftButton, 1, l));
            wb.mouseUp (mouseAt (wb, l + juce::Point<float> (0.0f, -60.0f)));
            check (afterMove == "L R P +5" && slotWord (0) == "R P +5", "dragging P after R reorders (" + afterMove + "); dropping L off the bar deletes it (" + slotWord (0) + ")");
        }
        wb.undo();
        check (slotWord (0) == "R P", "\xe2\x8c\xab removes the last op");
        wb.home();
        check (slotWord (0).empty(), "\xe2\x8c\x82 clears the word");
        setPlain (*h.p, params::slot, 4.0f);
        ed.refreshNow();
        check (printWord (wb.shownWord()).c_str() == std::string ("P L"), "the bar follows the slot parameter (E: P L)");

        // Walk: the bar edits W in the engine.
        setPlain (*h.p, params::mode, 1.0f);
        h.process (chordOn ({ 60, 64, 67 }));
        wb.appendOp (ops::R);
        wb.appendOp (ops::majorThirdUp);
        h.process();
        ed.refreshNow();
        check (printWord (h.p->getEngineStatus().walk.word).c_str() == std::string ("R +M3") && soundingOn (h, 2) == voiced ("R +M3", { 60, 64, 67 }),
               "Walk: R and +M3 from the palette walk the held chord (" + text (soundingOn (h, 2)) + ")");
        wb.appendOp (ops::majorThirdDown);
        h.process();
        check (printWord (h.p->getEngineStatus().walk.word).c_str() == std::string ("R"), "\xe2\x88\x92M3 after +M3 cancels, as an operator key would");
        // Orbit with a factory cycle: read only.
        setPlain (*h.p, params::mode, 2.0f);
        ed.refreshNow();
        check (! wb.editable() && printWord (wb.shownWord()).c_str() == std::string ("P L"), "Orbit (cycle PL): the bar shows the cycle, read only");
    }

    // Spelling and numerals in the key (§3.6), In key on the ghosts, and the key band.
    void testKeyOnLattice()
    {
        std::cout << "key on the lattice" << std::endl;
        MiniHost h;
        setPlain (*h.p, params::gatherMs, 0.0f);
        setPlain (*h.p, params::mode, 1.0f);
        setPlain (*h.p, params::key, 2.0f);
        setPlain (*h.p, params::keyTonic, 3.0f);              // E♭ minor (as held): C♭ is its sixth
        setPlain (*h.p, params::keyMinor, 1.0f);
        std::unique_ptr<juce::AudioProcessorEditor> e (h.p->createEditor());
        auto& ed = editorOf (e);
        ed.freezeAnimation (true);
        h.process (chordOn ({ 59, 63, 66 }));                 // B D♯ F♯, which E♭ minor spells C♭ E♭ G♭
        h.process();
        ed.refreshNow();
        const auto& lat = ed.latticeView();
        const Vec camera { (int) std::lround (lat.geometry().camera().a), (int) std::lround (lat.geometry().camera().b) };
        const auto label = lat.nodeLabel (nearestNode (11, nearestNode (3, camera)));
        check (label == juce::String::fromUTF8 ("C\xe2\x99\xad") && ed.model().source.name == juce::String::fromUTF8 ("C\xe2\x99\xad") && ed.model().source.numeral == "VI",
               "E\xe2\x99\xad minor: the node a third below the tonic is C\xe2\x99\xad, not B, and the chord is C\xe2\x99\xad (VI): " + label + ", "
               + ed.model().source.name + " (" + ed.model().source.numeral + ")");
        check (ed.model().keyText == juce::String::fromUTF8 ("E\xe2\x99\xad minor held") || ed.model().keyText == juce::String::fromUTF8 ("E\xe2\x99\xad minor"),
               "the key is named E\xe2\x99\xad minor: " + ed.model().keyText);
        std::set<juce::String> labels;
        int aLo, aHi, bLo, bHi;
        lat.geometry().visible (aLo, aHi, bLo, bHi, 0.0);
        for (int b = bLo; b <= bHi; ++b)
            for (int a = aLo; a <= aHi; ++a)
                labels.insert (lat.nodeLabel ({ a, b }));
        bool noTriple = true;
        for (const auto& l : labels)
            noTriple = noTriple && ! l.contains (juce::String::fromUTF8 ("\xe2\x99\xad\xe2\x99\xad\xe2\x99\xad")) && ! l.contains (juce::String::fromUTF8 ("\xe2\x99\xaf\xe2\x99\xaf\xe2\x99\xaf"));
        check (noTriple && labels.size() > 12, "every node in view is spelled with at most a double accidental (" + juce::String ((int) labels.size()) + " spellings)");

        // Root-position names don't take the lowest pitch class as the bass (the Live check
        // showed A C E named C6).
        check (ed.model().chordName (minorTriadMask (9), -1) == "Am" && ed.model().longChordName (minorTriadMask (9)) == "A minor"
                   && ed.model().chordName ((PcMask) (majorTriadMask (8) | pcBit (7)), -1) == juce::String::fromUTF8 ("A\xe2\x99\xadmaj7"),
               "A C E is Am (A minor), not C6; A\xe2\x99\xad C E\xe2\x99\xad G is A\xe2\x99\xadmaj7: " + ed.model().chordName (minorTriadMask (9), -1));

        // In key: C major held, the ghosts in the key are solid, the rest faint, exactly opsInKey.
        setPlain (*h.p, params::keyTonic, 0.0f);
        setPlain (*h.p, params::keyMinor, 0.0f);
        setPlain (*h.p, params::inKey, 1.0f);
        juce::MidiBuffer offs;
        for (int n : { 59, 63, 66 })
            offs.addEvent (juce::MidiMessage::noteOff (1, n), 0);
        h.process (offs);
        h.process (chordOn ({ 60, 64, 67 }));
        h.process();
        ed.refreshNow();
        const auto bits = opsInKey (h.p->getEngineStatus().generated, KeyRef { 0, false });
        bool exact = true;
        int solid = 0;
        for (const auto& g : ed.model().ghosts)
        {
            exact = exact && g.inKey == ((bits >> g.key & 1u) != 0) && g.faint == ! g.inKey;
            solid += g.inKey ? 1 : 0;
        }
        check (exact && solid == 4, "In key, C major in C major: the ghosts marked in the key are exactly opsInKey (" + juce::String (solid)
               + ": L R +5 -5), the others faint");
        setPlain (*h.p, params::inKey, 0.0f);
        ed.refreshNow();
        bool noneFaint = true;
        for (const auto& g : ed.model().ghosts)
            noneFaint = noneFaint && ! g.faint;
        check (noneFaint, "In key off: no ghost is faint");
    }

    // No allocation on the audio thread while the editor sends notes, ghost clicks and words.
    void testEditorRealtime()
    {
        std::cout << "editor, real time" << std::endl;
        MiniHost h;
        setPlain (*h.p, params::gatherMs, 0.0f);
        setPlain (*h.p, params::mode, 1.0f);
        std::unique_ptr<juce::AudioProcessorEditor> e (h.p->createEditor());
        auto& ed = editorOf (e);
        ed.freezeAnimation (true);
        h.process();
        Word w;
        parseWord ("P L R", w);
        int allocations = 0;
        for (int round = 0; round < 40; ++round)
        {
            // The message thread queues (allocation there is fine), then a block runs under the hook.
            const int root = 48 + round % 12;
            h.p->sendEditorCommand (EditorCommand::noteOn (root, 90));
            h.p->sendEditorCommand (EditorCommand::noteOn (root + 4, 90));
            h.p->sendEditorCommand (EditorCommand::noteOn (root + 7, 90));
            h.p->sendEditorCommand (EditorCommand::operatorKey (round % 10));
            if (round % 5 == 0)
                h.p->sendEditorCommand (EditorCommand::setWalk (w));
            {
                AllocWatch watch;
                allocCount = 0;
                h.process();
                allocations += allocCount.load();
            }
            h.p->sendEditorCommand (EditorCommand::noteOff (root));
            h.p->sendEditorCommand (EditorCommand::noteOff (root + 4));
            h.p->sendEditorCommand (EditorCommand::noteOff (root + 7));
            {
                AllocWatch watch;
                allocCount = 0;
                h.process();
                allocations += allocCount.load();
            }
            ed.refreshNow();
        }
        check (allocations == 0, "no allocation in processBlock while the editor plays chords, clicks ghosts and sets W (" + juce::String (allocations) + ")");
        check (h.sounding() == 0, "nothing left sounding");

        // A full queue refuses; the commands already in it all arrive.
        TonnetzProcessor q;
        int queued = 0;
        while (q.sendEditorCommand (EditorCommand::operatorKey (0)))
            ++queued;
        check (queued == EditorCommandFifo::capacity, "the command queue holds " + juce::String (queued) + " and then refuses");

        // A press and release in one block keep their order (one sample apart).
        MiniHost r;
        setPlain (*r.p, params::gatherMs, 0.0f);
        setPlain (*r.p, params::mix, 2.0f);
        r.process();
        r.p->sendEditorCommand (EditorCommand::noteOn (62, 100));
        r.p->sendEditorCommand (EditorCommand::noteOff (62));
        r.process();
        check (r.notes.size() == 2 && r.notes[0].velocity > 0 && r.notes[1].velocity == 0 && r.notes[1].sample == r.notes[0].sample + 1,
               "an editor note pressed and let go within one block sounds, then ends, a sample later");
    }
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc < 3)
    {
        std::cerr << "usage: TonnetzMLSnapshot <fixtures dir> <output dir> [--update | --no-pixels]" << std::endl;
        return 2;
    }
    const auto cwd = juce::File::getCurrentWorkingDirectory();
    const auto fixtures = cwd.getChildFile (juce::String::fromUTF8 (argv[1]));
    const auto output = cwd.getChildFile (juce::String::fromUTF8 (argv[2]));
    for (int i = 3; i < argc; ++i)
    {
        updateImages = updateImages || juce::String (argv[i]) == "--update";
        comparePixels = comparePixels && juce::String (argv[i]) != "--no-pixels";
    }

    testEditor (fixtures, output);
    testLatticeImages (fixtures, output);
    testDirectPlay();
    testControls();
    testWordBar();
    testKeyOnLattice();
    testEditorRealtime();
    testThrough();
    testRig (output);
    testParams();
    testMirror (output);
    testWalkOrbitKey (output);
    testState();
    testWalkState();

    std::cout << (failures == 0 ? "\nsnapshot: all passed" : "\n*** snapshot: " + std::to_string (failures) + " failure(s)") << std::endl;
    return failures == 0 ? 0 : 1;
}
