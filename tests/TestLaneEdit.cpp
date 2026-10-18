#include "fx/LaneEdit.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <functional>
#include <random>
#include <vector>

namespace edit = ba::edit;
using ba::Key;
using ba::Lane;
using ba::LaneRange;

namespace
{
    constexpr LaneRange kUnit {};

    // The largest difference between two lanes over t0..t1 (what plays).
    float maxDiff (const Lane& a, const Lane& b, float t0 = 0.f, float t1 = 1.f)
    {
        float worst = 0.f;
        for (int i = 0; i <= 2000; ++i)
        {
            const float t = t0 + (t1 - t0) * (float) i / 2000.f;
            worst = std::max (worst, std::abs (edit::valueAfter (a, t) - edit::valueAfter (b, t)));
        }
        return worst;
    }

    float maxDiff (const Lane& a, const std::function<float (float)>& f, float t0, float t1)
    {
        float worst = 0.f;
        for (int i = 0; i <= 2000; ++i)
        {
            const float t = t0 + (t1 - t0) * (float) i / 2000.f;
            worst = std::max (worst, std::abs (edit::valueAfter (a, t) - f (t)));
        }
        return worst;
    }

    // What every edit has to leave behind (LaneEdit.h).
    void checkTidy (const Lane& l, LaneRange r)
    {
        REQUIRE (l.count <= Lane::kMaxKeys);
        REQUIRE (l.count >= 0);
        for (int i = 0; i < l.count; ++i)
        {
            const auto& k = l.keys[(size_t) i];
            INFO ("key " << i << " of " << l.count << ": " << k.at << " " << k.value << " " << k.curve);
            REQUIRE (std::isfinite (k.at));
            REQUIRE (std::isfinite (k.value));
            REQUIRE (std::isfinite (k.curve));
            REQUIRE ((std::isfinite (k.offset) && k.offset >= 0.f));
            REQUIRE ((k.at >= 0.f && k.at <= 1.f));
            REQUIRE ((k.value >= r.lo && k.value <= r.hi));
            REQUIRE ((k.curve >= edit::kMinCurve && k.curve <= edit::kMaxCurve));
            if (i > 0) REQUIRE (l.keys[(size_t) i - 1].at <= k.at);
            if (i > 1) REQUIRE (! (l.keys[(size_t) i - 2].at == k.at && l.keys[(size_t) i - 1].at == k.at));
        }
    }
} // namespace

TEST_CASE ("Lane edit: a click on the line adds a key without changing what plays", "[edit]")
{
    const Lane straight { { 0.f, 0.f }, { 0.4f, 0.8f }, { 1.f, 0.2f } };
    auto l = straight;
    const int i = edit::insert (l, 0.23f, kUnit);
    CHECK (i == 1);
    CHECK (l.count == 4);
    CHECK (maxDiff (l, straight) < 1e-6f);
    CHECK (edit::insert (l, 0.23f, kUnit) == 1);   // on a key's time: that key
    CHECK (l.count == 4);

    // On a bent segment too, however steep, and again on the parts it made.
    for (const float c : { 0.1f, 0.3f, 0.5f, 2.2f, 4.f, 10.f })
    {
        INFO ("curve " << c);
        const Lane bent { { 0.f, 0.f }, { 1.f, 1.f, c } };
        for (const float at : { 0.05f, 0.3f, 0.6f, 0.95f })
        {
            auto e = bent;
            edit::insert (e, at, kUnit);
            CHECK (maxDiff (e, bent) < 1e-5f);
            edit::insert (e, 0.5f * (at + 1.f), kUnit);
            edit::insert (e, 0.5f * at, kUnit);
            CHECK (maxDiff (e, bent) < 1e-5f);
        }
    }
    // Off the line (a double-click): the key goes where it was asked.
    auto off = straight;
    const int j = edit::insert (off, 0.7f, 0.9f, kUnit);
    CHECK (off.keys[(size_t) j].at == 0.7f);
    CHECK (off.keys[(size_t) j].value == 0.9f);
    // A click on a key removes it.
    edit::remove (off, j);
    CHECK (maxDiff (off, straight) < 1e-6f);
}

