// Listens to notes and guesses their key (ChordChimp's docs/KEY_DETECTION_PLAN.md K3): the engine feeds it
// the notes that pass through in Analyse mode. Plain C++, no JUCE, no allocation: fixed arrays,
// called on the audio thread.
//
//   evidence   a pitch-class histogram: each note adds an onset (0.25 s) and the time it's
//              held (up to 2 s), times a velocity weight; everything halves every 12 s of
//              listening time (while a note sounds, and for 2 s after the last one ends), so
//              silence forgets nothing
//   estimate   every 50 ms of listening time, KeyProfiles.h's estimateKey with Temperley's
//              profiles (they won the corpus in Tests/test_keys.cpp); too few notes or pitch
//              classes, or a poor fit, is no guess
//   hysteresis the first guess shows once it has been the best for 2 s; then the key shown
//              changes only when another beats its fit by 0.05 for 1.5 s, or beats it at all
//              for 4 s
//
// Times are in seconds and must not run backwards (an earlier time is taken as now).
//
// Copied unchanged from ChordMonkey (namespace tonnetz; PLAN.md §3.6). In TonnetzML the engine
// feeds it the source notes only (never generated notes or operator keys), in every mode, while
// `key` is Listen (LatticeEngine.h).
#pragma once

#include "../theory/KeyProfiles.h"
#include <array>
#include <cmath>
#include <cstdint>

namespace tonnetz
{
    enum class GuessState : uint8_t { None, Tentative, Confident };

    // What the editor shows (and the test rig writes), packed into 64 bits for a lock-free
    // hand-over to other threads.
    struct KeyGuess
    {
        int tonic = -1;                   // 0..11, -1 = none
        bool minor = false;
        GuessState state = GuessState::None;
        int fit = 0;                      // percent, 0..100
        int runnerUpTonic = -1;
        bool runnerUpMinor = false;
        int runnerUpFit = 0;
        int notes = 0;                    // note-ons heard in this analysis (to 65535)

        bool has() const noexcept { return tonic >= 0 && state != GuessState::None; }

        uint64_t pack() const noexcept
        {
            const auto t = [] (int v) { return (uint64_t) (v < 0 ? 15 : v & 15); };
            return t (tonic) | (uint64_t) (minor ? 1 : 0) << 4 | (uint64_t) ((int) state & 3) << 5
                 | (uint64_t) (clampFit (fit)) << 8 | t (runnerUpTonic) << 16 | (uint64_t) (runnerUpMinor ? 1 : 0) << 20
                 | (uint64_t) clampFit (runnerUpFit) << 24 | (uint64_t) (notes < 0 ? 0 : (notes > 65535 ? 65535 : notes)) << 32;
        }

        static KeyGuess unpack (uint64_t v) noexcept
        {
            const auto t = [] (uint64_t x) { return (int) (x & 15) > 11 ? -1 : (int) (x & 15); };
            KeyGuess g;
            g.tonic = t (v);
            g.minor = ((v >> 4) & 1) != 0;
            const int s = (int) ((v >> 5) & 3);
            g.state = s > 2 ? GuessState::None : (GuessState) s;
            g.fit = clampFit ((int) ((v >> 8) & 255));
            g.runnerUpTonic = t (v >> 16);
            g.runnerUpMinor = ((v >> 20) & 1) != 0;
            g.runnerUpFit = clampFit ((int) ((v >> 24) & 255));
            g.notes = (int) ((v >> 32) & 65535);
            return g;
        }

        bool operator== (const KeyGuess& o) const noexcept { return pack() == o.pack(); }
        bool operator!= (const KeyGuess& o) const noexcept { return ! (*this == o); }

    private:
        static int clampFit (int f) noexcept { return f < 0 ? 0 : (f > 100 ? 100 : f); }
    };

    class KeyListener
    {
    public:
        // The constants (K3), tuned on the corpus in Tests/test_keys.cpp.
        static constexpr double onsetSeconds = 0.25;      // an onset counts as this much holding
        static constexpr double maxHeldSeconds = 2.0;     // a note's holding counts up to this
        static constexpr double halfLife = 12.0;          // of listening time
        static constexpr double tailSeconds = 2.0;        // listening time after the last note ends
        static constexpr double estimateEvery = 0.05;
        static constexpr double minOnsets = 6.0;
        static constexpr int minPitchClasses = 3;
        static constexpr double minFit = 0.50, confidentFit = 0.70, confidentMargin = 0.04;
        static constexpr double switchMargin = 0.05, switchHold = 1.5, leadHold = 4.0;
        static constexpr double firstHold = 2.0;          // the first guess: the best for this long

