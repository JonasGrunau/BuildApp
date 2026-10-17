#include "TestUtil.h"
#include "dsp/Halfband.h"
#include "dsp/Svf.h"
#include "fx/CutFilters.h"
#include "fx/Mapping.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

using namespace testutil;

namespace
{
    // Runs a stereo copy of `x` through the cuts with fixed knobs and returns the left channel.
    Signal filtered (const Signal& x, float lowCut, float highCut, double fs, int lowType = 0, int highType = 0, bool hq = false)
    {
        ba::CutFilters f;
        f.setTargets ({ lowCut, lowType }, { highCut, highType });
        f.prepare (fs);
        f.setHighQuality (hq);
        Signal l = x, r = x;
        f.process (l.data(), r.data(), (int) l.size());
        return l;
    }

    // One filter on its own, at a fixed cutoff.
    Signal through (bool lowpass, const ba::Character& c, double cutoffHz, double peak, const Signal& x, double fs)
    {
        ba::CharacterFilter f (lowpass);
        f.setCharacter (c);
        f.prepare (fs);
        f.setCutoff (std::log2 (cutoffHz), peak);
        Signal y (x.size());
        for (size_t i = 0; i < x.size(); ++i)
        {
            double v[2] = { x[i], x[i] };
            f.process (v);
            y[i] = (float) v[0];
        }
        return y;
    }

    // What is not a harmonic of `hz` (aliases), against the harmonics, over the last 32k samples.
    double inharmonic (const Signal& y, double hz, double fs)
    {
        const size_t n = 32768, from = y.size() - n;
        double harmonic = 0.0, other = 0.0;
        for (double f = 50.0; f < 0.49 * fs; f += 2.0 * fs / (double) n)
        {
            const double l = toneLevel (y.data() + from, n, f, fs), k = f / hz;
            (std::abs (k - std::round (k)) * hz < 60.0 ? harmonic : other) += l * l;
        }
        return std::sqrt (other / harmonic);
    }

    // Harmonics 2..7 of `hz` against the fundamental.
    double distortion (const Signal& y, double hz, double fs, size_t from)
    {
        double e = 0.0;
        for (int k = 2; k <= 7 && k * hz < 0.45 * fs; ++k)
        {
            const double l = toneLevel (y, k * hz, fs, from);
            e += l * l;
        }
        return std::sqrt (e) / toneLevel (y, hz, fs, from);
    }

