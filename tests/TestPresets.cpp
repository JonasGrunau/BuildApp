#include "TestUtil.h"
#include "fx/Presets.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <functional>
#include <limits>
#include <string>

using namespace testutil;

namespace
{
    // The loop through a chain whose BUILD follows buildAt (seconds), set
    // once per block as the processor does.
    Stereo run (const Stereo& x, const ba::Preset& p, const std::function<float (double)>& buildAt, double fs, int block = 480)
    {
        ba::Buildup b;
        const ba::Settings own;
        b.set (ba::applyPreset (p, buildAt (0.0), own));
        b.prepare (fs, block);
        Stereo y = x;
        for (size_t start = 0; start < y.size(); start += (size_t) block)
        {
            b.set (ba::applyPreset (p, buildAt ((double) start / fs), own));
            b.process (y.left.data() + start, y.right.data() + start, (int) std::min<size_t> ((size_t) block, y.size() - start));
        }
        return y;
    }

    Stereo runAt (const Stereo& x, const ba::Preset& p, float build, double fs) { return run (x, p, [build] (double) { return build; }, fs); }

    // Every lane with where its control rests (NaN: no rest position, the
    // control only matters once another one is up) and its range.
    struct LaneInfo
    {
        const char* name;
        const ba::Lane& lane;
        float rest, lo, hi;
    };

    std::vector<LaneInfo> lanes (const ba::Preset& p)
    {
        const float none = std::numeric_limits<float>::quiet_NaN();
        return { { "low cut", p.lowCut, 0.f, 0.f, 1.f },     { "low reso", p.lowReso, none, 0.f, 1.f },
                 { "high cut", p.highCut, 1.f, 0.f, 1.f },   { "high reso", p.highReso, none, 0.f, 1.f },
                 { "pitch", p.pitch, 0.f, 0.f, 12.f },       { "noise", p.noise, 0.f, 0.f, 1.f },
                 { "noise pitch", p.noisePitch, none, 0.f, 1.f }, { "noise low cut", p.noiseLowCut, none, 0.f, 1.f },
                 { "reverb", p.reverb, 0.f, 0.f, 1.f },      { "stereo", p.stereo, 0.f, 0.f, 1.f },
                 { "gain", p.gain, 0.f, -12.f, 12.f } };
    }
} // namespace

TEST_CASE ("Presets: at BUILD 0 every preset passes the loop bit for bit", "[presets]")
{
    const double fs = GENERATE (44100.0, 48000.0, 96000.0);
    const auto x = psyLoop (fs, 1.0);
    for (const auto& p : ba::kPresets)
    {
        INFO (p.name << " at " << fs);
        const auto y = runAt (x, p, 0.f, fs);
        CHECK (x.left == y.left);
        CHECK (x.right == y.right);
    }
}

TEST_CASE ("Presets: lanes start at rest, stay in range, and only drive what they say", "[presets]")
{
    CHECK (std::size (ba::kPresets) == 10);
    for (const auto& p : ba::kPresets)
    {
        INFO (p.name);
        CHECK (std::string (p.help).size() > 40);
        CHECK (std::string (p.name).size() + std::string (p.help).size() <= 98);   // fits the editor's footer
        CHECK ((p.lowCutType >= 0) == p.lowCut.driven());
        CHECK ((p.highCutType >= 0) == p.highCut.driven());
        CHECK ((! p.lowReso.driven() || p.lowCut.driven()));
        CHECK ((! p.highReso.driven() || p.highCut.driven()));
        int driven = 0;
        for (const auto& l : lanes (p))
        {
            if (! l.lane.driven()) continue;
            INFO (l.name);
            ++driven;
            const auto keys = l.lane.points();
            if (! std::isnan (l.rest)) CHECK (l.lane (0.f) == l.rest);
            for (size_t i = 0; i < keys.size(); ++i)
            {
                CHECK (keys[i].at >= 0.f);
                CHECK (keys[i].at <= 1.f);
                CHECK (keys[i].value >= l.lo);
                CHECK (keys[i].value <= l.hi);
                CHECK (keys[i].curve > 0.f);
                if (i > 0) CHECK (keys[i].at > keys[i - 1].at);
            }
        }
        CHECK (driven >= 3);

        // Output level and dry/wet stay the user's, and so does every control
        // the preset has no lane for.
        ba::Settings own;
        own.levelDb = -7.f;
        own.mix = 0.4f;
        own.pitch = 3.f;
        own.noise = 0.2f;
        own.noisePitch = 0.33f;
        own.noiseLowCut = 0.44f;
        own.reverb = 0.25f;
        own.stereo = 0.15f;
        own.lowCut = 0.3f;
        own.highCut = 0.7f;
        own.lowCutType = own.highCutType = 3;
        const auto s = ba::applyPreset (p, 0.7f, own);
        CHECK (s.levelDb == own.levelDb);
        CHECK (s.mix == own.mix);
        using C = ba::Control;
        if (! drives (p, C::pitch)) CHECK (s.pitch == own.pitch);
        if (! drives (p, C::noise)) CHECK (s.noise == own.noise);
        if (! drives (p, C::noisePitch)) CHECK (s.noisePitch == own.noisePitch);
        if (! drives (p, C::noiseLowCut)) CHECK (s.noiseLowCut == own.noiseLowCut);
        if (! drives (p, C::reverb)) CHECK (s.reverb == own.reverb);
        if (! drives (p, C::stereo)) CHECK (s.stereo == own.stereo);
        if (! drives (p, C::lowCut)) CHECK ((s.lowCut == own.lowCut && s.lowCutType == own.lowCutType));
        if (! drives (p, C::highCut)) CHECK ((s.highCut == own.highCut && s.highCutType == own.highCutType));
    }
}

