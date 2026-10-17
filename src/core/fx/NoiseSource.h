#pragma once

#include "Mapping.h"
#include "dsp/Smoother.h"
#include "dsp/Svf.h"

#include <array>
#include <cstdint>

namespace ba
{

// The riser noise: independent white noise per channel (full width), shaped
// into a band whose top edge carries a resonant peak, so it has a pitch you
// can sweep. NOISE PITCH moves the band from 80 Hz to 15 kHz; its level is
// kept at the same RMS wherever it sits, so the sweep brightens rather than
// swells. Its own low cut (24 dB/oct, after the level is set, so it takes
// the lows away rather than being made up for) keeps it off the kick and the
// bass; it rests at 200 Hz. Added on top of the signal after the filters.
class NoiseSource
{
public:
    static constexpr double kFullRms = 0.25;   // about -12 dBFS at full volume, with the low cut off

    NoiseSource() { setTargets (0.f, 0.5f, map::lowCutNorm (map::kNoiseLowCutDefault)); }

    void prepare (double sampleRate);
    void reset();

    // lowCut is a low cut knob position (Mapping.h): 0 = off.
    void setTargets (float volume, float pitch, float lowCut) noexcept;
    void process (float* left, float* right, int numSamples) noexcept;   // adds

    bool isSilent() const noexcept { return gain.settled() && gain.get() == 0.0; }

    // The band's shape at `hz` for a noise pitch position and low cut,
    // relative to its level at the top edge (where the pitch is), for the
    // display.
    static double shape (double hz, float pitch, float lowCut, double fs) noexcept;

private:
    static double compensation (double centreHz, double fs) noexcept;

    static constexpr std::array<uint32_t, 2> kSeeds { 0x9e3779b9u, 0x7f4a7c15u };   // reset starts them over: renders repeat

    double fs = 48000.0;
    std::array<uint32_t, 2> seed = kSeeds;
    std::array<dsp::SvfState, 2> lowpass, highpass;
    std::array<std::array<dsp::SvfState, 2>, 2> cutState;   // [channel][stage]
    dsp::SvfCoeffs lpCoeffs, hpCoeffs, cut1, cut2;
    dsp::Smoother gain, centreLog, cutLog;
    dsp::Fade cutFade;
    double shownLog = -1.0, normLog = -100.0, norm = 1.0, cutShown = -1.0;
};

} // namespace ba