TEST_CASE ("Lane edit: a dragged key removes the ones it passes, and gives them back on the way back", "[edit]")
{
    const Lane start { { 0.f, 0.f }, { 0.3f, 0.5f }, { 0.5f, 0.2f }, { 1.f, 1.f } };

    // Replayed on the lane as the drag began, as the editor does.
    auto l = start;
    int i = edit::move (l, 1, 1, 0.3f, 0.f, kUnit);   // over the key at 0.5
    CHECK (l.count == 3);
    CHECK (l.keys[(size_t) i].at == 0.6f);
    l = start;
    i = edit::move (l, 1, 1, 0.1f, 0.1f, kUnit);   // back short of it
    CHECK (l.count == 4);
    CHECK (std::abs (l.keys[(size_t) i].at - 0.4f) < 1e-6f);
    CHECK (std::abs (l.keys[(size_t) i].value - 0.6f) < 1e-6f);

    // Landing on a neighbour's time makes a step there.
    l = start;
    i = edit::move (l, 1, 1, 0.2f, 0.f, kUnit);
    CHECK (l.count == 4);
    CHECK (edit::valueBefore (l, 0.5f) == 0.5f);
    CHECK (edit::valueAfter (l, 0.5f) == 0.2f);

    // A run moves as one and stays on the path; values clip one by one.
    l = start;
    i = edit::move (l, 1, 2, 0.9f, 0.6f, kUnit);
    CHECK (l.keys[(size_t) i + 1].at == 1.f);   // the run's end stops at 1
    CHECK (l.keys[(size_t) i].value == 1.f);
    CHECK (std::abs (l.keys[(size_t) i + 1].value - 0.8f) < 1e-6f);
    checkTidy (l, kUnit);

    // Onto the start: the moved key replaces what was there.
    l = start;
    i = edit::move (l, 1, 1, -0.3f, 0.f, kUnit);
    CHECK (i == 0);
    CHECK (l.keys[0].value == 0.5f);
    CHECK (l.count == 3);
}

TEST_CASE ("Lane edit: times snap to the grid and to the neighbours, unless the snap is off", "[edit]")
{
    const float none[] = { 0.f };
    CHECK (edit::snap (0.13f, 1.f / 16.f, {}, 0.01f) == 0.125f);
    CHECK (edit::snap (0.14f, 1.f / 16.f, {}, 0.01f) == 0.14f);   // too far from a line
    const float neighbour[] = { 0.131f };
    CHECK (edit::snap (0.13f, 1.f / 16.f, neighbour, 0.01f) == 0.131f);   // closer than the grid line
    CHECK (edit::snap (0.13f, 1.f / 16.f, none, 0.f) == 0.13f);           // bypassed
}

TEST_CASE ("Lane edit: a bend passes through the mouse, and straightens back", "[edit]")
{
    Lane up { { 0.f, 0.f }, { 1.f, 1.f } };
    edit::bend (up, 1, 0.5f, 0.25f);
    CHECK (std::abs (up.keys[1].curve - 2.f) < 1e-4f);
    CHECK (std::abs (edit::valueAfter (up, 0.5f) - 0.25f) < 1e-4f);
    edit::bend (up, 1, 0.3f, 0.7f);
    CHECK (up.keys[1].curve < 1.f);
    CHECK (std::abs (edit::valueAfter (up, 0.3f) - 0.7f) < 1e-4f);

    Lane down { { 0.f, 1.f }, { 1.f, 0.f } };
    edit::bend (down, 1, 0.5f, 0.75f);
    CHECK (std::abs (edit::valueAfter (down, 0.5f) - 0.75f) < 1e-4f);

    // Past the corner it stops at the steepest curve; flat lines don't bend.
    edit::bend (up, 1, 0.5f, -3.f);
    CHECK (up.keys[1].curve == edit::kMaxCurve);
    Lane flat { { 0.f, 0.4f }, { 1.f, 0.4f } };
    edit::bend (flat, 1, 0.5f, 0.9f);
    CHECK (flat.keys[1].curve == 1.f);

    edit::straighten (up, 1);
    CHECK (up.keys[1].curve == 1.f);
}

