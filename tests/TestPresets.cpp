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
    // once per block as the processor does; noise false keeps the noise off.
    Stereo run (const Stereo& x, const ba::Preset& p, const std::function<float (double)>& buildAt, double fs, bool noise = true,
                const ba::Intensity& intensity = {})
    {
        constexpr int block = 480;
        const auto at = [&] (double t) {
            auto s = ba::applyPreset (p, buildAt (t), ba::Settings {}, intensity);
            if (! noise) s.noise = 0.f;
            return s;
        };
        ba::Buildup b;
        b.set (at (0.0));
        b.prepare (fs, block);
        Stereo y = x;
        for (size_t start = 0; start < y.size(); start += (size_t) block)
        {
            b.set (at ((double) start / fs));
            b.process (y.left.data() + start, y.right.data() + start, (int) std::min<size_t> ((size_t) block, y.size() - start));
        }
        return y;
    }

    Stereo runAt (const Stereo& x, const ba::Preset& p, float build, double fs, bool noise = true, const ba::Intensity& intensity = {})
    {
        return run (x, p, [build] (double) { return build; }, fs, noise, intensity);
    }

    // The intensity of each row of the timeline, by name.
    struct IntensityField
    {
        const char* name;
        float ba::Intensity::*field;
    };
    constexpr IntensityField kIntensities[] = {
        { "low cut", &ba::Intensity::lowCut },   { "low reso", &ba::Intensity::lowReso },       { "high cut", &ba::Intensity::highCut },
        { "high reso", &ba::Intensity::highReso }, { "pitch", &ba::Intensity::pitch },          { "noise", &ba::Intensity::noise },
        { "noise pitch", &ba::Intensity::noisePitch }, { "noise low cut", &ba::Intensity::noiseLowCut }, { "reverb", &ba::Intensity::reverb },
        { "stereo", &ba::Intensity::stereo },    { "level", &ba::Intensity::level },
    };

    ba::Intensity all (float k)
    {
        ba::Intensity i;
        for (const auto& f : kIntensities) i.*f.field = k;
        return i;
    }

    // Every control of a Settings with what it is, for comparing two of them.
    std::vector<std::pair<const char*, float>> controls (const ba::Settings& s)
    {
        return { { "low cut", s.lowCut },        { "low type", (float) s.lowCutType },   { "low reso", s.lowCutAuto ? -1.f : s.lowCutReso },
                 { "high cut", s.highCut },      { "high type", (float) s.highCutType }, { "high reso", s.highCutAuto ? -1.f : s.highCutReso },
                 { "pitch", s.pitch },           { "noise", s.noise },                   { "noise pitch", s.noisePitch },
                 { "noise low cut", s.noiseLowCut }, { "reverb", s.reverb },             { "stereo", s.stereo },
                 { "level", s.makeupDb + s.fadeDb }, { "output level", s.levelDb },      { "mix", s.mix } };
    }

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

TEST_CASE ("Presets: at BUILD 0 every preset that rests there passes the loop bit for bit", "[presets]")
{
    // The others start at a setting of their own: Sunrise and the Pumping Up
    // presets under a closed lowpass, Downsweep and Freefall at the hit.
    const double fs = GENERATE (44100.0, 48000.0, 96000.0);
    const auto x = psyLoop (fs, 1.0);
    int resting = 0;
    for (const auto& p : ba::kPresets)
    {
        if (! ba::restsAtZero (p)) continue;
        INFO (p.name << " at " << fs);
        ++resting;
        const auto y = runAt (x, p, 0.f, fs);
        CHECK (x.left == y.left);
        CHECK (x.right == y.right);
    }
    CHECK (resting == 13);
}

