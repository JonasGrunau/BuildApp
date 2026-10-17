#include "NoiseSource.h"

#include "Mapping.h"

#include <cmath>

namespace ba
{
namespace
{
    constexpr double kPeakQ = 2.2;    // the resonant top edge that gives the band its pitch
    constexpr double kFloorQ = 0.6;   // the highpass two octaves below the top edge
    constexpr double kFloorRatio = 1.0 / 3.0;

    double bandMagnitude (double hz, double topHz, double fs) noexcept
    {
        return dsp::svfMagnitude (false, topHz, kPeakQ, hz, fs) * dsp::svfMagnitude (true, topHz * kFloorRatio, kFloorQ, hz, fs);
    }
} // namespace

// Gain that brings uniform white noise (variance 1/3) through the band to
// kFullRms: output variance = 1/3 * 2/fs * integral of |H|^2 over 0..fs/2.
double NoiseSource::compensation (double topHz, double fs) noexcept
{
    constexpr int kSteps = 600;
    const double lo = std::log (1.0), hi = std::log (0.5 * fs), dl = (hi - lo) / kSteps;
    double integral = 0.0;
    for (int i = 0; i < kSteps; ++i)
    {
        const double f = std::exp (lo + (i + 0.5) * dl);
        const double m = bandMagnitude (f, topHz, fs);
        integral += m * m * f * dl;
    }
    const double variance = integral * 2.0 / (3.0 * fs);
    return kFullRms / std::sqrt (std::max (variance, 1e-12));
}

double NoiseSource::shape (double hz, float pitch, double fs) noexcept
{
    const double top = map::noiseHz (pitch);
    return bandMagnitude (hz, top, fs) / bandMagnitude (top, top, fs);
}

void NoiseSource::prepare (double sampleRate)
{
    fs = sampleRate;
    gain.prepare (fs, 0.02);
    centreLog.prepare (fs, 0.03);
    gain.snap (gain.getTarget());
    centreLog.snap (centreLog.getTarget());
    shownLog = -1.0;
    reset();
}

void NoiseSource::reset()
{
    for (auto& s : lowpass) s.reset();
    for (auto& s : highpass) s.reset();
}

void NoiseSource::setTargets (float volume, float pitch) noexcept
{
    gain.setTarget (map::noiseGain (volume));
    const double to = std::log2 (map::noiseHz (pitch));
    if (isSilent()) centreLog.snap (to);   // wakes up where the knob is
    else centreLog.setTarget (to);
}

void NoiseSource::process (float* left, float* right, int n) noexcept
{
    if (isSilent()) return;

    float* out[2] = { left, right };
    for (int i = 0; i < n; ++i)
    {
        const double g = gain.next(), lg = centreLog.next();
        if (lg != shownLog)
        {
            const double top = std::exp2 (lg);
            lpCoeffs.set (top, kPeakQ, fs);
            hpCoeffs.set (top * kFloorRatio, kFloorQ, fs);
            // The integral is costly; during a glide it is refreshed every 1/24 octave.
            if (std::abs (lg - normLog) > 1.0 / 24.0 || centreLog.settled())
            {
                norm = compensation (top, fs);
                normLog = lg;
            }
            shownLog = lg;
        }
        for (int c = 0; c < 2; ++c)
        {
            auto& s = seed[(size_t) c];   // xorshift32
            s ^= s << 13;
            s ^= s >> 17;
            s ^= s << 5;
            const double white = (double) (int32_t) s * (1.0 / 2147483648.0);
            const double y = highpass[(size_t) c].highpass (hpCoeffs, lowpass[(size_t) c].lowpass (lpCoeffs, white));
            out[c][i] += (float) (g * norm * y);
        }
    }
    if (isSilent()) reset();
}

} // namespace ba
