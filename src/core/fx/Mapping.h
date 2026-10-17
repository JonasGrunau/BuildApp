#pragma once

#include <algorithm>
#include <cmath>

// What each knob position means, shared by the DSP, the parameter texts and
// the display. Knob positions are 0..1 unless noted.
namespace ba::map
{
    // Low cut: all the way left is off, then 20 Hz .. 8 kHz, exponential.
    inline constexpr double kLowCutMin = 20.0, kLowCutMax = 8000.0;
    // High cut: all the way right is off, then 20 kHz down to 100 Hz to the left.
    inline constexpr double kHighCutMin = 100.0, kHighCutMax = 20000.0;
    // Pitch, semitones (the parameter is in semitones).
    inline constexpr double kMaxPitch = 12.0;
    // Noise pitch: where the noise band sits.
    inline constexpr double kNoiseMin = 80.0, kNoiseMax = 15000.0;
    // Level, dB (the parameter is in dB).
    inline constexpr double kLevelMin = -36.0, kLevelMax = 12.0;

    inline constexpr float kEnd = 1e-4f;   // this close to the end stop counts as on it

    inline bool lowCutOff (float t) noexcept { return t <= kEnd; }
    inline bool highCutOff (float t) noexcept { return t >= 1.f - kEnd; }

    inline double expMap (double lo, double hi, double t) noexcept { return lo * std::pow (hi / lo, std::clamp (t, 0.0, 1.0)); }
    inline float expNorm (double lo, double hi, double v) noexcept
    {
        return (float) std::clamp (std::log (std::max (v, 1e-9) / lo) / std::log (hi / lo), 0.0, 1.0);
    }

    inline double lowCutHz (float t) noexcept { return expMap (kLowCutMin, kLowCutMax, t); }
    inline float lowCutNorm (double hz) noexcept { return std::max (2.f * kEnd, expNorm (kLowCutMin, kLowCutMax, hz)); }
    inline double highCutHz (float t) noexcept { return expMap (kHighCutMin, kHighCutMax, t); }
    inline float highCutNorm (double hz) noexcept { return std::min (1.f - 2.f * kEnd, expNorm (kHighCutMin, kHighCutMax, hz)); }

    inline double noiseHz (float t) noexcept { return expMap (kNoiseMin, kNoiseMax, t); }
    inline float noiseHzNorm (double hz) noexcept { return expNorm (kNoiseMin, kNoiseMax, hz); }

    // Noise volume: an audio taper (t^3); full is 0 dB on the display, which
    // is noise at about -12 dBFS RMS.
    inline double noiseGain (float t) noexcept { return (double) t * t * t; }
    inline double noiseDb (float t) noexcept { return t <= kEnd ? -200.0 : 60.0 * std::log10 ((double) t); }
    inline float noiseNormFromDb (double db) noexcept { return (float) std::clamp (std::pow (10.0, db / 60.0), 0.0, 1.0); }

    // Stereo: the knob narrows. 0 = the full image (100 %), 1 = mono (0 %).
    inline double width (float t) noexcept { return 1.0 - std::clamp ((double) t, 0.0, 1.0); }

    inline double dbToGain (double db) noexcept { return std::pow (10.0, db / 20.0); }
} // namespace ba::map
