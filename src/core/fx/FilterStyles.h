#pragma once

#include <algorithm>
#include <cmath>

// The eleven filter styles of FabFilter Volcano 3. A style is three things
// on top of the same state-variable core (see CharacterFilter.h):
//   - an input gain into the filter (Classic 1.8, Hard 1.4, the rest 1),
//   - a soft clipper after it, atan (h x) / h, with its own hardness h,
//   - a voicing: for each cutoff and peak, how hard the clipper is driven,
//     how much cubic damping tames the band, and the peak the filter
//     really runs at. Measured surfaces, 11 x 11, interpolated bilinearly.
// Clean is the plain core: linear, no gain, no clipper, no voicing.
namespace ba::style
{
    enum Style { classic, smooth, raw, hard, hollow, extreme, gentle, tube, metal, easyGoing, clean, count };

    struct Voice
    {
        double peak = 0.0;    // the peak the core runs at (above 1 it oscillates, held by the clamps)
        double cubic = 0.0;   // band damping proportional to band^3
        double clip = 0.1;    // drive into the output clipper
    };

    const char* name (int style) noexcept;
    inline bool isClean (int style) noexcept { return style < 0 || style >= clean; }
    double inputGain (int style) noexcept;
    double hardness (int style) noexcept;

    // The voicing at `log2Hz` (cutoff, log2 of Hz) and `peak` (0..1).
    Voice voice (int style, double log2Hz, double peak) noexcept;

    // The output clipper: unity slope at zero, saturating at +-pi / (2 h).
    // Volcano tabulates it over +-40; beyond that it holds.
    inline double clip (double x, double h) noexcept { return std::atan (h * std::clamp (x, -40.0, 40.0)) / h; }
} // namespace ba::style