TEST_CASE ("Lane edit: Draw Mode draws grid steps, freehand draws the stroke", "[edit]")
{
    const Lane ramp { { 0.f, 0.f }, { 1.f, 1.f } };
    auto l = ramp;
    const edit::Cell cells[] = { { 0.25f, 0.3125f, 0.2f }, { 0.3125f, 0.375f, 0.2f }, { 0.375f, 0.4375f, 0.6f }, { 0.4375f, 0.5f, 0.8f } };
    edit::drawSteps (l, cells, kUnit);
    for (const auto& c : cells) CHECK (edit::valueAfter (l, 0.5f * (c.from + c.to)) == c.value);
    CHECK (maxDiff (l, ramp, 0.f, 0.249f) < 1e-6f);
    CHECK (maxDiff (l, ramp, 0.501f, 1.f) < 1e-6f);
    CHECK (l.count == 2 + 6 + 2);   // the edges, then three steps (two cells merged)
    checkTidy (l, kUnit);

    // Steps one 1/64 wide across the whole path fill the lane exactly.
    l = ramp;
    std::vector<edit::Cell> all;
    for (int i = 0; i < 64; ++i) all.push_back ({ (float) i / 64.f, (float) (i + 1) / 64.f, (float) ((i * 37) % 64) / 64.f });
    edit::drawSteps (l, all, kUnit);
    CHECK (l.count <= Lane::kMaxKeys);
    for (const auto& c : all) CHECK (edit::valueAfter (l, 0.5f * (c.from + c.to)) == c.value);
    checkTidy (l, kUnit);

    // Freehand: a few hundred mouse points become a handful of bent keys
    // within the tolerance.
    const auto f = [] (float t) { return 0.5f + 0.4f * std::sin (6.f * t); };
    std::vector<Key> stroke;
    for (int i = 0; i <= 500; ++i)
    {
        const float t = 0.2f + 0.6f * (float) i / 500.f;
        stroke.push_back ({ t, f (t), 1.f });
    }
    std::shuffle (stroke.begin(), stroke.end(), std::mt19937 (3));   // any order
    l = ramp;
    edit::drawFreehand (l, stroke, 0.004f, kUnit);
    INFO (l.count << " keys");
    CHECK (l.count < 40);
    CHECK (maxDiff (l, f, 0.2f, 0.7999f) < 0.01f);   // the stroke's end steps back to the lane
    CHECK (maxDiff (l, ramp, 0.f, 0.19f) < 1e-6f);
    checkTidy (l, kUnit);
}

TEST_CASE ("Lane edit: erase, copy, paste and duplicate leave the rest alone", "[edit]")
{
    const Lane lines { { 0.f, 0.1f }, { 0.2f, 0.9f }, { 0.45f, 0.3f }, { 0.7f, 0.7f }, { 1.f, 0.2f } };
    auto l = lines;
    edit::erase (l, 0.3f, 0.6f, kUnit);
    CHECK (maxDiff (l, lines, 0.f, 0.3f) < 1e-6f);
    CHECK (maxDiff (l, lines, 0.6f, 1.f) < 1e-6f);
    const float a = edit::valueAfter (lines, 0.3f), z = edit::valueAfter (lines, 0.6f);
    CHECK (std::abs (edit::valueAfter (l, 0.45f) - 0.5f * (a + z)) < 1e-5f);   // a line between the edges

    // Bent segments cut at the edges stay exactly as they were outside.
    const Lane bent { { 0.f, 0.f }, { 0.5f, 1.f, 2.f }, { 1.f, 0.f, 0.5f } };
    l = bent;
    edit::erase (l, 0.3f, 0.6f, kUnit);
    CHECK (maxDiff (l, bent, 0.f, 0.3f) < 1e-5f);
    CHECK (maxDiff (l, bent, 0.6f, 1.f) < 1e-5f);

    // Copy, paste elsewhere: the pasted span plays the copied one.
    l = lines;
    const auto clip = edit::copy (l, 0.1f, 0.4f, kUnit);
    CHECK (std::abs (clip.length - 0.3f) < 1e-6f);
    edit::paste (l, clip, 0.6f, kUnit);
    for (int i = 0; i < 100; ++i)   // up to the pasted span's end (a step back to what follows)
    {
        const float t = 0.1f + 0.3f * (float) i / 100.f;
        CHECK (std::abs (edit::valueAfter (l, t + 0.5f) - edit::valueAfter (lines, t)) < 1e-4f);
    }
    CHECK (maxDiff (l, lines, 0.f, 0.599f) < 1e-6f);

    // Past the path's end the paste is cut.
    l = lines;
    edit::paste (l, clip, 0.85f, kUnit);
    CHECK (std::abs (edit::valueAfter (l, 1.f) - edit::valueAfter (lines, 0.25f)) < 1e-4f);
    checkTidy (l, kUnit);

    // Into another lane: through each lane's range (half the unit lane is 6 st).
    Lane pitch = edit::flat (0.f);
    const auto half = edit::copy (edit::flat (0.5f), 0.f, 0.5f, kUnit);
    edit::paste (pitch, half, 0.25f, ba::laneRange (ba::LaneId::pitch));
    CHECK (std::abs (edit::valueAfter (pitch, 0.5f) - 6.f) < 1e-5f);
    CHECK (edit::valueAfter (pitch, 0.1f) == 0.f);

    // Duplicate: the copy right after the selection.
    l = lines;
    const auto sel = edit::copy (l, 0.2f, 0.45f, kUnit);
    edit::paste (l, sel, 0.45f, kUnit);
    CHECK (std::abs (edit::valueAfter (l, 0.55f) - edit::valueAfter (lines, 0.3f)) < 1e-4f);
}

