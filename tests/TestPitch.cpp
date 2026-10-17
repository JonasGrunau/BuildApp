#include "TestUtil.h"
#include "fx/PitchShifter.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

using namespace testutil;

namespace
{
    Stereo shifted (const Stereo& x, float semitones, double fs, bool highQuality = false)
    {
        ba::PitchShifter p;
        p.setSemitones (semitones);
        p.prepare (fs);
        p.setHighQuality (highQuality);
        Stereo y = x;
        for (size_t start = 0; start < y.size(); start += 512)
        {
            const int n = (int) std::min<size_t> (512, y.size() - start);
            p.process (y.left.data() + start, y.right.data() + start, n);
        }
        return y;
    }

    Stereo both (const Signal& s) { return { s, s }; }
} // namespace

TEST_CASE ("Pitch: 0 semitones passes the signal bit for bit, with no delay", "[pitch]")
{
    const auto x = both (noise (30000, 11));
    for (bool hq : { false, true })
    {
        const auto y = shifted (x, 0.f, 48000.0, hq);
        CHECK (x.left == y.left);
        CHECK (x.right == y.right);
    }
}

TEST_CASE ("Pitch: lands on the interval", "[pitch]")
{
    const double fs = GENERATE (44100.0, 48000.0, 96000.0);
    for (const float st : { 1.f, 3.5f, 7.f, 12.f })
    {
        const auto y = shifted (both (sine (220.0, fs, (size_t) (1.5 * fs))), st, fs);
        const double want = 220.0 * std::pow (2.0, st / 12.0);
        const double got = dominantHz (y.left, fs, 200.0, 500.0, (size_t) (0.3 * fs));
        INFO ("fs " << fs << "  +" << st << " st: want " << want << " Hz, got " << got);
        CHECK (std::abs (got / want - 1.0) < 0.003);
    }
}

TEST_CASE ("Pitch: splices are clean on tonal material", "[pitch]")
{
    const double fs = 48000.0;
    for (const double hz : { 55.0, 110.0, 440.0 })
        for (const float st : { 2.f, 5.f, 12.f })
        {
            const auto y = shifted (both (sine (hz, fs, (size_t) (2.0 * fs))), st, fs);
            const double want = hz * std::pow (2.0, st / 12.0);
            const size_t from = (size_t) (0.3 * fs);
            const double total = rms (y.left, from), residual = sineResidualDb (y.left, want, fs, from);
            INFO (hz << " Hz +" << st << " st: level " << db (total / (0.5 / std::sqrt (2.0))) << " dB, worst residual " << residual << " dB");
            CHECK (std::abs (db (total / (0.5 / std::sqrt (2.0)))) < 1.0);
            CHECK (residual < -40.0);
        }
}

TEST_CASE ("Pitch: a loop keeps its level and stays finite", "[pitch]")
{
    const double fs = 44100.0;
    const auto x = psyLoop (fs, 4.0);
    for (const float st : { 0.5f, 6.f, 12.f })
    {
        const auto y = shifted (x, st, fs);
        bool finite = true;
        for (float v : y.left) finite = finite && std::isfinite (v);
        CHECK (finite);
        const double change = db (rms (y.left, 22050) / rms (x.left, 22050));
        INFO ("+" << st << " st: level change " << change << " dB");
        CHECK (std::abs (change) < 1.5);
    }
}

TEST_CASE ("Pitch: a chord comes out clean, live and in high quality", "[pitch]")
{
    // Eight partials from 110 Hz to 1.3 kHz: everything not at ratio x a
    // partial is an artifact (the old delay-line shifter measured -9, -5 and
    // -2 dB here).
    const double fs = 48000.0;
    const double partials[] = { 110.0, 164.81, 220.0, 277.18, 329.63, 440.0, 880.0, 1318.5 };
    Signal chord ((size_t) (3.0 * fs), 0.f);
    for (size_t i = 0; i < chord.size(); ++i)
    {
        double v = 0.0;
        int k = 0;
        for (double f : partials) v += 0.07 * std::sin (2.0 * pi * f * (double) i / fs + k++);
        chord[i] = (float) v;
    }
    for (bool hq : { false, true })
        for (const float st : { 3.f, 7.f, 12.f })
        {
            const auto y = shifted (both (chord), st, fs, hq);
            const size_t from = (size_t) fs;
            const double total = rms (y.left, from), ratio = std::pow (2.0, st / 12.0);
            double tonal = 0.0;
            for (double f : partials) tonal += 0.5 * std::pow (toneLevel (y.left, f * ratio, fs, from), 2.0);
            const double artifacts = 10.0 * std::log10 (std::max (total * total - tonal, 1e-12) / tonal);
            INFO ((hq ? "high quality" : "live") << " +" << st << " st: artifacts " << artifacts << " dB");
            CHECK (artifacts < (hq ? -17.0 : -13.5));
        }
}

