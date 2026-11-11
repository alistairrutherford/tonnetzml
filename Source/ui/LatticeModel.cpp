#include "LatticeModel.h"
#include "../core/lattice/Cycles.h"
#include "../core/theory/RomanNumeral.h"

namespace tonnetz::ui
{
    namespace
    {
        void centreOf (const Embedding& e, double& a, double& b)
        {
            a = b = 0.0;
            if (e.size == 0)
                return;
            const Vec s = e.sum();
            a = (double) s.a / e.size;
            b = (double) s.b / e.size;
        }

        // The pitch class of the lowest note sounding in a velocity table, or -1.
        int lowestPc (const std::array<uint8_t, 128>& v)
        {
            for (int n = 0; n < 128; ++n)
                if (v[(size_t) n] != 0)
                    return n % 12;
            return -1;
        }

        const Word& factoryCycleWord (int index)
        {
            static const std::array<Word, 4> words = []
            {
                std::array<Word, 4> w {};
                for (size_t i = 0; i < w.size(); ++i)
                    parseWord (factoryCycles[i].word, w[i]);
                return w;
            }();
            return words[(size_t) juce::jlimit (0, 3, index)];
        }
    }

    juce::String LatticeModel::pretty (const std::string& text)
    {
        juce::String s;
        for (size_t i = 0; i < text.size(); ++i)
        {
            const char c = text[i];
            if (c == '#')
                s << juce::String::fromUTF8 ("\xe2\x99\xaf");
            else if (c == 'b' && i > 0 && (std::string ("ABCDEFG#b").find (text[i - 1]) != std::string::npos || std::isdigit ((unsigned char) (i + 1 < text.size() ? text[i + 1] : ' '))))
                s << juce::String::fromUTF8 ("\xe2\x99\xad");
            else
                s << juce::String::charToString ((juce::juce_wchar) (unsigned char) c);
        }
        return s;
    }

    juce::String LatticeModel::opLabel (int k)
    {
        static const char* labels[numOperatorKeys] = { "P", "L", "R", "N", "S", "H", "+5", "\xe2\x88\x92" "5", "+M3", "\xe2\x88\x92M3", "Undo", "Home" };
        return juce::String::fromUTF8 (labels[juce::jlimit (0, numOperatorKeys - 1, k)]);
    }

    Spelling LatticeModel::spellingNow() const
    {
        const SpellingMap m = key.known() ? spellingForKey (key.tonic, key.minor) : spellingForKey (0, false);
        return spellingFromFifths (m.index);
    }

    // A chord's root with no bass to go by: the cheapest reading (the namer's cost), a tie going
    // to `hint`, then to the lowest pitch class. chordRoot (mask, -1) would take the lowest
    // pitch class as the bass, which reads A C E as C6 (found in the Phase 4 Live check).
    int LatticeModel::rootInPosition (PcMask mask, int hint)
    {
        int best = -1, bestCost = 1 << 30;
        for (int r = 0; r < 12; ++r)
        {
            if (! hasPc (mask, r))
                continue;
            const int cost = naming::analyse (naming::relativeTo (mask, r)).cost;
            if (cost < bestCost || (cost == bestCost && r == hint))
            {
                bestCost = cost;
                best = r;
            }
        }
        return best;
    }

    juce::String LatticeModel::chordName (PcMask mask, int bassPc, int* root) const
    {
        if ((mask & 0xfffu) == 0)
            return {};
        const auto sp = spellingNow();
        const auto n = nameChord (mask, bassPc < 0 ? rootInPosition (mask, -1) : bassPc, sp);
        if (root != nullptr)
            *root = n.root;
        juce::String s = pretty (n.rootName) + pretty (n.quality);
        if (n.bass != n.root)
            s << "/" << pretty (n.bassName);
        return s;
    }

    juce::String LatticeModel::longChordName (PcMask mask) const
    {
        const int r = rootInPosition (mask, -1);
        if (r < 0)
            return {};
        const auto sp = spellingNow();
        const auto n = nameChord (mask, r, sp);
        if (n.quality.empty())
            return pretty (n.rootName) + " major";
        if (n.quality == "m")
            return pretty (n.rootName) + " minor";
        return chordName (mask, r);
    }

