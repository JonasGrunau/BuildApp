#include "TestUtil.h"
#include "fx/Buildup.h"
#include "fx/Mapping.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

using namespace testutil;

namespace
{
    Stereo run (const Stereo& x, const ba::Settings& s, double fs, int block = 480)
    {
        ba::Buildup b;
        b.set (s);
        b.prepare (fs, block);
        Stereo y = x;
        for (size_t start = 0; start < y.size(); start += (size_t) block)
            b.process (y.left.data() + start, y.right.data() + start, (int) std::min<size_t> ((size_t) block, y.size() - start));
        return y;
    }

    ba::Settings everything()
    {
        ba::Settings s;
        s.lowCut = 0.6f;
        s.highCut = 0.5f;
        s.pitch = 12.f;
        s.noise = 1.f;
        s.noisePitch = 1.f;
        s.reverb = 1.f;
        s.stereo = 0.5f;
        s.levelDb = 12.f;
        return s;
    }

    double correlation (const Signal& a, const Signal& b, size_t from)
    {
        double ab = 0.0, aa = 0.0, bb = 0.0;
        for (size_t i = from; i < a.size(); ++i)
        {
            ab += (double) a[i] * b[i];
            aa += (double) a[i] * a[i];
            bb += (double) b[i] * b[i];
        }
        return ab / std::sqrt (aa * bb + 1e-30);
    }
} // namespace

TEST_CASE ("Chain: every knob at its default passes the loop bit for bit", "[chain]")
{
    const double fs = GENERATE (44100.0, 48000.0, 96000.0);
    const auto x = psyLoop (fs, 2.0);
    const auto y = run (x, ba::Settings {}, fs);
    CHECK (x.left == y.left);
    CHECK (x.right == y.right);
}

TEST_CASE ("Chain: dry/wet at 0 is the dry signal, whatever else is on", "[chain]")
{
    auto s = everything();
    s.mix = 0.f;
    s.levelDb = 0.f;
    const auto x = psyLoop (48000.0, 1.0);
    const auto y = run (x, s, 48000.0);
    CHECK (x.left == y.left);
    CHECK (x.right == y.right);
}

TEST_CASE ("Chain: stereo all the way up is mono; level is in dB", "[chain]")
{
    const auto x = psyLoop (48000.0, 1.0);
    ba::Settings s;
    s.stereo = 1.f;
    auto y = run (x, s, 48000.0);
    CHECK (y.left == y.right);
    CHECK (correlation (x.left, x.right, 0) < 0.995);   // the loop really is stereo

    s = {};
    s.levelDb = -6.f;
    y = run (x, s, 48000.0);
    CHECK (std::abs (db (rms (y.left) / rms (x.left)) + 6.0) < 0.05);
}

TEST_CASE ("Chain: noise holds its level across its pitch range, uncorrelated left/right", "[chain]")
{
    const double fs = GENERATE (44100.0, 96000.0);
    const Stereo silence { Signal ((size_t) (4.0 * fs), 0.f), Signal ((size_t) (4.0 * fs), 0.f) };
    for (const float p : { 0.f, 0.25f, 0.5f, 0.75f, 1.f })
    {
        ba::Settings s;
        s.noise = 1.f;
        s.noisePitch = p;
        const auto y = run (silence, s, fs);
        const size_t from = (size_t) (0.2 * fs);
        const double level = db (rms (y.left, from) / ba::NoiseSource::kFullRms);
        INFO ("fs " << fs << " pitch " << p << " (" << ba::map::noiseHz (p) << " Hz): " << level << " dB re full, L/R corr " << correlation (y.left, y.right, from));
        CHECK (std::abs (level) < 1.0);
        CHECK (std::abs (correlation (y.left, y.right, from)) < 0.15);   // narrow bands estimate noisily
    }
    ba::Settings off;
    off.noisePitch = 1.f;
    const auto y = run (silence, off, fs);
    CHECK (rms (y.left) == 0.0);
}

TEST_CASE ("Chain: reverb tail follows the knob, and the drop cuts it", "[chain]")
{
    const double fs = 48000.0;
    for (const float amount : { 0.4f, 1.f })
    {
        // Impulse in, measure T20 on the energy decay curve.
        Stereo x { Signal ((size_t) (14.0 * fs), 0.f), Signal ((size_t) (14.0 * fs), 0.f) };
        x.left[100] = x.right[100] = 1.f;
        ba::Settings s;
        s.reverb = amount;
        const auto y = run (x, s, fs);
        std::vector<double> edc (y.size());
        double acc = 0.0;
        for (size_t i = y.size(); i-- > 200;)
        {
            acc += (double) y.left[i] * y.left[i] + (double) y.right[i] * y.right[i];
            edc[i] = acc;
        }
        const double total = edc[200];
        size_t t5 = 0, t25 = 0;
        for (size_t i = 200; i < y.size(); ++i)
        {
            const double d = 10.0 * std::log10 (edc[i] / total + 1e-30);
            if (t5 == 0 && d < -5.0) t5 = i;
            if (t25 == 0 && d < -25.0) { t25 = i; break; }
        }
        const double rt = 3.0 * (double) (t25 - t5) / fs, want = ba::Reverb::decaySeconds (amount);
        INFO ("amount " << amount << ": RT60 " << rt << " s, designed " << want << " s");
        CHECK (rt > 0.7 * want);
        CHECK (rt < 1.3 * want);
    }

    // Full reverb on the loop, then the knob back to zero: within half a
    // second the output is the input again, bit for bit.
    const auto x = psyLoop (fs, 3.0);
    ba::Buildup b;
    ba::Settings s;
    s.reverb = 1.f;
    b.set (s);
    b.prepare (fs, 480);
    Stereo y = x;
    const size_t half = y.size() / 2;
    b.process (y.left.data(), y.right.data(), (int) half);
    const double wet = rms (y.left.data() + half / 2, half / 2), dry = rms (x.left.data() + half / 2, half / 2);
    INFO ("full reverb on the loop: " << db (wet / dry) << " dB re dry");
    CHECK (std::abs (db (wet / dry)) < 3.0);
    s.reverb = 0.f;
    b.set (s);
    b.process (y.left.data() + half, y.right.data() + half, (int) (y.size() - half));
    const size_t settled = half + (size_t) (0.5 * fs);
    CHECK (std::equal (y.left.begin() + (long) settled, y.left.end(), x.left.begin() + (long) settled));
}

TEST_CASE ("Chain: everything at full stays finite and bounded at any rate", "[chain]")
{
    const double fs = GENERATE (44100.0, 48000.0, 96000.0, 192000.0);
    const auto y = run (psyLoop (fs, 3.0), everything(), fs, 256);
    bool finite = true;
    double peak = 0.0;
    for (float v : y.left)
    {
        finite = finite && std::isfinite (v);
        peak = std::max (peak, (double) std::abs (v));
    }
    INFO ("fs " << fs << " peak " << peak);
    CHECK (finite);
    CHECK (peak < 8.0);
}
