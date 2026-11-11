// The offline render of a Live check (PLAN.md §7.4): plays a test rig file (Keys.txt, §7.3)
// through the same path as the plug-in (RigPlayer → LatticeEngine, block by block, from ppq 0
// with the transport running) and prints the notes that come out as JSON, in beats, for a
// comparison with what Live recorded.
//
//   render_scenario [options] Keys.txt
//
//   --slot A..H          the slot's factory word (default A: P)
//   --word TEXT          any word instead (e.g. "PL", "P L R")
//   --bpm N              tempo (default 120)
//   --rate N             sample rate (default 48000)
//   --block N            block size (default 512)
//   --gather MS          gather window, 0..80 (default 20, the parameter's default)
//   --mix generated|both|source        (default generated)
//   --source-channel N   1..16 (default 1)
//   --gen-channel N      1..16 (default 2)
//   --register stay|follow             (default stay)
//   --bass off|root      (default off)
//   --octave N           -2..2 (default 0)
//   --until BEATS        how far to play (default: one beat after the last key ends)
//
// Output: {"word": …, "bpm": …, "rate": …, "block": …, "notes": [{"pitch", "start", "length",
// "velocity", "channel"}, …]}, notes by start, then pitch; beats from ppq 0, channels 1..16.
#include "core/engine/LatticeEngine.h"
#include "core/engine/TestRig.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace tonnetz;

namespace
{
    int usage (const char* why)
    {
        std::cerr << "render_scenario: " << why << "\n(see the top of Tests/render_scenario.cpp for the options)\n";
        return 2;
    }

    struct Note
    {
        int pitch, velocity, channel;
        int64_t start, end;
    };
}

int main (int argc, char* argv[])
{
    EngineSettings s;
    std::string wordText = factorySlotTexts[0], keysPath;
    double bpm = 120.0, rate = 48000.0, until = -1.0;
    int block = 512;

    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        const auto next = [&] () -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
        if (a == "--slot")
        {
            const auto v = next();
            if (v.size() != 1 || v[0] < 'A' || v[0] > 'H') return usage ("--slot takes A..H");
            wordText = factorySlotTexts[v[0] - 'A'];
        }
        else if (a == "--word") wordText = next();
        else if (a == "--bpm") bpm = std::atof (next().c_str());
        else if (a == "--rate") rate = std::atof (next().c_str());
        else if (a == "--block") block = std::atoi (next().c_str());
        else if (a == "--gather") s.gatherMs = std::atoi (next().c_str());
        else if (a == "--mix")
        {
            const auto v = next();
            s.mix = v == "both" ? Mix::SourceAndGenerated : v == "source" ? Mix::SourceOnly : Mix::Generated;
        }
        else if (a == "--source-channel") s.sourceChannel = std::atoi (next().c_str()) - 1;
        else if (a == "--gen-channel") s.genChannel = std::atoi (next().c_str()) - 1;
        else if (a == "--register") s.voicing.reg = next() == "follow" ? Register::Follow : Register::Stay;
        else if (a == "--bass") s.voicing.bass = next() == "root" ? BassMode::RootBelow : BassMode::Off;
        else if (a == "--octave") s.voicing.octave = std::atoi (next().c_str());
        else if (a == "--until") until = std::atof (next().c_str());
        else if (! a.empty() && a[0] == '-') return usage (("unknown option " + a).c_str());
        else keysPath = a;
    }
    if (keysPath.empty()) return usage ("no Keys.txt");
    if (! (bpm > 0.0) || ! (rate > 0.0) || block < 1) return usage ("bad --bpm, --rate or --block");
    int errorAt = -1;
    if (! parseWord (wordText.c_str(), s.word, &errorAt)) return usage (("not a word: " + wordText).c_str());
    if (s.sourceChannel < 0 || s.sourceChannel > 15 || s.genChannel < 0 || s.genChannel > 15) return usage ("channels are 1..16");

    std::ifstream file (keysPath, std::ios::binary);
    if (! file) return usage (("can't read " + keysPath).c_str());
    std::stringstream text;
    text << file.rdbuf();
    const std::string t = text.str();
    const auto keys = std::make_unique<RigKeys> (parseRigText (t.data(), t.size()));
    if (until < 0.0)
    {
        until = 0.0;
        for (int i = 0; i < keys->count; ++i)
            until = std::max (until, keys->keys[(size_t) i].to + 1.0);
    }

    // The plug-in's block loop (TonnetzProcessor::run): rig keys in, engine out.
    const auto engine = std::make_unique<LatticeEngine>();
    const auto rig = std::make_unique<RigPlayer>();
    auto rigEvents = std::make_unique<BlockEvents>(), out = std::make_unique<BlockEvents>();
    Transport tr;
    tr.sampleRate = rate;
    tr.bpm = bpm;
    tr.playing = true;
    const double spq = rate * 60.0 / bpm;
    const auto end = (int64_t) std::ceil (until * spq);
    std::map<std::pair<int, int>, std::deque<Note>> open;
    std::vector<Note> notes;
    for (int64_t pos = 0; pos < end + block; pos += block)
    {
        const bool stopping = pos >= end;      // one stopped block at the end ends everything
        tr.playing = ! stopping;
        tr.ppq = (double) pos / spq;
        rigEvents->clear();
        out->clear();
        rig->generate (*keys, true, tr, block, *rigEvents);
        engine->process (tr, rigEvents->begin(), rigEvents->size(), block, s, *out);
        for (const auto& e : *out)
        {
            const auto k = std::make_pair (e.channel(), e.note());
            if (e.isNoteOn())
                open[k].push_back ({ e.note(), e.velocity(), e.channel() + 1, pos + e.sample, -1 });
            else if (e.isNoteOff() && ! open[k].empty())
            {
                Note n = open[k].front();
                open[k].pop_front();
                n.end = pos + e.sample;
                notes.push_back (n);
            }
        }
        if (stopping)
            break;
    }

    std::sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b) { return a.start != b.start ? a.start < b.start : a.pitch < b.pitch; });
    const auto beats = [spq] (int64_t samples)
    {
        char buf[32];
        std::snprintf (buf, sizeof (buf), "%.6f", (double) samples / spq);
        return std::string (buf);
    };
    std::cout << "{\"word\": \"" << printWord (s.word).c_str() << "\", \"bpm\": " << bpm << ", \"rate\": " << rate
              << ", \"block\": " << block << ", \"gatherMs\": " << s.gatherMs << ", \"notes\": [";
    for (size_t i = 0; i < notes.size(); ++i)
        std::cout << (i ? ",\n  " : "\n  ") << "{\"pitch\": " << notes[i].pitch << ", \"start\": " << beats (notes[i].start)
                  << ", \"length\": " << beats (notes[i].end - notes[i].start) << ", \"velocity\": " << notes[i].velocity
                  << ", \"channel\": " << notes[i].channel << "}";
    std::cout << "\n]}\n";
    return 0;
}
