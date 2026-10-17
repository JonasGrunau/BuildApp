#include "PitchShifter.h"

#include "Mapping.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace ba
{

namespace
{
    constexpr double kTwoPi = 2.0 * std::numbers::pi;
    constexpr double kHitRise = 4.0;   // the highs over the last hop, against their recent average
    constexpr int kHitGap = 8, kHitHold = 3;   // frames
    constexpr double kFall = 0.65;   // the analysis window's tail: Hann to this power
    constexpr double kMaxLift = 4.0;

    double wrap (double a) noexcept { return a - kTwoPi * std::round (a / kTwoPi); }

    int orderOf (int size) noexcept
    {
        int o = 0;
        while ((1 << o) < size) ++o;
        return o;
    }

    int pow2AtLeast (int n) noexcept { return 1 << orderOf (n); }

    constexpr double kChainA[] { 0.6923878, 0.9360654322959, 0.9882295226860, 0.9987488452737 };
    constexpr double kChainB[] { 0.4021921162426, 0.8561710882420, 0.9722909545651, 0.9952884791278 };
} // namespace

void PitchShifter::Quadrature::process (double in, float& re, float& im) noexcept
{
    double out[2];
    for (int chain = 0; chain < 2; ++chain)
    {
        const auto& coefs = chain == 0 ? kChainA : kChainB;
        double v = in;
        for (size_t k = 0; k < 4; ++k)
        {
            const double a = coefs[k] * coefs[k];
            const double y = a * (v + y2[(size_t) chain][k]) - x2[(size_t) chain][k];
            x2[(size_t) chain][k] = x1[(size_t) chain][k];
            x1[(size_t) chain][k] = v;
            y2[(size_t) chain][k] = y1[(size_t) chain][k];
            y1[(size_t) chain][k] = y;
            v = y;
        }
        out[chain] = v;
    }
    // The first chain a sample late against the second: -90 degrees +-0.7
    // from 30 Hz to 20 kHz at 48 kHz, so the second, negated, lags by 90.
    re = (float) delayed;
    im = (float) -out[1];
    delayed = out[0];
}

// Asymmetric windows (Mauler & Martin): the analysis window rises over the
// whole frame but falls over only the last `tail` samples; the synthesis
// window lives on the last 2 tails, chosen so analysis x synthesis is a Hann
// of 2 tails there, which overlap-adds to a constant at this hop. The
// analysis window falls as Hann^0.65 (not its square root), so it ends
// smoothly: see `undo`.
void PitchShifter::design (Shape& s, int size, int hop) const
{
    s.size = size;
    s.hop = hop;
    s.analysis.assign ((size_t) size, 0.f);
    s.synthesis.assign ((size_t) size, 0.f);
    const int m = tail, rise = size - m;
    const auto hann2m = [m] (int j) { return 0.5 - 0.5 * std::cos (std::numbers::pi * j / m); };   // periodic Hann of 2m
    for (int n = 0; n < size; ++n)
        s.analysis[(size_t) n] = (float) (n < rise ? std::sqrt (0.5 - 0.5 * std::cos (std::numbers::pi * n / rise))
                                                   : std::pow (hann2m (n - (size - 2 * m)), kFall));
    const double ola = (double) hop / m;   // the Hann of 2m sums to m / hop
    for (int n = size - 2 * m; n < size; ++n)
    {
        const int j = n - (size - 2 * m);
        const double w = j < m ? hann2m (j) / s.analysis[(size_t) n] : std::pow (hann2m (j), 1.0 - kFall);
        s.synthesis[(size_t) n] = (float) (w * ola);
    }

    // The analytic signal is the input through the first chain, so the lows
    // come out late (7 ms at 30 Hz, 2.4 ms at 110 Hz), which smears a kick.
    // Each bin turns that chain's phase back out, so every frequency lines up
    // again. In the frame this moves a low frequency's analysis window
    // earlier by its delay, and less of it lands under the synthesis window:
    // `lift` makes up for that, per peak at its frequency (+7 dB at 41 Hz,
    // at most +12 dB).
    const auto chainA = [] (double w) {
        const dsp::cplx z2 = std::polar (1.0, -2.0 * w);
        dsp::cplx h = std::polar (1.0, -w);
        for (double c : kChainA)
            h *= (c * c - z2) / (1.0 - c * c * z2);
        return h;
    };
    const auto overlap = [&] (double d) {   // analysis moved d samples earlier, against synthesis
        double sum = 0.0;
        for (int n = size - 2 * m; n < size; ++n)
        {
            const double at = n + d;
            const int i = (int) at;
            const double f = at - i;
            const double a0 = i < size ? s.analysis[(size_t) i] : 0.0, a1 = i + 1 < size ? s.analysis[(size_t) i + 1] : 0.0;
            sum += s.synthesis[(size_t) n] * (a0 + f * (a1 - a0));
        }
        return sum;
    };
    const double full = overlap (0.0);
    const int half = size / 2;
    s.undo.assign ((size_t) half + 1, 1.0);
    s.lift.assign ((size_t) half + 1, 1.0);
    for (int k = 1; k <= half; ++k)
    {
        const double w = kTwoPi * k / size, dw = 1e-6;
        const double delay = -std::arg (chainA (w + dw) * std::conj (chainA (w - dw))) / (2.0 * dw);
        s.lift[(size_t) k] = delay > 0.25 ? std::min (kMaxLift, full / std::max (overlap (delay), 1e-9)) : 1.0;
        s.undo[(size_t) k] = std::conj (chainA (w));
    }
    s.lift[0] = s.lift[1];
}

void PitchShifter::prepare (double sampleRate)
{
    fs = sampleRate;
    const int k = fs < 50000.0 ? 1 : fs < 100000.0 ? 2 : 4;
    tail = 384 * k;
    lag = 2 * tail;
    design (live, 4096 * k, tail / 4);
    design (high, 8192 * k, tail / 4);
    liveFft = std::make_unique<dsp::Fft> (orderOf (live.size));
    highFft = std::make_unique<dsp::Fft> (orderOf (high.size));

    const int biggest = high.size, bins = biggest / 2 + 1;
    for (auto* rings : { &input, &real, &imag })
        for (auto& b : *rings) b.assign ((size_t) pow2AtLeast (2 * biggest), 0.f);
    for (auto& b : acc) b.assign ((size_t) pow2AtLeast (4 * lag), 0.f);
    inMask = (int) input[0].size() - 1;
    accMask = (int) acc[0].size() - 1;
    work.assign ((size_t) biggest, {});
    for (int c = 0; c < 2; ++c)
    {
        spec[(size_t) c].assign ((size_t) bins, {});
        out[(size_t) c].assign ((size_t) bins, {});
        rotation[(size_t) c].assign ((size_t) bins, 0.0);
        nextRotation[(size_t) c].assign ((size_t) bins, 0.0);
    }
    lastSpec = spec;
    mag.assign ((size_t) bins, 0.0);
    peaks.reserve ((size_t) bins);

    semis.prepare (fs, 0.03);
    engage.prepare (fs, 0.025);
    semis.snap (semis.getTarget());
    engage.settle();
    use (wantHigh ? high : live);
    reset();
}

void PitchShifter::use (const Shape& s) noexcept
{
    shape = &s;
    fft = &s == &high ? highFft.get() : liveFft.get();
}

void PitchShifter::reset()
{
    for (auto* rings : { &input, &real, &imag })
        for (auto& b : *rings) std::fill (b.begin(), b.end(), 0.f);
    for (auto& q : quadrature) q.reset();
    for (auto& b : acc) std::fill (b.begin(), b.end(), 0.f);
    written = 0;
    running = false;
}

void PitchShifter::setSemitones (float st) noexcept
{
    const double s = std::clamp ((double) st, 0.0, map::kMaxPitch);
    semis.setTarget (s);
    engage.setOn (s > 0.001);
}

// The stage starts (or changes window) mid-stream: clear the analysis state
// and run the frames that the coming output needs over the input it already
// has, so the first shifted sample is as good as any later one.
void PitchShifter::wake() noexcept
{
    for (auto& b : acc) std::fill (b.begin(), b.end(), 0.f);
    for (int c = 0; c < 2; ++c)
    {
        std::fill (rotation[(size_t) c].begin(), rotation[(size_t) c].end(), 0.0);
        std::fill (lastSpec[(size_t) c].begin(), lastSpec[(size_t) c].end(), dsp::cplx {});
    }
    lastHf = 0.0;
    sinceHit = 0;
    const int64_t now = written - 1, hop = shape->hop;
    int64_t first = now - lag;
    first += (hop - 1 - (first % hop) + hop) % hop;   // the first frame end on the hop grid
    frame (first - hop, false);
    for (int64_t e = first; e <= now; e += hop)
        frame (e, true);
    running = true;
}

void PitchShifter::process (float* left, float* right, int n) noexcept
{
    if (const Shape& want = wantHigh ? high : live; &want != shape)
    {
        use (want);
        if (running) wake();
    }

    for (int i = 0; i < n; ++i)
    {
        ratio = std::exp2 (semis.next() / 12.0);
        const float x[2] = { left[i], right[i] };
        for (int c = 0; c < 2; ++c)
        {
            const auto at = (size_t) (written & inMask);
            input[(size_t) c][at] = x[c];
            quadrature[(size_t) c].process (x[c], real[(size_t) c][at], imag[(size_t) c][at]);
        }
        const int64_t now = written++;

        if (! running)
        {
            if (! engage.isTargetOn()) continue;   // at rest: untouched, no delay
            wake();
        }
        else if ((now + 1) % shape->hop == 0)
            frame (now, true);

        // The shifted signal runs `lag` behind; that sample is complete now.
        float y[2];
        for (int c = 0; c < 2; ++c)
        {
            auto& a = acc[(size_t) c][(size_t) ((now - lag) & accMask)];
            y[c] = a;
            a = 0.f;
        }
        const double e = engage.next();
        left[i] = (float) (x[0] + e * (y[0] - x[0]));
        right[i] = (float) (x[1] + e * (y[1] - x[1]));
        if (engage.isOff()) running = false;
    }
}

void PitchShifter::frame (int64_t end, bool synthesise) noexcept
{
    const int size = shape->size, half = size / 2, hop = shape->hop;
    const auto& wa = shape->analysis;
    const int64_t start = end - size + 1;

    // Each channel's analytic signal: its spectrum lives on the positive bins.
    for (int c = 0; c < 2; ++c)
    {
        for (int i = 0; i < size; ++i)
        {
            const auto at = (size_t) ((start + i) & inMask);
            work[(size_t) i] = { wa[(size_t) i] * real[(size_t) c][at], wa[(size_t) i] * imag[(size_t) c][at] };
        }
        fft->forward (work.data());
        for (int k = 0; k <= half; ++k)
            spec[(size_t) c][(size_t) k] = work[(size_t) k] * shape->undo[(size_t) k];
    }

    // A hit: the input's highs (its slope, summed over both channels) jump
    // over the last hop against the hops before. Measured on the input, so
    // it does not depend on the window. For a few frames from there the
    // peaks keep their analysed phases: the attack stays together. Both
    // channels reset together, so the image holds.
    double hf = 0.0;
    for (int64_t i = end - hop + 1; i <= end; ++i)
        for (int c = 0; c < 2; ++c)
        {
            const double d = input[(size_t) c][(size_t) (i & inMask)] - input[(size_t) c][(size_t) ((i - 1) & inMask)];
            hf += d * d;
        }
    hf /= hop;
    const bool hit = hf > kHitRise * lastHf && hf > 1e-7 && sinceHit > kHitGap;
    lastHf += (hf - lastHf) * 0.3;
    sinceHit = hit ? 0 : sinceHit + 1;

    for (int c = 0; c < 2; ++c)
        shift (c, sinceHit <= kHitHold);
    if (! synthesise) return;

    // Only the real parts of the two analytic outputs are wanted, so both go
    // back through one transform, each made symmetric (halved, mirrored); DC
    // and Nyquist are left out.
    work[0] = {};
    work[(size_t) half] = {};
    for (int k = 1; k < half; ++k)
    {
        const dsp::cplx a = 0.5 * out[0][(size_t) k], b = 0.5 * out[1][(size_t) k];
        work[(size_t) k] = a + dsp::cplx (-b.imag(), b.real());
        work[(size_t) (size - k)] = std::conj (a) + dsp::cplx (b.imag(), b.real());
    }
    fft->inverse (work.data());
    const auto& ws = shape->synthesis;
    for (int i = size - lag; i < size; ++i)
    {
        const auto at = (size_t) ((start + i) & accMask);
        acc[0][at] += ws[(size_t) i] * (float) work[(size_t) i].real();
        acc[1][at] += ws[(size_t) i] * (float) work[(size_t) i].imag();
    }
}

void PitchShifter::shift (int c, bool hit) noexcept
{
    const int size = shape->size, half = size / 2, hop = shape->hop;
    auto& x = spec[(size_t) c];
    auto& y = out[(size_t) c];
    auto& last = lastSpec[(size_t) c];
    auto& rot = rotation[(size_t) c];
    auto& nextRot = nextRotation[(size_t) c];
    std::fill (y.begin(), y.begin() + half + 1, dsp::cplx {});
    const dsp::cplx centre = std::polar (1.0, -kTwoPi * (size - tail) / size);   // one bin's turn at the synthesis window's middle

    double top = 0.0;
    for (int k = 0; k <= half; ++k)
        top = std::max (top, mag[(size_t) k] = std::norm (x[(size_t) k]));

    // Peaks: louder than the two bins either side, above -120 dB of the loudest.
    peaks.clear();
    const double floor = top * 1e-12;
    for (int k = 2; k <= half - 2; ++k)
    {
        const double m = mag[(size_t) k];
        if (m > floor && m > mag[(size_t) k - 1] && m > mag[(size_t) k - 2] && m >= mag[(size_t) k + 1] && m >= mag[(size_t) k + 2])
            peaks.push_back (k);
    }

    // Each peak takes the bins down to the lowest one between it and the next.
    int lo = 0;
    for (size_t p = 0; p < peaks.size(); ++p)
    {
        const int k0 = peaks[p];
        int hi = half;
        if (p + 1 < peaks.size())
        {
            hi = k0;
            for (int k = k0 + 1; k < peaks[p + 1]; ++k)
                if (mag[(size_t) k] < mag[(size_t) hi]) hi = k;
        }

        // True frequency from the phase advance since the last frame (rad/sample).
        const double advance = std::arg (x[(size_t) k0] * std::conj (last[(size_t) k0]));
        const double w = kTwoPi * k0 / size + wrap (advance - kTwoPi * k0 * hop / size) / hop;
        const double theta = hit ? 0.0 : wrap (rot[(size_t) k0] + (ratio - 1.0) * w * hop);
        // The shift is rarely a whole number of bins: the region goes to the
        // two nearest shifts, weighted by the fraction, the upper one turned
        // so the two agree in the middle of the synthesis window. There, where
        // the output comes from, the blend is the fractional shift itself:
        // the partial comes out at its exact frequency in every frame, and
        // consecutive frames line up.
        const double bins = (ratio - 1.0) * w * size / kTwoPi;
        const int whole = (int) std::floor (bins);
        const double frac = bins - whole;
        const double at = std::clamp (w * size / kTwoPi, 0.0, (double) half - 1.0);
        const auto below = (size_t) at;
        const double lift = shape->lift[below] + (at - (double) below) * (shape->lift[below + 1] - shape->lift[below]);
        const dsp::cplx turn = std::polar (lift, theta), a = (1.0 - frac) * turn, b = frac * turn * centre;
        for (int k = lo; k <= hi; ++k)
        {
            const int to = k + whole;
            if (to > 0 && to < half) y[(size_t) to] += x[(size_t) k] * a;
            if (to + 1 > 0 && to + 1 < half) y[(size_t) to + 1] += x[(size_t) k] * b;
            nextRot[(size_t) k] = theta;
        }
        lo = hi + 1;
    }
    for (int k = lo; k <= half; ++k)
        nextRot[(size_t) k] = 0.0;

    std::copy (x.begin(), x.begin() + half + 1, last.begin());
    std::swap (rot, nextRot);
}

} // namespace ba