    bool LatticeModel::update (const Inputs& in)
    {
        const auto& st = in.status;
        mode = in.mode;
        inKey = in.inKey;
        keyMode = in.keyMode;
        sourceVelocity = st.sourceVelocity;
        generatedVelocity = st.generatedVelocity;
        key = {};
        if (st.keyTonic >= 0)
        {
            key.tonic = st.keyTonic;
            key.minor = st.keyMinor;
        }
        guess = KeyGuess::unpack (st.keyGuess);
        word = mode == (int) Mode::Mirror ? in.slotWord : st.current;
        wordOps = mode == (int) Mode::Mirror ? in.slotWord.size : st.currentOps;

        const Word& cycleWord = in.orbitCycle >= 4 ? in.slotWord : factoryCycleWord (in.orbitCycle);
        const bool present = (st.source.mask & 0xfffu) != 0;
        if (present != builtPresent || ! (st.source == builtSource) || ! (st.generated == builtGenerated) || ! (word == builtWord)
            || ! (key.tonic == builtKey.tonic && key.minor == builtKey.minor) || mode != builtMode || ! (cycleWord == builtCycle)
            || in.voicing != builtVoicing || st.generatedVelocity != builtGenVel)
        {
            builtPresent = present;
            builtSource = st.source;
            builtGenerated = st.generated;
            builtWord = word;
            builtKey = key;
            builtMode = mode;
            builtCycle = cycleWord;
            builtVoicing = in.voicing;
            builtGenVel = st.generatedVelocity;
            rebuildShapes (in);
            rebuildGhosts (in);
            rebuildCycle (in);
        }
        inKeyBits = generated.present ? opsInKey (generated.chord, key) : 0;
        for (auto& g : ghosts)
        {
            g.inKey = (inKeyBits >> g.key & 1u) != 0;
            g.faint = inKey && key.known() && ! g.inKey;
        }
        rebuildText();

        // What changed, as a string (cheap at 60 Hz, and exact).
        juce::String sig;
        sig << (int) st.source.mask << ":" << (int) st.generated.mask << ":" << sentence << ":" << keyText << ":" << mode << ":"
            << (int) inKey << ":" << (int) inKeyBits << ":" << (int) trail.size() << ":" << cycleAt << ":" << keyMode;
        for (int n = 0; n < 128; ++n)
            if (st.sourceVelocity[(size_t) n] != 0 || st.generatedVelocity[(size_t) n] != 0)
                sig << " " << n << "," << (int) st.sourceVelocity[(size_t) n] << "," << (int) st.generatedVelocity[(size_t) n];
        if (! trail.empty())
            sig << "|" << trail.back().a << "," << trail.back().b;
        const bool changed = sig != lastSignature;
        lastSignature = sig;
        return changed;
    }

    void LatticeModel::rebuildShapes (const Inputs& in)
    {
        const auto& st = in.status;
        source = {};
        generated = {};
        path.clear();
        if (! builtPresent)
            return;

        source.present = true;
        source.chord = st.source;
        source.shape = embed (st.source.mask, haveLastSource ? &lastSourceShape : nullptr);
        if (! haveLastSource || lastSourceShape.mask() != source.shape.mask())
        {
            lastSourceShape = source.shape;
            haveLastSource = true;
        }
        centreOf (source.shape, source.a, source.b);

        // W's path, op by op, each shape near the one before.
        WordRunner run (st.source);
        const Embedding* previous = &source.shape;
        for (int i = 0; i < word.size; ++i)
        {
            const Step s = run.step (word[i]);
            ChordView v;
            v.present = true;
            v.chord = run.chord();
            v.shape = embed (v.chord.mask, previous);
            centreOf (v.shape, v.a, v.b);
            path.push_back (v);
            previous = &path.back().shape;
            (void) s;
        }

        if ((st.generated.mask & 0xfffu) != 0)
        {
            generated.present = true;
            generated.chord = st.generated;
            if (! path.empty() && path.back().chord.mask == st.generated.mask)
                generated.shape = path.back().shape;
            else if (word.empty() && st.generated.mask == st.source.mask)
                generated.shape = source.shape;
            else
                generated.shape = embed (st.generated.mask, &source.shape);
            centreOf (generated.shape, generated.a, generated.b);

            if (trail.empty() || trail.back().mask != st.generated.mask
                || std::abs (trail.back().a - generated.a) > 1e-9 || std::abs (trail.back().b - generated.b) > 1e-9)
            {
                trail.push_back ({ generated.a, generated.b, st.generated.mask });
                if ((int) trail.size() > maxTrail)
                    trail.erase (trail.begin());
            }
        }
    }

