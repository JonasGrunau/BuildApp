#pragma once

#include "Presets.h"

#include <span>
#include <utility>
#include <vector>

// Editing a lane the way Ableton Live edits an automation envelope (the
// preset editor's LaneEditor maps the mouse onto these). Everything is in
// lane terms: `at` along the path 0..1, values in the lane's own units
// (LaneRange). Values are raw: a lane's `step` (pitch stairs) applies when
// it plays, not here.
//
// Every edit leaves the lane tidy: keys sorted by `at`, at most two at one
// `at` (a step), values in range, curves in kMinCurve..kMaxCurve, and no
// more than Lane::kMaxKeys (an edit that would need more is simplified until
// it fits). A drag is meant to be replayed on the lane as it was when the
// gesture began, so whatever it passes over comes back when it goes back.
namespace ba::edit
{
    inline constexpr float kMinCurve = 1.f / 12.f, kMaxCurve = 12.f;

    // A segment from key a to key z, u (0..1) of the way along it.
    inline float segment (const Key& a, const Key& z, float u) noexcept
    {
        return a.value + (z.value - a.value) * bent (u, z.curve, z.offset);
    }

    // Key z's bend for only the part u0..u1 of the segment arriving at it,
    // as a segment of its own: exact (Presets.h `bent`).
    inline void cut (Key& z, float u0, float u1) noexcept
    {
        if (z.curve != 1.f && u1 > u0) z.offset = std::min (1e6f, (z.offset + u0) / (u1 - u0));
    }

    // The lane's raw value at `at` approaching from the left, and from the
    // right (what plays there). They differ only on a step.
    float valueBefore (std::span<const Key> keys, float at) noexcept;
    float valueAfter (std::span<const Key> keys, float at) noexcept;
    inline float valueBefore (const Lane& l, float at) noexcept { return valueBefore (l.points(), at); }
    inline float valueAfter (const Lane& l, float at) noexcept { return valueAfter (l.points(), at); }

    // Sorts, clamps and caps the lane (see above).
    void tidy (Lane&, LaneRange);
    // The lane as `keys` (any number, any order), tidied into it.
    void assign (Lane&, const std::vector<Key>& keys, LaneRange);
    // A lane held at `value` all along (one key).
    Lane flat (float value) noexcept;

    // A new key on the line at `at` (a click on the line); returns its index,
    // -1 when the lane is full. A bent segment keeps its shape exactly. On an
    // existing key's time: that key.
    int insert (Lane&, float at, LaneRange);
    // A new key at a value of its own (a double-click off the line, Add Value).
    int insert (Lane&, float at, float value, LaneRange);
    void remove (Lane&, int index);

    // Moves the run of keys first..last by dAt and dValue. The run stays
    // within 0..1 as a whole; values clip one by one. The keys it passes over
    // go (landing on one's time makes a step). Returns the run's new first index.
    int move (Lane&, int first, int last, float dAt, float dValue, LaneRange);

    // Bends the segment that ends at key `index` so that, `u` (0..1) of the
    // way along it, it passes through `target` (keeping its offset). A flat
    // or vertical segment doesn't bend. Straightening makes it a line again.
    void bend (Lane&, int index, float u, float target);
    void straighten (Lane&, int index);

    // Replaces t0..t1 with `inner` (keys within t0..t1) and leaves the lane
    // outside as it was: a key at each edge holds the value it had there, so
    // an inner key with another value at an edge is a step.
    void overwrite (Lane&, float t0, float t1, std::span<const Key> inner, LaneRange);

    // Draw Mode on the grid: each cell from..to holds its value (cells in
    // order, end to end); neighbours with one value merge.
    struct Cell
    {
        float from, to, value;
    };
    void drawSteps (Lane&, std::span<const Cell>, LaneRange);
    // Draw Mode off the grid: the stroke (points in any order) replaces what
    // it spans, simplified to within `tolerance` (lane units).
    void drawFreehand (Lane&, std::vector<Key> stroke, float tolerance, LaneRange);

    // A time selection's content: keys from 0 to `length`, values 0..1 of the
    // lane's range, so it pastes into any lane.
    struct Clip
    {
        float length = 0.f;
        std::vector<Key> keys;
    };
    Clip copy (const Lane&, float t0, float t1, LaneRange);
    void paste (Lane&, const Clip&, float at, LaneRange);   // over at..at + length, cut at the path's end
    void erase (Lane&, float t0, float t1, LaneRange);      // a line between the values at its edges
    // Dragging a segment inside a time selection: the part moves as one, with
    // a step at each edge.
    void shiftRange (Lane&, float t0, float t1, float dValue, LaneRange);

    // The lowest and highest value over t0..t1 (the selection's box).
    std::pair<float, float> extent (const Lane&, float t0, float t1);
    // The box handles: content between `bottom` and `top` maps onto a box
    // whose bottom and top edges run from the left corners to the right ones.
    // Both sides equal: a vertical stretch; different: a skew.
    struct Box
    {
        float bottomLeft, topLeft, bottomRight, topRight;
    };
    void transform (Lane&, float t0, float t1, float bottom, float top, Box to, LaneRange);
    // The side handles: t0..t1 stretched to n0..n1. The keys the new span
    // covers go; with `moveOutside` the rest of the path makes room instead.
    void stretchTime (Lane&, float t0, float t1, float n0, float n1, bool moveOutside, LaneRange);

    // Insert Shape: the waveforms span the lane's range; the ramps and the
    // ADSR link to the value before the selection or after it.
    enum class Shape { sine, triangle, saw, inverseSaw, square, rampUp, rampDown, rampUpInto, rampDownInto, adsr };
    void insertShape (Lane&, float t0, float t1, Shape, LaneRange);

    // Simplify Envelope over t0..t1: the fewest keys within `tolerance` of the
    // lane (lane units), curved where a curve fits. Steps stay.
    void simplify (Lane&, float t0, float t1, float tolerance, LaneRange);

    // Time snap: `at` to the nearest grid line (grid > 0) or `extra` time
    // (the neighbours') within `tolerance`, or as it is.
    float snap (float at, float grid, std::span<const float> extra, float tolerance) noexcept;
} // namespace ba::edit
