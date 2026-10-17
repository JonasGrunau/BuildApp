#include "CharacterFilter.h"

#include "Mapping.h"

#include <complex>
#include <numbers>

namespace ba
{

namespace
{
    constexpr double kLog2Of44k = 15.428491035332245;   // the rate the styles were voiced at

    int stagesFor (int slope) noexcept { return slope >= 48 ? 4 : slope >= 24 ? 2 : 1; }

    // The frequency coefficient for x = log2 (fc / fs), before the 0.95 limit.
    double freqCoef (double x) noexcept { return 2.0 * std::sin (std::numbers::pi / 3.0 * std::min (std::exp2 (x), 1.0)); }

    // Peak to damping: 2 at no peak, 0 at 1, negative (self-oscillation) above.
    double dampFor (double peak) noexcept { return 2.0 * (1.0 - std::sqrt (std::sqrt (std::max (0.0, peak)))); }

    // The most damping a stage takes at `f` before it turns unstable.
    double dampLimit (double f) noexcept { return f > 0.01 ? 2.0 / f - f : 2.0; }

    template <size_t N>
    double breakpoints (double x, const double (&xs)[N], const double (&ys)[N]) noexcept
    {
        if (x <= xs[0]) return ys[0];
        for (size_t i = 1; i < N; ++i)
            if (x <= xs[i]) return ys[i - 1] + (x - xs[i - 1]) / (xs[i] - xs[i - 1]) * (ys[i] - ys[i - 1]);
        return ys[N - 1];
    }

    // The second stage of a steep filter is critically damped up to f 0.55,
    // then is allowed to ring a little as the cutoff nears Nyquist.
    double secondStageLimit (double f) noexcept
    {
        static constexpr double xs[] { 0.0, 0.55, 0.6, 0.65, 0.7, 0.75, 0.8, 0.85, 0.9, 0.95 };
        static constexpr double ys[] { 2.0, 2.0, 1.67, 1.4, 1.0, 0.8, 0.77, 0.73, 0.7, 0.65 };
        return breakpoints (f, xs, ys);
    }

    // A steep highpass gives up its peak close to Nyquist (x = log2 (fc / fs)).
    double peakLimit (double x) noexcept
    {
        static constexpr double xs[] { -1.5, -1.32, -0.5, 0.4 };
        static constexpr double ys[] { 1.0, 0.085, 0.04, 0.001 };
        return breakpoints (x, xs, ys);
    }

    // One of the three sub-steps of a stage; returns the highpass output.
    inline double subStep (double& lp, double& bp, double u, double f, double d, double q) noexcept
    {
        lp += f * bp;
        const double hp = u - d * bp - lp;
        bp += f * hp - q * bp * bp * bp;
        return hp;
    }

