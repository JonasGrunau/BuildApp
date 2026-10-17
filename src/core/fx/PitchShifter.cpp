#include "PitchShifter.h"

#include "Mapping.h"

#include <cmath>
#include <numbers>

namespace ba
{

void PitchShifter::prepare (double sampleRate)
{
    fs = sampleRate;
    fadeLen = std::round (0.008 * fs);
    jumpMin = (int) std::round (0.010 * fs);
    jumpMax = (int) std::round (0.030 * fs);
    window = (int) std::round (0.008 * fs);

    int size = 1;
    while (size < (int) (0.25 * fs)) size <<= 1;
    mask = size - 1;
    for (auto& b : buf) b.assign ((size_t) size, 0.f);

    semis.prepare (fs, 0.03);
    engage.prepare (fs, 0.025);
    semis.snap (semis.getTarget());
    engage.settle();
    reset();
}

void PitchShifter::reset()
{
    for (auto& b : buf) std::fill (b.begin(), b.end(), 0.f);
    written = 0;
    for (auto& ch : antiAlias)
        for (auto& s : ch) s.reset();
    aaRatio = -1.0;
    current.delay = lowDelay (0.0) + jumpMin;
    splicing = false;
}

void PitchShifter::setSemitones (float st) noexcept
{
    const double s = std::clamp ((double) st, 0.0, map::kMaxPitch);
    semis.setTarget (s);
    engage.setOn (s > 0.001);
}

float PitchShifter::read (int channel, double delay) const noexcept
{
    const double pos = (double) (written - 1) - delay;
    const double base = std::floor (pos);
    const double f = pos - base;
    const int64_t n = (int64_t) base;
    const auto& b = buf[(size_t) channel];
    const float xm1 = b[(size_t) ((n - 1) & mask)], x0 = b[(size_t) (n & mask)];
    const float x1 = b[(size_t) ((n + 1) & mask)], x2 = b[(size_t) ((n + 2) & mask)];
    // 4-point Catmull-Rom (Hermite).
    const double c1 = 0.5 * (x1 - xm1);
    const double c2 = xm1 - 2.5 * x0 + 2.0 * x1 - 0.5 * x2;
    const double c3 = 0.5 * (x2 - xm1) + 1.5 * (x0 - x1);
    return (float) (((c3 * f + c2) * f + c1) * f + x0);
}

// The jump back (samples) at which the material around the read head best
// matches itself: normalised correlation of the mono sum over the last 8 ms
// and whatever has been written ahead of the head. Coarse grid, then refined.
double PitchShifter::findJump (double delay) noexcept
{
    const auto& m = buf[2];
    const int64_t newest = written - 1;
    const int64_t at = (int64_t) std::floor ((double) newest - delay);
    const int from = -window, to = (int) (newest - at);

    const auto score = [&] (int jump, int stride) {
        double num = 0.0, ea = 0.0, eb = 0.0;
        for (int k = from; k < to; k += stride)
        {
            const float a = m[(size_t) ((at + k) & mask)], b = m[(size_t) ((at - jump + k) & mask)];
            num += (double) a * b;
            ea += (double) a * a;
            eb += (double) b * b;
        }
        return ea < 1e-9 ? 1.0 : num / std::sqrt (ea * eb + 1e-18);   // silence matches anything
    };

    const int coarse = std::max (1, (int) std::round (fs / 16000.0));
    int best = jumpMin;
    double bestScore = -2.0;
    for (int j = jumpMin; j <= jumpMax; j += coarse)
        if (const double s = score (j, 2); s > bestScore) { bestScore = s; best = j; }

    const int centre = best;
    bestScore = -2.0;
    for (int j = std::max (jumpMin, centre - coarse); j <= std::min (jumpMax, centre + coarse); ++j)
        if (const double s = score (j, 1); s > bestScore) { bestScore = s; best = j; }

    // The peak between samples (parabola through the neighbours): the heads
    // read at fractional positions anyway, so the jump can be fractional too.
    double jump = best;
    if (best > jumpMin && best < jumpMax)
    {
        const double sl = score (best - 1, 1), sr = score (best + 1, 1), den = sl - 2.0 * bestScore + sr;
        if (den < 0.0) jump += std::clamp (0.5 * (sl - sr) / den, -0.5, 0.5);
    }
    match = std::clamp (bestScore, 0.0, 1.0);
    return jump;
}

void PitchShifter::startSplice() noexcept
{
    next.delay = current.delay + findJump (current.delay);
    splicing = true;
    fadePos = 0.0;
}

void PitchShifter::process (float* left, float* right, int n) noexcept
{
    for (int i = 0; i < n; ++i)
    {
        const double ratio = std::exp2 (semis.next() / 12.0);
        if (std::abs (ratio - aaRatio) > 1e-5)
        {
            const double g = dsp::SvfCoeffs::prewarp (std::min (0.45 * fs / ratio, 20000.0), fs);
            aa1.set (g, 0.5412);
            aa2.set (g, 1.3066);
            aaRatio = ratio;
        }

        const float in[2] = { left[i], right[i] };
        float mono = 0.f;
        for (int c = 0; c < 2; ++c)
        {
            auto& st = antiAlias[(size_t) c];
            const float y = (float) st[1].lowpass (aa2, st[0].lowpass (aa1, in[c]));
            buf[(size_t) c][(size_t) (written & mask)] = y;
            mono += y;
        }
        buf[2][(size_t) (written & mask)] = mono;
        ++written;

        const bool wasOff = engage.get() == 0.0;
        const double e = engage.next();
        if (e == 0.0) continue;

        const double rate = ratio - 1.0;
        if (wasOff)
        {
            current.delay = lowDelay (rate) + jumpMin;
            splicing = false;
        }

        current.delay = std::max (kMargin, current.delay - rate);
        if (splicing) next.delay = std::max (kMargin, next.delay - rate);
        else if (current.delay <= lowDelay (rate)) startSplice();

        float y[2];
        if (splicing)
        {
            fadePos += 1.0;
            const double f = std::min (1.0, fadePos / fadeLen);
            // Equal gain where the two heads match, equal power where they don't.
            const double gainIn = 0.5 - 0.5 * std::cos (std::numbers::pi * f);
            const double powIn = std::sin (0.5 * std::numbers::pi * f), powOut = std::cos (0.5 * std::numbers::pi * f);
            const double gIn = match * gainIn + (1.0 - match) * powIn;
            const double gOut = match * (1.0 - gainIn) + (1.0 - match) * powOut;
            for (int c = 0; c < 2; ++c)
                y[c] = (float) (gOut * read (c, current.delay) + gIn * read (c, next.delay));
            if (f >= 1.0)
            {
                current = next;
                splicing = false;
            }
        }
        else
            for (int c = 0; c < 2; ++c)
                y[c] = read (c, current.delay);

        left[i] = (float) (in[0] + e * (y[0] - in[0]));
        right[i] = (float) (in[1] + e * (y[1] - in[1]));
    }
}

} // namespace ba
