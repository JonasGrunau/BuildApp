#pragma once

// Signals and measurements for the tests and the dev tools. No JUCE.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <numbers>
#include <utility>
#include <vector>

namespace testutil
{
using Signal = std::vector<float>;

struct Stereo
{
    Signal left, right;
    size_t size() const { return left.size(); }
};

inline constexpr double pi = std::numbers::pi;

inline Signal sine (double hz, double fs, size_t n, double amp = 0.5)
{
    Signal s (n);
    for (size_t i = 0; i < n; ++i) s[i] = (float) (amp * std::sin (2.0 * pi * hz * (double) i / fs));
    return s;
}

inline Signal noise (size_t n, uint32_t seed, double amp = 0.5)
{
    Signal s (n);
    for (auto& x : s)
    {
        seed = seed * 1664525u + 1013904223u;
        x = (float) (amp * ((double) (seed >> 8) / 8388608.0 - 1.0));
    }
    return s;
}

inline double rms (const float* x, size_t n)
{
    double e = 0.0;
    for (size_t i = 0; i < n; ++i) e += (double) x[i] * x[i];
    return n > 0 ? std::sqrt (e / (double) n) : 0.0;
}

inline double rms (const Signal& s, size_t from = 0) { return from < s.size() ? rms (s.data() + from, s.size() - from) : 0.0; }
inline double db (double linear) { return 20.0 * std::log10 (std::max (linear, 1e-12)); }

// Amplitude of the `hz` component (Goertzel over a Hann window).
inline double toneLevel (const float* x, size_t n, double hz, double fs)
{
    const double w = 2.0 * pi * hz / fs, c = 2.0 * std::cos (w);
    double s1 = 0.0, s2 = 0.0, wsum = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        const double win = 0.5 - 0.5 * std::cos (2.0 * pi * (double) i / (double) n);
        wsum += win;
        const double s0 = x[i] * win + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    const double re = s1 - s2 * std::cos (w), im = s2 * std::sin (w);
    return 2.0 * std::sqrt (re * re + im * im) / wsum;
}

inline double toneLevel (const Signal& s, double hz, double fs, size_t from = 0)
{
    return toneLevel (s.data() + from, s.size() - from, hz, fs);
}

// How far `s` is from a sine at `hz`, in dB re its level: the worst of
// short windows (~43 ms), each fitted with its own amplitude and phase, so
// splice glitches count and an inaudible slow phase drift doesn't.
inline double sineResidualDb (const Signal& s, double hz, double fs, size_t from, size_t win = 2048)
{
    double worst = -300.0;
    for (size_t w = from; w + win <= s.size(); w += win)
    {
        double ss = 0, sc = 0, cc = 0, ys = 0, yc = 0;
        for (size_t i = w; i < w + win; ++i)
        {
            const double a = 2.0 * pi * hz * (double) i / fs, sn = std::sin (a), cs = std::cos (a);
            ss += sn * sn; cc += cs * cs; sc += sn * cs; ys += s[i] * sn; yc += s[i] * cs;
        }
        const double det = ss * cc - sc * sc, a = (ys * cc - yc * sc) / det, b = (yc * ss - ys * sc) / det;
        double e = 0, t = 0;
        for (size_t i = w; i < w + win; ++i)
        {
            const double ph = 2.0 * pi * hz * (double) i / fs, d = s[i] - (a * std::sin (ph) + b * std::cos (ph));
            e += d * d;
            t += (double) s[i] * s[i];
        }
        worst = std::max (worst, 10.0 * std::log10 (e / std::max (t, 1e-30) + 1e-30));
    }
    return worst;
}

// The strongest frequency between lo and hi, searched on a fine grid.
inline double dominantHz (const Signal& s, double fs, double lo, double hi, size_t from = 0)
{
    double best = lo, bestLevel = -1.0;
    for (double f = lo; f <= hi; f *= 1.0005)
        if (const double l = toneLevel (s, f, fs, from); l > bestLevel) { bestLevel = l; best = f; }
    return best;
}

// Loudness of samples [from, to) in LUFS, BS.1770 K-weighted (shelf + RLB
// highpass, designed for `fs` the way libebur128 does), both channels,
// ungated: for comparing renders of the same material, not for metering.
inline double loudness (const Stereo& s, double fs, size_t from, size_t to)
{
    struct Biquad
    {
        double b0, b1, b2, a1, a2, z1 = 0.0, z2 = 0.0;
        double operator() (double x)
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };
    const auto design = [fs] {
        double k = std::tan (pi * 1681.974450955533 / fs), q = 0.7071752369554196;
        const double vh = std::pow (10.0, 3.999843853973347 / 20.0), vb = std::pow (vh, 0.4996667741545416);
        double a0 = 1.0 + k / q + k * k;
        const Biquad shelf { (vh + vb * k / q + k * k) / a0, 2.0 * (k * k - vh) / a0, (vh - vb * k / q + k * k) / a0,
                             2.0 * (k * k - 1.0) / a0, (1.0 - k / q + k * k) / a0 };
        k = std::tan (pi * 38.13547087602444 / fs);
        q = 0.5003270373238773;
        a0 = 1.0 + k / q + k * k;
        const Biquad rlb { 1.0, -2.0, 1.0, 2.0 * (k * k - 1.0) / a0, (1.0 - k / q + k * k) / a0 };
        return std::pair { shelf, rlb };
    };
    to = std::min (to, s.size());
    double sum = 0.0;
    for (const Signal* ch : { &s.left, &s.right })
    {
        auto [shelf, rlb] = design();
        for (size_t i = 0; i < to; ++i)
        {
            const double y = rlb (shelf ((*ch)[i]));
            if (i >= from) sum += y * y;
        }
    }
    return -0.691 + 10.0 * std::log10 (std::max (sum / (double) std::max<size_t> (1, to - from), 1e-30));
}

// RMS frequency of samples [from, to) of the mid signal, Hz: how bright it
// is (the square root of the power spectrum's second moment).
inline double brightness (const Stereo& s, double fs, size_t from, size_t to)
{
    to = std::min (to, s.size());
    double e = 0.0, d = 0.0;
    for (size_t i = std::max<size_t> (from, 1); i < to; ++i)
    {
        const double m = 0.5 * ((double) s.left[i] + s.right[i]), prev = 0.5 * ((double) s.left[i - 1] + s.right[i - 1]);
        e += m * m;
        d += (m - prev) * (m - prev);
    }
    return fs / (2.0 * pi) * 2.0 * std::asin (std::min (1.0, 0.5 * std::sqrt (d / std::max (e, 1e-30))));
}

// Side over mid RMS of samples [from, to), dB: how wide it is.
inline double widthDb (const Stereo& s, size_t from, size_t to)
{
    to = std::min (to, s.size());
    double mm = 0.0, ss = 0.0;
    for (size_t i = from; i < to; ++i)
    {
        const double m = 0.5 * ((double) s.left[i] + s.right[i]), d = 0.5 * ((double) s.left[i] - s.right[i]);
        mm += m * m;
        ss += d * d;
    }
    return 10.0 * std::log10 (std::max (ss, 1e-30) / std::max (mm, 1e-30));
}

// Writes `s` as a 32-bit float stereo WAV. Returns false if it couldn't.
inline bool writeWav (const char* path, const Stereo& s, double fs)
{
    std::FILE* f = std::fopen (path, "wb");
    if (f == nullptr) return false;
    const auto u32 = [f] (uint32_t v) { std::fwrite (&v, 4, 1, f); };
    const auto u16 = [f] (uint16_t v) { std::fwrite (&v, 2, 1, f); };
    const auto bytes = (uint32_t) (s.size() * 2 * sizeof (float));
    std::fwrite ("RIFF", 1, 4, f);
    u32 (36 + bytes);
    std::fwrite ("WAVEfmt ", 1, 8, f);
    u32 (16);
    u16 (3);   // IEEE float
    u16 (2);
    u32 ((uint32_t) fs);
    u32 ((uint32_t) fs * 8);
    u16 (8);
    u16 (32);
    std::fwrite ("data", 1, 4, f);
    u32 (bytes);
    for (size_t i = 0; i < s.size(); ++i)
    {
        const float lr[2] = { s.left[i], s.right[i] };
        std::fwrite (lr, sizeof (float), 2, f);
    }
    return std::fclose (f) == 0;
}

// A psytrance loop at 145 BPM: kick on the beat, rolling bass on the three
// sixteenths after it, offbeat hats, a filtered sixteenth arp in stereo.
inline Stereo psyLoop (double fs, double seconds)
{
    const size_t n = (size_t) (seconds * fs);
    Stereo out { Signal (n, 0.f), Signal (n, 0.f) };
    const double beat = 60.0 / 145.0, six = beat / 4.0;
    uint32_t seed = 0x1234567u;
    const auto rnd = [&] { seed = seed * 1664525u + 1013904223u; return (double) (seed >> 8) / 8388608.0 - 1.0; };
    const double arpNotes[8] = { 0, 7, 12, 3, 7, 12, 15, 10 };

    double kickPhase = 0.0, bassPhase = 0.0, arpPhase[2] = { 0.0, 0.25 };
    double bassLp = 0.0, bassBp = 0.0, arpLp[2] = { 0.0, 0.0 }, hatHp = 0.0, hatPrev = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        const double t = (double) i / fs;
        const double inBeat = std::fmod (t, beat);
        const int sixteenth = (int) (std::fmod (t, beat) / six);
        const double inSix = std::fmod (t, six);

        // Kick: 150 -> 48 Hz sweep, with a click.
        const double kf = 48.0 + 102.0 * std::exp (-inBeat * 38.0);
        kickPhase += 2.0 * pi * kf / fs;
        if (inBeat < 1.0 / fs) kickPhase = 0.0;
        double kick = std::sin (kickPhase) * std::exp (-inBeat * 7.0) * (inBeat < 0.28 ? 1.0 : std::exp (-(inBeat - 0.28) * 40.0));
        kick += 0.3 * std::exp (-inBeat * 900.0) * rnd();

        // Bass: F1 saw, sixteenths 2-4 of each beat, filter envelope.
        double bass = 0.0;
        if (sixteenth > 0)
        {
            bassPhase += 43.65 / fs;
            bassPhase -= std::floor (bassPhase);
            const double saw = 2.0 * bassPhase - 1.0;
            const double cutoff = 180.0 + 1400.0 * std::exp (-inSix * 45.0);
            const double g = std::tan (pi * cutoff / fs), k = 1.0 / 0.9;
            const double a1 = 1.0 / (1.0 + g * (g + k)), a2 = g * a1, a3 = g * a2;
            const double v3 = saw - bassLp, v1 = a1 * bassBp + a2 * v3, v2 = bassLp + a2 * bassBp + a3 * v3;
            bassBp = 2.0 * v1 - bassBp;
            bassLp = 2.0 * v2 - bassLp;
            bass = v2 * std::min (1.0, inSix * 400.0) * std::exp (-inSix * 9.0);
        }

        // Hats: offbeat eighths.
        const double inEighth = std::fmod (t + beat / 2.0, beat);
        const double white = rnd();
        hatHp = 0.92 * (hatHp + white - hatPrev);
        hatPrev = white;
        const double hat = hatHp * std::exp (-inEighth * 60.0);

        // Arp: sixteenth notes from F4, two slightly detuned voices for width.
        const int step = (int) (t / six) % 8;
        const double hz = 349.23 * std::pow (2.0, arpNotes[step] / 12.0);
        double arp[2];
        for (int c = 0; c < 2; ++c)
        {
            arpPhase[c] += hz * (c == 0 ? 0.997 : 1.003) / fs;
            arpPhase[c] -= std::floor (arpPhase[c]);
            const double saw = 2.0 * arpPhase[c] - 1.0;
            arpLp[c] += (1.0 - std::exp (-2.0 * pi * 2600.0 / fs)) * (saw - arpLp[c]);
            arp[c] = arpLp[c] * std::exp (-inSix * 14.0);
        }

        const double mono = 0.55 * kick + 0.42 * bass;
        out.left[i] = (float) (mono + 0.12 * hat + 0.10 * arp[0]);
        out.right[i] = (float) (mono + 0.10 * hat + 0.10 * arp[1]);
    }
    return out;
}

} // namespace testutil
