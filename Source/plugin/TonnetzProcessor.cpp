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

        // Phase 3.
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { mode, 1 }, "Mode",
                                                                   juce::StringArray { "Mirror", "Walk", "Orbit", "Explore" }, 0));
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { latch, 1 }, "Latch", false));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { walkInput, 1 }, "Walk input",
                                                                   juce::StringArray { "Relative", "Reset" }, 0));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { orbitCycle, 1 }, "Orbit cycle",
                                                                   juce::StringArray { "PL", "PR", "LR", "PLR", "Slot" }, 0));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { orbitRate, 1 }, "Orbit rate",
                                                                   juce::StringArray { orbitRateNames[0], orbitRateNames[1], orbitRateNames[2],
                                                                                       orbitRateNames[3], orbitRateNames[4] }, defaultOrbitRate));
        // Keys are named as Live names them (C3 = 60): the zone's default C0 is MIDI 24.
        layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { opZoneStart, 1 }, "Operator keys from", 0, 116, 24,
                                                                juce::AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int)
                                                                {
                                                                    static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
                                                                    return juce::String (names[v % 12]) + juce::String (v / 12 - 2);
                                                                })));
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { opZoneOn, 1 }, "Operator keys", true));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { key, 1 }, "Key",
                                                                   juce::StringArray { "Off", "Listen", "Hold" }, 1));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { keyTonic, 1 }, "Key tonic",
                                                                   juce::StringArray { "C", "C#/Db", "D", "Eb", "E", "F", "F#/Gb", "G", "Ab", "A", "Bb", "B" }, 0));
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { keyMinor, 1 }, "Key minor", false));
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { inKey, 1 }, "In key", false));
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
        modeParam = state.getRawParameterValue (params::mode);
        latchParam = state.getRawParameterValue (params::latch);
        walkInputParam = state.getRawParameterValue (params::walkInput);
        orbitCycleParam = state.getRawParameterValue (params::orbitCycle);
        orbitRateParam = state.getRawParameterValue (params::orbitRate);
        opZoneStartParam = state.getRawParameterValue (params::opZoneStart);
        opZoneOnParam = state.getRawParameterValue (params::opZoneOn);
        keyParam = state.getRawParameterValue (params::key);
        keyTonicParam = state.getRawParameterValue (params::keyTonic);
        keyMinorParam = state.getRawParameterValue (params::keyMinor);
        inKeyParam = state.getRawParameterValue (params::inKey);
        lastKeyMode = (int) std::lround (keyParam->load());
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
        // A restored W and latched chord (setStateInformation).
        if (const auto& restore = walkForAudio.read(); restore.seq != restoredSeq)
        {
            restoredSeq = restore.seq;
            engine.restore (restore.state, engineOut);
            walkTaken.store (restore.seq);
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
        es.mode = (Mode) juce::jlimit (0, 3, (int) std::lround (modeParam->load()));
        es.latch = latchParam->load() > 0.5f;
        es.walkInput = (WalkInput) juce::jlimit (0, 1, (int) std::lround (walkInputParam->load()));
        es.orbitCycle = (OrbitCycle) juce::jlimit (0, 4, (int) std::lround (orbitCycleParam->load()));
        es.orbitRate = juce::jlimit (0, numOrbitRates - 1, (int) std::lround (orbitRateParam->load()));
        es.zone.start = juce::jlimit (0, 116, (int) std::lround (opZoneStartParam->load()));
        es.zone.on = opZoneOnParam->load() > 0.5f;
        es.key = (KeyMode) juce::jlimit (0, 2, (int) std::lround (keyParam->load()));
        es.keyTonic = juce::jlimit (0, 11, (int) std::lround (keyTonicParam->load()));
        es.keyMinor = keyMinorParam->load() > 0.5f;
        es.inKey = inKeyParam->load() > 0.5f;

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

        status.walk = engine.walkState();
        status.current = engine.currentWord (status.currentOps);
        status.keyGuess = engine.keyGuess().pack();
        const auto k = engine.currentKey();
        status.keyTonic = k.known() ? k.tonic : -1;
        status.keyMinor = k.minor;
        statusForMessage.publish (status);
    }

    //==========================================================================
    // The engine's state. Any thread but the audio thread (locked).

    EngineStatus TonnetzProcessor::getEngineStatus() const
    {
        const juce::ScopedLock sl (statusLock);
        lastStatus = statusForMessage.read();
        return lastStatus;
    }

    WalkState TonnetzProcessor::getWalkState() const
    {
        const juce::ScopedLock sl (statusLock);
        if (pendingWalk.seq != walkTaken.load())
            return pendingWalk.state;                  // not taken yet (or the audio thread isn't running)
        lastStatus = statusForMessage.read();
        return lastStatus.walk;
    }

    void TonnetzProcessor::setWalkState (const WalkState& w)
    {
        const juce::ScopedLock sl (statusLock);
        ++pendingWalk.seq;
        pendingWalk.state = w;
        walkForAudio.publish (pendingWalk);
    }

    // Listen → Hold: the guess at that moment (the engine freezes it) goes into keyTonic and
    // keyMinor, where it can also be changed by hand.
    void TonnetzProcessor::followKeyMode()
    {
        const int k = (int) std::lround (keyParam->load());
        if (lastKeyMode == (int) KeyMode::Listen && k == (int) KeyMode::Hold)
        {
            const auto g = KeyGuess::unpack (getEngineStatus().keyGuess);
            if (g.has())
            {
                for (const auto& [id, value] : { std::make_pair (params::keyTonic, (float) g.tonic), std::make_pair (params::keyMinor, g.minor ? 1.0f : 0.0f) })
                {
                    auto* param = state.getParameter (id);
                    param->beginChangeGesture();
                    param->setValueNotifyingHost (param->convertTo0to1 (value));
                    param->endChangeGesture();
                }
            }
        }
        lastKeyMode = k;
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
        const auto engineStatus = getEngineStatus();
        status << "slot: " << slotName (slot) << "\n"
               << "word: " << printWord (engineStatus.current).c_str() << "\n"
               << "source: " << chordText (statSourceMask.load(), statSourceLowest.load()) << "\n"
               << "generated: " << chordText (statGenMask.load(), statGenLowest.load()) << "\n"
               << "sounding: " << statSounding.load() << "\n"
               // What the host gives the plug-in: its sample rate, the last block's length and
               // the longest block since the host last prepared it (Live's buffer size).
               << "sampleRate: " << statRate.load() << "\n"
               << "block: " << statBlock.load() << "\n"
               << "maxBlock: " << statMaxBlock.load() << "\n";
        // Phase 3: the mode and W, the latched chord, the key.
        const auto keyName = [] (int tonic, bool minor)
        {
            return tonic < 0 ? juce::String ("-") : juce::String (guessKeyName (tonic, minor)) + (minor ? " minor" : " major");
        };
        unsigned latchedMask = 0;
        int latchedLowest = -1;
        for (int n = 0; n < 128; ++n)
            if (engineStatus.walk.latched[(size_t) n] != 0)
            {
                latchedMask |= 1u << (n % 12);
                latchedLowest = latchedLowest < 0 ? n : latchedLowest;
            }
        const auto guess = KeyGuess::unpack (engineStatus.keyGuess);
        const char* states[] = { "none", "tentative", "confident" };
        const int mode = juce::jlimit (0, 3, (int) std::lround (modeParam->load()));
        status << "mode: " << modeNames[mode] << "\n"
               << "wordOps: " << engineStatus.currentOps << "\n"
               << "walk: " << printWord (engineStatus.walk.word).c_str() << "\n"
               << "orbitStep: " << (juce::int64) engineStatus.walk.orbitStep << "\n"
               << "latch: " << (latchParam->load() > 0.5f ? "on" : "off") << "\n"
               << "latched: " << chordText ((int) latchedMask, latchedLowest) << "\n"
               << "keyMode: " << keyModeNames[juce::jlimit (0, 2, (int) std::lround (keyParam->load()))] << "\n"
               << "key: " << (guess.has() ? keyName (guess.tonic, guess.minor) : juce::String ("-")) << "\n"
               << "keyFit: " << guess.fit << "\n"
               << "keyState: " << states[(int) guess.state] << "\n"
               << "keyNotes: " << guess.notes << "\n"
               << "keyInForce: " << keyName (engineStatus.keyTonic, engineStatus.keyMinor) << "\n";
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

    // The state is the parameters (the APVTS tree) plus a SLOTS child with the eight words as text
    // and a WALK child: Walk's word (text), Orbit's step and the latched chord ("60:100 64:100").
    static juce::ValueTree walkTree (const WalkState& w)
    {
        juce::ValueTree walk ("WALK");
        walk.setProperty ("word", juce::String::fromUTF8 (printWord (w.word).c_str()), nullptr);
        walk.setProperty ("orbitStep", (juce::int64) w.orbitStep, nullptr);
        juce::StringArray latched;
        for (int n = 0; n < 128; ++n)
            if (w.latched[(size_t) n] != 0)
                latched.add (juce::String (n) + ":" + juce::String ((int) w.latched[(size_t) n]));
        walk.setProperty ("latched", latched.joinIntoString (" "), nullptr);
        return walk;
    }

    // A WALK child (missing or damaged parts are left empty).
    static WalkState walkFromTree (const juce::ValueTree& walk)
    {
        WalkState w;
        if (! walk.isValid())
            return w;
        const auto text = walk.getProperty ("word").toString();
        const auto utf8 = text.toRawUTF8();
        if (! parseWord (utf8, std::strlen (utf8), w.word))
            w.word.clear();
        const auto step = (juce::int64) walk.getProperty ("orbitStep", 0);
        w.orbitStep = step > 0 && step < (juce::int64) 0xffffffff ? (uint32_t) step : 0u;
        for (const auto& item : juce::StringArray::fromTokens (walk.getProperty ("latched").toString(), " ", ""))
        {
            const int n = item.upToFirstOccurrenceOf (":", false, false).getIntValue();
            const int v = item.fromFirstOccurrenceOf (":", false, false).getIntValue();
            if (item.containsChar (':') && n >= 0 && n < 128 && v > 0 && v < 128)
                w.latched[(size_t) n] = (uint8_t) v;
        }
        return w;
    }

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
        tree.removeChild (tree.getChildWithName ("WALK"), nullptr);
        tree.appendChild (walkTree (getWalkState()), nullptr);
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
        // W and the latched chord: a state without them (before Phase 3) has neither.
        const auto walk = tree.getChildWithName ("WALK");
        const WalkState walkState = walkFromTree (walk);
        tree.removeChild (walk, nullptr);
        state.replaceState (tree);
        lastKeyMode = (int) std::lround (keyParam->load());     // a set saved in Hold keeps its key
        setWalkState (walkState);
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