        // The profiles the estimates use (KeyProfiles.h); the default is the corpus's choice.
        static constexpr KeyProfiles defaultProfiles = KeyProfiles::Temperley;

        explicit KeyListener (KeyProfiles set = defaultProfiles) noexcept : profiles (set) { reset(); }

        // Forgets everything: a new analysis.
        void reset() noexcept
        {
            weights.fill (0.0);
            onsets = 0.0;
            heldMask = { 0, 0 };
            credited.fill (0.0);
            velWeight.fill (0.0);
            heldCount = 0;
            tailEnd = -1.0;
            started = false;
            listened = 0.0;
            nextEstimate = estimateEvery;
            shown = {};
            challenger = {};
            marginTime = leadTime = 0.0;
            noteCount = 0;
            published = {};
        }

        void noteOn (int note, int velocity, double t) noexcept
        {
            if (note < 0 || note > 127)
                return;
            advance (t);
            const double v = 0.5 + 0.5 * (velocity < 0 ? 0 : (velocity > 127 ? 127 : velocity)) / 127.0;
            if (! isHeld (note))
                ++heldCount;
            setHeld (note, true);
            credited[(size_t) note] = 0.0;
            velWeight[(size_t) note] = v;
            weights[(size_t) (note % 12)] += onsetSeconds * v;
            onsets += 1.0;
            noteCount = noteCount < 65535 ? noteCount + 1 : noteCount;
        }

        void noteOff (int note, double t) noexcept
        {
            if (note < 0 || note > 127 || ! isHeld (note))
                return;
            advance (t);
            setHeld (note, false);
            --heldCount;
            if (heldCount == 0)
                tailEnd = now + tailSeconds;
        }

        // Every held note ends now.
        void allUp (double t) noexcept
        {
            advance (t);
            if (heldCount > 0)
                tailEnd = now + tailSeconds;
            heldMask = { 0, 0 };
            heldCount = 0;
        }

        // Time moves on to `t`: held notes count, the evidence decays and the estimates run, for
        // the listening time in between.
        void advance (double t) noexcept
        {
            if (! started)
            {
                if (std::isfinite (t))
                {
                    now = t;
                    started = true;
                }
                return;
            }
            if (! (t > now) || ! std::isfinite (t))
                return;
            for (int guard = 0; now < t && guard < maxStepsPerAdvance; ++guard)
            {
                if (heldCount == 0 && now >= tailEnd)
                {
                    now = t;                  // silence: no listening time passes
                    return;
                }
                double end = t;
                if (heldCount == 0 && tailEnd < end)
                    end = tailEnd;
                const double toEstimate = nextEstimate - listened;
                if (now + toEstimate < end)
                    end = now + toEstimate;
                const double dt = end - now;
                if (dt > 0.0)
                    step (dt);
                now = end;
                listened += dt > 0.0 ? dt : 0.0;
                if (listened >= nextEstimate - 1.0e-9)
                {
                    estimate();
                    nextEstimate += estimateEvery;
                }
            }
            if (now < t && (heldCount > 0 || now < tailEnd))
            {
                // A jump too long to step through: the rest in one step, without estimates.
                const double end = heldCount > 0 ? t : (tailEnd < t ? tailEnd : t);
                step (end - now);
                listened += end - now;
                nextEstimate = listened + estimateEvery;
            }
            now = t;
        }

        KeyGuess guess() const noexcept { return published; }
        const PitchWeights& getWeights() const noexcept { return weights; }
        double listeningTime() const noexcept { return listened; }

    private:
        static constexpr int maxStepsPerAdvance = 256;

        bool isHeld (int note) const noexcept { return ((heldMask[(size_t) (note >> 6)] >> (note & 63)) & 1u) != 0; }
        void setHeld (int note, bool on) noexcept
        {
            const uint64_t bit = (uint64_t) 1 << (note & 63);
            auto& word = heldMask[(size_t) (note >> 6)];
            word = on ? (word | bit) : (word & ~bit);
        }

