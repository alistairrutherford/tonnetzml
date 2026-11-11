#include "TonnetzProcessor.h"
#include "../ui/TonnetzEditor.h"
#include "../core/theory/ChordNamer.h"
#include <cmath>

namespace tonnetz
{
    juce::AudioProcessorValueTreeState::ParameterLayout params::createLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { enable, 1 }, "Enable", true));
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { testRig, 1 }, "Test rig", false));

        // Phase 2.
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { slot, 1 }, "Slot",
                                                                   juce::StringArray { "A", "B", "C", "D", "E", "F", "G", "H" }, 0));
        layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { gatherMs, 1 }, "Gather (ms)", 0, maxGatherMs, 20));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { mix, 1 }, "Mix",
                                                                   juce::StringArray { "Generated", "Source + Generated", "Source" }, 0));
        layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { sourceChannel, 1 }, "Source channel", 1, 16, 1));
        layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { genChannel, 1 }, "Generated channel", 1, 16, 2));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { registerMode, 1 }, "Register",
                                                                   juce::StringArray { "Stay", "Follow" }, 0));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { bass, 1 }, "Bass",
                                                                   juce::StringArray { "Off", "Root below" }, 0));
        layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { octave, 1 }, "Octave", -2, 2, 0));
        return layout;
    }

    SlotWords factorySlotWords()
    {
        SlotWords w;
        for (int i = 0; i < numSlots; ++i)
            parseWord (factorySlotTexts[i], w.words[(size_t) i]);
        return w;
    }

    juce::AudioProcessor::BusesProperties TonnetzProcessor::makeBuses()
    {
        // An instrument with a silent stereo output: hosts route MIDI-only instruments more
        // reliably when there's an audio bus.
        return BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true);
    }

    TonnetzProcessor::TonnetzProcessor()
        : AudioProcessor (makeBuses()),
          state (*this, nullptr, "TonnetzML", params::createLayout())
    {
        enableParam = state.getRawParameterValue (params::enable);
        testRigParam = state.getRawParameterValue (params::testRig);
        slotParam = state.getRawParameterValue (params::slot);
        gatherParam = state.getRawParameterValue (params::gatherMs);
        mixParam = state.getRawParameterValue (params::mix);
        sourceChannelParam = state.getRawParameterValue (params::sourceChannel);
        genChannelParam = state.getRawParameterValue (params::genChannel);
        registerParam = state.getRawParameterValue (params::registerMode);
        bassParam = state.getRawParameterValue (params::bass);
        octaveParam = state.getRawParameterValue (params::octave);
        merged.ensureSize (65536);
        slotWords = factorySlotWords();
        publishSlotWords();
        startTimerHz (20);
    }

    TonnetzProcessor::~TonnetzProcessor()
    {
        stopTimer();
    }

    void TonnetzProcessor::prepareToPlay (double sampleRate, int)
    {
        preparedSampleRate.store (sampleRate);
        statMaxBlock.store (0);
        merged.ensureSize (65536);
        resetRequested.store (true);
    }

    void TonnetzProcessor::releaseResources()
    {
        resetRequested.store (true);
    }

    // JUCE's VST3 wrapper calls this when the host stops processing. No MIDI can go out then, so
    // the rig's held keys end on the first block after it resumes.
    void TonnetzProcessor::reset()
    {
        resetRequested.store (true);
    }

    bool TonnetzProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
    {
        const auto out = layouts.getMainOutputChannelSet();
        return layouts.getMainInputChannelSet().isDisabled()
            && (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo());
    }

    Transport TonnetzProcessor::readTransport()
    {
        Transport t = lastTransport;
        t.sampleRate = getSampleRate() > 0.0 ? getSampleRate() : preparedSampleRate.load();
        t.playing = false;
        t.looping = false;
        t.hasBarStart = false;

        if (auto* ph = getPlayHead())
        {
            if (auto pos = ph->getPosition())
            {
                t.playing = pos->getIsPlaying();
                if (auto b = pos->getBpm(); b.hasValue() && std::isfinite (*b) && *b > 0.0)
                    t.bpm = *b;
                if (auto p = pos->getPpqPosition(); p.hasValue() && std::isfinite (*p))
                    t.ppq = *p;
                if (auto ts = pos->getTimeSignature(); ts.hasValue() && ts->numerator > 0 && ts->denominator > 0)
                {
                    t.numerator = ts->numerator;
                    t.denominator = ts->denominator;
                }
                if (auto loop = pos->getLoopPoints(); loop.hasValue() && std::isfinite (loop->ppqStart) && std::isfinite (loop->ppqEnd))
                {
                    t.looping = pos->getIsLooping() && loop->ppqEnd > loop->ppqStart;
                    t.loopStart = loop->ppqStart;
                    t.loopEnd = loop->ppqEnd;
                }
                if (auto bar = pos->getPpqPositionOfLastBarStart(); bar.hasValue() && std::isfinite (*bar))
                {
                    t.hasBarStart = true;
                    t.barStartPpq = *bar;
                }
            }
        }
        lastTransport = t;
        return t;
    }

    void TonnetzProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
    {
        run (buffer, midi, false);
    }

    void TonnetzProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
    {
        run (buffer, midi, true);
    }

    // The host's MIDI, with the rig's keys merged in as if the host sent them, goes through the
    // engine (LatticeEngine.h). SysEx and other long messages go straight through.
    void TonnetzProcessor::run (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, bool bypassed)
    {
        juce::ScopedNoDenormals noDenormals;
        buffer.clear();

        const int numSamples = buffer.getNumSamples();
        const Transport transport = readTransport();
        const bool enabled = ! bypassed && enableParam->load() > 0.5f;
        const bool rigOn = enabled && testRigParam->load() > 0.5f;

        rigEvents.clear();
        engineIn.clear();
        engineOut.clear();
        merged.clear();
        if (numSamples > 0 && resetRequested.exchange (false))
        {
            rig.releaseAll (0, rigEvents);
            rig.forgetPosition();
            engine.releaseAll (0, engineOut);
        }
        rig.generate (rigKeysForAudio.read(), rigOn, transport, numSamples, rigEvents);

        for (const auto meta : midi)
        {
            if (meta.numBytes >= 1 && meta.numBytes <= 3 && meta.data[0] != 0xf0)
            {
                MidiEvent e;
                e.sample = meta.samplePosition;
                e.size = (uint8_t) meta.numBytes;
                for (int i = 0; i < meta.numBytes; ++i)
                    e.data[i] = meta.data[i];
                engineIn.push (e);
            }
            else
            {
                merged.addEvent (meta.data, meta.numBytes, meta.samplePosition);
            }
        }
        for (const auto& e : rigEvents)
            engineIn.push (e);
        if (rigEvents.size() > 0)
            engineIn.sortBySample (true);

        // The settings, from the parameters (choices and ints are stored as their index / value).
        auto& es = engineSettings;
        es.enabled = enabled;
        es.gatherMs = juce::jlimit (0, maxGatherMs, (int) std::lround (gatherParam->load()));
        es.mix = (Mix) juce::jlimit (0, 2, (int) std::lround (mixParam->load()));
        es.sourceChannel = juce::jlimit (1, 16, (int) std::lround (sourceChannelParam->load())) - 1;
        es.genChannel = juce::jlimit (1, 16, (int) std::lround (genChannelParam->load())) - 1;
        es.voicing.reg = (Register) juce::jlimit (0, 1, (int) std::lround (registerParam->load()));
        es.voicing.bass = (BassMode) juce::jlimit (0, 1, (int) std::lround (bassParam->load()));
        es.voicing.octave = juce::jlimit (-2, 2, (int) std::lround (octaveParam->load()));
        es.word = slotWordsForAudio.read().words[(size_t) juce::jlimit (0, numSlots - 1, (int) std::lround (slotParam->load()))];

        engine.process (transport, engineIn.begin(), engineIn.size(), numSamples, es, engineOut);

        for (const auto& e : engineOut)
            merged.addEvent (e.data, e.size, e.sample);
        midi.clear();
        midi.addEvents (merged, 0, -1, 0);

        statPlaying.store (transport.playing, std::memory_order_relaxed);
        statHeld.store (rig.heldCount(), std::memory_order_relaxed);
        statNoteOns.store (rig.noteOnCount(), std::memory_order_relaxed);
        statNoteOffs.store (rig.noteOffCount(), std::memory_order_relaxed);
        statBlock.store (numSamples, std::memory_order_relaxed);
        if (numSamples > statMaxBlock.load (std::memory_order_relaxed))
            statMaxBlock.store (numSamples, std::memory_order_relaxed);
        statRate.store (transport.sampleRate, std::memory_order_relaxed);
        statSourceMask.store (engine.sourceMask(), std::memory_order_relaxed);
        statSourceLowest.store (engine.sourceLowest(), std::memory_order_relaxed);
        statGenMask.store (engine.generatedMask(), std::memory_order_relaxed);
        statGenLowest.store (engine.generatedLowest(), std::memory_order_relaxed);
        statSounding.store (engine.soundingNotes(), std::memory_order_relaxed);
    }

    //==========================================================================
    // Slot words. Message thread.

    int TonnetzProcessor::currentSlot() const noexcept
    {
        return juce::jlimit (0, numSlots - 1, (int) std::lround (slotParam->load()));
    }

    Word TonnetzProcessor::getSlotWord (int slot) const
    {
        const juce::ScopedLock sl (slotLock);
        return slotWords.words[(size_t) juce::jlimit (0, numSlots - 1, slot)];
    }

    void TonnetzProcessor::setSlotWord (int slot, const Word& word)
    {
        {
            const juce::ScopedLock sl (slotLock);
            slotWords.words[(size_t) juce::jlimit (0, numSlots - 1, slot)] = word;
        }
        publishSlotWords();
    }

    bool TonnetzProcessor::setSlotWordText (int slot, const juce::String& text)
    {
        Word w;
        const auto utf8 = text.toRawUTF8();
        if (! parseWord (utf8, std::strlen (utf8), w))
            return false;
        setSlotWord (slot, w);
        return true;
    }

    void TonnetzProcessor::publishSlotWords()
    {
        const juce::ScopedLock sl (slotLock);
        slotWordsForAudio.publish (slotWords);
    }

    //==========================================================================
    // The test rig's files. Message thread.

    juce::File TonnetzProcessor::defaultRigFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("TonnetzML").getChildFile ("TestRig");
    }

    void TonnetzProcessor::pollTestRig()
    {
        if (testRigParam->load() < 0.5f)
        {
            if (rigLoaded)                   // nothing stale to play when the rig is next on
            {
                rigLoaded = false;
                rigKeysForAudio.publish (RigKeys());
            }
            rigKeysStamp = {};               // read the file afresh then
            statusWritten = {};
            return;
        }

        const auto keysFile = rigFolder.getChildFile ("Keys.txt");
        const bool found = keysFile.existsAsFile();
        const FileStamp stamp { found ? keysFile.getLastModificationTime().toMilliseconds() : 0, found ? keysFile.getSize() : -1 };
        if (! rigLoaded || ! (stamp == rigKeysStamp) || found != rigKeysFound)
        {
            rigLoaded = true;
            rigKeysStamp = stamp;
            rigKeysFound = found;
            const auto text = found ? keysFile.loadFileAsString() : juce::String();
            const auto utf8 = text.toRawUTF8();
            const RigKeys keys = parseRigText (utf8, std::strlen (utf8));
            rigKeyCount = keys.count;
            rigRejected = keys.rejected;
            rigDropped = keys.dropped;
            rigKeysForAudio.publish (keys);
        }

        // Status.txt: what a check in Live can read back. Later phases add the key guess, the
        // word and the chord names (PLAN.md §7.3). Written when it changes, four times a second
        // at most.
        juce::String status;
        status << "rig: " << (enableParam->load() > 0.5f ? "on" : "on, but Enable is off") << "\n"
               << "keysFile: " << (found ? "found" : "missing") << "\n"
               << "keys: " << rigKeyCount << "\n"
               << "rejectedLines: " << rigRejected << "\n"
               << "droppedKeys: " << rigDropped << "\n"
               << "playing: " << (statPlaying.load() ? "yes" : "no") << "\n"
               << "noteOns: " << (juce::int64) statNoteOns.load() << "\n"
               << "noteOffs: " << (juce::int64) statNoteOffs.load() << "\n"
               << "held: " << statHeld.load() << "\n";
        // Phase 2: the slot, its word, and the chords (named in C major's spelling).
        const auto spelling = makeSpelling (0, Scale::Major);
        const auto chordText = [&spelling] (int mask, int lowest)
        {
            return mask == 0 ? juce::String ("-") : juce::String (nameChord ((unsigned) mask, lowest, spelling).text());
        };
        const int slot = currentSlot();
        status << "slot: " << slotName (slot) << "\n"
               << "word: " << printWord (getSlotWord (slot)).c_str() << "\n"
               << "source: " << chordText (statSourceMask.load(), statSourceLowest.load()) << "\n"
               << "generated: " << chordText (statGenMask.load(), statGenLowest.load()) << "\n"
               << "sounding: " << statSounding.load() << "\n"
               // What the host gives the plug-in: its sample rate, the last block's length and
               // the longest block since the host last prepared it (Live's buffer size).
               << "sampleRate: " << statRate.load() << "\n"
               << "block: " << statBlock.load() << "\n"
               << "maxBlock: " << statMaxBlock.load() << "\n";
        const auto now = juce::Time::getMillisecondCounter();
        if (status != statusWritten && (statusWritten.isEmpty() || now - statusWrittenAt >= 250))
        {
            if (! rigFolder.isDirectory())
                rigFolder.createDirectory();
            if (rigFolder.getChildFile ("Status.txt").replaceWithText (status, false, false, "\n"))
            {
                statusWritten = status;
                statusWrittenAt = now;
            }
        }
    }

    //==========================================================================

    // The state is the parameters (the APVTS tree) plus a SLOTS child with the eight words as text.
    void TonnetzProcessor::getStateInformation (juce::MemoryBlock& dest)
    {
        auto tree = state.copyState();
        juce::ValueTree slots ("SLOTS");
        {
            const juce::ScopedLock sl (slotLock);
            for (int i = 0; i < numSlots; ++i)
                slots.setProperty (juce::Identifier (slotName (i)), juce::String::fromUTF8 (printWord (slotWords.words[(size_t) i]).c_str()), nullptr);
        }
        tree.removeChild (tree.getChildWithName ("SLOTS"), nullptr);
        tree.appendChild (slots, nullptr);
        if (auto xml = tree.createXml())
            copyXmlToBinary (*xml, dest);
    }

    void TonnetzProcessor::setStateInformation (const void* data, int size)
    {
        auto xml = getXmlFromBinary (data, size);
        if (xml == nullptr || ! xml->hasTagName (state.state.getType()))
            return;
        auto tree = juce::ValueTree::fromXml (*xml);
        // A saved value that isn't a finite number (a damaged set) is dropped: the parameter
        // goes to its default.
        for (auto child : tree)
            if (child.hasType ("PARAM") && child.hasProperty ("value") && ! std::isfinite ((double) child.getProperty ("value")))
                child.removeProperty ("value", nullptr);

        // Slot words: a slot missing from the state, or one that doesn't parse, gets its
        // factory word (a set saved before Phase 2 has none).
        SlotWords words = factorySlotWords();
        const auto slots = tree.getChildWithName ("SLOTS");
        for (int i = 0; i < numSlots && slots.isValid(); ++i)
        {
            const auto text = slots.getProperty (juce::Identifier (slotName (i))).toString();
            Word w;
            const auto utf8 = text.toRawUTF8();
            if (slots.hasProperty (juce::Identifier (slotName (i))) && parseWord (utf8, std::strlen (utf8), w))
                words.words[(size_t) i] = w;
        }
        tree.removeChild (slots, nullptr);
        state.replaceState (tree);
        {
            const juce::ScopedLock sl (slotLock);
            slotWords = words;
        }
        publishSlotWords();
    }

    juce::AudioProcessorEditor* TonnetzProcessor::createEditor()
    {
        return new TonnetzEditor (*this);
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new tonnetz::TonnetzProcessor();
}
