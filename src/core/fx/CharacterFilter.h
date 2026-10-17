#pragma once

#include "Characters.h"

#include <array>

namespace ba
{

// One cut, stereo, working the way FabFilter Volcano 3's filters do:
//
//   x ─ gain ─ SVF stage ─ SVF stage ... ─ clipper ─ out
//
// Each stage is a Chamberlin state-variable filter run three times per
// sample on the held input, so its frequency coefficient is
// f = 2 sin (pi fc / 3 fs) (at most 0.95): accurate tuning up high without
// oversampling the signal. The band state is clamped to +-1 and the low
// state to +-10 every sample, the first stage adds the style's cubic band
// damping, and the clipper is atan (h x) / h (see FilterStyles.h). 12 dB/oct
// is one stage, 24 two, 48 four; the first stage carries the peak, the
// others a little of it (0.105, 0.07, 0.035), each damping held below the
// stability limit of the stage.
//
// Volcano's own "High Quality" only runs the clipper at 3x; the clamps and
// the cubic damping inside the core alias just as much, so it measures
// within a dB of normal. For offline renders CutFilters oversamples the
// whole filter instead (it runs at any rate and sounds the same).
//
// Coefficients follow the cutoff (log2 Hz) and the resonance (Volcano's peak,
// 0..1) per sample; setting the same values again costs nothing.
class CharacterFilter
{
public:
    static constexpr int kMaxStages = 4;

    struct Coeffs
    {
        double f = 0.1;
        std::array<double, kMaxStages> damp { 2.0, 2.0, 2.0, 2.0 };
        double cubic = 0.0;                      // first stage only
        double inGain = 1.0, clipIn = 1.0, outScale = 1.0, hardness = 1.0;
        int stages = 1;
        bool lowpass = true, clean = true;

        // Small-signal gain from input to output (the clipper's slope at 0 is 1).
        double gain() const noexcept { return clean ? 1.0 : inGain * clipIn * outScale; }
    };

    explicit CharacterFilter (bool isLowpass = true);

    void prepare (double sampleRate);   // sets the rate and clears the states
    void setSampleRate (double sampleRate) noexcept { fs = sampleRate; dirty = true; }   // keeps them
    void reset() noexcept;

    void setCharacter (const Character&) noexcept;
    void setCutoff (double log2Hz, double peak) noexcept;

    // Starts the states settled on `v` (both channels), as if it had been
    // running into the filter forever: no thump when a lowpass wakes up
    // under a running signal. After setCutoff.
    void prime (const double* v) noexcept;

    // One sample, both channels, in place.
    void process (double* v) noexcept;

    const Coeffs& coeffs() const noexcept { return c; }

    // What the stage runs with for this character, cutoff, peak and rate.
    static Coeffs coefficients (bool lowpass, const Character&, double log2Hz, double peak, double fs) noexcept;

    // |H| at `hz` of the linear part (no clamps, no cubic damping, the clipper
    // at its slope at zero), as it runs at `fs`: what it does to a signal
    // well below clipping.
    static double magnitude (bool lowpass, const Character&, double cutoffHz, double peak, double hz, double fs) noexcept;

private:
    struct Stage
    {
        double lp = 0.0, bp = 0.0;
    };
    using Stages = std::array<Stage, kMaxStages>;

    double run (Stages&, double x) const noexcept;

    bool lowpass;
    Character character;
    Coeffs c;
    double fs = 48000.0, shownHz = -1.0, shownPeak = -1.0;
    bool dirty = true;
    std::array<Stages, 2> state {};
};

} // namespace ba