TEST_CASE ("Lane edit: dragging a segment in a selection moves that part as one", "[edit]")
{
    auto l = edit::flat (0.5f);
    edit::shiftRange (l, 0.25f, 0.5f, 0.2f, kUnit);
    CHECK (std::abs (edit::valueAfter (l, 0.3f) - 0.7f) < 1e-6f);
    CHECK (edit::valueBefore (l, 0.25f) == 0.5f);
    CHECK (std::abs (edit::valueAfter (l, 0.25f) - 0.7f) < 1e-6f);
    CHECK (edit::valueAfter (l, 0.6f) == 0.5f);
    edit::shiftRange (l, 0.25f, 0.5f, 5.f, kUnit);   // clips at the top
    CHECK (edit::valueAfter (l, 0.3f) == 1.f);
}

TEST_CASE ("Lane edit: the selection's handles stretch and skew", "[edit]")
{
    const Lane ramp { { 0.f, 0.f }, { 1.f, 1.f } };
    auto [lo, hi] = edit::extent (ramp, 0.25f, 0.75f);
    CHECK (std::abs (lo - 0.25f) < 1e-6f);
    CHECK (std::abs (hi - 0.75f) < 1e-6f);

    // Top handle up: the box 0.25..0.75 becomes 0.25..1.
    auto l = ramp;
    edit::transform (l, 0.25f, 0.75f, lo, hi, { 0.25f, 1.f, 0.25f, 1.f }, kUnit);
    CHECK (std::abs (edit::valueAfter (l, 0.5f) - 0.625f) < 1e-5f);
    CHECK (std::abs (edit::valueBefore (l, 0.75f) - 1.f) < 1e-5f);
    CHECK (maxDiff (l, ramp, 0.f, 0.25f) < 1e-6f);

    // Top-right corner down to the bottom: the right edge flattens.
    l = ramp;
    edit::transform (l, 0.25f, 0.75f, lo, hi, { 0.25f, 0.75f, 0.25f, 0.25f }, kUnit);
    CHECK (std::abs (edit::valueBefore (l, 0.75f) - 0.25f) < 1e-5f);
    CHECK (std::abs (edit::valueAfter (l, 0.25f) - 0.25f) < 1e-5f);

    // Side handles: squeeze 0..0.5 into 0..0.25; the rest stays.
    const Lane peak { { 0.f, 0.f }, { 0.25f, 1.f }, { 0.5f, 0.f }, { 0.75f, 0.5f }, { 1.f, 0.f } };
    l = peak;
    edit::stretchTime (l, 0.f, 0.5f, 0.f, 0.25f, false, kUnit);
    CHECK (std::abs (edit::valueAfter (l, 0.125f) - 1.f) < 1e-5f);
    CHECK (edit::valueAfter (l, 0.75f) == 0.5f);
    // Stretched over a key outside: it goes.
    l = peak;
    edit::stretchTime (l, 0.25f, 0.5f, 0.25f, 0.8f, false, kUnit);
    CHECK (edit::valueAfter (l, 0.8f) == 0.f);
    for (int i = 0; i < l.count; ++i) CHECK (l.keys[(size_t) i].at != 0.75f);
    // With Shift the outside makes room.
    l = peak;
    edit::stretchTime (l, 0.f, 0.5f, 0.f, 0.25f, true, kUnit);
    CHECK (std::abs (edit::valueAfter (l, 0.625f) - 0.5f) < 1e-5f);   // 0.75 moved to 0.25 + 0.25 * 1.5
    checkTidy (l, kUnit);
}