    // Anything above 2 kHz in `y` (a 220 Hz sine went in): clicks.
    double above2k (const Signal& y, double fs, size_t from)
    {
        ba::dsp::SvfCoeffs c;
        c.set (2000.0, 0.7071, fs);
        ba::dsp::SvfState s1, s2;
        double worst = 0.0;
        for (size_t i = 0; i < y.size(); ++i)
        {
            const double h = s2.highpass (c, s1.highpass (c, y[i]));
            if (i > from) worst = std::max (worst, std::abs (h));
        }
        return worst;
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
    for (int type : { 0, 3, 9 })
    {
        const auto y = filtered (x, 0.f, 1.f, fs, type, type, type == 9);
        CHECK (x == y);
    }
}

TEST_CASE ("Cut filters: below clipping every type measures as its computed response", "[filters]")
{
    // The response the display draws (CutFilters::magnitude) is worked out
    // from the filter's equations; a quiet sine through the real thing must
    // land on it, for every type, at every rate.
    const double fs = GENERATE (44100.0, 48000.0, 96000.0);
    for (bool lowpass : { false, true })
        for (int type = 0; type < (int) ba::characters (lowpass).size(); ++type)
        {
            const float low = lowpass ? 0.f : ba::map::lowCutNorm (300.0), high = lowpass ? ba::map::highCutNorm (3000.0) : 1.f;
            for (double hz : { 60.0, 150.0, 300.0, 700.0, 1500.0, 3000.0, 6000.0, 12000.0, 17000.0 })
            {
                const double expected = ba::CutFilters::magnitude (hz, { low, type }, { high, type }, fs);
                if (db (expected) < -60.0) continue;
                const double amp = 0.003;
                const auto y = filtered (sine (hz, fs, (size_t) (0.5 * fs), amp), low, high, fs, type, type);
                const double measured = toneLevel (y, hz, fs, (size_t) (0.25 * fs)) / amp;
                INFO ("fs " << fs << " " << ba::character (lowpass, type).name << " at " << hz << " Hz: measured " << db (measured)
                            << " dB, computed " << db (expected) << " dB");
                CHECK (std::abs (db (measured) - db (expected)) < 0.15);
            }
        }
}

TEST_CASE ("Cut filters: 12, 24 and 48 dB/oct fall that fast past the corner, and deep", "[filters]")
{
    // The lowpass keeps its slope; the highpass, fed the held input three
    // sub-steps at a time (as in Volcano), falls about 10 dB/oct per stage
    // over its first octaves and flattens towards 6 below -45 dB per stage.
    for (int slope : { 12, 24, 48 })
    {
        const ba::Character c { "", "", ba::style::clean, slope, 0.0, false, 0.0, 0.0 };
        const auto hp = [&] (double hz) { return db (ba::CharacterFilter::magnitude (false, c, 2000.0, 0.0, hz, 48000.0)); };
        const auto lp = [&] (double hz) { return db (ba::CharacterFilter::magnitude (true, c, 200.0, 0.0, hz, 48000.0)); };
        const double stages = slope / 12.0;
        INFO (slope << " dB/oct: highpass " << hp (500.0) - hp (1000.0) << " dB/oct below the corner, " << hp (60.0)
                    << " dB at 60 Hz; lowpass " << lp (6400.0) - lp (3200.0) << " dB/oct");
        CHECK (hp (500.0) - hp (1000.0) < -9.5 * stages);
        CHECK (hp (60.0) < -45.0 * stages);
        CHECK (std::abs (lp (6400.0) - lp (3200.0) + slope) < 2.0);   // bends further down nearing Nyquist
    }
}

TEST_CASE ("Cut filters: Clean is linear, the styles saturate", "[filters]")
{
    const double fs = 48000.0;
    const auto x = sine (220.0, fs, 48000, 0.7);
    for (int s = 0; s < ba::style::count; ++s)
    {
        const ba::Character c { "", "", s, 12, 6.0, false, 0.3, 0.3 };
        const double thd = db (distortion (through (true, c, 4000.0, 0.3, x, fs), 220.0, fs, 24000));
        INFO (ba::style::name (s) << ": harmonics " << thd << " dB");
        if (ba::style::isClean (s)) CHECK (thd < -120.0);
        else CHECK (thd > -90.0);
    }
}

TEST_CASE ("Cut filters: switching on and off, changing type or resonance mode, never clicks", "[filters]")
{
    // A 220 Hz sine through the stage while the filters come and go, change
    // type and go from auto to manual resonance: anything above 2 kHz in the
    // output is a click. The types here are quiet ones, so their own
    // harmonics stay far below.
    const double fs = 48000.0;
    const auto x = sine (220.0, fs, 96000, 0.5);
    ba::CutFilters f;
    f.prepare (fs);
    Signal l = x, r = x;
    const int part = 12000;
    struct Step { float low, high; int lowType, highType; float reso; bool autoReso; };
    const Step steps[] = { { 0.f, 1.f, 0, 4, 0.3f, true }, { 0.002f, 0.998f, 0, 4, 0.3f, true }, { 0.002f, 0.998f, 9, 4, 0.3f, true },
                           { 0.002f, 1.f, 0, 2, 0.3f, true }, { 0.002f, 0.998f, 0, 2, 0.3f, true }, { 0.002f, 0.998f, 0, 2, 0.05f, false },
                           { 0.002f, 0.998f, 0, 2, 0.5f, false }, { 0.f, 1.f, 0, 2, 0.5f, false } };
    for (int q = 0; q < 8; ++q)
    {
        const auto& k = steps[q];
        f.setTargets ({ k.low, k.lowType, k.reso, k.autoReso }, { k.high, k.highType, k.reso, k.autoReso });
        f.process (l.data() + q * part, r.data() + q * part, part);
    }
    const double worst = above2k (l, fs, 2000);
    INFO ("largest content above 2 kHz: " << db (worst) << " dBFS");
    CHECK (db (worst) < -70.0);
}

TEST_CASE ("Cut filters: every type sweeps the loop bounded and rings out", "[filters]")
{
    const double fs = 44100.0;
    const auto loop = psyLoop (fs, 2.0);
    for (bool lowpass : { false, true })
        for (int type = 0; type < (int) ba::characters (lowpass).size(); ++type)
            for (bool hq : { false, true })
            {
                ba::CutFilters f;
                f.prepare (fs);
                f.setHighQuality (hq);
                Signal l = loop.left, r = loop.right;
                const size_t n = l.size();
                l.resize (n + (size_t) fs, 0.f);
                r.resize (n + (size_t) fs, 0.f);
                bool finite = true;
                double peak = 0.0;
                for (size_t s = 0; s < l.size(); s += 64)
                {
                    // ~6 Hz wobble across the whole range while the loop plays, then silence.
                    const float t = (float) (0.5 + 0.5 * std::sin ((double) s / 64.0 * 0.18));
                    f.setTargets ({ lowpass ? 0.f : 0.02f + 0.9f * t, type }, { lowpass ? 0.98f - 0.9f * t : 1.f, type });
                    f.process (l.data() + s, r.data() + s, (int) std::min<size_t> (64, l.size() - s));
                }
                for (float v : l)
                {
                    finite = finite && std::isfinite (v);
                    peak = std::max (peak, (double) std::abs (v));
                }
                const double tail = rms (l.data() + n + (size_t) (0.5 * fs), (size_t) (0.5 * fs));
                INFO (ba::character (lowpass, type).name << (hq ? " (HQ)" : "") << ": peak " << peak << ", tail " << db (tail) << " dB");
                CHECK (finite);
                CHECK (peak < 1.5);
                CHECK (db (tail) < -120.0);
            }
}

TEST_CASE ("Cut filters: auto resonance moves with the cutoff the way each type wants; manual holds the knob", "[filters]")
{
    for (bool lowpass : { false, true })
        for (int type = 0; type < (int) ba::characters (lowpass).size(); ++type)
        {
            const auto& c = ba::character (lowpass, type);
            const auto knobs = [&] (float t) { return ba::CutKnobs { lowpass ? 1.f - t : t, type, 0.3f, true }; };
            double lo = 1.0, hi = 0.0, loudest = -100.0;
            for (float t = 0.02f; t < 0.99f; t += 0.02f)
            {
                const double reso = ba::CutFilters::resonance (lowpass, knobs (t));
                lo = std::min (lo, reso);
                hi = std::max (hi, reso);
                const auto k = knobs (t);
                const double fc = lowpass ? ba::map::highCutHz (k.position) : ba::map::lowCutHz (k.position);
                for (double hz = 20.0; hz < 20000.0; hz *= 1.02)
                    loudest = std::max (loudest, db (ba::CharacterFilter::magnitude (lowpass, c, fc, reso, hz, 48000.0)));
            }
            INFO (c.name << ": auto resonance " << lo << " .. " << hi << ", largest bump " << loudest << " dB");
            CHECK (lo >= c.autoFrom - 1e-9);
            CHECK (hi <= c.autoTo + 1e-9);
            CHECK (hi - lo > 0.6 * (c.autoTo - c.autoFrom));   // it really moves
            CHECK (loudest < 14.0);
        }

    // A low cut sings more the higher it climbs; a high cut is most resonant mid-way.
    const auto low = [] (float t) { return ba::CutFilters::resonance (false, { t, 0, 0.3f, true }); };
    const auto high = [] (float t) { return ba::CutFilters::resonance (true, { 1.f - t, 0, 0.3f, true }); };
    CHECK (low (0.1f) < low (0.5f));
    CHECK (low (0.5f) < low (0.85f));
    CHECK (high (0.5f) > high (0.1f));
    CHECK (high (0.5f) > high (0.95f));

    // Manual: the knob, whatever the cutoff.
    for (float t : { 0.1f, 0.5f, 0.9f })
        CHECK (ba::CutFilters::resonance (false, { t, 0, 0.62f, false }) == Catch::Approx (0.62));
}

TEST_CASE ("Cut filters: high quality oversamples the whole filter, so it aliases far less", "[filters]")
{
    // Hot sines into resonant, driven filters at 44.1 kHz: everything that is
    // not a harmonic of the tone is aliasing.
    const double fs = 44100.0;
    struct Case { bool lowpass; int type; double cutoff, hz; };
    for (const auto k : { Case { true, 9, 4000.0, 3100.0 }, Case { true, 1, 3000.0, 2900.0 }, Case { false, 1, 2000.0, 2000.0 }, Case { false, 2, 1000.0, 1000.0 } })
    {
        const auto x = sine (k.hz, fs, 65536, 0.5);
        const float low = k.lowpass ? 0.f : ba::map::lowCutNorm (k.cutoff), high = k.lowpass ? ba::map::highCutNorm (k.cutoff) : 1.f;
        const double normal = inharmonic (filtered (x, low, high, fs, k.type, k.type, false), k.hz, fs);
        const double hq = inharmonic (filtered (x, low, high, fs, k.type, k.type, true), k.hz, fs);
        INFO (ba::character (k.lowpass, k.type).name << ": aliasing " << db (normal) << " dB live, " << db (hq) << " dB high quality");
        CHECK (db (hq) < db (normal) - 15.0);
    }
}