    inline void clampState (double& lp, double& bp) noexcept
    {
        bp = std::clamp (bp, -1.0, 1.0);
        lp = std::clamp (lp, -10.0, 10.0);
    }
} // namespace

CharacterFilter::CharacterFilter (bool isLowpass)
    : lowpass (isLowpass), character (ba::character (isLowpass, 0))
{
}

void CharacterFilter::prepare (double sampleRate)
{
    setSampleRate (sampleRate);
    reset();
}

void CharacterFilter::reset() noexcept
{
    state = {};
}

void CharacterFilter::setCharacter (const Character& ch) noexcept
{
    const int was = stagesFor (character.slope), now = stagesFor (ch.slope);
    character = ch;
    dirty = true;
    // Stages that join start where the stage before them is (a lowpass
    // cascade settles to the same state) or from rest (a highpass's later
    // stages see little low end); stages that leave forget.
    for (auto& st : state)
        for (int k = 0; k < kMaxStages; ++k)
            if (k >= now) st[(size_t) k] = {};
            else if (k >= was) st[(size_t) k] = lowpass ? st[(size_t) was - 1] : Stage {};
}

void CharacterFilter::setCutoff (double log2Hz, double peak) noexcept
{
    if (log2Hz == shownHz && peak == shownPeak && ! dirty) return;
    c = coefficients (lowpass, character, log2Hz, peak, fs);
    shownHz = log2Hz;
    shownPeak = peak;
    dirty = false;
}

void CharacterFilter::prime (const double* v) noexcept
{
    for (int ch = 0; ch < 2; ++ch)
    {
        const double u = std::clamp (c.inGain * v[ch], -10.0, 10.0);
        for (int k = 0; k < c.stages; ++k)
            state[(size_t) ch][(size_t) k] = { lowpass || k == 0 ? u : 0.0, 0.0 };
    }
}

void CharacterFilter::process (double* v) noexcept
{
    for (int ch = 0; ch < 2; ++ch)
        v[ch] = run (state[(size_t) ch], v[ch]);
}

double CharacterFilter::run (Stages& st, double x) const noexcept
{
    double u = c.inGain * x;
    for (int k = 0; k < c.stages; ++k)
    {
        auto& s = st[(size_t) k];
        const double d = c.damp[(size_t) k], q = k == 0 ? c.cubic : 0.0;
        subStep (s.lp, s.bp, u, c.f, d, q);
        subStep (s.lp, s.bp, u, c.f, d, q);
        const double hp = subStep (s.lp, s.bp, u, c.f, d, q);
        if (! c.clean) clampState (s.lp, s.bp);
        u = lowpass ? s.lp : hp;
    }
    return c.clean ? u : c.outScale * style::clip (c.clipIn * u, c.hardness);
}

CharacterFilter::Coeffs CharacterFilter::coefficients (bool lowpass, const Character& ch, double log2Hz, double peakIn, double fs) noexcept
{
    Coeffs k;
    k.lowpass = lowpass;
    k.clean = style::isClean (ch.style);
    k.stages = stagesFor (ch.slope);

    const double x = log2Hz - std::log2 (fs);
    const double f = freqCoef (x);
    k.f = std::min (f, 0.95);

    double peak = std::clamp (peakIn, 0.0, 1.0);
    if (! lowpass && k.stages > 1) peak = std::min (peak, peakLimit (x));
    const auto v = style::voice (ch.style, log2Hz, peak);
    // The cubic damping acts once per sub-step: scaled with f it does the
    // same at any rate as at 44.1 kHz, where it was voiced.
    k.cubic = fs > 44100.0 ? v.cubic * f / freqCoef (log2Hz - kLog2Of44k) : v.cubic;

    const double limit = dampLimit (k.f);
    k.damp[0] = std::min (dampFor (v.peak), limit);
    k.damp[1] = std::min (secondStageLimit (k.f), dampFor (0.105 * peak));
    k.damp[2] = std::min (dampFor (0.07 * peak), limit);
    k.damp[3] = std::min (dampFor (0.035 * peak), limit);
    if (k.clean) return k;

    // Drive below 0 dB backs off the clipper; above, it pushes the filter
    // itself harder and makes up only part of the gain.
    k.hardness = style::hardness (ch.style);
    const double g = style::inputGain (ch.style), drive = map::dbToGain (std::clamp (ch.driveDb, -18.0, 18.0));
    if (drive <= 1.0)
    {
        k.inGain = g;
        k.clipIn = v.clip * drive;
        k.outScale = 1.0 / (v.clip * drive * g);
    }
    else
    {
        k.inGain = g * drive;
        k.clipIn = v.clip;
        k.outScale = 1.0 / (v.clip * (2.15 * (std::sqrt (drive) - 1.0) + 1.0) * g);
    }
    return k;
}

double CharacterFilter::magnitude (bool lowpass, const Character& ch, double cutoffHz, double peak, double hz, double fs) noexcept
{
    using cd = std::complex<double>;
    const auto k = coefficients (lowpass, ch, std::log2 (cutoffHz), peak, fs);
    const cd z = std::polar (1.0, 2.0 * std::numbers::pi * std::clamp (hz, 0.0, 0.4999 * fs) / fs);

    double m = k.gain();
    for (int s = 0; s < k.stages; ++s)
    {
        // One sub-step is state' = M state + N u, with N = (0, f). A sample
        // is three of them: state[n+1] = A state[n] + B u, A = M^3,
        // B = (M^2 + M + 1) N; the output is read after the third from the
        // state after two, s2 = M^2 state + (M + 1) N u.
        const double f = k.f, d = k.damp[(size_t) s];
        const double m00 = 1.0, m01 = f, m10 = -f, m11 = 1.0 - f * d - f * f;
        const double q00 = m00 * m00 + m01 * m10, q01 = m00 * m01 + m01 * m11;   // M^2
        const double q10 = m10 * m00 + m11 * m10, q11 = m10 * m01 + m11 * m11;
        const double a00 = q00 * m00 + q01 * m10, a01 = q00 * m01 + q01 * m11;   // M^3
        const double a10 = q10 * m00 + q11 * m10, a11 = q10 * m01 + q11 * m11;
        const double b0 = (q01 + m01) * f, b1 = (q11 + m11 + 1.0) * f;
        const double p0 = m01 * f, p1 = (m11 + 1.0) * f;   // (M + 1) N

        const cd det = (z - a00) * (z - a11) - a01 * a10;
        const cd v0 = ((z - a11) * b0 + a01 * b1) / det, v1 = (a10 * b0 + (z - a00) * b1) / det;
        const cd w0 = q00 * v0 + q01 * v1 + p0, w1 = q10 * v0 + q11 * v1 + p1;
        // Lowpass: lp after the third step, lp + f bp. Highpass: u - lp - (d + f) bp.
        const cd h = lowpass ? w0 + f * w1 : 1.0 - w0 - (d + f) * w1;
        m *= std::abs (h);
    }
    return m;
}

} // namespace ba
