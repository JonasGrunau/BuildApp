#pragma once

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
// swells. Added on top of the signal after the filters.
class NoiseSource
{
public:
    static constexpr double kFullRms = 0.25;   // about -12 dBFS at full volume

    NoiseSource() { setTargets (0.f, 0.5f); }

    void prepare (double sampleRate);
    void reset();

    void setTargets (float volume, float pitch) noexcept;
    void process (float* left, float* right, int numSamples) noexcept;   // adds

    bool isSilent() const noexcept { return gain.settled() && gain.get() == 0.0; }

    // The band's shape at `hz` for a noise pitch position, relative to its
    // level at the top edge (where the pitch is), for the display.
    static double shape (double hz, float pitch, double fs) noexcept;

private:
    static double compensation (double centreHz, double fs) noexcept;

    double fs = 48000.0;
    std::array<uint32_t, 2> seed { 0x9e3779b9u, 0x7f4a7c15u };
    std::array<dsp::SvfState, 2> lowpass, highpass;
    dsp::SvfCoeffs lpCoeffs, hpCoeffs;
    dsp::Smoother gain, centreLog;
    double shownLog = -1.0, normLog = -100.0, norm = 1.0;
};

} // namespace ba