    void LatticeModel::rebuildGhosts (const Inputs& in)
    {
        const auto& st = in.status;
        // Where each op's chip goes when the op has nowhere to show: its lattice direction.
        static const double nominal[numOperatorOps][2] = { { 0.33, -0.66 }, { 0.66, 0.33 }, { -0.33, 0.66 }, { -0.66, -0.66 }, { 0.0, -1.1 },
                                                           { -1.1, 0.33 }, { 1.0, 0.0 }, { -1.0, 0.0 }, { 0.0, 1.0 }, { 0.0, -1.0 } };
        // The generated voicing, to say how the voices would move.
        Voicing voices;
        for (int n = 0; n < 128; ++n)
            if (st.generatedVelocity[(size_t) n] != 0)
                voices.push (n, n);
        VoicerSettings vs = in.voicing;
        vs.bass = BassMode::Off;
        vs.octave = 0;

        for (int k = 0; k < numOperatorOps; ++k)
        {
            auto& g = ghosts[(size_t) k];
            g = {};
            g.key = k;
            g.label = opLabel (k);
            if (! generated.present)
                continue;
            Op op;
            operatorOp (k, op);
            const Step s = apply (op, generated.chord);
            g.available = s.available;
            if (! s.available)
            {
                g.a = generated.a + nominal[k][0];
                g.b = generated.b + nominal[k][1];
                g.description = g.label + juce::String::fromUTF8 (" \xe2\x80\x94 not available on this chord");
                continue;
            }
            g.result = s.chord;
            g.shape = embed (s.chord.mask, &generated.shape);
            centreOf (g.shape, g.a, g.b);

            // How the voices would move (as the voicer moves them: common tones stay).
            juce::String moves;
            if (voices.count > 0)
            {
                Word w;
                w.push (op);
                Voicing out;
                voiceWord (w, generated.chord, voices, vs, out);
                int moved = 0, lo = 1000, hi = 0;
                for (const auto& v : out)
                    if (v.owner < 128 && v.pitch != v.owner)
                    {
                        const int d = std::abs (v.pitch - v.owner);
                        ++moved;
                        lo = juce::jmin (lo, d);
                        hi = juce::jmax (hi, d);
                    }
                if (moved == 0)
                    moves = "no voice moves";
                else if (moved == 1)
                    moves = "1 voice moves " + juce::String (lo) + (lo == 1 ? " semitone" : " semitones");
                else if (lo == hi)
                    moves = juce::String (moved) + " voices move " + juce::String (lo) + (lo == 1 ? " semitone" : " semitones");
                else
                    moves = juce::String (moved) + " voices move " + juce::String (lo) + juce::String::fromUTF8 ("\xe2\x80\x93") + juce::String (hi) + " semitones";
            }
            g.description = g.label + juce::String::fromUTF8 (" \xe2\x86\x92 ") + longChordName (s.chord.mask) + (moves.isEmpty() ? juce::String() : ", " + moves);
        }

        // Chips that would sit on each other are pushed apart, outward from the chord.
        for (int pass = 0; pass < 6; ++pass)
            for (int i = 0; i < numOperatorOps; ++i)
                for (int j = 0; j < i; ++j)
                {
                    auto& p = ghosts[(size_t) i];
                    const auto& q = ghosts[(size_t) j];
                    const double dx = (p.a + p.b * 0.5) - (q.a + q.b * 0.5), dy = (p.b - q.b) * 0.866;
                    if (std::hypot (dx, dy) < 0.36)
                    {
                        double ox = (p.a + p.b * 0.5) - (generated.a + generated.b * 0.5), oy = (p.b - generated.b) * 0.866;
                        const double len = std::hypot (ox, oy);
                        if (len < 1e-6) { ox = 0.0; oy = -1.0; }
                        else { ox /= len; oy /= len; }
                        const double step = 0.22;
                        const double nb = p.b + oy * step / 0.866;
                        p.a = p.a + ox * step - (nb - p.b) * 0.5;
                        p.b = nb;
                    }
                }
    }