        void step (double dt) noexcept
        {
            for (size_t w = 0; w < 2; ++w)
                for (uint64_t bits = heldMask[w]; bits != 0; bits &= bits - 1)
                {
                    const int note = (int) (w * 64) + __builtin_ctzll (bits);
                    auto& c = credited[(size_t) note];
                    const double add = c + dt <= maxHeldSeconds ? dt : (maxHeldSeconds > c ? maxHeldSeconds - c : 0.0);
                    c += add;
                    weights[(size_t) (note % 12)] += add * velWeight[(size_t) note];
                }
            const double k = std::exp2 (-dt / halfLife);
            for (auto& x : weights)
                x = x * k < 1.0e-12 ? 0.0 : x * k;
            onsets *= k;
        }

        GuessState stateOf (double fit, double margin) const noexcept
        {
            if (fit < minFit)
                return GuessState::None;
            return fit >= confidentFit && margin >= confidentMargin ? GuessState::Confident : GuessState::Tentative;
        }

        void estimate() noexcept
        {
            double hi = 0.0;
            for (const double x : weights)
                hi = x > hi ? x : hi;
            int distinct = 0;
            for (const double x : weights)
                distinct += x > 0.05 * hi && hi > 0.0 ? 1 : 0;

            const KeyEstimate e = estimateKey (weights, profiles);
            if (! e.valid || onsets < minOnsets || distinct < minPitchClasses)
            {
                published = {};
                published.notes = noteCount;
                marginTime = leadTime = 0.0;
                return;
            }

            // The key shown: the best, once it's a guess at all; then another only once it has led
            // by switchMargin for switchHold, or led at all for leadHold.
            if (shown.tonic < 0)
            {
                if (! challenger.sameKey (e.best))
                    leadTime = 0.0;
                challenger = e.best;
                leadTime += estimateEvery;
                if (leadTime >= firstHold - 1.0e-9 && stateOf (e.best.fit, e.best.fit - e.runnerUp.fit) != GuessState::None)
                {
                    shown = e.best;
                    challenger = {};
                    marginTime = leadTime = 0.0;
                }
            }
            else if (! shown.sameKey (e.best))
            {
                const double lead = e.best.fit - keyFit (weights, shown.tonic, shown.minor, profiles);
                if (! challenger.sameKey (e.best))
                    marginTime = leadTime = 0.0;
                challenger = e.best;
                leadTime += estimateEvery;
                marginTime = lead >= switchMargin ? marginTime + estimateEvery : 0.0;
                if (marginTime >= switchHold - 1.0e-9 || leadTime >= leadHold - 1.0e-9)
                {
                    shown = e.best;
                    challenger = {};
                    marginTime = leadTime = 0.0;
                }
            }
            else
            {
                challenger = {};
                marginTime = leadTime = 0.0;
            }

            KeyGuess g;
            g.notes = noteCount;
            if (shown.tonic >= 0)
            {
                const double fit = keyFit (weights, shown.tonic, shown.minor, profiles);
                const KeyCandidate& rival = shown.sameKey (e.best) ? e.runnerUp : e.best;
                g.state = stateOf (fit, fit - rival.fit);
                if (g.state != GuessState::None)
                {
                    g.tonic = shown.tonic;
                    g.minor = shown.minor;
                    g.fit = (int) std::lround (100.0 * fit);
                    g.runnerUpTonic = rival.tonic;
                    g.runnerUpMinor = rival.minor;
                    g.runnerUpFit = (int) std::lround (100.0 * (rival.fit > 0.0 ? rival.fit : 0.0));
                }
            }
            published = g;
        }

        KeyProfiles profiles;
        PitchWeights weights {};
        double onsets = 0.0;
        std::array<uint64_t, 2> heldMask {};
        std::array<double, 128> credited {}, velWeight {};
        int heldCount = 0;
        double now = 0.0, tailEnd = -1.0, listened = 0.0, nextEstimate = estimateEvery;
        bool started = false;
        KeyCandidate shown, challenger;
        double marginTime = 0.0, leadTime = 0.0;
        int noteCount = 0;
        KeyGuess published;
    };
}
