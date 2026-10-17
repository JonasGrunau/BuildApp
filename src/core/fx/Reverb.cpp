#include "Reverb.h"

#include <cmath>
#include <numbers>
#include <random>

namespace ba
{
namespace
{
    constexpr int N = Reverb::kChannels;
    constexpr double kWet = 1.42;          // calibrated: at full, the wash is ~6 dB above what is sent into it
    constexpr double kShelfKeep = 0.72;    // high shelf in the loop: highs above ~4.5 kHz keep 72 % per pass
    constexpr double kEarly = 0.3;         // diffused signal in the output (early part of the wash)

    // In place, unnormalised fast Walsh-Hadamard transform of 8 values.
    void hadamard (double* v) noexcept
    {
        for (int h = 1; h < N; h *= 2)
            for (int i = 0; i < N; i += 2 * h)
                for (int j = i; j < i + h; ++j)
                {
                    const double a = v[j], b = v[j + h];
                    v[j] = a + b;
                    v[j + h] = a - b;
                }
    }

    // Two rows of the 8x8 Hadamard matrix, orthogonal to each other and to the
    // all-ones direction the Householder matrix singles out: left and right
    // read different, uncorrelated mixes of the lines.
    constexpr double kLeft[N] = { 1, -1, 1, -1, 1, -1, 1, -1 };
    constexpr double kRight[N] = { 1, 1, -1, -1, 1, 1, -1, -1 };
} // namespace

double Reverb::decaySeconds (double a) noexcept { return 0.8 + 11.2 * std::pow (std::clamp (a, 0.0, 1.0), 1.7); }
double Reverb::dryGain (double a) noexcept { return 1.0 - 0.5 * a * a; }

// Send rises fast at first (t^0.6); longer tails hold more energy, so the
// level is pulled back by decay^-0.4: louder overall, but not exploding.
double Reverb::wetGain (double a) noexcept
{
    return kWet * std::pow (std::clamp (a, 0.0, 1.0), 0.6) * std::pow (decaySeconds (a) / 1.5, -0.4);
}

void Reverb::Line::allocate (int maxDelay)
{
    int size = 1;
    while (size < maxDelay + 4) size <<= 1;
    data.assign ((size_t) size, 0.f);
    mask = size - 1;
    pos = 0;
}

void Reverb::Line::clear() { std::fill (data.begin(), data.end(), 0.f); }

float Reverb::Line::readFrac (double delay) const noexcept
{
    const int d = (int) delay;
    const double f = delay - d;
    // Catmull-Rom over the delays d-1 .. d+2, f of the way from d towards d+1.
    const float xm1 = read (d - 1), x0 = read (d), x1 = read (d + 1), x2 = read (d + 2);
    const double c1 = 0.5 * (x1 - xm1);
    const double c2 = xm1 - 2.5 * x0 + 2.0 * x1 - 0.5 * x2;
    const double c3 = 0.5 * (x2 - xm1) + 1.5 * (x0 - x1);
    return (float) (((c3 * f + c2) * f + c1) * f + x0);
}

void Reverb::prepare (double sampleRate)
{
    fs = sampleRate;
    const auto ms = [&] (double v) { return v * 0.001 * fs; };
    std::mt19937 rng (0x5eed);
    std::uniform_real_distribution<double> uni (0.0, 1.0);

    sendLowC.set (160.0, 0.6, fs);
    sendHighC.set (9000.0, 0.6, fs);
    preDelaySamples = (int) std::round (ms (22.0));
    for (auto& l : preDelay) l.allocate (preDelaySamples + 1);

    // Diffusion: each step spreads the 8 channels over its range.
    constexpr double kStepMs[kSteps] = { 7.0, 13.0, 23.0, 37.0 };
    for (int s = 0; s < kSteps; ++s)
        for (int c = 0; c < N; ++c)
        {
            const int d = std::max (1, (int) std::round (ms (kStepMs[s] * (c + uni (rng)) / N)));
            diffuserDelay[(size_t) s][(size_t) c] = d;
            diffuserSign[(size_t) s][(size_t) c] = uni (rng) < 0.5 ? -1.f : 1.f;
            diffuser[(size_t) s][(size_t) c].allocate (d + 1);
        }

    // Feedback lines, 55..170 ms, exponentially spaced and jittered; each with
    // its own slow LFO (0.07..0.45 Hz, up to 0.6 ms).
    for (int c = 0; c < N; ++c)
    {
        const double base = 55.0 * std::pow (170.0 / 55.0, (double) c / (N - 1));
        loopDelay[(size_t) c] = std::round (ms (base * (0.96 + 0.08 * uni (rng))));
        lfoDepth[(size_t) c] = ms (0.25 + 0.35 * uni (rng));
        lfoStep[(size_t) c] = 2.0 * std::numbers::pi * (0.07 + 0.38 * uni (rng)) / fs;
        lfoPhase[(size_t) c] = 2.0 * std::numbers::pi * uni (rng);
        loop[(size_t) c].allocate ((int) (loopDelay[(size_t) c] + lfoDepth[(size_t) c]) + 4);
    }
    dampCoef = 1.0 - std::exp (-2.0 * std::numbers::pi * 4500.0 / fs);

    amountSmooth.prepare (fs, 0.03);
    amountSmooth.epsilon = 1e-5;
    amountSmooth.snap (target);
    decayShown = -1.0;
    reset();
}

void Reverb::reset()
{
    for (auto& s : sendLow) s.reset();
    for (auto& s : sendHigh) s.reset();
    for (auto& l : preDelay) l.clear();
    for (auto& step : diffuser)
        for (auto& l : step) l.clear();
    for (auto& l : loop) l.clear();
    damp.fill (0.0);
    cleared = true;
}

void Reverb::updateDecay (double amount) noexcept
{
    const double t60 = decaySeconds (amount);
    for (int c = 0; c < N; ++c)
        loopGain[(size_t) c] = std::pow (10.0, -3.0 * loopDelay[(size_t) c] / (t60 * fs));
    decayShown = amount;
}

void Reverb::process (float* left, float* right, int n) noexcept
{
    if (isBypassed())
    {
        if (! cleared) reset();
        return;
    }
    cleared = false;

    for (int i = 0; i < n; ++i)
    {
        const double a = amountSmooth.next();
        if ((i & 15) == 0 && std::abs (a - decayShown) > 1e-4) updateDecay (a);
        const double dry = dryGain (a), wet = wetGain (a);

        // Send: band-limited, pre-delayed, spread over the 8 channels.
        double v[N];
        {
            const double in[2] = { left[i], right[i] };
            double sent[2];
            for (int c = 0; c < 2; ++c)
            {
                const double x = sendHigh[(size_t) c].lowpass (sendHighC, sendLow[(size_t) c].highpass (sendLowC, in[c]));
                sent[c] = preDelay[(size_t) c].read (preDelaySamples);
                preDelay[(size_t) c].write ((float) x);
            }
            for (int c = 0; c < N; ++c)
                v[c] = sent[c & 1];
        }

        // Diffusion.
        for (int s = 0; s < kSteps; ++s)
        {
            auto& lines = diffuser[(size_t) s];
            for (int c = 0; c < N; ++c)
            {
                const double x = v[c];
                v[c] = lines[(size_t) c].read (diffuserDelay[(size_t) s][(size_t) c]) * diffuserSign[(size_t) s][(size_t) c];
                lines[(size_t) c].write ((float) x);
            }
            hadamard (v);
            for (double& x : v) x *= 0.35355339059327373;   // 1 / sqrt 8
        }

        // Feedback network.
        double y[N], sum = 0.0;
        for (int c = 0; c < N; ++c)
        {
            lfoPhase[(size_t) c] += lfoStep[(size_t) c];
            if (lfoPhase[(size_t) c] > 2.0 * std::numbers::pi) lfoPhase[(size_t) c] -= 2.0 * std::numbers::pi;
            y[c] = loop[(size_t) c].readFrac (loopDelay[(size_t) c] + lfoDepth[(size_t) c] * std::sin (lfoPhase[(size_t) c]));
            sum += y[c];
        }
        const double house = sum * (2.0 / N);
        double outL = 0.0, outR = 0.0;
        for (int c = 0; c < N; ++c)
        {
            double fb = (y[c] - house) * loopGain[(size_t) c];
            auto& lp = damp[(size_t) c];
            lp += dampCoef * (fb - lp);
            fb = lp + kShelfKeep * (fb - lp);
            loop[(size_t) c].write ((float) (v[c] + fb));
            outL += kLeft[c] * (y[c] + kEarly * v[c]);
            outR += kRight[c] * (y[c] + kEarly * v[c]);
        }

        const double scale = wet * 0.35355339059327373;
        left[i] = (float) (dry * left[i] + scale * outL);
        right[i] = (float) (dry * right[i] + scale * outR);
    }
}

} // namespace ba
