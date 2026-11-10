#include "TonnetzProcessor.h"
#include "../ui/TonnetzEditor.h"
#include <cmath>

namespace tonnetz
{
    juce::AudioProcessorValueTreeState::ParameterLayout params::createLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { enable, 1 }, "Enable", true));
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { testRig, 1 }, "Test rig", false));
        return layout;
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
        merged.ensureSize (65536);
        startTimerHz (20);
    }

    TonnetzProcessor::~TonnetzProcessor()
    {
        stopTimer();
    }

    void TonnetzProcessor::prepareToPlay (double sampleRate, int)
    {
        preparedSampleRate.store (sampleRate);
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

    // Phase 0: the host's MIDI goes through untouched (`midi` is left as it is). The rig's keys,
    // if any fall in this block, are merged in by sample.
    void TonnetzProcessor::run (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, bool bypassed)
    {
        juce::ScopedNoDenormals noDenormals;
        buffer.clear();

        const int numSamples = buffer.getNumSamples();
        const Transport transport = readTransport();
        const bool rigOn = ! bypassed && enableParam->load() > 0.5f && testRigParam->load() > 0.5f;

        rigEvents.clear();
        if (numSamples > 0 && resetRequested.exchange (false))
        {
            rig.releaseAll (0, rigEvents);
            rig.forgetPosition();
        }
        rig.generate (rigKeysForAudio.read(), rigOn, transport, numSamples, rigEvents);

        if (rigEvents.size() > 0)
        {
            merged.clear();
            merged.addEvents (midi, 0, -1, 0);
            for (const auto& e : rigEvents)
                merged.addEvent (e.data, e.size, e.sample);
            midi.swapWith (merged);
        }

        statPlaying.store (transport.playing, std::memory_order_relaxed);
        statHeld.store (rig.heldCount(), std::memory_order_relaxed);
        statNoteOns.store (rig.noteOnCount(), std::memory_order_relaxed);
        statNoteOffs.store (rig.noteOffCount(), std::memory_order_relaxed);
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

    void TonnetzProcessor::getStateInformation (juce::MemoryBlock& dest)
    {
        if (auto xml = state.copyState().createXml())
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
        state.replaceState (tree);
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