TEST_CASE ("Lane edit: shapes span the range, ramps and the ADSR link to their neighbours", "[edit]")
{
    auto l = edit::flat (0.3f);
    edit::insertShape (l, 0.f, 1.f, edit::Shape::sine, kUnit);
    const auto sine = [] (float t) { return 0.5f + 0.5f * std::sin (2.f * 3.14159265f * t); };
    CHECK (maxDiff (l, sine, 0.f, 1.f) < 0.01f);
    CHECK (std::abs (edit::valueAfter (l, 0.25f) - 1.f) < 1e-5f);
    CHECK (std::abs (edit::valueAfter (l, 0.75f)) < 1e-5f);

    l = edit::flat (0.3f);
    edit::insertShape (l, 0.2f, 0.6f, edit::Shape::square, kUnit);
    CHECK (edit::valueAfter (l, 0.3f) == 1.f);
    CHECK (edit::valueAfter (l, 0.5f) == 0.f);
    CHECK (edit::valueAfter (l, 0.1f) == 0.3f);
    CHECK (edit::valueAfter (l, 0.7f) == 0.3f);

    l = edit::flat (0.3f);
    edit::insertShape (l, 0.5f, 1.f, edit::Shape::rampUp, kUnit);
    CHECK (std::abs (edit::valueAfter (l, 0.5f) - 0.3f) < 1e-6f);   // from what came before, no jump
    CHECK (edit::valueAfter (l, 1.f) == 1.f);

    l = edit::flat (0.3f);
    edit::insertShape (l, 0.f, 0.5f, edit::Shape::rampDownInto, kUnit);
    CHECK (edit::valueAfter (l, 0.f) == 1.f);
    CHECK (std::abs (edit::valueBefore (l, 0.5f) - 0.3f) < 1e-6f);   // into what follows

    l = edit::flat (0.3f);
    edit::insertShape (l, 0.2f, 0.8f, edit::Shape::adsr, kUnit);
    CHECK (std::abs (edit::valueAfter (l, 0.2f) - 0.3f) < 1e-6f);
    CHECK (std::abs (edit::valueAfter (l, 0.29f) - 1.f) < 1e-5f);
    CHECK (std::abs (edit::valueBefore (l, 0.8f) - 0.3f) < 1e-6f);
    checkTidy (l, kUnit);
}

TEST_CASE ("Lane edit: Simplify Envelope keeps the shape with fewer keys, and keeps steps", "[edit]")
{
    const auto f = [] (float t) { return 0.5f + 0.45f * std::tanh (8.f * (t - 0.4f)); };
    std::vector<Key> many;
    for (int i = 0; i <= 120; ++i) many.push_back ({ (float) i / 120.f, f ((float) i / 120.f), 1.f });
    Lane l;
    edit::assign (l, many, kUnit);
    REQUIRE (l.count == 121);
    const Lane before = l;
    edit::simplify (l, 0.f, 1.f, 0.005f, kUnit);
    INFO (l.count << " keys left");
    CHECK (l.count < 20);
    CHECK (maxDiff (l, before) < 0.008f);

    // A step in the middle of a straight run stays a step.
    std::vector<Key> stepped;
    for (int i = 0; i <= 20; ++i) stepped.push_back ({ (float) i / 40.f, 0.2f, 1.f });
    for (int i = 20; i <= 40; ++i) stepped.push_back ({ (float) i / 40.f, 0.8f, 1.f });
    edit::assign (l, stepped, kUnit);
    edit::simplify (l, 0.f, 1.f, 0.01f, kUnit);
    CHECK (l.count == 4);
    CHECK (edit::valueBefore (l, 0.5f) == 0.2f);
    CHECK (edit::valueAfter (l, 0.5f) == 0.8f);
}

