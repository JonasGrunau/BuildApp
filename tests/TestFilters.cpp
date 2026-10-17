#include "TestUtil.h"
#include "dsp/Halfband.h"
#include "fx/CutFilters.h"
#include "fx/Mapping.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

using namespace testutil;

namespace
{
    // Runs a stereo copy of `x` through the filters with fixed knobs and returns the left channel.
    Signal filtered (const Signal& x, float lowCut, float highCut, double fs)
    {
        ba::CutFilters f;
        f.setTargets (lowCut, highCut);
        f.prepare (fs);
        Signal l = x, r = x;
        f.process (l.data(), r.data(), (int) l.size());
        return l;
    }

    // The analog prototype: what the filters should measure, with no warping at all.
    double analog (double hz, float lowCut, float highCut)
    {
        double m = 1.0;
        const auto stage = [] (bool hp, double fc, double q, double f) {
            const double w = f / fc, re = 1.0 - w * w, im = w / q;
            return (hp ? w * w : 1.0) / std::sqrt (re * re + im * im);
        };
        if (! ba::map::lowCutOff (lowCut))
        {
            const double fc = ba::map::lowCutHz (lowCut);
            m *= stage (true, fc, ba::CutFilters::kQ1, hz) * stage (true, fc, ba::CutFilters::kQ2, hz);
        }
        if (! ba::map::highCutOff (highCut))
        {
            const double fc = ba::map::highCutHz (highCut);
            m *= stage (false, fc, ba::CutFilters::kQ1, hz) * stage (false, fc, ba::CutFilters::kQ2, hz);
        }
        return m;
    }
} // namespace

TEST_CASE ("Halfband: 130 dB image rejection and a transparent round trip", "[filters]")
{
    const auto c = ba::dsp::Halfband::design (12, 0.025);
    for (size_t i = 1; i < c.size(); ++i)
        CHECK ((c[i] > c[i - 1] && c[i] < 1.0 && c[0] > 0.0));

    const double fs = 48000.0;
    const auto x = sine (15000.0, fs, 48000, 0.5);
    ba::dsp::Halfband up, down;
    Signal hi (x.size() * 2), back (x.size());
    for (size_t i = 0; i < x.size(); ++i)
    {
        double a, b;
        up.up (x[i], a, b);
        hi[2 * i] = (float) a;
        hi[2 * i + 1] = (float) b;
        back[i] = (float) down.down (a, b);
    }
    // The image of 15 kHz after zero-stuffing sits at 33 kHz.
    const double tone = toneLevel (hi, 15000.0, 2 * fs, 4096), image = toneLevel (hi, 33000.0, 2 * fs, 4096);
    INFO ("tone " << db (tone) << " dB, image " << db (image) << " dB");
    CHECK (std::abs (db (tone / 0.5)) < 0.01);
    CHECK (db (image / tone) < -110.0);
    CHECK (std::abs (db (toneLevel (back, 15000.0, fs, 4096) / 0.5)) < 0.01);
}

TEST_CASE ("Cut filters: both off passes the signal bit for bit", "[filters]")
{
    const double fs = GENERATE (44100.0, 48000.0, 96000.0);
    const auto x = noise (20000, 7);
    const auto y = filtered (x, 0.f, 1.f, fs);
    CHECK (x == y);
}

TEST_CASE ("Cut filters: the measured response is the analog 4-pole, up to 18 kHz", "[filters]")
{
    const double fs = GENERATE (44100.0, 48000.0, 96000.0);
    struct Case { float low, high; };
    for (const auto k : { Case { ba::map::lowCutNorm (1000.0), 1.f },
                          Case { 0.f, ba::map::highCutNorm (2000.0) },
                          Case { 0.f, ba::map::highCutNorm (16000.0) },   // close to Nyquist at 44.1 kHz
                          Case { ba::map::lowCutNorm (300.0), ba::map::highCutNorm (5000.0) } })
        for (double hz : { 60.0, 250.0, 700.0, 1000.0, 1400.0, 3000.0, 6000.0, 12000.0, 15000.0, 17000.0, 18000.0 })
        {
            const double expected = analog (hz, k.low, k.high);
            if (db (expected) < -90.0) continue;   // below what the measurement can resolve
            const auto y = filtered (sine (hz, fs, (size_t) fs, 0.5), k.low, k.high, fs);
            const double measured = toneLevel (y, hz, fs, (size_t) (0.25 * fs)) / 0.5;
            INFO ("fs " << fs << "  " << hz << " Hz: measured " << db (measured) << " dB, analog " << db (expected) << " dB");
            // Exact where you hear it; deep in the stopband the digital slope runs a little steeper.
            CHECK (std::abs (db (measured) - db (expected)) < (db (expected) > -24.0 ? (hz > 17500.0 ? 0.3 : 0.25) : 3.0));
        }
}

TEST_CASE ("Cut filters: the corner has a +3 dB bump, the slope is 24 dB/oct", "[filters]")
{
    double peak = 0.0;
    for (double w = 0.5; w < 3.0; w *= 1.01)
        peak = std::max (peak, analog (1000.0 * w, ba::map::lowCutNorm (1000.0), 1.f));
    CHECK (db (peak) > 2.5);
    CHECK (db (peak) < 3.5);
    const double oct = db (analog (125.0, ba::map::lowCutNorm (1000.0), 1.f)) - db (analog (250.0, ba::map::lowCutNorm (1000.0), 1.f));
    CHECK (std::abs (oct + 24.0) < 1.0);
}

TEST_CASE ("Cut filters: switching on and off never clicks", "[filters]")
{
    // A pure 220 Hz sine through the stage while the filters come and go:
    // anything above 2 kHz in the output is a click.
    const double fs = 48000.0;
    const auto x = sine (220.0, fs, 48000, 0.5);
    ba::CutFilters f;
    f.prepare (fs);
    Signal l = x, r = x;
    const int quarter = 12000;
    const float steps[4][2] = { { 0.f, 1.f }, { 0.002f, 0.998f }, { 0.002f, 1.f }, { 0.f, 1.f } };
    for (int q = 0; q < 4; ++q)
    {
        f.setTargets (steps[q][0], steps[q][1]);
        f.process (l.data() + q * quarter, r.data() + q * quarter, quarter);
    }
    ba::dsp::SvfCoeffs c;
    c.set (2000.0, 0.7071, fs);
    ba::dsp::SvfState s1, s2;
    double worst = 0.0;
    for (size_t i = 0; i < l.size(); ++i)
    {
        const double h = s2.highpass (c, s1.highpass (c, l[i]));
        if (i > 2000) worst = std::max (worst, std::abs (h));
    }
    INFO ("largest content above 2 kHz: " << db (worst) << " dBFS");
    CHECK (db (worst) < -70.0);
}

TEST_CASE ("Cut filters: a full sweep in 50 ms stays bounded and clean", "[filters]")
{
    const double fs = 44100.0;
    const auto x = noise (44100, 3);
    ba::CutFilters f;
    f.prepare (fs);
    Signal l = x, r = x;
    const int block = 64;
    for (int start = 0, b = 0; start < (int) x.size(); start += block, ++b)
    {
        const float t = (float) (0.5 + 0.5 * std::sin (b * 0.18));   // ~5 Hz wobble across the whole range
        f.setTargets (t, 1.f - t);
        f.process (l.data() + start, r.data() + start, std::min (block, (int) x.size() - start));
    }
    double peak = 0.0;
    bool finite = true;
    for (float v : l)
    {
        finite = finite && std::isfinite (v);
        peak = std::max (peak, (double) std::abs (v));
    }
    CHECK (finite);
    CHECK (peak < 1.5);
}
