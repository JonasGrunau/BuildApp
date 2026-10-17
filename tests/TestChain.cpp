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
        s.noiseLowCut = 0.f;   // the band's own level, without the low cut
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

TEST_CASE ("Chain: the noise's low cut rests at 200 Hz and takes the lows away", "[chain]")
{
    const double fs = 48000.0;
    const Stereo silence { Signal ((size_t) (3.0 * fs), 0.f), Signal ((size_t) (3.0 * fs), 0.f) };
    CHECK (std::abs (ba::map::lowCutHz (ba::Settings {}.noiseLowCut) - 200.0) < 0.01);
    ba::Settings s;
    s.noise = 1.f;
    s.noisePitch = 0.1f;   // a band low enough to reach under 200 Hz
    const auto band = [&] (const Signal& y, double lo, double hi) {
        double e = 0.0;
        for (double f = lo; f < hi; f *= 1.05) e += std::pow (toneLevel (y, f, fs, (size_t) (0.5 * fs)), 2.0);
        return std::sqrt (e);
    };
    const auto with = run (silence, s, fs);
    s.noiseLowCut = 0.f;
    const auto without = run (silence, s, fs);
    const double lows = db (band (with.left, 30.0, 80.0) / band (without.left, 30.0, 80.0));
    const double highs = db (band (with.left, 800.0, 1600.0) / band (without.left, 800.0, 1600.0));
    INFO ("30-80 Hz: " << lows << " dB, 0.8-1.6 kHz: " << highs << " dB with the low cut at 200 Hz");
    CHECK (lows < -30.0);
    CHECK (std::abs (highs) < 1.0);
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

TEST_CASE ("Chain: a reset forgets the tail, and a render after prepare repeats exactly", "[chain]")
{
    const double fs = 48000.0;
    const auto x = psyLoop (fs, 1.0);

    // The host stops or jumps: what comes after doesn't carry the reverb
    // tail or the pitch history from before.
    ba::Settings s;
    s.reverb = 1.f;
    s.pitch = 7.f;
    ba::Buildup b;
    b.set (s);
    b.prepare (fs, 480);
    Stereo y = x;
    b.process (y.left.data(), y.right.data(), (int) y.size());
    b.reset();
    Stereo silence { Signal ((size_t) fs, 0.f), Signal ((size_t) fs, 0.f) };
    b.process (silence.left.data(), silence.right.data(), (int) silence.size());
    CHECK (rms (silence.left) == 0.0);
    CHECK (rms (silence.right) == 0.0);

    // Everything on, the noise included: prepared again, the same input
    // renders to the same output.
    b.set (everything());
    const auto render = [&] {
        b.prepare (fs, 480);
        Stereo out = x;
        for (size_t start = 0; start < out.size(); start += 480)
            b.process (out.left.data() + start, out.right.data() + start, (int) std::min<size_t> (480, out.size() - start));
        return out;
    };
    const auto first = render(), second = render();
    CHECK (first.left == second.left);
    CHECK (first.right == second.right);
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
