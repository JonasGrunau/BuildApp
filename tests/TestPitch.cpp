#include "TestUtil.h"
#include "fx/PitchShifter.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

using namespace testutil;

namespace
{
    Stereo shifted (const Stereo& x, float semitones, double fs)
    {
        ba::PitchShifter p;
        p.setSemitones (semitones);
        p.prepare (fs);
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

TEST_CASE ("Pitch: 0 semitones passes the signal bit for bit", "[pitch]")
{
    const auto x = both (noise (30000, 11));
    const auto y = shifted (x, 0.f, 48000.0);
    CHECK (x.left == y.left);
    CHECK (x.right == y.right);
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