TEST_CASE ("Pitch: kicks stay on time and keep their attack, at the same delay in both qualities", "[pitch]")
{
    const double fs = 48000.0;
    const auto loop = psyLoop (fs, 5.0);
    const double beat = 60.0 / 145.0;
    const auto env = [&] (const Signal& x, size_t i) { return rms (x.data() + i, (size_t) (0.001 * fs)); };
    // Attack (10 ms from where the kick should be) over the 10 ms before it.
    const auto contrast = [&] (const Signal& x, size_t at) {
        return db (rms (x.data() + at, (size_t) (0.01 * fs)) / rms (x.data() + at - (size_t) (0.012 * fs), (size_t) (0.01 * fs)));
    };
    ba::PitchShifter probe;
    probe.prepare (fs);
    const double lagMs = 1000.0 * probe.delay() / fs;
    for (bool hq : { false, true })
        for (const float st : { 7.f, 12.f })
        {
            const auto y = shifted (loop, st, fs, hq);
            std::vector<double> rises;
            double contrastIn = 0.0, contrastOut = 0.0;
            int beats = 0;
            for (int b = 2; (b + 1) * beat < 5.0; ++b, ++beats)
            {
                const size_t t = (size_t) (b * beat * fs);
                // Where the kick comes out: its envelope first reaches half its
                // peak, against the input's ...
                const auto onset = [&] (const Signal& x, size_t from) {
                    const size_t to = from + (size_t) (0.065 * fs);
                    double peak = 0.0;
                    for (size_t i = from; i < to; i += 24) peak = std::max (peak, env (x, i));
                    size_t i = from;
                    while (env (x, i) < 0.5 * peak) i += 24;
                    return (double) i;
                };
                const size_t early = t - (size_t) (0.005 * fs);
                const double ms = 1000.0 * (onset (y.left, early + (size_t) probe.delay()) - onset (loop.left, early)) / fs - lagMs;
                // ... and its attack: the envelope's steepest rise.
                double steepest = 0.0, rise = 0.0;
                for (size_t i = t - (size_t) (0.005 * fs); i < t + (size_t) (0.06 * fs); i += 24)
                    if (const double d = env (y.left, i + 48) - env (y.left, i); d > steepest)
                    {
                        steepest = d;
                        rise = 1000.0 * ((double) i - (double) t) / fs;
                    }
                rises.push_back (rise);
                const size_t arrive = t + (size_t) probe.delay();
                const double in = contrast (loop.left, t), out = contrast (y.left, arrive);
                contrastIn += in;
                contrastOut += out;
                const double level = db (rms (y.left.data() + arrive, (size_t) (0.01 * fs)) / rms (loop.left.data() + t, (size_t) (0.01 * fs)));
                INFO ((hq ? "high quality" : "live") << " +" << st << " st, beat " << b << ": lands " << ms << " ms after the delay (" << lagMs
                      << " ms), steepest rise at +" << rise << " ms, " << out << " dB over the 10 ms before (input " << in << " dB), "
                      << level << " dB re input");
                CHECK (std::abs (ms) <= 3.0);
                CHECK (out > 7.0);
                CHECK (std::abs (level) < 3.0);
            }
            std::nth_element (rises.begin(), rises.begin() + beats / 2, rises.end());
            const double median = rises[(size_t) beats / 2];
            INFO ((hq ? "high quality" : "live") << " +" << st << " st: attack at +" << median << " ms (median), "
                  << contrastOut / beats << " dB over what comes before on average (input " << contrastIn / beats << " dB)");
            CHECK (median > lagMs - 1.0);
            CHECK (median < lagMs + 4.0);
            CHECK (contrastOut / beats > contrastIn / beats - 2.0);
        }
}