TEST_CASE ("Presets: lanes stay in range and only drive what they say", "[presets]")
{
    CHECK (std::size (ba::kPresets) == 19);
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
            if (ba::restsAtZero (p) && ! std::isnan (l.rest)) CHECK (l.lane (0.f) == l.rest);
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

TEST_CASE ("Presets: progressive builds keep the groove and open the lowpass", "[presets]")
{
    // Until the last bar of a 16-bar build the low cut stays under 400 Hz, so
    // the rolling bass is thinned, not gone; a high cut that drops in only
    // opens from there on.
    int progressive = 0;
    for (const auto& p : ba::kPresets)
    {
        if (std::string (p.section) != "Progressive") continue;
        INFO (p.name);
        ++progressive;
        for (int i = 0; i <= 90; ++i)
            CHECK (p.lowCut ((float) i / 100.f) <= 0.5f);
        if (! p.highCut.driven()) continue;
        for (int i = 0; i < 100; ++i)
            CHECK (p.highCut ((float) (i + 1) / 100.f) >= p.highCut ((float) i / 100.f));
    }
    CHECK (progressive == 3);
}

TEST_CASE ("Presets: the Pumping Up presets open a lowpass and hand it over to the low cut", "[presets]")
{
    // The groove pumps up from the bottom: at 0 % the high cut is under
    // 400 Hz, it only opens from there, and it lets go before the top. The low cut takes over without a squeeze (it
    // is still under 50 Hz when the high cut lets go) and without a stall
    // (from a quarter of BUILD on, one of the cuts moves in every 32nd).
    int open = 0;
    for (const auto& p : ba::kPresets)
    {
        if (std::string (p.section) != "Pumping Up") continue;
        INFO (p.name);
        ++open;
        REQUIRE (p.highCut.driven());
        REQUIRE (p.lowCut.driven());
        CHECK (p.highCut (0.f) <= ba::map::highCutNorm (400.0));
        CHECK (! ba::restsAtZero (p));
        float off = 1.f;
        for (int i = 1; i <= 1000; ++i)
        {
            const float b = (float) i / 1000.f;
            CHECK (p.highCut (b) >= p.highCut (b - 0.001f));
            if (ba::map::highCutOff (p.highCut (b))) off = std::min (off, b);
        }
        CHECK (off < 1.f);
        CHECK (p.lowCut (off) <= ba::map::lowCutNorm (50.0));
        CHECK (p.lowCut (1.f) >= ba::map::lowCutNorm (200.0));
        for (int i = 8; i < 32; ++i)
        {
            const float a = (float) i / 32.f, b = (float) (i + 1) / 32.f;
            INFO ("BUILD " << a);
            CHECK (std::abs (p.highCut (b) - p.highCut (a)) + std::abs (p.lowCut (b) - p.lowCut (a)) > 0.01f);
        }
    }
    CHECK (open == 3);
}

TEST_CASE ("Presets: past the path every preset fades towards its reverb tail", "[presets]")
{
    // Up to kPathEnd BUILD plays the preset's path as designed. Then only the
    // reverb moves (up, for the tail), and once it is there the level goes
    // evenly in dB from the preset's gain down to kFadeToDb.
    const auto near = [] (float a, float b) { return std::abs (a - b) < 1e-4f * std::max (1.f, std::abs (b)); };
    for (const auto& p : ba::kPresets)
    {
        INFO (p.name);
        ba::Settings own;
        own.reverb = 0.1f;
        for (int i = 0; i <= 40; ++i)
        {
            const float along = (float) i / 40.f;
            const auto s = ba::applyPreset (p, along * ba::kPathEnd, own);
            CHECK (s.fadeDb == 0.f);
            if (p.lowCut.driven()) CHECK (near (s.lowCut, p.lowCut (along)));
            if (p.highCut.driven()) CHECK (near (s.highCut, p.highCut (along)));
            CHECK (near (s.reverb, p.reverb.driven() ? p.reverb (along) : own.reverb));
            if (p.gain.driven()) CHECK (near (s.makeupDb, p.gain (along)));
        }

        const auto end = ba::applyPreset (p, ba::kPathEnd, own), from = ba::applyPreset (p, ba::kFadeFrom, own);
        CHECK (from.reverb >= std::max (end.reverb, ba::kTailReverb));
        CHECK (from.fadeDb == 0.f);
        const float level = from.makeupDb;
        for (int i = 0; i <= 20; ++i)
        {
            const float down = (float) i / 20.f;
            const auto s = ba::applyPreset (p, ba::kFadeFrom + (1.f - ba::kFadeFrom) * down, own);
            INFO ("fade " << down);
            CHECK (near (s.makeupDb + s.fadeDb, level + (ba::kFadeToDb - level) * down));   // a straight line in dB
            CHECK (s.lowCut == from.lowCut);
            CHECK (s.highCut == from.highCut);
            CHECK (s.pitch == from.pitch);
            CHECK (s.noise == from.noise);
            CHECK (s.reverb == from.reverb);
            CHECK (s.stereo == from.stereo);
        }
        CHECK (ba::drives (p, ba::Control::reverb, 1.f));
    }

    // Held at the top, the chain puts out exactly what it does where the fade
    // starts, that much quieter: the fade sits before the reverb, so the
    // tail follows the music down.
    const double fs = 48000.0;
    const auto x = psyLoop (fs, 4.0);
    const size_t after = (size_t) (2.0 * fs);
    for (const auto& p : ba::kPresets)
    {
        INFO (p.name);
        const auto from = ba::applyPreset (p, ba::kFadeFrom, ba::Settings {}), top = ba::applyPreset (p, 1.f, ba::Settings {});
        const double lu = loudness (runAt (x, p, 1.f, fs), fs, after, x.size()) - loudness (runAt (x, p, ba::kFadeFrom, fs), fs, after, x.size());
        INFO (lu << " LU from the fade's start to the top, " << top.fadeDb << " dB of fade");
        CHECK (std::abs (lu - (double) top.fadeDb) < 0.3);
        CHECK (near (from.makeupDb + top.fadeDb, ba::kFadeToDb));
    }
}

TEST_CASE ("Presets: the drop (BUILD back to 0) gives the loop back bit for bit", "[presets]")
{
    // Four seconds of build to the top, then BUILD straight to 0: within half
    // a second every stage has faded out and the output is the input (for a
    // preset that rests at 0; the others are back at their 0 % setting).
    const double fs = 48000.0;
    const auto x = psyLoop (fs, 5.5);
    for (const auto& p : ba::kPresets)
    {
        INFO (p.name);
        const auto y = run (x, p, [] (double t) { return t < 4.0 ? (float) (t / 4.0) : 0.f; }, fs);
        bool finite = true;
        for (float v : y.left) finite = finite && std::isfinite (v);
        CHECK (finite);
        if (! ba::restsAtZero (p)) continue;
        const auto settled = (long) (4.5 * fs);
        CHECK (std::equal (y.left.begin() + settled, y.left.end(), x.left.begin() + settled));
        CHECK (std::equal (y.right.begin() + settled, y.right.end(), x.right.begin() + settled));
    }
}

TEST_CASE ("Presets: loudness holds through the build, and the peaks stay sane", "[presets][level]")
{
    // BUILD held at each quarter of the path, measured after the reverb has
    // built up.
    // The window comes from the measured spread (buildapp_render: worst
    // -5.2 LU in Squeeze, +1.3 LU in White-Out; held here, -5.9 in Inhale's
    // breath and +1.9 in Sink) plus margin. The test loop
    // is kick and bass heavy, so a cut taking the lows away costs more here
    // than on a full mix: the gain lanes only make up part of it on purpose.
    const double fs = 48000.0;
    const auto x = psyLoop (fs, 4.0);
    const size_t from = (size_t) (2.0 * fs);
    const double dry = loudness (x, fs, from, x.size());
    for (const auto& p : ba::kPresets)
        for (const float along : { 0.f, 0.25f, 0.5f, 0.75f, 1.f })
        {
            const float build = along * ba::kPathEnd;   // the path; past it the music fades on purpose
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

TEST_CASE ("Presets: the noise stays under the music", "[presets][level]")
{
    // A riser sits under the build, not on top of it. BUILD held at 0, a
    // half, three quarters and the end of the path (a downlifter's hit is
    // at 0): the noise alone (a silent input) against
    // the music alone (the noise off), both through the chain. Measured: at
    // most -9.3, -2.2 and -0.1 LU, in White-Out, where the noise is the
    // point (and at the top in Vacuum's slam); Full-On Rise was at about -4,
    // +4 and +9 before its noise came down.
    const double fs = 48000.0;
    const auto x = psyLoop (fs, 4.0);
    const Stereo silence { Signal (x.size(), 0.f), Signal (x.size(), 0.f) };
    const size_t from = (size_t) (2.0 * fs);
    for (const auto& p : ba::kPresets)
    {
        if (! p.noise.driven()) continue;
        for (const auto& [along, most] : { std::pair { 0.f, -6.0 }, { 0.5f, -6.0 }, { 0.75f, -1.5 }, { 1.f, 1.0 } })
        {
            const float build = along * ba::kPathEnd;
            const double lu = loudness (runAt (silence, p, build, fs), fs, from, x.size())
                              - loudness (runAt (x, p, build, fs, false), fs, from, x.size());
            INFO (p.name << " at BUILD " << build << ": noise " << lu << " LU re the music");
            CHECK (lu < most);
        }
    }
}

TEST_CASE ("Presets: no two presets take the same path", "[presets]")
{
    // Where each preset has the chain over the build, as positions along the
    // knobs' travel (pitch over the octave, gain over 12 dB), with the noise
    // pitch and low cut counted only as loud as the noise is. Two presets
    // must differ by at least half of a knob's travel on average, summed
    // over the controls (the closest pair, Acid Scream and Sunrise, measured
    // 0.58).
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

TEST_CASE ("Presets: a lane's intensity takes it from where its control rests to the preset's path", "[presets]")
{
    // 0 is where the control rests by default (the cuts off, no pitch, no
    // noise, the noise band and its low cut at their defaults, auto
    // resonance, 0 dB), 1 the preset as designed, halfway is halfway. Pitch
    // stairs stay on whole semitones. Without a reverb lane only the tail
    // scales, on top of your own reverb.
    const auto near = [] (float a, float b) { return std::abs (a - b) < 1e-5f * std::max (1.f, std::abs (b)); };
    const auto mid = [] (float base, float v) { return base + (v - base) * 0.5f; };
    const ba::Settings rest;
    ba::Settings own;
    own.reverb = 0.1f;
    for (const auto& p : ba::kPresets)
    {
        INFO (p.name);
        const float reverbBase = p.reverb.driven() ? rest.reverb : own.reverb;
        for (int i = 0; i <= 40; ++i)
        {
            const float build = (float) i / 40.f;
            INFO ("BUILD " << build);
            const auto full = ba::applyPreset (p, build, own), none = ba::applyPreset (p, build, own, ba::Intensity::none()),
                       half = ba::applyPreset (p, build, own, all (0.5f));
            CHECK (ba::applyPreset (p, build, own, all (1.f)).lowCut == full.lowCut);

            if (p.lowCut.driven())
            {
                CHECK ((none.lowCut == rest.lowCut && none.lowCutAuto && none.lowCutType == full.lowCutType));
                CHECK (near (half.lowCut, mid (rest.lowCut, full.lowCut)));
            }
            if (p.highCut.driven())
            {
                CHECK ((none.highCut == rest.highCut && none.highCutAuto && none.highCutType == full.highCutType));
                CHECK (near (half.highCut, mid (rest.highCut, full.highCut)));
            }
            for (const bool lowpass : { false, true })
            {
                if (! (lowpass ? p.highReso : p.lowReso).driven()) continue;
                INFO ((lowpass ? "high reso" : "low reso"));
                auto knobs = lowpass ? half.highCutKnobs() : half.lowCutKnobs();
                CHECK (! knobs.autoReso);
                knobs.autoReso = true;
                const float a = (float) ba::CutFilters::resonance (lowpass, knobs);
                CHECK (near (lowpass ? half.highCutReso : half.lowCutReso, mid (a, lowpass ? full.highCutReso : full.lowCutReso)));
            }
            if (p.pitch.driven())
            {
                CHECK (none.pitch == 0.f);
                if (p.pitch.step > 0.f) CHECK (half.pitch == std::floor (full.pitch * 0.5f + 1e-4f));
                else CHECK (near (half.pitch, 0.5f * full.pitch));
            }
            if (p.noise.driven())
            {
                // The noise in amplitude: half is 6 dB down.
                CHECK (none.noise == 0.f);
                CHECK (std::abs (ba::map::noiseGain (half.noise) - 0.5 * ba::map::noiseGain (full.noise)) < 1e-6);
            }
            const std::pair<const ba::Lane*, float ba::Settings::*> plain[] = {
                { &p.noisePitch, &ba::Settings::noisePitch },
                { &p.noiseLowCut, &ba::Settings::noiseLowCut }, { &p.stereo, &ba::Settings::stereo }
            };
            for (const auto& [lane, field] : plain)
            {
                if (! lane->driven()) continue;
                CHECK (none.*field == rest.*field);
                CHECK (near (half.*field, mid (rest.*field, full.*field)));
            }
            CHECK (none.reverb == reverbBase);
            CHECK (near (half.reverb, mid (reverbBase, full.reverb)));
            CHECK ((none.makeupDb == 0.f && none.fadeDb == 0.f));
            CHECK (near (half.makeupDb + half.fadeDb, 0.5f * (full.makeupDb + full.fadeDb)));
        }
    }
}

TEST_CASE ("Presets: each intensity moves its own lane and nothing else", "[presets]")
{
    ba::Settings own;
    own.reverb = 0.1f;
    for (const auto& p : ba::kPresets)
        for (const auto& f : kIntensities)
        {
            INFO (p.name << ", " << f.name << " at 30 %");
            ba::Intensity k;
            k.*f.field = 0.3f;
            bool moved = false;
            for (int i = 0; i <= 40; ++i)
            {
                const float build = (float) i / 40.f;
                const auto a = controls (ba::applyPreset (p, build, own)), b = controls (ba::applyPreset (p, build, own, k));
                for (size_t c = 0; c < a.size(); ++c)
                {
                    if (std::string (a[c].first) == f.name) moved = moved || a[c].second != b[c].second;
                    else CHECK (a[c].second == b[c].second);
                }
            }
            // Every lane the timeline shows answers: the preset's own, and the
            // reverb and level rows every preset ends in (the tail, the fade).
            const std::pair<const char*, const ba::Lane*> lanesByName[] = {
                { "low cut", &p.lowCut }, { "low reso", &p.lowReso }, { "high cut", &p.highCut }, { "high reso", &p.highReso },
                { "pitch", &p.pitch }, { "noise", &p.noise }, { "noise pitch", &p.noisePitch }, { "noise low cut", &p.noiseLowCut },
                { "stereo", &p.stereo }
            };
            bool hasLane = std::string (f.name) == "reverb" || std::string (f.name) == "level";
            for (const auto& [name, lane] : lanesByName)
                if (std::string (name) == f.name) hasLane = lane->driven();
            CHECK (moved == hasLane);
        }
}

TEST_CASE ("Presets: at any intensity a preset that rests at BUILD 0 still passes the loop bit for bit", "[presets]")
{
    const ba::Intensity mixed { 0.3f, 0.7f, 0.f, 1.f, 0.5f, 0.2f, 0.9f, 0.6f, 0.4f, 0.8f, 0.1f };
    for (const auto& p : ba::kPresets)
    {
        if (! ba::restsAtZero (p)) continue;
        INFO (p.name);
        for (const auto& k : { ba::Intensity::none(), all (0.5f), mixed })
        {
            const auto s = ba::applyPreset (p, 0.f, ba::Settings {}, k);
            CHECK ((ba::map::lowCutOff (s.lowCut) && ba::map::highCutOff (s.highCut) && s.pitch == 0.f && s.noise == 0.f
                    && s.reverb == 0.f && s.stereo == 0.f && s.makeupDb == 0.f && s.fadeDb == 0.f));
        }
    }
    const double fs = 48000.0;
    const auto x = psyLoop (fs, 1.0);
    for (const auto& p : ba::kPresets)
    {
        if (! ba::restsAtZero (p)) continue;
        INFO (p.name);
        const auto y = runAt (x, p, 0.f, fs, true, mixed);
        CHECK (x.left == y.left);
        CHECK (x.right == y.right);
    }
}

TEST_CASE ("Presets: turning an intensity down glides; what the chain is told never jumps", "[presets][click]")
{
    // The chain glides every knob it is told (tested with the stages); here
    // the knobs themselves move evenly with the intensity, all lanes at once
    // from 100 % to 0 in steps of 0.1 %, resonance handing over to auto
    // included (the noise in amplitude, the way the noise source glides it).
    // Only pitch stairs step, a semitone at a time, as they do along BUILD.
    constexpr int kSteps = 1000;
    const auto reso = [] (bool lowpass, const ba::Settings& s) {
        return (float) ba::CutFilters::resonance (lowpass, lowpass ? s.highCutKnobs() : s.lowCutKnobs());
    };
    for (const auto& p : ba::kPresets)
        for (const float build : { 0.3f, 0.6f, 0.9f })
        {
            INFO (p.name << " at BUILD " << build);
            auto last = ba::applyPreset (p, build, ba::Settings {});
            float worstKnob = 0.f, worstDb = 0.f, worstPitch = 0.f;
            for (int i = kSteps - 1; i >= 0; --i)
            {
                const auto s = ba::applyPreset (p, build, ba::Settings {}, all ((float) i / kSteps));
                const auto gain = [] (const ba::Settings& x) { return (float) ba::map::noiseGain (x.noise); };
                for (const auto& [a, b] : { std::pair { s.lowCut, last.lowCut }, { s.highCut, last.highCut }, { gain (s), gain (last) },
                                            { s.noisePitch, last.noisePitch }, { s.noiseLowCut, last.noiseLowCut }, { s.reverb, last.reverb },
                                            { s.stereo, last.stereo }, { reso (false, s), reso (false, last) }, { reso (true, s), reso (true, last) } })
                    worstKnob = std::max (worstKnob, std::abs (a - b));
                worstDb = std::max (worstDb, std::abs (s.makeupDb + s.fadeDb - last.makeupDb - last.fadeDb));
                worstPitch = std::max (worstPitch, std::abs (s.pitch - last.pitch));
                CHECK ((s.lowCutType == last.lowCutType && s.highCutType == last.highCutType));
                last = s;
            }
            INFO ("largest step: " << worstKnob << " of a knob, " << worstDb << " dB, " << worstPitch << " st");
            CHECK (worstKnob < 0.003f);
            CHECK (worstDb < 0.03f);
            CHECK (worstPitch <= (p.pitch.step > 0.f ? p.pitch.step : 0.015f));
        }
}