TEST_CASE ("Presets: lanes hold outside their keys, bend with their curve, and stairs step", "[presets]")
{
    const ba::Lane bent { { 0.f, 0.f }, { 0.5f, 0.f }, { 1.f, 1.f, 2.f } };
    CHECK (bent (0.25f) == 0.f);
    CHECK (std::abs (bent (0.75f) - 0.25f) < 1e-6f);   // halfway along the bent segment: 0.5^2
    CHECK (bent (1.5f) == 1.f);

    const auto& stairs = ba::kPresets[2].pitch;
    CHECK (stairs.step == 1.f);
    for (int k = 1; k <= 12; ++k)
    {
        const float edge = 0.1875f + (float) k / 16.f;   // bar 4 + k of a 16-bar build
        INFO ("step " << k);
        CHECK (stairs (edge - 0.002f) == (float) (k - 1));
        CHECK (stairs (edge + 0.002f) == (float) k);
    }
    CHECK (stairs (1.f) == 12.f);
}

TEST_CASE ("Presets: the drop (BUILD back to 0) gives the loop back bit for bit", "[presets]")
{
    // Four seconds of build to the top, then BUILD straight to 0: within half
    // a second every stage has faded out and the output is the input.
    const double fs = 48000.0;
    const auto x = psyLoop (fs, 5.5);
    for (const auto& p : ba::kPresets)
    {
        INFO (p.name);
        const auto y = run (x, p, [] (double t) { return t < 4.0 ? (float) (t / 4.0) : 0.f; }, fs);
        bool finite = true;
        for (float v : y.left) finite = finite && std::isfinite (v);
        CHECK (finite);
        const auto settled = (long) (4.5 * fs);
        CHECK (std::equal (y.left.begin() + settled, y.left.end(), x.left.begin() + settled));
        CHECK (std::equal (y.right.begin() + settled, y.right.end(), x.right.begin() + settled));
    }
}

TEST_CASE ("Presets: loudness holds through the build, and the peaks stay sane", "[presets][level]")
{
    // BUILD held at each quarter, measured after the reverb has built up.
    // The window comes from the measured spread (buildapp_render: worst
    // -5.2 LU in Squeeze, +1.3 LU in White-Out; held here, -5.1 and +1.3)
    // plus margin. The test loop
    // is kick and bass heavy, so a cut taking the lows away costs more here
    // than on a full mix: the gain lanes only make up part of it on purpose.
    const double fs = 48000.0;
    const auto x = psyLoop (fs, 4.0);
    const size_t from = (size_t) (2.0 * fs);
    const double dry = loudness (x, fs, from, x.size());
    for (const auto& p : ba::kPresets)
        for (const float build : { 0.25f, 0.5f, 0.75f, 1.f })
        {
            const auto y = runAt (x, p, build, fs);
            const double lu = loudness (y, fs, from, y.size()) - dry;
            double peak = 0.0;
            for (size_t i = from; i < y.size(); ++i) peak = std::max ({ peak, (double) std::abs (y.left[i]), (double) std::abs (y.right[i]) });
            INFO (p.name << " at BUILD " << build << ": " << lu << " LU re dry, peak " << db (peak) << " dBFS");
            CHECK (lu > -6.5);
            CHECK (lu < 2.5);
            CHECK (db (peak) < 3.0);
        }
}

TEST_CASE ("Presets: no two presets take the same path", "[presets]")
{
    // Where each preset has the chain over the build, as positions along the
    // knobs' travel (pitch over the octave, gain over 12 dB), with the noise
    // pitch and low cut counted only as loud as the noise is. Two presets
    // must differ by at least half of a knob's travel on average, summed
    // over the controls (the closest pair, Full-On Rise and Rocket, measured
    // 0.72).
    const auto path = [] (const ba::Preset& p) {
        std::vector<std::array<float, 9>> out;
        for (int i = 1; i <= 20; ++i)
        {
            const auto s = ba::applyPreset (p, (float) i / 20.f, ba::Settings {});
            out.push_back ({ s.lowCut, 1.f - s.highCut, s.pitch / 12.f, s.noise, s.noise * s.noisePitch, s.noise * s.noiseLowCut,
                             s.reverb, s.stereo, s.makeupDb / 12.f });
        }
        return out;
    };
    double closest = 1e9;
    for (size_t i = 0; i < std::size (ba::kPresets); ++i)
        for (size_t j = i + 1; j < std::size (ba::kPresets); ++j)
        {
            const auto a = path (ba::kPresets[i]), b = path (ba::kPresets[j]);
            double d = 0.0;
            for (size_t t = 0; t < a.size(); ++t)
                for (size_t k = 0; k < a[t].size(); ++k) d += std::abs (a[t][k] - b[t][k]);
            d /= (double) a.size();
            closest = std::min (closest, d);
            INFO (ba::kPresets[i].name << " vs " << ba::kPresets[j].name << ": " << d);
            CHECK (d > 0.5);
        }
    INFO ("closest pair: " << closest);
    CHECK (closest > 0.5);
}