    void LatticeModel::rebuildCycle (const Inputs& in)
    {
        cycle.clear();
        cycleAt = -1;
        if (mode != (int) Mode::Orbit || ! source.present || builtCycle.empty())
            return;
        const CycleInfo info = cycleOf (builtCycle, source.chord, 64);
        if (info.steps <= 0)
            return;
        WordRunner run (source.chord);
        Embedding shape = source.shape;
        cycle.push_back ({ source.a, source.b, source.chord.mask });
        for (int i = 0; i < info.steps; ++i)
        {
            run.step (builtCycle[i % builtCycle.size]);
            shape = embed (run.chord().mask, &shape);
            TrailPoint p;
            centreOf (shape, p.a, p.b);
            p.mask = run.chord().mask;
            cycle.push_back (p);
        }
        const auto step = in.status.walk.orbitStep;
        cycleAt = (int) (step % (uint32_t) info.steps);
    }

    void LatticeModel::rebuildText()
    {
        // The key: "C major 91%" (the guess's fit while listening, "held" in Hold).
        keyText = {};
        if (key.known())
        {
            keyText = pretty (noteNameOfFifths (tonicFifthsIndex (key.tonic, key.minor)).c_str()) + (key.minor ? " minor" : " major");
            if (keyMode == (int) KeyMode::Listen && guess.has() && guess.tonic == key.tonic && guess.minor == key.minor)
                keyText << " " << guess.fit << "%";
        }

        // Names in the key (bass: the lowest note sounding).
        if (source.present)
        {
            source.name = chordName (source.chord.mask, lowestPc (sourceVelocity), &source.root);
            source.numeral = key.known() ? juce::String::fromUTF8 (romanNumeral (source.root, source.chord.mask, key.tonic, key.minor).c_str()) : juce::String();
        }
        if (generated.present)
        {
            // The generated chord is named in root position: its bass is the voicer's, not a choice.
            // (A tie between readings goes to the voicing's bass: A♭ C E♭ G over C is A♭maj7.)
            const int root = rootInPosition (generated.chord.mask, lowestPc (generatedVelocity));
            generated.name = chordName (generated.chord.mask, root, &generated.root);
            generated.numeral = key.known() ? juce::String::fromUTF8 (romanNumeral (generated.root, generated.chord.mask, key.tonic, key.minor).c_str()) : juce::String();
        }

        sentence = {};
        if (! source.present)
            return;
        const auto withNumeral = [] (const ChordView& c) { return c.numeral.isEmpty() ? c.name : c.name + " (" + c.numeral + ")"; };
        sentence = withNumeral (source);
        if (generated.present && (wordOps > 0 || generated.chord.mask != source.chord.mask))
        {
            juce::String w = juce::String::fromUTF8 (printWord (word, "\xc2\xb7").c_str());
            w = w.replace ("-", juce::String::fromUTF8 ("\xe2\x88\x92"));
            if (wordOps > word.size)
                w << juce::String::fromUTF8 ("\xe2\x80\xa6");
            sentence << juce::String::fromUTF8 (" \xe2\x94\x80\xe2\x94\x80") << w << juce::String::fromUTF8 ("\xe2\x94\x80\xe2\x94\x80\xe2\x96\xba ") << withNumeral (generated);
        }
    }
}
