#pragma once

#include "dsp/Smoother.h"
#include "dsp/Svf.h"

#include <array>
#include <cstdint>
#include <vector>

namespace ba
{

// Pitch up by 0..12 semitones in real time: a delay line read faster than
// it is written, re-spliced whenever the read head catches up. Each splice
// looks for the jump (10..30 ms back) at which the signal best matches
// itself, so it lands in phase with whatever is playing (kick, bass, lead)
// and the crossfade is inaudible on tonal material; the crossfade shape
// moves from equal gain (matched) to equal power (noise) with how well it
// matched. Both channels splice together, so the image stays put.
//
// A 4-pole lowpass at 0.45 fs / ratio in front keeps the top octave from
// folding back as aliasing. At 0 semitones the stage is bypassed (crossfade);
// above that the signal runs 10..40 ms late, which no delay-line shifter avoids.
class PitchShifter
{
public:
    void prepare (double sampleRate);
    void reset();

    void setSemitones (float st) noexcept;
    void process (float* left, float* right, int numSamples) noexcept;

    bool isBypassed() const noexcept { return engage.isOff(); }

private:
    struct Head
    {
        double delay = 0.0;   // samples behind the newest written sample
    };

    float read (int channel, double delay) const noexcept;
    double findJump (double delay) noexcept;
    void startSplice() noexcept;
    double lowDelay (double rate) const noexcept { return kMargin + rate * fadeLen + 1.0; }

    static constexpr double kMargin = 3.0;   // the interpolator reads two samples ahead

    double fs = 48000.0;
    std::array<std::vector<float>, 3> buf;   // left, right, mono sum (for matching)
    int mask = 0;
    int64_t written = 0;   // index of the next sample to write

    dsp::Smoother semis;
    dsp::Fade engage;
    std::array<std::array<dsp::SvfState, 2>, 2> antiAlias;   // [channel][stage]
    dsp::SvfCoeffs aa1, aa2;
    double aaRatio = -1.0;

    Head current, next;
    bool splicing = false;
    double fadePos = 0.0, fadeLen = 0.0, match = 0.0;
    int jumpMin = 0, jumpMax = 0, window = 0;
};

} // namespace ba
