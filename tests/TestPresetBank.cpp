#include "TestUtil.h"
#include "dsp/Svf.h"
#include "fx/LaneEdit.h"
#include "fx/PresetBank.h"
#include "fx/PresetText.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <random>
#include <string>
#include <thread>

using namespace testutil;

namespace
{
    bool sameLane (const ba::Lane& a, const ba::Lane& b)
    {
        if (a.count != b.count || a.step != b.step) return false;
        for (int i = 0; i < a.count; ++i)
        {
            const auto &x = a.keys[(size_t) i], &y = b.keys[(size_t) i];
            if (x.at != y.at || x.value != y.value || x.curve != y.curve) return false;
        }
        return true;
    }

    bool sameSettings (const ba::Settings& a, const ba::Settings& b)
    {
        return a.lowCut == b.lowCut && a.highCut == b.highCut && a.lowCutType == b.lowCutType && a.highCutType == b.highCutType
               && a.lowCutReso == b.lowCutReso && a.highCutReso == b.highCutReso && a.lowCutAuto == b.lowCutAuto
               && a.highCutAuto == b.highCutAuto && a.pitch == b.pitch && a.noise == b.noise && a.noisePitch == b.noisePitch
               && a.noiseLowCut == b.noiseLowCut && a.reverb == b.reverb && a.stereo == b.stereo && a.makeupDb == b.makeupDb
               && a.fadeDb == b.fadeDb;
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

TEST_CASE ("Preset text: every factory preset reads back exactly", "[presets][text]")
{
    for (const auto& p : ba::kPresets)
    {
        INFO (p.name);
        const auto text = ba::toText (p);
        ba::Preset q {};
        REQUIRE (ba::fromText (text, q));
        CHECK (q.lowCutType == p.lowCutType);
        CHECK (q.highCutType == p.highCutType);
        for (const auto id : ba::kLaneIds)
        {
            INFO (ba::laneKey (id));
            CHECK (sameLane (ba::lane (q, id), ba::lane (p, id)));
        }
        for (int i = 0; i <= 1000; ++i)
        {
            const float build = (float) i / 1000.f;
            CHECK (sameSettings (ba::applyPreset (p, build, ba::Settings {}), ba::applyPreset (q, build, ba::Settings {})));
        }
        CHECK (ba::toText (q) == text);
    }
}

TEST_CASE ("Preset text: whatever it is given reads back as a tidy path, or as nothing", "[presets][text]")
{
    ba::Preset p {};
    CHECK (! ba::fromText ("", p));
    CHECK (! ba::fromText ("lane lowCut 0 2  0 0 1 0  1 1 1 0", p));   // no header
    CHECK (! ba::hasLane (p, ba::LaneId::lowCut));

    // Out of range, unsorted, a curve too steep, a cut lane without its type.
    REQUIRE (ba::fromText ("buildapp-path 1\nlowCutType 999\nhighCutType 2\n"
                           "lane lowCut 0 2  0 0 1 0  1 1 1 0\n"
                           "lane highCut 0 3  0.9 0.5 1 0  -4 7 1 0  0.2 0.3 900 -2\n"
                           "lane pitch 1 2  0 0 1 0  1 40 1 0\n"
                           "lane nonsense 0 1  0 0 1 0\n"
                           "lane noise 0 1000  0 0.5\n",
                           p));
    CHECK (p.lowCutType == -1);
    CHECK (! p.lowCut.driven());
    CHECK (p.highCutType == 2);
    REQUIRE (p.highCut.count == 3);
    CHECK (p.highCut.keys[0].at == 0.f);
    CHECK (p.highCut.keys[0].value == 1.f);
    CHECK (p.highCut.keys[1].curve == ba::edit::kMaxCurve);
    CHECK (p.highCut.keys[1].offset == 0.f);
    CHECK (p.pitch.keys[1].value == 12.f);
    CHECK (p.pitch.step == 1.f);
    CHECK (! p.noise.driven());   // its one key was cut short

    // Random bytes and random cuts of a real text never crash, and stay tidy.
    std::mt19937 rng (11);
    const auto real = ba::toText (ba::kPresets[0]);
    for (int i = 0; i < 2000; ++i)
    {
        std::string s = "buildapp-path 1\n" + real.substr (rng() % real.size());
        for (int j = 0; j < 8; ++j) s[rng() % s.size()] = (char) (rng() % 128);
        ba::Preset q {};
        ba::fromText (s, q);
        for (const auto id : ba::kLaneIds)
        {
            const auto& l = ba::lane (q, id);
            const auto r = ba::laneRange (id);
            REQUIRE (l.count <= ba::Lane::kMaxKeys);
            for (int k = 0; k < l.count; ++k)
            {
                REQUIRE ((l.keys[(size_t) k].value >= r.lo && l.keys[(size_t) k].value <= r.hi));
                REQUIRE ((l.keys[(size_t) k].at >= 0.f && l.keys[(size_t) k].at <= 1.f));
                if (k > 0) REQUIRE (l.keys[(size_t) k - 1].at <= l.keys[(size_t) k].at);
            }
        }
    }
}

TEST_CASE ("Preset bank: an empty slot plays nothing; a published preset plays until cleared", "[presets][bank]")
{
    ba::PresetBank bank;
    CHECK (bank.forAudio (0) == nullptr);
    CHECK (bank.forAudio (-1) == nullptr);
    CHECK (bank.forAudio (ba::PresetBank::kSlots) == nullptr);
    ba::Preset out {};
    CHECK (! bank.latest (3, out));

    const auto g0 = bank.generation();
    bank.publish (3, ba::kPresets[1]);
    CHECK (bank.generation() != g0);
    const auto* p = bank.forAudio (3);
    REQUIRE (p != nullptr);
    CHECK (sameLane (p->pitch, ba::kPresets[1].pitch));
    CHECK (bank.latest (3, out));
    CHECK (sameLane (out.pitch, ba::kPresets[1].pitch));
    CHECK (bank.forAudio (4) == nullptr);

    bank.publish (3, ba::kPresets[2]);
    p = bank.forAudio (3);
    REQUIRE (p != nullptr);
    CHECK (sameLane (p->pitch, ba::kPresets[2].pitch));

    bank.clear (3);
    CHECK (bank.forAudio (3) == nullptr);
    CHECK (! bank.latest (3, out));
}

TEST_CASE ("Preset bank: the audio side only ever sees whole presets while the editor publishes", "[presets][bank]")
{
    // Every published preset carries its number in all its keys and in its
    // key count; a torn read would mix two.
    ba::PresetBank bank;
    constexpr int kWrites = 30000;
    std::atomic<bool> done { false };
    std::atomic<int> torn { 0 }, seen { 0 };
    std::thread reader ([&] {
        int last = -1;
        while (! done.load())
        {
            if (const auto* p = bank.forAudio (5))
            {
                const int n = (int) p->lowCut.keys[0].value;
                bool ok = p->lowCut.count == 1 + n % ba::Lane::kMaxKeys && p->lowCutType == n % 7;
                for (int i = 0; i < p->lowCut.count; ++i) ok = ok && p->lowCut.keys[(size_t) i].value == (float) n;
                if (! ok) torn.fetch_add (1);
                if (n != last) { seen.fetch_add (1); last = n; }
            }
        }
    });
    ba::Preset p {};
    for (int n = 0; n < kWrites; ++n)
    {
        p.lowCutType = n % 7;
        p.lowCut.count = 1 + n % ba::Lane::kMaxKeys;
        for (int i = 0; i < p.lowCut.count; ++i) p.lowCut.keys[(size_t) i] = { (float) i / 200.f, (float) n, 1.f };
        bank.publish (5, p);
    }
    done = true;
    reader.join();
    INFO ("the reader saw " << seen.load() << " of " << kWrites << " presets");
    CHECK (torn.load() == 0);
    CHECK (seen.load() > 10);
}

TEST_CASE ("Preset bank: a lane edited while the loop plays glides, without a click", "[presets][bank][click]")
{
    // A 220 Hz sine through the chain playing a user preset (a clean low
    // cut under it, a high cut over it) while its lanes are dragged around
    // the way the editor publishes them: a new version every 20 ms, keys
    // jumping, the high cut switching on and off. Anything above 2 kHz is a click.
    const double fs = 48000.0;
    constexpr int block = 256;
    const auto x = sine (220.0, fs, (size_t) (fs * 2.0), 0.5);
    Signal l = x, r = x;

    ba::Preset user {};
    user.lowCutType = ba::lowType ("Clean 48 / Steep");
    user.highCutType = ba::highType ("Gentle 12 / Default");
    user.lowCut = ba::edit::flat (0.1f);
    user.highCut = ba::edit::flat (1.f);
    ba::PresetBank bank;
    bank.publish (0, user);

    ba::Buildup chain;
    const auto settings = [&] {
        const auto* p = bank.forAudio (0);
        return p != nullptr ? ba::applyPreset (*p, 0.4f, ba::Settings {}) : ba::Settings {};
    };
    chain.set (settings());
    chain.prepare (fs, block);
    const float lows[] = { 0.1f, 0.3f, 0.12f, 0.28f, 0.05f, 0.2f };
    const float highs[] = { 1.f, 0.85f, 0.95f, 1.f, 0.8f, 0.9f };
    int version = 0;
    for (size_t at = 0; at < l.size(); at += block)
    {
        if (at % (size_t) (0.02 * fs) < (size_t) block)
        {
            const int v = version++ % 6;
            ba::edit::move (user.lowCut, 0, 0, 0.f, lows[v] - user.lowCut.keys[0].value, ba::laneRange (ba::LaneId::lowCut));
            ba::edit::drawSteps (user.highCut, std::array { ba::edit::Cell { 0.f, 1.f, highs[v] } }, {});
            bank.publish (0, user);
        }
        chain.set (settings());
        chain.process (l.data() + at, r.data() + at, (int) std::min<size_t> (block, l.size() - at));
    }
    const double worst = above2k (l, fs, 4800);
    INFO ("largest content above 2 kHz: " << db (worst) << " dBFS over " << version << " edits");
    CHECK (db (worst) < -70.0);
}