TEST_CASE ("Lane edit: a full lane takes no new key, and anything an edit makes stays within the lane", "[edit]")
{
    std::vector<Key> full;
    for (int i = 0; i < Lane::kMaxKeys; ++i) full.push_back ({ (float) i / (float) Lane::kMaxKeys, (float) (i % 2), 1.f });
    Lane l;
    edit::assign (l, full, kUnit);
    REQUIRE (l.count == Lane::kMaxKeys);
    CHECK (edit::insert (l, 0.9999f, kUnit) == -1);

    // Too many keys for a lane: simplified until they fit.
    std::vector<Key> lots;
    for (int i = 0; i < 1000; ++i) lots.push_back ({ (float) i / 999.f, 0.5f + 0.5f * std::sin ((float) i * 0.05f), 1.f });
    edit::assign (l, lots, kUnit);
    CHECK (l.count <= Lane::kMaxKeys);
    checkTidy (l, kUnit);
}

TEST_CASE ("Lane edit: random edits never break a lane", "[edit]")
{
    std::mt19937 rng (7);
    std::uniform_real_distribution<float> u (-0.2f, 1.2f);
    const LaneRange ranges[] = { kUnit, ba::laneRange (ba::LaneId::pitch), ba::laneRange (ba::LaneId::level) };
    for (int round = 0; round < 40; ++round)
    {
        const auto r = ranges[(size_t) round % 3];
        const auto val = [&] { return r.lo + (r.hi - r.lo) * u (rng); };
        Lane l = edit::flat (r.clamp (val()));
        for (int step = 0; step < 150; ++step)
        {
            const float a = u (rng), b = u (rng);
            switch (rng() % 14)
            {
                case 0: edit::insert (l, a, r); break;
                case 1: edit::insert (l, a, val(), r); break;
                case 2: if (l.count > 0) edit::remove (l, (int) (rng() % (unsigned) l.count)); break;
                case 3:
                    if (l.count > 0)
                    {
                        const int f = (int) (rng() % (unsigned) l.count), t = std::min (l.count - 1, f + (int) (rng() % 3));
                        edit::move (l, f, t, a - 0.5f, val() - val(), r);
                    }
                    break;
                case 4: if (l.count > 1) edit::bend (l, 1 + (int) (rng() % (unsigned) (l.count - 1)), u (rng), val()); break;
                case 5:
                {
                    std::vector<edit::Cell> cells;
                    const float from = std::clamp (std::min (a, b), 0.f, 1.f);
                    for (int i = 0; i < 1 + (int) (rng() % 9); ++i)
                        cells.push_back ({ std::min (1.f, from + (float) i / 32.f), std::min (1.f, from + (float) (i + 1) / 32.f), val() });
                    edit::drawSteps (l, cells, r);
                    break;
                }
                case 6:
                {
                    std::vector<Key> stroke;
                    for (int i = 0; i < 1 + (int) (rng() % 60); ++i) stroke.push_back ({ u (rng), val(), 1.f });
                    edit::drawFreehand (l, stroke, 0.01f * (r.hi - r.lo), r);
                    break;
                }
                case 7: edit::erase (l, a, b, r); break;
                case 8: edit::paste (l, edit::copy (l, a, b, r), u (rng), r); break;
                case 9: edit::shiftRange (l, a, b, val() - val(), r); break;
                case 10:
                {
                    const auto [lo, hi] = edit::extent (l, a, b);
                    edit::transform (l, a, b, lo, hi, { val(), val(), val(), val() }, r);
                    break;
                }
                case 11: edit::stretchTime (l, a, b, u (rng), u (rng), (rng() & 1) != 0, r); break;
                case 12: edit::insertShape (l, a, b, (edit::Shape) (rng() % 10), r); break;
                case 13: edit::simplify (l, a, b, 0.02f * (r.hi - r.lo), r); break;
            }
            checkTidy (l, r);
        }
    }
}
