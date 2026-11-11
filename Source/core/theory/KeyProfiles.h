// Key estimation from a pitch-class histogram (ChordChimp's docs/KEY_DETECTION_PLAN.md K3): the
// Krumhansl-Schmuckler method with the Krumhansl-Kessler profiles, over the 24 major and minor
// keys, and how a guess maps onto ChordMonkey's 13 scales. Plain C++, no JUCE, no allocation.
//
//   profiles     Krumhansl-Kessler (1982) and Temperley's Kostka-Payne (2007), as music21 has
//                them; Tests/test_keys.cpp runs the corpus with both (the plan's bake-off)
//   keyFit       the Pearson correlation of a histogram with one key's profile (-1..1)
//   estimateKey  the best key and the runner-up
//   scaleFamily  a scale's tonic triad: major, minor or neither (Locrian)
//   scaleForGuess  the Scale a guess sets: the current one if it's the guess's family
// Copied unchanged from ChordMonkey (namespace tonnetz; PLAN.md §3.6). TonnetzML's Scales.h has only
// Major and Natural Minor, which is all scaleForGuess ever returns.
#pragma once

#include "Scales.h"
#include <array>
#include <cmath>
#include <cstdint>

namespace tonnetz
{
    using PitchWeights = std::array<double, 12>;

    enum class KeyProfiles : uint8_t { KrumhanslKessler, Temperley };

    namespace keyprofiles
    {
        // From the tonic up by semitones.
        constexpr double kkMajor[12] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
        constexpr double kkMinor[12] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };
        constexpr double tMajor[12] = { 0.748, 0.060, 0.488, 0.082, 0.670, 0.460, 0.096, 0.715, 0.104, 0.366, 0.057, 0.400 };
        constexpr double tMinor[12] = { 0.712, 0.084, 0.474, 0.618, 0.049, 0.460, 0.105, 0.747, 0.404, 0.067, 0.133, 0.330 };

        inline const double* profile (bool isMinor, KeyProfiles set) noexcept
        {
            if (set == KeyProfiles::Temperley)
                return isMinor ? tMinor : tMajor;
            return isMinor ? kkMinor : kkMajor;
        }
    }

    struct KeyCandidate
    {
        int tonic = -1;                   // 0..11, -1 = none
        bool minor = false;
        double fit = 0.0;                 // correlation, -1..1

        bool sameKey (const KeyCandidate& o) const noexcept { return tonic == o.tonic && minor == o.minor; }
    };

    struct KeyEstimate
    {
        KeyCandidate best, runnerUp;
        bool valid = false;               // false for a flat histogram (nothing, or every pitch class alike)
    };

    // The correlation of `w` with the profile of `tonic` major or minor; 0 if `w` is flat.
    inline double keyFit (const PitchWeights& w, int tonic, bool isMinor, KeyProfiles set) noexcept
    {
        const double* p = keyprofiles::profile (isMinor, set);
        double wMean = 0.0, pMean = 0.0;
        for (int i = 0; i < 12; ++i)
        {
            wMean += w[(size_t) i];
            pMean += p[i];
        }
        wMean /= 12.0;
        pMean /= 12.0;
        double cross = 0.0, wVar = 0.0, pVar = 0.0;
        for (int pc = 0; pc < 12; ++pc)
        {
            const double a = w[(size_t) pc] - wMean;
            const double b = p[((pc - tonic) % 12 + 12) % 12] - pMean;
            cross += a * b;
            wVar += a * a;
            pVar += b * b;
        }
        if (! (wVar > 1.0e-18) || ! (pVar > 0.0))
            return 0.0;
        return cross / std::sqrt (wVar * pVar);
    }

    // Every key scored; ties go to major before minor, then the lower tonic.
    inline KeyEstimate estimateKey (const PitchWeights& w, KeyProfiles set) noexcept
    {
        KeyEstimate e;
        double total = 0.0, lo = w[0], hi = w[0];
        for (const double x : w)
        {
            if (! std::isfinite (x))
                return e;
            total += x;
            lo = x < lo ? x : lo;
            hi = x > hi ? x : hi;
        }
        if (! (total > 0.0) || hi - lo <= 1.0e-12 * hi)
            return e;

        for (int m = 0; m < 2; ++m)
            for (int tonic = 0; tonic < 12; ++tonic)
            {
                const KeyCandidate c { tonic, m == 1, keyFit (w, tonic, m == 1, set) };
                if (e.best.tonic < 0 || c.fit > e.best.fit)
                {
                    e.runnerUp = e.best;
                    e.best = c;
                }
                else if (e.runnerUp.tonic < 0 || c.fit > e.runnerUp.fit)
                {
                    e.runnerUp = c;
                }
            }
        e.valid = true;
        return e;
    }

    enum class ScaleFamily : uint8_t { Major, Minor, Neither };

    // By the scale's tonic triad: a major 3rd and perfect 5th, a minor 3rd and perfect 5th, or
    // neither (Locrian's diminished 5th).
    inline ScaleFamily scaleFamily (Scale s) noexcept
    {
        const auto& st = scaleInfo (s).semitones;
        if (st[4] != 7)
            return ScaleFamily::Neither;
        return st[2] == 4 ? ScaleFamily::Major : (st[2] == 3 ? ScaleFamily::Minor : ScaleFamily::Neither);
    }

    // The Scale a guess sets (K3.5): `current` if it's the guess's family, else Major or
    // Natural Minor.
    inline Scale scaleForGuess (Scale current, bool guessIsMinor) noexcept
    {
        const ScaleFamily want = guessIsMinor ? ScaleFamily::Minor : ScaleFamily::Major;
        if (scaleFamily (current) == want)
            return current;
        return guessIsMinor ? Scale::NaturalMinor : Scale::Major;
    }

    // A key spelled the usual way for its key signature (the Key menu says "C#/Db"; a guess
    // picks one).
    inline const char* guessKeyName (int tonic, bool isMinor) noexcept
    {
        static constexpr const char* majors[12] = { "C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
        static constexpr const char* minors[12] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "G#", "A", "Bb", "B" };
        const int t = ((tonic % 12) + 12) % 12;
        return isMinor ? minors[t] : majors[t];
    }
}
