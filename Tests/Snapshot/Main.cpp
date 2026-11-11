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
//   5. params   the ten parameters in order, with their defaults
//   6. realtime no allocation in processBlock while the rig plays through the engine (a malloc
//               hook counts every one on the audio thread)
//   7. state    a saved state restores the parameters and the slot words into a new instance;
//               a state without slot words, or with a bad one, gets the factory words; garbage
//               is ignored
//
// Later phases add the lattice images, hit-testing and "every parameter has a control".
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
    void snapshot (TonnetzProcessor& p, int width, int height, const juce::File& fixtures, const juce::File& output)
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
        const auto name = "editor_" + juce::String (width) + "x" + juce::String (height);
        if (editor == nullptr)
        {
            check (false, name + ": the processor made an editor");
            return;
        }
        editor->setSize (width, height);
        check (editor->getWidth() == width && editor->getHeight() == height, name + ": the editor takes the size");
        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), false, 1.0f);

        juce::PNGImageFormat png;
        output.createDirectory();
        const auto outFile = output.getChildFile (name + ".png");
        outFile.deleteFile();
        if (juce::FileOutputStream out (outFile); out.openedOk())
            png.writeImageToStream (image, out);

        // Not blank: the page colour and the ink of the name are both there.
        std::set<juce::uint32> colours;
        for (int y = 0; y < image.getHeight(); y += 2)
            for (int x = 0; x < image.getWidth(); x += 2)
                colours.insert (image.getPixelAt (x, y).getARGB());
        check (image.getWidth() == width && image.getHeight() == height && colours.size() > 8, name + ": rendered (" + juce::String ((int) colours.size()) + " colours)");

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

    void testEditor (const juce::File& fixtures, const juce::File& output)
    {
        std::cout << "editor" << std::endl;
        TonnetzProcessor p;
        check (p.hasEditor(), "the processor has an editor");
        snapshot (p, TonnetzEditor::minWidth, TonnetzEditor::minHeight, fixtures, output);
        snapshot (p, 1200, 800, fixtures, output);
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
        editor->setSize (200, 100);
        check (editor->isResizable() && editor->getWidth() == 200, "resizable (the host is held to 760 x 520 by the constrainer)");
        if (auto* c = editor->getConstrainer())
            check (c->getMinimumWidth() == TonnetzEditor::minWidth && c->getMinimumHeight() == TonnetzEditor::minHeight, "smallest size 760 x 520");

        // The word field (Phase 2).
        auto& ed = dynamic_cast<TonnetzEditor&> (*editor);
        check (ed.wordField().getText() == "P", "the word field shows slot A's word (P)");
        ed.wordField().setText (juce::String::fromUTF8 ("P\xc2\xb7L R"), false);   // P·L R
        ed.commitWord();
        check (printWord (p.getSlotWord (0)).c_str() == std::string ("P L R") && ed.wordField().getText() == "P L R",
               "a word typed in is stored in the slot and shown as printed (P L R)");
        ed.wordField().setText ("P Q", false);
        ed.commitWord();
        check (printWord (p.getSlotWord (0)).c_str() == std::string ("P L R") && ed.wordMessage().startsWith ("Not a word"),
               "text that isn't a word is refused, and the slot keeps its word");
        setPlain (p, params::slot, 4.0f);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
        check (ed.wordField().getText() == "P L", "the field follows the slot parameter (E: P L)");
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
        check (ids.joinIntoString (" ") == "enable testRig slot gatherMs mix sourceChannel genChannel register bass octave",
               "ten parameters, Phase 2's appended after Enable and Test rig: " + ids.joinIntoString (" "));
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

        const char garbage[] = "not a state at all";
        b.setStateInformation (garbage, (int) sizeof (garbage));
        b.setStateInformation (nullptr, 0);
        check (b.getState().getParameter (params::enable)->getValue() == 0.0f && b.getSlotWord (7) == a.getSlotWord (7), "garbage is ignored");
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
    testThrough();
    testRig (output);
    testParams();
    testMirror (output);
    testState();

    std::cout << (failures == 0 ? "\nsnapshot: all passed" : "\n*** snapshot: " + std::to_string (failures) + " failure(s)") << std::endl;
    return failures == 0 ? 0 : 1;
}
