// The host transport for one block, as the engine sees it. Plain C++.
#pragma once

namespace tonnetz
{
    struct Transport
    {
        double sampleRate = 44100.0;
        bool playing = false;
        double ppq = 0.0;                 // quarter notes at the first sample of the block
        double bpm = 120.0;
        int numerator = 4, denominator = 4;

        // Loop and bar position (appended, so brace-initialised transports keep working).
        bool looping = false;             // the host's loop / cycle is on
        double loopStart = 0.0, loopEnd = 0.0;   // ppq
        bool hasBarStart = false;         // the host gave the ppq of the bar the block starts in
        double barStartPpq = 0.0;

        double samplesPerBeat() const noexcept { return sampleRate * 60.0 / (bpm > 0.0 ? bpm : 120.0); }

        // Quarter notes per bar.
        double barLength() const noexcept
        {
            return numerator > 0 && denominator > 0 ? numerator * 4.0 / denominator : 4.0;
        }
    };
}
