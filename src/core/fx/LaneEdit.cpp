#include "LaneEdit.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace ba::edit
{
namespace
{
    using Keys = std::vector<Key>;
    using Samples = std::vector<std::pair<float, float>>;   // (t, value)

    Keys keysOf (const Lane& l) { return { l.points().begin(), l.points().end() }; }
    float clamp01 (float v) noexcept { return std::isfinite (v) ? std::clamp (v, 0.f, 1.f) : 0.f; }

    // First key at or after `at`, first key after it.
    size_t lowerIndex (std::span<const Key> k, float at)
    {
        return (size_t) (std::lower_bound (k.begin(), k.end(), at, [] (const Key& key, float t) { return key.at < t; }) - k.begin());
    }
    size_t upperIndex (std::span<const Key> k, float at)
    {
        return (size_t) (std::upper_bound (k.begin(), k.end(), at, [] (float t, const Key& key) { return t < key.at; }) - k.begin());
    }

    // The curve whose t^c best fits normalised samples (t 0..1, n 0..1), by
    // golden-section search over log2 c (the error is unimodal in c: the
    // curves are ordered).
    float fitSamples (const Samples& s)
    {
        if (s.empty()) return 1.f;
        const auto error = [&s] (float lc) {
            const double c = std::exp2 ((double) lc);
            double e = 0.0;
            for (const auto& [t, n] : s)
            {
                const double d = std::pow ((double) std::clamp (t, 0.f, 1.f), c) - (double) n;
                e += d * d;
            }
            return e;
        };
        constexpr float g = 0.618034f;
        float lo = std::log2 (kMinCurve), hi = std::log2 (kMaxCurve);
        float x1 = hi - g * (hi - lo), x2 = lo + g * (hi - lo);
        double f1 = error (x1), f2 = error (x2);
        for (int i = 0; i < 40; ++i)
        {
            if (f1 < f2)
            {
                hi = x2; x2 = x1; f2 = f1;
                x1 = hi - g * (hi - lo); f1 = error (x1);
            }
            else
            {
                lo = x1; x1 = x2; f1 = f2;
                x2 = lo + g * (hi - lo); f2 = error (x2);
            }
        }
        const float c = std::exp2 (0.5f * (lo + hi));
        return std::abs (c - 1.f) < 2e-3f ? 1.f : std::clamp (c, kMinCurve, kMaxCurve);
    }

    // Drops the inner key that matters least (how far it sits off the line
    // between its neighbours): the last resort when steps fill a lane.
    void dropLeast (Keys& k)
    {
        size_t best = 1;
        float least = 1e30f;
        for (size_t i = 1; i + 1 < k.size(); ++i)
        {
            const Key &a = k[i - 1], &z = k[i + 1];
            const float u = z.at > a.at ? (k[i].at - a.at) / (z.at - a.at) : 0.5f;
            const float d = std::abs (k[i].value - (a.value + (z.value - a.value) * u));
            if (d < least) { least = d; best = i; }
        }
        k.erase (k.begin() + (std::ptrdiff_t) best);
    }

    // Appends the keys after s up to e (included) that the stretch s..e needs
    // within `tol`: one curved segment if it fits, else split at the key that
    // strays furthest.
    void span (const Keys& k, size_t s, size_t e, float tol, Keys& out)
    {
        if (e == s + 1 || k[e].at <= k[s].at)
        {
            for (size_t i = s + 1; i <= e; ++i) out.push_back (k[i]);
            return;
        }
        const float t0 = k[s].at, t1 = k[e].at, a = k[s].value, z = k[e].value;
        Samples pts;
        for (size_t i = s; i < e; ++i)
        {
            for (int j = 0; j < 8; ++j)
            {
                const float t = k[i].at + (k[i + 1].at - k[i].at) * ((float) j + 0.5f) / 8.f;
                pts.emplace_back (t, valueAfter (k, t));
            }
            if (i > s) pts.emplace_back (k[i].at, k[i].value);
        }
        float c = 1.f;
        if (std::abs (z - a) > 1e-9f)
        {
            Samples norm;
            norm.reserve (pts.size());
            for (const auto& [t, v] : pts) norm.emplace_back ((t - t0) / (t1 - t0), (v - a) / (z - a));
            c = fitSamples (norm);
        }
        float worst = 0.f;
        const auto line = [&] (float t) { return a + (z - a) * bent ((t - t0) / (t1 - t0), c, 0.f); };
        for (const auto& [t, v] : pts) worst = std::max (worst, std::abs (line (t) - v));
        if (worst <= tol)
        {
            out.push_back ({ t1, z, c, 0.f });
            return;
        }
        size_t m = s + 1;
        float far = -1.f;
        for (size_t i = s + 1; i < e; ++i)
        {
            const float d = std::abs (line (k[i].at) - k[i].value);
            if (d > far) { far = d; m = i; }
        }
        span (k, s, m, tol, out);
        span (k, m, e, tol, out);
    }

    // Keys p..q simplified within tol; steps (two keys at one time) stay.
    Keys simplified (const Keys& k, size_t p, size_t q, float tol)
    {
        if (q >= k.size() || q < p + 2) return k;
        std::vector<char> anchor (k.size(), 0);
        anchor[p] = anchor[q] = 1;
        for (size_t i = p + 1; i < q; ++i)
            if (k[i - 1].at == k[i].at || k[i + 1].at == k[i].at) anchor[i] = 1;
        Keys out (k.begin(), k.begin() + (std::ptrdiff_t) p + 1);
        size_t s = p;
        for (size_t e = p + 1; e <= q; ++e)
            if (anchor[e])
            {
                span (k, s, e, tol, out);
                s = e;
            }
        out.insert (out.end(), k.begin() + (std::ptrdiff_t) q + 1, k.end());
        return out;
    }

    // Writes `in` into the lane, tidy (see the header). Returns where the key
    // that was at in[track] ended up (or the key that absorbed it), -1 if gone.
    int store (Lane& lane, const Keys& in, LaneRange r, int track = -1)
    {
        struct Tagged
        {
            Key k;
            int tag;
        };
        std::vector<Tagged> v;
        v.reserve (in.size());
        for (size_t i = 0; i < in.size(); ++i)
        {
            Key k = in[i];
            k.at = clamp01 (k.at);
            k.value = std::isfinite (k.value) ? r.clamp (k.value) : r.lo;
            k.curve = std::isfinite (k.curve) ? std::clamp (k.curve, kMinCurve, kMaxCurve) : 1.f;
            k.offset = std::isfinite (k.offset) && k.curve != 1.f ? std::clamp (k.offset, 0.f, 1e6f) : 0.f;
            v.push_back ({ k, (int) i });
        }
        std::stable_sort (v.begin(), v.end(), [] (const Tagged& a, const Tagged& b) { return a.k.at < b.k.at; });

        // Drops v[i], handing a tracked tag to the key that stays in its place.
        const auto drop = [&v, track] (size_t i, size_t heir) {
            if (v[i].tag == track) v[heir].tag = track;
            v.erase (v.begin() + (std::ptrdiff_t) i);
        };
        // At most two keys at one time: the first and the last.
        for (size_t i = 0; i + 2 < v.size();)
            if (v[i].k.at == v[i + 1].k.at && v[i + 1].k.at == v[i + 2].k.at) drop (i + 1, i + 2);
            else ++i;
        // A step to the same value is no step.
        for (size_t i = 0; i + 1 < v.size();)
            if (v[i].k.at == v[i + 1].k.at && v[i].k.value == v[i + 1].k.value) drop (i + 1, i);
            else ++i;
        // Nothing plays before 0 or after 1 (past the path the lane holds its
        // value at 1): a step there keeps only the side that plays.
        if (v.size() >= 2 && v[0].k.at == 0.f && v[1].k.at == 0.f) drop (0, 1);
        if (v.size() >= 2 && v[v.size() - 1].k.at == 1.f && v[v.size() - 2].k.at == 1.f) drop (v.size() - 1, v.size() - 2);

        int found = -1;
        if (v.size() > (size_t) Lane::kMaxKeys)
        {
            Keys plain;
            for (const auto& t : v) plain.push_back (t.k);
            const float range = r.hi - r.lo;
            for (float tol = range * 1e-3f; plain.size() > (size_t) Lane::kMaxKeys && tol <= range; tol *= 2.f)
                plain = simplified (plain, 0, plain.size() - 1, tol);
            while (plain.size() > (size_t) Lane::kMaxKeys) dropLeast (plain);
            lane.count = (int) plain.size();
            std::copy (plain.begin(), plain.end(), lane.keys.begin());
        }
        else
        {
            lane.count = (int) v.size();
            for (size_t i = 0; i < v.size(); ++i)
            {
                lane.keys[i] = v[i].k;
                if (v[i].tag == track && track >= 0) found = (int) i;
            }
        }
        return found;
    }
} // namespace

float valueBefore (std::span<const Key> k, float at) noexcept
{
    if (k.empty()) return 0.f;
    const size_t j = lowerIndex (k, at);
    if (j >= k.size()) return k.back().value;
    if (j == 0) return k[0].value;
    const Key &a = k[j - 1], &z = k[j];
    return segment (a, z, (at - a.at) / (z.at - a.at));
}

float valueAfter (std::span<const Key> k, float at) noexcept
{
    if (k.empty()) return 0.f;
    const size_t j = upperIndex (k, at);
    if (j == 0) return k[0].value;
    if (j >= k.size()) return k.back().value;
    const Key &a = k[j - 1], &z = k[j];
    return segment (a, z, (at - a.at) / (z.at - a.at));
}

void tidy (Lane& lane, LaneRange r) { store (lane, keysOf (lane), r); }

void assign (Lane& lane, const std::vector<Key>& keys, LaneRange r) { store (lane, keys, r); }

Lane flat (float value) noexcept
{
    Lane l;
    l.keys[0] = { 0.f, value, 1.f };
    l.count = 1;
    return l;
}

int insert (Lane& lane, float at, LaneRange r)
{
    Keys k = keysOf (lane);
    at = clamp01 (at);
    for (size_t i = k.size(); i-- > 0;)
        if (k[i].at == at) return (int) i;
    if (k.size() >= (size_t) Lane::kMaxKeys) return -1;
    if (k.empty()) return store (lane, { { at, r.clamp (0.f), 1.f } }, r, 0);

    const size_t j = upperIndex (k, at);
    Key m { at, 0.f, 1.f };
    if (j == 0) m.value = k[0].value;
    else if (j >= k.size()) m.value = k.back().value;
    else
    {
        Key& z = k[j];
        const Key& a = k[j - 1];
        const float u = (at - a.at) / (z.at - a.at);
        m = { at, segment (a, z, u), z.curve, z.offset };
        cut (m, 0.f, u);   // the part before
        cut (z, u, 1.f);   // and after: the same bend, exactly
    }
    k.insert (k.begin() + (std::ptrdiff_t) j, m);
    return store (lane, k, r, (int) j);
}

int insert (Lane& lane, float at, float value, LaneRange r)
{
    Keys k = keysOf (lane);
    at = clamp01 (at);
    if (k.size() >= (size_t) Lane::kMaxKeys) return -1;
    const size_t j = upperIndex (k, at);
    const float curve = j > 0 && j < k.size() ? k[j].curve : 1.f;
    k.insert (k.begin() + (std::ptrdiff_t) j, Key { at, r.clamp (value), curve });
    return store (lane, k, r, (int) j);
}

void remove (Lane& lane, int index)
{
    if (index < 0 || index >= lane.count) return;
    std::copy (lane.keys.begin() + index + 1, lane.keys.begin() + lane.count, lane.keys.begin() + index);
    --lane.count;
}

int move (Lane& lane, int first, int last, float dAt, float dValue, LaneRange r)
{
    const Keys k = keysOf (lane);
    if (first < 0 || last >= (int) k.size() || first > last) return first;
    const auto f = (size_t) first, l = (size_t) last;
    dAt = std::clamp (dAt, -k[f].at, 1.f - k[l].at);
    const float nf = k[f].at + dAt, nl = k[l].at + dAt;

    Keys out;
    for (size_t i = 0; i < f; ++i)
        if (k[i].at <= nf) out.push_back (k[i]);   // the ones passed over go
    if (out.size() >= 2 && out[out.size() - 1].at == nf && out[out.size() - 2].at == nf) out.pop_back();
    const auto index = (int) out.size();
    for (size_t i = f; i <= l; ++i) out.push_back ({ k[i].at + dAt, r.clamp (k[i].value + dValue), k[i].curve, k[i].offset });
    Keys after;
    for (size_t i = l + 1; i < k.size(); ++i)
        if (k[i].at >= nl) after.push_back (k[i]);
    if (after.size() >= 2 && after[0].at == nl && after[1].at == nl) after.erase (after.begin());
    out.insert (out.end(), after.begin(), after.end());
    const int moved = store (lane, out, r, index);
    return moved >= 0 ? moved : std::clamp (index, 0, std::max (0, lane.count - 1));
}

void bend (Lane& lane, int index, float u, float target)
{
    if (index < 1 || index >= lane.count) return;
    const Key& a = lane.keys[(size_t) index - 1];
    Key& z = lane.keys[(size_t) index];
    if (z.at <= a.at || std::abs (z.value - a.value) < 1e-9f) return;
    u = std::clamp (u, 0.02f, 0.98f);
    const float n = (target - a.value) / (z.value - a.value);
    float c = 1.f;
    if (z.offset <= 0.f) c = n <= 0.f ? kMaxCurve : n >= 1.f ? kMinCurve : std::log (n) / std::log (u);
    else if (n >= bent (u, kMinCurve, z.offset)) c = kMinCurve;
    else if (n <= bent (u, kMaxCurve, z.offset)) c = kMaxCurve;
    else
    {
        // The bend at u falls as the curve grows: bisect it (in octaves).
        float lo = std::log2 (kMinCurve), hi = std::log2 (kMaxCurve);
        for (int i = 0; i < 40; ++i)
        {
            const float mid = 0.5f * (lo + hi);
            (bent (u, std::exp2 (mid), z.offset) > n ? lo : hi) = mid;
        }
        c = std::exp2 (0.5f * (lo + hi));
    }
    z.curve = std::clamp (std::isfinite (c) ? c : 1.f, kMinCurve, kMaxCurve);
}

void straighten (Lane& lane, int index)
{
    if (index < 0 || index >= lane.count) return;
    lane.keys[(size_t) index].curve = 1.f;
    lane.keys[(size_t) index].offset = 0.f;
}

void overwrite (Lane& lane, float t0, float t1, std::span<const Key> inner, LaneRange r)
{
    t0 = clamp01 (t0);
    t1 = clamp01 (t1);
    if (t1 < t0) std::swap (t0, t1);
    const Keys k = keysOf (lane);
    Keys out;
    if (k.empty())
    {
        for (const auto& key : inner) out.push_back ({ std::clamp (key.at, t0, t1), key.value, key.curve, key.offset });
        store (lane, out, r);
        return;
    }
    // Before t0 as it was; the edge holds what arrived there, bent as it was.
    const size_t j = lowerIndex (k, t0);
    out.assign (k.begin(), k.begin() + (std::ptrdiff_t) j);
    Key edge { t0, valueBefore (k, t0), 1.f, 0.f };
    if (j > 0 && j < k.size())
    {
        edge.curve = k[j].curve;
        edge.offset = k[j].offset;
        cut (edge, 0.f, (t0 - k[j - 1].at) / (k[j].at - k[j - 1].at));
    }
    out.push_back (edge);
    for (const auto& key : inner) out.push_back ({ std::clamp (key.at, t0, t1), key.value, key.curve, key.offset });
    // After t1 as it was, from the value there (the segment crossing t1 refitted).
    out.push_back ({ t1, valueAfter (k, t1), 1.f });
    const size_t next = upperIndex (k, t1);
    for (size_t i = next; i < k.size(); ++i)
    {
        Key key = k[i];
        if (i == next && i > 0 && k[i - 1].at < t1)
            cut (key, (t1 - k[i - 1].at) / (key.at - k[i - 1].at), 1.f);
        out.push_back (key);
    }
    store (lane, out, r);
}

void drawSteps (Lane& lane, std::span<const Cell> cells, LaneRange r)
{
    if (cells.empty()) return;
    Keys inner;
    for (const auto& c : cells)
    {
        const float v = r.clamp (c.value);
        if (inner.size() >= 2 && inner.back().value == v && inner.back().at == c.from) inner.back().at = c.to;
        else
        {
            inner.push_back ({ c.from, v, 1.f });
            inner.push_back ({ c.to, v, 1.f });
        }
    }
    overwrite (lane, cells.front().from, cells.back().to, inner, r);
}

void drawFreehand (Lane& lane, std::vector<Key> stroke, float tolerance, LaneRange r)
{
    if (stroke.empty()) return;
    for (auto& k : stroke) k = { clamp01 (k.at), r.clamp (k.value), 1.f };
    std::stable_sort (stroke.begin(), stroke.end(), [] (const Key& a, const Key& b) { return a.at < b.at; });
    Keys pts;
    for (const auto& k : stroke)
        if (! pts.empty() && pts.back().at == k.at) pts.back().value = k.value;   // the latest wins
        else pts.push_back (k);
    if (pts.size() > 2) pts = simplified (pts, 0, pts.size() - 1, tolerance);
    overwrite (lane, pts.front().at, pts.back().at, pts, r);
}

Clip copy (const Lane& lane, float t0, float t1, LaneRange r)
{
    t0 = clamp01 (t0);
    t1 = clamp01 (t1);
    if (t1 < t0) std::swap (t0, t1);
    Clip c;
    c.length = t1 - t0;
    const Keys k = keysOf (lane);
    if (k.empty()) return c;
    c.keys.push_back ({ 0.f, r.norm (valueAfter (k, t0)), 1.f });
    const size_t first = upperIndex (k, t0);
    for (size_t i = first; i < k.size() && k[i].at < t1; ++i)
    {
        Key key = k[i];
        if (i == first && i > 0 && k[i - 1].at < t0)   // the segment crossing t0: only its part after t0
            cut (key, (t0 - k[i - 1].at) / (key.at - k[i - 1].at), 1.f);
        c.keys.push_back ({ key.at - t0, r.norm (key.value), key.curve, key.offset });
    }
    // The segment arriving at t1: the part of it from max(t0, its start).
    Key last { c.length, r.norm (valueBefore (k, t1)), 1.f, 0.f };
    const size_t j = lowerIndex (k, t1);
    if (j > 0 && j < k.size() && k[j].at > k[j - 1].at)
    {
        const Key &y = k[j - 1], &z = k[j];
        last.curve = z.curve;
        last.offset = z.offset;
        cut (last, y.at < t0 ? (t0 - y.at) / (z.at - y.at) : 0.f, (t1 - y.at) / (z.at - y.at));
    }
    c.keys.push_back (last);
    return c;
}

void paste (Lane& lane, const Clip& clip, float at, LaneRange r)
{
    if (clip.keys.empty()) return;
    at = clamp01 (at);
    const float end = std::min (1.f, at + clip.length);
    Keys inner;
    for (const auto& k : clip.keys)
        if (at + k.at <= end) inner.push_back ({ at + k.at, r.value (std::clamp (k.value, 0.f, 1.f)), k.curve, k.offset });
    if (at + clip.length > end)   // cut at the path's end: what the clip has there
    {
        const float into = end - at;
        Key last { end, r.value (std::clamp (valueBefore (clip.keys, into), 0.f, 1.f)), 1.f, 0.f };
        const size_t j = lowerIndex (clip.keys, into);
        if (j > 0 && j < clip.keys.size())
        {
            const Key &y = clip.keys[j - 1], &z = clip.keys[j];
            last.curve = z.curve;
            last.offset = z.offset;
            cut (last, 0.f, (into - y.at) / (z.at - y.at));
        }
        inner.push_back (last);
    }
    overwrite (lane, at, end, inner, r);
}

void erase (Lane& lane, float t0, float t1, LaneRange r) { overwrite (lane, t0, t1, {}, r); }

void shiftRange (Lane& lane, float t0, float t1, float dValue, LaneRange r)
{
    auto c = copy (lane, t0, t1, r);
    for (auto& k : c.keys) k.value = r.norm (r.clamp (r.value (k.value) + dValue));
    paste (lane, c, std::min (t0, t1), r);
}

std::pair<float, float> extent (const Lane& lane, float t0, float t1)
{
    const auto c = copy (lane, t0, t1, {});   // the unit range: raw values
    if (c.keys.empty()) return { 0.f, 0.f };
    float lo = c.keys[0].value, hi = lo;
    for (const auto& k : c.keys)
    {
        lo = std::min (lo, k.value);
        hi = std::max (hi, k.value);
    }
    return { lo, hi };
}

void transform (Lane& lane, float t0, float t1, float bottom, float top, Box to, LaneRange r)
{
    auto c = copy (lane, t0, t1, r);
    for (auto& k : c.keys)
    {
        const float x = c.length > 0.f ? k.at / c.length : 0.f, v = r.value (k.value);
        const float n = top - bottom > 1e-9f ? (v - bottom) / (top - bottom) : 0.5f;
        const float lo = to.bottomLeft + (to.bottomRight - to.bottomLeft) * x, hi = to.topLeft + (to.topRight - to.topLeft) * x;
        k.value = r.norm (r.clamp (lo + n * (hi - lo)));
    }
    paste (lane, c, std::min (t0, t1), r);
}

void stretchTime (Lane& lane, float t0, float t1, float n0, float n1, bool moveOutside, LaneRange r)
{
    if (t1 < t0) std::swap (t0, t1);
    if (t1 - t0 < 1e-6f || lane.count == 0) return;
    n0 = clamp01 (n0);
    n1 = std::max (clamp01 (n1), n0);
    insert (lane, t0, r);   // keys at the edges, so exactly the selection moves
    insert (lane, t1, r);
    Keys out;
    for (const auto& key : keysOf (lane))
    {
        float t = key.at;
        if (t >= t0 && t <= t1) t = n0 + (t - t0) * (n1 - n0) / (t1 - t0);
        else if (moveOutside) t = t < t0 ? (t0 > 0.f ? t * n0 / t0 : t) : (t1 < 1.f ? n1 + (t - t1) * (1.f - n1) / (1.f - t1) : t);
        else if (t > n0 && t < n1) continue;   // covered by the new span
        out.push_back ({ t, key.value, key.curve, key.offset });
    }
    store (lane, out, r);
}

void insertShape (Lane& lane, float t0, float t1, Shape s, LaneRange r)
{
    if (t1 < t0) std::swap (t0, t1);
    if (t1 - t0 < 1e-6f) return;
    const float lo = r.lo, hi = r.hi, mid = 0.5f * (lo + hi), amp = 0.5f * (hi - lo);
    const float before = valueBefore (lane, t0), after = valueAfter (lane, t1);
    Keys inner;   // at 0..1 across the selection
    switch (s)
    {
        case Shape::sine:
        {
            constexpr int n = 16;   // one cycle in sixteenths, each a bent segment
            const auto sine = [] (float x) { return std::sin (2.f * std::numbers::pi_v<float> * x); };
            inner.push_back ({ 0.f, mid, 1.f });
            for (int i = 1; i <= n; ++i)
            {
                const float x0 = (float) (i - 1) / n, x1 = (float) i / n, s0 = sine (x0), s1 = sine (x1);
                Samples pts;
                for (int j = 0; j < 16; ++j)
                {
                    const float t = ((float) j + 0.5f) / 16.f;
                    pts.emplace_back (t, (sine (x0 + t * (x1 - x0)) - s0) / (s1 - s0));
                }
                inner.push_back ({ x1, mid + amp * s1, fitSamples (pts) });
            }
            break;
        }
        case Shape::triangle:   inner = { { 0.f, lo, 1.f }, { 0.5f, hi, 1.f }, { 1.f, lo, 1.f } }; break;
        case Shape::saw:        inner = { { 0.f, lo, 1.f }, { 1.f, hi, 1.f } }; break;
        case Shape::inverseSaw: inner = { { 0.f, hi, 1.f }, { 1.f, lo, 1.f } }; break;
        case Shape::square:     inner = { { 0.f, hi, 1.f }, { 0.5f, hi, 1.f }, { 0.5f, lo, 1.f }, { 1.f, lo, 1.f } }; break;
        case Shape::rampUp:       inner = { { 0.f, before, 1.f }, { 1.f, hi, 1.f } }; break;
        case Shape::rampDown:     inner = { { 0.f, before, 1.f }, { 1.f, lo, 1.f } }; break;
        case Shape::rampUpInto:   inner = { { 0.f, lo, 1.f }, { 1.f, after, 1.f } }; break;
        case Shape::rampDownInto: inner = { { 0.f, hi, 1.f }, { 1.f, after, 1.f } }; break;
        case Shape::adsr:
        {
            const float sustain = lo + 0.6f * (hi - lo);
            inner = { { 0.f, before, 1.f }, { 0.15f, hi, 0.7f }, { 0.35f, sustain, 0.5f }, { 0.75f, sustain, 1.f }, { 1.f, after, 0.5f } };
            break;
        }
    }
    for (auto& k : inner) k.at = t0 + k.at * (t1 - t0);
    overwrite (lane, t0, t1, inner, r);
}

void simplify (Lane& lane, float t0, float t1, float tolerance, LaneRange r)
{
    if (t1 < t0) std::swap (t0, t1);
    const Keys k = keysOf (lane);
    const size_t p = lowerIndex (k, t0), end = upperIndex (k, t1);
    if (end < p + 3) return;
    store (lane, simplified (k, p, end - 1, tolerance), r);
}

float snap (float at, float grid, std::span<const float> extra, float tolerance) noexcept
{
    float best = at, bestD = tolerance;
    if (grid > 0.f)
    {
        const float g = std::round (at / grid) * grid;
        if (std::abs (g - at) <= bestD) { best = g; bestD = std::abs (g - at); }
    }
    for (const float e : extra)
        if (std::abs (e - at) <= bestD) { best = e; bestD = std::abs (e - at); }
    return clamp01 (best);
}
} // namespace ba::edit
