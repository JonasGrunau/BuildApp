#include "LaneEditor.h"

#include "fx/CutFilters.h"
#include "fx/Mapping.h"

#include <cmath>

using namespace theme;
namespace edit = ba::edit;
namespace map = ba::map;

namespace
{
    // The grids, in path terms: on a 16-bar build 1/16 is a bar, 1/64 a beat.
    constexpr float kGrids[] = { 1.f / 4.f, 1.f / 8.f, 1.f / 16.f, 1.f / 32.f, 1.f / 64.f };
    static_assert (std::size (kGrids) == LaneEditor::kGridCount);

    // Live's modifiers: Cmd (Mac) / Alt (Windows) takes the snap off; Option /
    // Alt bends a segment. Names for the footer.
   #if JUCE_MAC
    bool noSnap (const juce::ModifierKeys& m) { return m.isCommandDown(); }
    const juce::String kNoSnap ("Cmd"), kBend ("Option");
   #else
    bool noSnap (const juce::ModifierKeys& m) { return m.isAltDown(); }
    const juce::String kNoSnap ("Alt"), kBend ("Alt");
   #endif

    // One clipboard for every editor window: copy in one, paste in another.
    std::vector<edit::Clip>& clipboard()
    {
        static std::vector<edit::Clip> c;
        return c;
    }

    enum Command { cmdEditValue = 1, cmdDeleteKey, cmdAddValue, cmdStraighten, cmdCut, cmdCopy, cmdPaste, cmdDuplicate, cmdDelete,
                   cmdSelectAll, cmdSimplify, cmdShape = 100 };

    struct ShapeItem
    {
        edit::Shape shape;
        const char* name;
    };
    constexpr ShapeItem kShapes[] = { { edit::Shape::sine, "Sine" },           { edit::Shape::triangle, "Triangle" },
                                      { edit::Shape::saw, "Saw" },             { edit::Shape::inverseSaw, "Inverse Saw" },
                                      { edit::Shape::square, "Square" },       { edit::Shape::rampUp, "Ramp Up" },
                                      { edit::Shape::rampDown, "Ramp Down" },  { edit::Shape::rampUpInto, "Ramp Up Into" },
                                      { edit::Shape::rampDownInto, "Ramp Down Into" }, { edit::Shape::adsr, "ADSR" } };

    // A pencil (Draw Mode), its tip bottom left: light with a dark rim, so it
    // reads on the sheet and off it.
    juce::MouseCursor makePencil()
    {
        juce::Image img (juce::Image::ARGB, 40, 40, true);
        {
            juce::Graphics g (img);
            g.addTransform (juce::AffineTransform::scale (2.f));
            juce::Path body;
            body.startNewSubPath (2.f, 18.f);
            body.lineTo (3.6f, 12.8f);
            body.lineTo (14.2f, 2.2f);
            body.lineTo (17.8f, 5.8f);
            body.lineTo (7.2f, 16.4f);
            body.closeSubPath();
            g.setColour (juce::Colour (0xff131211));
            g.strokePath (body, juce::PathStrokeType (2.4f, juce::PathStrokeType::curved));
            g.setColour (juce::Colour (0xffece7db));
            g.fillPath (body);
            g.setColour (juce::Colour (0xff131211));
            g.drawLine (3.6f, 12.8f, 7.2f, 16.4f, 1.f);
            g.drawLine (12.4f, 4.f, 16.f, 7.6f, 1.f);
        }
        return juce::MouseCursor (juce::ScaledImage (img, 2.0), { 2, 18 });
    }

    // What the chain runs for a lane's control, in lane units.
    float settingOf (ba::LaneId id, const ba::Settings& s)
    {
        using L = ba::LaneId;
        switch (id)
        {
            case L::lowCut:      return s.lowCut;
            case L::lowReso:     return s.lowCutAuto ? (float) ba::CutFilters::resonance (false, s.lowCutKnobs()) : s.lowCutReso;
            case L::highCut:     return s.highCut;
            case L::highReso:    return s.highCutAuto ? (float) ba::CutFilters::resonance (true, s.highCutKnobs()) : s.highCutReso;
            case L::pitch:       return s.pitch;
            case L::noise:       return s.noise;
            case L::noisePitch:  return s.noisePitch;
            case L::noiseLowCut: return s.noiseLowCut;
            case L::reverb:      return s.reverb;
            case L::stereo:      return s.stereo;
            case L::level:       return s.makeupDb + s.fadeDb;
        }
        return 0.f;
    }

    // Lines worth a glance on each lane's graph paper, with their words.
    std::vector<std::pair<float, juce::String>> guides (ba::LaneId id)
    {
        using L = ba::LaneId;
        std::vector<std::pair<float, juce::String>> out;
        const auto hz = [&out] (std::initializer_list<double> list, float (*norm) (double)) {
            for (const double f : list) out.push_back ({ norm (f), f >= 1000.0 ? fmt::fixed (f / 1000.0, 0) + " kHz" : fmt::fixed (f, 0) + " Hz" });
        };
        switch (id)
        {
            case L::lowCut:
            case L::noiseLowCut: hz ({ 100.0, 1000.0 }, map::lowCutNorm); break;
            case L::highCut:     hz ({ 1000.0, 10000.0 }, map::highCutNorm); break;
            case L::noisePitch:  hz ({ 200.0, 1000.0, 5000.0 }, map::noiseHzNorm); break;
            case L::pitch:       out = { { 5.f, "+5 st" }, { 7.f, "+7 st" } }; break;
            case L::noise:
                for (const double db : { -30.0, -12.0 }) out.push_back ({ map::noiseNormFromDb (db), fmt::signedNumber (db, 0) + " dB" });
                break;
            case L::level:       out = { { -6.f, fmt::signedNumber (-6.0, 0) + " dB" }, { 0.f, "0 dB" }, { 6.f, "+6 dB" } }; break;
            case L::lowReso:
            case L::highReso:
            case L::reverb:      out = { { 0.5f, "50 %" } }; break;
            case L::stereo:      out = { { 0.5f, "" } }; break;
        }
        return out;
    }

    juce::juce_wchar upper (int code) { return juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) code); }
} // namespace

LaneEditor::LaneEditor (LaneModel& m) : model (m), pencil (makePencil())
{
    setOpaque (true);
    setWantsKeyboardFocus (true);
    rows = model.lanes();
}

juce::String LaneEditor::gridName (int g)
{
    constexpr const char* names[] = { "1/4", "1/8", "1/16", "1/32", "1/64" };
    return names[juce::jlimit (0, kGridCount - 1, g)];
}

void LaneEditor::setDrawMode (bool on)
{
    if (on == draw) return;
    draw = on;
    updateCursor (hover, juce::ModifierKeys::getCurrentModifiers());
    repaint();
    if (onModeChange) onModeChange();
}

void LaneEditor::setGrid (int g)
{
    g = juce::jlimit (0, kGridCount - 1, g);
    if (g == gridIndex) return;
    gridIndex = g;
    repaint();
    if (onModeChange) onModeChange();
}

void LaneEditor::setSnap (bool on)
{
    if (on == snap) return;
    snap = on;
    if (onModeChange) onModeChange();
}

void LaneEditor::setTriplets (bool on)
{
    if (on == triplet) return;
    triplet = on;
    repaint();
    if (onModeChange) onModeChange();
}

void LaneEditor::refresh()
{
    const float b = model.build();
    if (b == shownBuild) return;
    const float x0 = xOfBuild (std::max (0.f, shownBuild)), x1 = xOfBuild (b);
    shownBuild = b;
    // The playhead's strip (what it lit or unlit on the way) and the values.
    repaint (juce::Rectangle<float> (std::min (x0, x1) - 4.f, 0.f, std::abs (x1 - x0) + 8.f, (float) getHeight()).getSmallestIntegerContainer());
    repaint (juce::Rectangle<int> (getWidth() - (int) kValueW - 2, 0, (int) kValueW + 2, getHeight()));
}

void LaneEditor::presetChanged()
{
    rows = model.lanes();
    sel = {};
    gesture = Gesture::none;
    hover = {};
    entry.reset();
    repaint();
}

// ---- Geometry ----------------------------------------------------------------------

juce::Rectangle<float> LaneEditor::plot() const
{
    return getLocalBounds().toFloat().withTrimmedTop (kRuler).withTrimmedLeft (kPad).withTrimmedRight (kValueW + kPad);
}

float LaneEditor::rowHeight() const { return rows.empty() ? 0.f : plot().getHeight() / (float) rows.size(); }
float LaneEditor::rowTop (int r) const { return plot().getY() + rowHeight() * (float) r; }

juce::Rectangle<float> LaneEditor::row (int r) const
{
    const auto p = plot();
    return { p.getX(), rowTop (r), p.getWidth(), rowHeight() };
}

float LaneEditor::xOfBuild (float b) const
{
    const auto p = plot();
    return p.getX() + p.getWidth() * b;
}

float LaneEditor::pathOfX (float x) const
{
    const auto p = plot();
    return (x - p.getX()) / p.getWidth() / ba::kPathEnd;
}

float LaneEditor::pathPerPixel() const { return 1.f / std::max (1.f, plot().getWidth() * ba::kPathEnd); }

// Values fill the row 8 px clear of its edges (room for the keys).
float LaneEditor::yOf (int r, float v) const
{
    const auto b = row (r).reduced (0.f, 8.f);
    return b.getBottom() - b.getHeight() * rangeOf (r).norm (v);
}

float LaneEditor::valueOf (int r, float y) const
{
    const auto b = row (r).reduced (0.f, 8.f);
    return rangeOf (r).value ((b.getBottom() - y) / std::max (1.f, b.getHeight()));
}

float LaneEditor::gridStep() const { return kGrids[gridIndex] * (triplet ? 2.f / 3.f : 1.f); }

std::pair<int, int> LaneEditor::selectedKeys (int r) const
{
    const auto& l = laneAt (r);
    int first = l.count, last = -1;
    for (int i = 0; i < l.count; ++i)
        if (l.keys[(size_t) i].at >= sel.t0 && l.keys[(size_t) i].at <= sel.t1)
        {
            first = std::min (first, i);
            last = i;
        }
    return { first, last };
}

float LaneEditor::snapAt (float at, const ba::Lane& l, std::span<const int> moving, const juce::ModifierKeys& mods) const
{
    if (noSnap (mods)) return juce::jlimit (0.f, 1.f, at);
    std::vector<float> neighbours;
    for (int i = 0; i < l.count; ++i)
        if (std::find (moving.begin(), moving.end(), i) == moving.end()) neighbours.push_back (l.keys[(size_t) i].at);
    return edit::snap (at, snap ? gridStep() : 0.f, neighbours, 6.f * pathPerPixel());
}

std::optional<juce::Rectangle<float>> LaneEditor::handleBox (int r) const
{
    if (! sel.covers (r)) return std::nullopt;
    const auto [lo, hi] = edit::extent (laneAt (r), sel.t0, sel.t1);
    auto b = juce::Rectangle<float>::leftTopRightBottom (xOfPath (sel.t0), yOf (r, hi), xOfPath (sel.t1), yOf (r, lo));
    if (b.getHeight() < 8.f) b = b.withSizeKeepingCentre (b.getWidth(), 8.f);
    return b;
}

LaneEditor::Handle LaneEditor::handleAt (int r, juce::Point<float> pt) const
{
    const auto b = handleBox (r);
    if (! b || ! (gesture == Gesture::handle || (hover.row == r && pt.x >= b->getX() - 6.f && pt.x <= b->getRight() + 6.f))) return Handle::none;
    const std::pair<Handle, juce::Point<float>> spots[] = {
        { Handle::topLeft, b->getTopLeft() },       { Handle::top, { b->getCentreX(), b->getY() } },       { Handle::topRight, b->getTopRight() },
        { Handle::left, { b->getX(), b->getCentreY() } },                                                 { Handle::right, { b->getRight(), b->getCentreY() } },
        { Handle::bottomLeft, b->getBottomLeft() }, { Handle::bottom, { b->getCentreX(), b->getBottom() } }, { Handle::bottomRight, b->getBottomRight() },
    };
    for (const auto& [h, at] : spots)
        if (pt.getDistanceFrom (at) <= 5.f) return h;
    return Handle::none;
}

LaneEditor::Hit LaneEditor::hitAt (juce::Point<float> pt) const
{
    Hit h;
    const auto p = plot();
    if (pt.y < kRuler)
    {
        if (pt.x <= p.getRight() + kPad) h.part = Part::ruler;
        return h;
    }
    if (rows.empty() || pt.y >= p.getBottom() || pt.x > p.getRight() + 4.f) return h;   // the values on the right: nothing
    h.row = juce::jlimit (0, (int) rows.size() - 1, (int) ((pt.y - p.getY()) / rowHeight()));
    h.at = juce::jlimit (0.f, 1.f, pathOfX (pt.x));
    h.value = rangeOf (h.row).clamp (valueOf (h.row, pt.y));
    if ((h.handle = handleAt (h.row, pt)) != Handle::none)
    {
        h.part = Part::handle;
        return h;
    }
    if (pathOfX (pt.x) > 1.f + 4.f * pathPerPixel())
    {
        h.part = Part::tail;
        return h;
    }
    const auto& l = laneAt (h.row);
    float best = 7.f;
    for (int i = 0; i < l.count; ++i)
    {
        const auto& k = l.keys[(size_t) i];
        const float d = pt.getDistanceFrom ({ xOfPath (k.at), yOf (h.row, k.value) });
        if (d <= best)
        {
            best = d;
            h.key = i;
            h.part = Part::key;
        }
    }
    if (h.part == Part::key) return h;

    // The line: how far off it the mouse is, a step's vertical included.
    float d = std::abs (pt.y - yOf (h.row, edit::valueAfter (l, h.at)));
    h.key = (int) (std::upper_bound (l.keys.begin(), l.keys.begin() + l.count, h.at, [] (float t, const ba::Key& k) { return t < k.at; })
                   - l.keys.begin());
    for (int i = 1; i < l.count; ++i)
        if (l.keys[(size_t) i].at == l.keys[(size_t) i - 1].at)
        {
            const float x = xOfPath (l.keys[(size_t) i].at), ya = yOf (h.row, l.keys[(size_t) i - 1].value), yb = yOf (h.row, l.keys[(size_t) i].value);
            if (std::abs (pt.x - x) <= 4.f && pt.y >= std::min (ya, yb) - 2.f && pt.y <= std::max (ya, yb) + 2.f)
            {
                d = 0.f;
                h.key = i;
            }
        }
    h.part = d <= 4.f ? Part::line : d <= 11.f ? Part::nearLine : Part::background;
    return h;
}

std::pair<float, float> LaneEditor::selectionOrCell (float at) const
{
    if (sel.active()) return { sel.t0, sel.t1 };
    const float g = gridStep(), c = std::floor (std::min (at, 0.9999f) / g) * g;   // no selection: the grid cell (as Live)
    return { c, std::min (1.f, c + g) };
}

// ---- Gestures ----------------------------------------------------------------------

void LaneEditor::beginGesture (Gesture g, const Hit& h, const juce::MouseEvent& e)
{
    gesture = g;
    start = h;
    downAt = e.position;
    horizontal.reset();
    dragKey = -1;
    if (g == Gesture::scrub)
    {
        model.scrub (juce::jlimit (0.f, 1.f, (e.position.x - plot().getX()) / plot().getWidth()), -1);
        return;
    }
    if (g == Gesture::select)
    {
        selBefore = sel;
        const float at = snapAt (h.at, laneAt (h.row), {}, e.mods);
        sel = { at, at, h.row, h.row };
        repaint();
        return;
    }
    before = laneAt (h.row);
    model.beginEdit();
    const auto& l = before;
    switch (g)
    {
        case Gesture::maybeDelete:
        case Gesture::moveKeys:
        {
            gestureName = "Move keys";
            const auto [f, last] = selectedKeys (h.row);
            const bool inside = sel.covers (h.row) && h.key >= f && h.key <= last;
            runFirst = inside ? f : h.key;
            runLast = inside ? last : h.key;
            dragKey = h.key;
            selBefore = sel;
            break;
        }
        case Gesture::moveSegment:
            gestureName = "Move segment";
            if (h.key <= 0) runFirst = runLast = 0;                      // the hold before the first key
            else if (h.key >= l.count) runFirst = runLast = l.count - 1;  // after the last
            else { runFirst = h.key - 1; runLast = h.key; }
            break;
        case Gesture::shiftRange: gestureName = "Move selection"; break;
        case Gesture::bend:
        {
            gestureName = "Bend";
            if (h.key <= 0 || h.key >= l.count || l.keys[(size_t) h.key].at <= l.keys[(size_t) h.key - 1].at)
            {
                gesture = Gesture::none;   // no segment to bend there
                model.endEdit ({});
                return;
            }
            const auto &a = l.keys[(size_t) h.key - 1], &z = l.keys[(size_t) h.key];
            grabU = (h.at - a.at) / (z.at - a.at);
            grabValue = edit::valueAfter (l, h.at);
            break;
        }
        case Gesture::handle:
            gestureName = h.handle == Handle::left || h.handle == Handle::right ? "Stretch in time"
                        : h.handle == Handle::top || h.handle == Handle::bottom ? "Stretch" : "Skew";
            box = edit::extent (l, sel.t0, sel.t1);
            selBefore = sel;
            break;
        case Gesture::drawSteps:
            gestureName = "Draw";
            cells.clear();
            dragValueBase = h.value;
            drawCellsTo (h.at, h.value);
            break;
        case Gesture::drawFree:
            gestureName = "Draw";
            stroke = { { h.at, h.value, 1.f, 0.f } };
            break;
        case Gesture::none:
        case Gesture::scrub:
        case Gesture::select: break;
    }
    if (heldB) editedWhileB = true;
    dragGesture (e);
}

void LaneEditor::drawCellsTo (float at, float value)
{
    const float g = gridStep();
    const int n = std::max (1, (int) std::ceil (1.f / g - 1e-4f));
    const int to = juce::jlimit (0, n - 1, (int) (at / g));
    // Every cell between the last one drawn and this one, so a fast mouse leaves no gaps.
    int from = to;
    float fromValue = value;
    if (! cells.empty())
    {
        const auto& last = cells.back();
        from = juce::jlimit (0, n - 1, (int) std::round (last.from / g));
        fromValue = last.value;
    }
    const auto set = [&] (int c, float v) {
        const float a = (float) c * g, b = std::min (1.f, (float) (c + 1) * g);
        for (auto& cell : cells)
            if (std::abs (cell.from - a) < 1e-6f) { cell.value = v; return; }
        cells.push_back ({ a, b, v });
    };
    const int step = to >= from ? 1 : -1;
    for (int c = from; c != to + step; c += step)
    {
        const float u = to == from ? 1.f : (float) (c - from) / (float) (to - from);
        set (c, fromValue + (value - fromValue) * u);
    }
    std::sort (cells.begin(), cells.end(), [] (const edit::Cell& a, const edit::Cell& b) { return a.from < b.from; });
    // Keep them end to end: a cell drawn earlier stays where it is.
    std::vector<edit::Cell> run;
    for (const auto& c : cells)
        if (run.empty() || std::abs (run.back().to - c.from) < 1e-6f) run.push_back (c);
        else
        {
            // A gap (the stroke turned back past where it started): fill it from the lane.
            for (float a = run.back().to; a < c.from - 1e-6f; a += g)
                run.push_back ({ a, std::min (1.f, a + g), edit::valueAfter (before, a + 0.5f * g) });
            run.push_back (c);
        }
    cells = std::move (run);
}

void LaneEditor::dragGesture (const juce::MouseEvent& e)
{
    const auto d = e.position - downAt;
    const bool shift = e.mods.isShiftDown();
    if (gesture == Gesture::scrub)
    {
        model.scrub (juce::jlimit (0.f, 1.f, (e.position.x - plot().getX()) / plot().getWidth()), 0);
        return;
    }
    if (gesture == Gesture::none || start.row < 0) return;
    const int r = start.row;
    const auto range = rangeOf (r);
    const float inner = std::max (1.f, row (r).getHeight() - 16.f);
    // Shift: one axis (the one the mouse took), and finer up and down.
    float dx = d.x, dy = d.y;
    if (shift && (gesture == Gesture::moveKeys || gesture == Gesture::moveSegment))
    {
        if (! horizontal && d.getDistanceFromOrigin() > 4.f) horizontal = std::abs (d.x) > std::abs (d.y);
        if (horizontal.value_or (false)) dy = 0.f;
        else dx = 0.f;
    }
    const float fine = shift ? 0.1f : 1.f;
    const float dValue = -dy / inner * (range.hi - range.lo) * fine;
    const float mouseValue = range.clamp (valueOf (r, e.position.y));
    const float mouseAt = juce::jlimit (0.f, 1.f, pathOfX (e.position.x));

    switch (gesture)
    {
        case Gesture::maybeDelete:
            if (d.getDistanceFromOrigin() < 3.f) return;
            gesture = Gesture::moveKeys;
            [[fallthrough]];
        case Gesture::moveKeys:
        case Gesture::moveSegment:
        {
            const bool hold = gesture == Gesture::moveSegment && (start.key <= 0 || start.key >= before.count);
            const int anchor = gesture == Gesture::moveKeys ? start.key : runFirst;
            float dAt = hold ? 0.f : dx * pathPerPixel();
            if (dAt != 0.f && anchor >= 0 && anchor < before.count)
            {
                std::vector<int> moving;
                for (int i = runFirst; i <= runLast; ++i) moving.push_back (i);
                const float from = before.keys[(size_t) anchor].at;
                dAt = snapAt (from + dAt, before, moving, e.mods) - from;   // against the neighbours as the drag found them
            }
            ba::Lane l = before;
            const int first = edit::move (l, runFirst, runLast, dAt, dValue, range);
            dragKey = first + ((gesture == Gesture::moveKeys ? start.key : runFirst) - runFirst);
            apply (r, l);
            if (gesture == Gesture::moveKeys && selBefore.covers (r))
            {
                sel = selBefore;
                const float shiftBy = juce::jlimit (-sel.t0, 1.f - sel.t1, dAt);
                sel.t0 += shiftBy;
                sel.t1 += shiftBy;
            }
            break;
        }
        case Gesture::shiftRange:
        {
            ba::Lane l = before;
            edit::shiftRange (l, sel.t0, sel.t1, dValue, range);
            apply (r, l);
            break;
        }
        case Gesture::bend:
        {
            ba::Lane l = before;
            edit::bend (l, start.key, grabU, range.clamp (grabValue + dValue));
            apply (r, l);
            break;
        }
        case Gesture::select:
        {
            const float at = snapAt (mouseAt, laneAt (r), {}, e.mods);
            const int rowNow = rows.empty() ? r : juce::jlimit (0, (int) rows.size() - 1, (int) ((e.position.y - plot().getY()) / rowHeight()));
            const float from = snapAt (start.at, laneAt (r), {}, e.mods);
            sel = { std::min (from, at), std::max (from, at), std::min (r, rowNow), std::max (r, rowNow) };
            repaint();
            return;
        }
        case Gesture::handle:
        {
            ba::Lane l = before;
            const bool both = e.mods.isAltDown();   // Live: the opposite handle mirrors
            const auto [lo, hi] = box;
            const float t0 = selBefore.t0, t1 = selBefore.t1;
            if (start.handle == Handle::left || start.handle == Handle::right)
            {
                float dAt = d.x * pathPerPixel();
                const bool right = start.handle == Handle::right;
                if (! noSnap (e.mods) && snap) dAt = edit::snap ((right ? t1 : t0) + dAt, gridStep(), {}, 1.f) - (right ? t1 : t0);
                float n0 = t0, n1 = t1;
                (right ? n1 : n0) += dAt;
                if (both) (right ? n0 : n1) -= dAt;
                n0 = juce::jlimit (0.f, 1.f, n0);
                n1 = juce::jlimit (n0 + 1.f / 512.f, 1.f, n1);
                edit::stretchTime (l, t0, t1, n0, n1, shift, range);
                sel.t0 = n0;
                sel.t1 = n1;
            }
            else
            {
                edit::Box to { lo, hi, lo, hi };
                const auto h = start.handle;
                const auto move = [&] (float& edge, float& mirror) { edge += dValue; if (both) mirror -= dValue; };
                if (h == Handle::top) { to.topLeft += dValue; to.topRight += dValue; if (both) { to.bottomLeft -= dValue; to.bottomRight -= dValue; } }
                else if (h == Handle::bottom) { to.bottomLeft += dValue; to.bottomRight += dValue; if (both) { to.topLeft -= dValue; to.topRight -= dValue; } }
                else if (h == Handle::topLeft) move (to.topLeft, to.topRight);
                else if (h == Handle::topRight) move (to.topRight, to.topLeft);
                else if (h == Handle::bottomLeft) move (to.bottomLeft, to.bottomRight);
                else if (h == Handle::bottomRight) move (to.bottomRight, to.bottomLeft);
                edit::transform (l, t0, t1, lo, hi, to, range);
            }
            apply (r, l);
            break;
        }
        case Gesture::drawSteps:
        {
            const float v = shift ? range.clamp (dragValueBase + (mouseValue - dragValueBase) * 0.1f) : mouseValue;
            if (! shift) dragValueBase = mouseValue;
            drawCellsTo (mouseAt, v);
            ba::Lane l = before;
            edit::drawSteps (l, cells, range);
            apply (r, l);
            break;
        }
        case Gesture::drawFree:
        {
            stroke.push_back ({ mouseAt, mouseValue, 1.f, 0.f });
            ba::Lane l = before;
            edit::drawFreehand (l, stroke, 0.0025f * (range.hi - range.lo), range);
            apply (r, l);
            break;
        }
        case Gesture::none:
        case Gesture::scrub: break;
    }
    repaint();
}

void LaneEditor::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    entry.reset();
    const auto h = hitAt (e.position);
    if (e.mods.isPopupMenu())
    {
        showMenu (h);
        return;
    }
    if (h.part == Part::ruler)
    {
        beginGesture (Gesture::scrub, h, e);
        return;
    }
    if (h.row < 0 || h.part == Part::none || h.part == Part::tail)
    {
        sel = {};
        repaint();
        return;
    }
    if (drawMode())
    {
        beginGesture (noSnap (e.mods) || ! snap ? Gesture::drawFree : Gesture::drawSteps, h, e);
        return;
    }
    const bool inSelection = sel.covers (h.row) && h.at >= sel.t0 && h.at <= sel.t1;
    switch (h.part)
    {
        case Part::handle: beginGesture (Gesture::handle, h, e); break;
        case Part::key: beginGesture (Gesture::maybeDelete, h, e); break;
        case Part::line:
            if (e.mods.isAltDown()) beginGesture (Gesture::bend, h, e);
            else if (e.mods.isShiftDown()) beginGesture (inSelection ? Gesture::shiftRange : Gesture::moveSegment, h, e);
            else
            {
                // A key where the line was clicked, then dragged like any other.
                model.beginEdit();
                ba::Lane l = laneAt (h.row);
                const int i = edit::insert (l, snapAt (h.at, l, {}, e.mods), rangeOf (h.row));
                if (i < 0)
                {
                    model.endEdit ({});
                    model.say ("This lane is full (" + juce::String (ba::Lane::kMaxKeys) + " keys): Simplify Envelope makes room.");
                    return;
                }
                apply (h.row, l);
                model.endEdit ("Add key");
                Hit k = h;
                k.part = Part::key;
                k.key = i;
                beginGesture (Gesture::moveKeys, k, e);
                gestureName = "Add key";
            }
            break;
        case Part::nearLine:
            if (e.mods.isAltDown()) beginGesture (Gesture::bend, h, e);
            else beginGesture (inSelection ? Gesture::shiftRange : Gesture::moveSegment, h, e);
            break;
        case Part::background: beginGesture (Gesture::select, h, e); break;
        case Part::none:
        case Part::ruler:
        case Part::tail: break;
    }
}

void LaneEditor::mouseDrag (const juce::MouseEvent& e) { dragGesture (e); }

void LaneEditor::mouseUp (const juce::MouseEvent& e)
{
    const auto g = gesture;
    gesture = Gesture::none;
    if (g == Gesture::scrub) model.scrub (model.build(), 1);
    else if (g == Gesture::maybeDelete)
    {
        // A click on a key: it goes.
        ba::Lane l = before;
        edit::remove (l, start.key);
        apply (start.row, l);
        model.endEdit ("Delete key");
    }
    else if (g == Gesture::select)
    {
        if (! sel.active())
        {
            sel = {};
            marker = start.at;
            markerRow = start.row;
        }
    }
    else if (g != Gesture::none) model.endEdit (gestureName);
    dragKey = -1;
    hoverTo (hitAt (e.position));
    repaint();
}

void LaneEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto h = hitAt (e.position);
    if (h.part == Part::ruler)
    {
        model.scrub (0.f, -1);
        model.scrub (0.f, 1);
        return;
    }
    if (drawMode() || h.row < 0) return;
    if (e.mods.isAltDown() && (h.part == Part::line || h.part == Part::nearLine) && h.key > 0 && h.key < laneAt (h.row).count)
    {
        model.beginEdit();
        ba::Lane l = laneAt (h.row);
        edit::straighten (l, h.key);
        apply (h.row, l);
        model.endEdit ("Straighten");
    }
    else if (h.part == Part::background)
    {
        model.beginEdit();
        ba::Lane l = laneAt (h.row);
        if (edit::insert (l, snapAt (h.at, l, {}, e.mods), h.value, rangeOf (h.row)) >= 0) apply (h.row, l);
        model.endEdit ("Add key");
        sel = {};
    }
    repaint();
}

// ---- Commands ----------------------------------------------------------------------

void LaneEditor::command (int what)
{
    if (what == cmdSelectAll)
    {
        if (! rows.empty()) sel = { 0.f, 1.f, 0, (int) rows.size() - 1 };
        repaint();
        return;
    }
    if (what == cmdPaste)
    {
        auto& clips = clipboard();
        if (clips.empty() || rows.empty()) return;
        const float at = sel.active() ? sel.t0 : marker;
        const int r0 = sel.active() ? sel.first : juce::jlimit (0, (int) rows.size() - 1, markerRow < 0 ? hover.row : markerRow);
        if (r0 < 0) return;
        model.beginEdit();
        int r = r0;
        for (const auto& clip : clips)
        {
            if (r >= (int) rows.size()) break;
            ba::Lane l = laneAt (r);
            edit::paste (l, clip, at, rangeOf (r));
            apply (r, l);
            ++r;
        }
        model.endEdit ("Paste");
        sel = { at, std::min (1.f, at + clips.front().length), r0, r - 1 };
        repaint();
        return;
    }
    if (what == cmdSimplify && ! sel.active() && hover.row >= 0)
    {
        model.beginEdit();
        ba::Lane l = laneAt (hover.row);
        edit::simplify (l, 0.f, 1.f, 0.004f * (rangeOf (hover.row).hi - rangeOf (hover.row).lo), rangeOf (hover.row));
        apply (hover.row, l);
        model.endEdit ("Simplify Envelope");
        return;
    }
    if (! sel.active()) return;
    if (what == cmdCopy || what == cmdCut)
    {
        clipboard().clear();
        for (int r = sel.first; r <= sel.last; ++r) clipboard().push_back (edit::copy (laneAt (r), sel.t0, sel.t1, rangeOf (r)));
        if (what == cmdCopy) return;
    }
    const juce::String name = what == cmdCut ? "Cut" : what == cmdDuplicate ? "Duplicate" : what == cmdSimplify ? "Simplify Envelope" : "Delete";
    model.beginEdit();
    for (int r = sel.first; r <= sel.last; ++r)
    {
        ba::Lane l = laneAt (r);
        const auto range = rangeOf (r);
        if (what == cmdDuplicate) edit::paste (l, edit::copy (l, sel.t0, sel.t1, range), sel.t1, range);
        else if (what == cmdSimplify) edit::simplify (l, sel.t0, sel.t1, 0.004f * (range.hi - range.lo), range);
        else edit::erase (l, sel.t0, sel.t1, range);
        apply (r, l);
    }
    model.endEdit (name);
    if (what == cmdDuplicate)
    {
        const float len = sel.t1 - sel.t0;
        sel.t0 = sel.t1;
        sel.t1 = std::min (1.f, sel.t1 + len);
    }
    repaint();
}

void LaneEditor::insertShape (edit::Shape s)
{
    const int r0 = sel.active() ? sel.first : hover.row, r1 = sel.active() ? sel.last : hover.row;
    if (r0 < 0) return;
    const auto [t0, t1] = selectionOrCell (hover.at);
    model.beginEdit();
    for (int r = r0; r <= r1; ++r)
    {
        ba::Lane l = laneAt (r);
        edit::insertShape (l, t0, t1, s, rangeOf (r));
        apply (r, l);
    }
    model.endEdit ("Insert shape");
    repaint();
}

void LaneEditor::showMenu (const Hit& h)
{
    if (h.row < 0 || h.part == Part::ruler || h.part == Part::tail) return;
    hover = h;
    juce::PopupMenu m;
    const auto item = [&m] (int id, const juce::String& text, const juce::String& keys, bool enabled = true) {
        juce::PopupMenu::Item i (text);
        i.itemID = id;
        i.isEnabled = enabled;
        i.shortcutKeyDescription = keys;
        m.addItem (i);
    };
   #if JUCE_MAC
    const juce::String cmd ("Cmd ");
   #else
    const juce::String cmd ("Ctrl ");
   #endif
    const auto& l = laneAt (h.row);
    if (h.part == Part::key)
    {
        item (cmdEditValue, juce::String (juce::CharPointer_UTF8 ("Edit Value\xe2\x80\xa6")), {});
        item (cmdDeleteKey, "Delete", {});
        m.addSeparator();
    }
    else if (h.part == Part::line || h.part == Part::nearLine)
    {
        item (cmdAddValue, juce::String (juce::CharPointer_UTF8 ("Add Value\xe2\x80\xa6")), {});
        if (h.key > 0 && h.key < l.count && l.keys[(size_t) h.key].curve != 1.f) item (cmdStraighten, "Straighten", kBend + " double-click");
        m.addSeparator();
    }
    m.addSectionHeader (sel.active() ? "Insert shape" : "Insert shape (grid cell)");
    for (int i = 0; i < (int) std::size (kShapes); ++i)
    {
        if (i == 5) m.addSeparator();
        item (cmdShape + i, kShapes[i].name, {});
    }
    m.addSeparator();
    item (cmdSimplify, "Simplify Envelope", {});
    m.addSeparator();
    item (cmdCut, "Cut", cmd + "X", sel.active());
    item (cmdCopy, "Copy", cmd + "C", sel.active());
    item (cmdPaste, "Paste", cmd + "V", ! clipboard().empty());
    item (cmdDuplicate, "Duplicate", cmd + "D", sel.active());
    item (cmdDelete, "Delete", "Del", sel.active());
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition().withStandardItemHeight (22),
                     [safe = juce::Component::SafePointer<LaneEditor> (this), h] (int id) {
                         if (safe == nullptr || id == 0) return;
                         auto& self = *safe;
                         if (id >= cmdShape) self.insertShape (kShapes[(size_t) (id - cmdShape)].shape);
                         else if (id == cmdEditValue) self.editValue (h, false);
                         else if (id == cmdAddValue) self.editValue (h, true);
                         else if (id == cmdDeleteKey || id == cmdStraighten)
                         {
                             self.model.beginEdit();
                             ba::Lane edited = self.laneAt (h.row);
                             if (id == cmdDeleteKey) edit::remove (edited, h.key);
                             else edit::straighten (edited, h.key);
                             self.apply (h.row, edited);
                             self.model.endEdit (id == cmdDeleteKey ? "Delete key" : "Straighten");
                         }
                         else self.command (id);
                     });
}

void LaneEditor::editValue (const Hit& h, bool addNew)
{
    const auto& l = laneAt (h.row);
    if (! addNew && (h.key < 0 || h.key >= l.count)) return;
    const float at = addNew ? h.at : l.keys[(size_t) h.key].at;
    const float v = addNew ? edit::valueAfter (l, h.at) : l.keys[(size_t) h.key].value;
    entry = std::make_unique<juce::TextEditor>();
    entry->setFont (mono (10.5f));
    entry->setJustification (juce::Justification::centred);
    entry->setIndents (2, 1);
    entry->setText (model.words (laneOf (h.row), v), false);
    const auto spot = juce::Point<float> (xOfPath (at), yOf (h.row, v));
    entry->setBounds (juce::Rectangle<float> (88.f, 18.f).withCentre (spot.translated (0.f, spot.y - 20.f < row (h.row).getY() ? 18.f : -18.f))
                          .constrainedWithin (getLocalBounds().toFloat())
                          .toNearestInt());
    const auto done = [safe = juce::Component::SafePointer<LaneEditor> (this)] {
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->entry.reset(); });
    };
    entry->onReturnKey = [this, h, at, addNew, done] {
        if (const auto value = model.parse (laneOf (h.row), entry->getText()))
        {
            model.beginEdit();
            ba::Lane edited = laneAt (h.row);
            const auto range = rangeOf (h.row);
            if (addNew) edit::insert (edited, at, *value, range);
            else if (h.key < edited.count) edited.keys[(size_t) h.key].value = range.clamp (*value);
            apply (h.row, edited);
            model.endEdit (addNew ? "Add value" : "Edit value");
        }
        done();
    };
    entry->onEscapeKey = done;
    entry->onFocusLost = done;
    addAndMakeVisible (*entry);
    entry->selectAll();
    entry->grabKeyboardFocus();
}

bool LaneEditor::keyPressed (const juce::KeyPress& k)
{
    const auto mods = k.getModifiers();
    const auto key = upper (k.getKeyCode());
    if (key == 'B' && ! mods.isCommandDown() && ! mods.isAltDown() && ! mods.isCtrlDown())
    {
        if (! heldB)
        {
            heldB = true;
            editedWhileB = false;
            updateCursor (hover, mods);
            repaint();
            if (onModeChange) onModeChange();
        }
        return true;
    }
    if (mods.isCommandDown() && ! mods.isAltDown())
    {
        switch (key)
        {
            case '1': setGrid (gridIndex + 1); return true;   // narrower
            case '2': setGrid (gridIndex - 1); return true;   // wider
            case '3': setTriplets (! triplet); return true;
            case '4': setSnap (! snap); return true;
            case 'A': command (cmdSelectAll); return true;
            case 'C': command (cmdCopy); return true;
            case 'X': command (cmdCut); return true;
            case 'V': command (cmdPaste); return true;
            case 'D': command (cmdDuplicate); return true;
            default: break;
        }
    }
    if (k.isKeyCode (juce::KeyPress::deleteKey) || k.isKeyCode (juce::KeyPress::backspaceKey))
    {
        command (cmdDelete);
        return true;
    }
    if (k.isKeyCode (juce::KeyPress::escapeKey) && sel.active())
    {
        sel = {};
        repaint();
        return true;
    }
    return false;
}

bool LaneEditor::keyStateChanged (bool)
{
    if (heldB && ! juce::KeyPress::isKeyCurrentlyDown ('b') && ! juce::KeyPress::isKeyCurrentlyDown ('B'))
    {
        heldB = false;
        if (! editedWhileB) draw = ! draw;   // a tap: Draw Mode stays the other way
        updateCursor (hover, juce::ModifierKeys::getCurrentModifiers());
        repaint();
        if (onModeChange) onModeChange();
    }
    return false;
}

// ---- Hover -------------------------------------------------------------------------

void LaneEditor::updateCursor (const Hit& h, const juce::ModifierKeys& mods)
{
    using C = juce::MouseCursor;
    auto c = C (C::NormalCursor);
    if (h.part == Part::ruler) c = C::LeftRightResizeCursor;
    else if (h.part == Part::tail || h.row < 0) c = C::NormalCursor;
    else if (drawMode()) c = pencil;
    else
        switch (h.part)
        {
            case Part::key: c = C::PointingHandCursor; break;
            case Part::line: c = mods.isAltDown() ? C::UpDownResizeCursor : C::NormalCursor; break;
            case Part::nearLine: c = C::UpDownResizeCursor; break;
            case Part::background: c = C::IBeamCursor; break;
            case Part::handle:
                c = h.handle == Handle::top || h.handle == Handle::bottom ? C::UpDownResizeCursor
                  : h.handle == Handle::left || h.handle == Handle::right ? C::LeftRightResizeCursor
                  : h.handle == Handle::topLeft || h.handle == Handle::bottomRight ? C::TopLeftCornerResizeCursor : C::TopRightCornerResizeCursor;
                break;
            case Part::none:
            case Part::ruler:
            case Part::tail: break;
        }
    setMouseCursor (c);
}

juce::String LaneEditor::describe (const Hit& h) const
{
    const auto& dot = theme::dot;
    if (h.part == Part::ruler) return "BUILD: drag to scrub it" + dot + "double-click to drop it to 0.";
    if (h.part == Part::tail)
        return juce::String (juce::CharPointer_UTF8 ("Past 75 % every preset fades into its tail: the reverb comes up, the level goes to \xe2\x88\x92" "20 dB. Shared by all presets, not editable."));
    if (h.row < 0) return {};
    const auto id = laneOf (h.row);
    if (drawMode())
        return "Draw Mode (B): drag to draw steps " + gridName (gridIndex) + (triplet ? " T" : "") + " wide" + dot + "Shift finer" + dot + kNoSnap + " freehand.";
    const auto& l = laneAt (h.row);
    switch (h.part)
    {
        case Part::key:
        {
            const auto& k = l.keys[(size_t) h.key];
            return model.words (id, k.value) + " at " + fmt::fixed (k.at * ba::kPathEnd * 100.f, 1) + " % BUILD" + dot + "drag to move it (" + kNoSnap
                   + ": no snap, Shift: one axis)" + dot + "click to delete it" + dot + "right-click: Edit Value.";
        }
        case Part::line: return "Click to add a key" + dot + kBend + "-drag to bend the segment" + dot + "Shift-drag to move it.";
        case Part::nearLine: return "Drag to move this segment" + dot + kBend + "-drag to bend it" + dot + kBend + "-double-click to straighten it.";
        case Part::handle:
            return h.handle == Handle::left || h.handle == Handle::right ? "Drag to stretch the selection in time (Shift: the rest makes room, " + kBend + ": both sides)."
                 : h.handle == Handle::top || h.handle == Handle::bottom ? "Drag to stretch the selection up or down (Shift: finer, " + kBend + ": both edges)."
                                                                          : "Drag to skew the selection (Shift: finer, " + kBend + ": the other corner too).";
        case Part::background: return "Drag for a time selection" + dot + "double-click for a key here" + dot + "right-click: shapes, Simplify Envelope.";
        case Part::none:
        case Part::ruler:
        case Part::tail: break;
    }
    return {};
}

void LaneEditor::hoverTo (const Hit& h)
{
    const bool same = h.part == hover.part && h.row == hover.row && h.key == hover.key && h.handle == hover.handle;
    const bool lineMoved = (h.part == Part::line || hover.part == Part::line) && std::abs (h.at - hover.at) > 1e-6f;
    hover = h;
    updateCursor (h, juce::ModifierKeys::getCurrentModifiers());
    if (! same) model.say (describe (h));
    if (! same || lineMoved) repaint();
}

void LaneEditor::mouseMove (const juce::MouseEvent& e) { hoverTo (hitAt (e.position)); }

void LaneEditor::modifierKeysChanged (const juce::ModifierKeys& mods)
{
    updateCursor (hover, mods);
    if (hover.part == Part::line || hover.part == Part::nearLine) repaint();
}

void LaneEditor::mouseExit (const juce::MouseEvent&)
{
    hover = {};
    model.say ({});
    repaint();
}

// ---- Painting ------------------------------------------------------------------------

void LaneEditor::paint (juce::Graphics& g)
{
    g.fillAll (col::sheet);
    const auto p = plot();
    const float playX = xOfBuild (model.build());

    // What the chain runs along BUILD: as designed, and as the intensities have it.
    constexpr int kSteps = 240;
    std::vector<ba::Settings> along ((size_t) kSteps + 1), played ((size_t) kSteps + 1);
    for (int k = 0; k <= kSteps; ++k)
    {
        along[(size_t) k] = model.run ((float) k / kSteps, true);
        played[(size_t) k] = model.run ((float) k / kSteps, false);
    }

    // Ruler: BUILD in quarters, the grid along the path, the tail named.
    {
        g.setFont (mono (9.f));
        const float base = kRuler - 4.f;
        g.setColour (col::hairline);
        g.fillRect (p.getX(), base, p.getWidth(), 1.f);
        const float stepX = xOfPath (gridStep()) - xOfPath (0.f);
        if (stepX >= 4.f)
            for (float t = 0.f; t <= 1.0001f; t += gridStep())
            {
                g.setColour (col::inkFaint);
                g.fillRect (xOfPath (t) - 0.5f, base - 3.f, 1.f, 3.f);
            }
        for (int q = 0; q <= 4; ++q)
        {
            const float x = xOfBuild ((float) q / 4.f);
            g.setColour (col::inkSoft);
            g.fillRect (x - 0.5f, base - 6.f, 1.f, 6.f);
            const auto t = q == 4 ? juce::String ("100 % BUILD") : juce::String (q * 25) + " %";
            const auto j = q == 0 ? juce::Justification::centredLeft : q == 4 ? juce::Justification::centredRight : juce::Justification::centred;
            const auto r = q == 0 ? juce::Rectangle<float> (x + 3.f, 2.f, 60.f, 12.f)
                         : q == 4 ? juce::Rectangle<float> (x - 83.f, 2.f, 80.f, 12.f)
                                  : juce::Rectangle<float> (x - 30.f, 2.f, 60.f, 12.f);
            g.drawText (t, r, j, false);
        }
        caption (g, "Tail", { xOfPath (1.f), 2.f, xOfBuild (1.f) - xOfPath (1.f) - 84.f, 12.f }, juce::Justification::centred, col::inkFaint, 9.f);
        g.setColour (col::accent);
        juce::Path head;
        head.addTriangle (playX - 4.f, base - 7.f, playX + 4.f, base - 7.f, playX, base);
        g.fillPath (head);
    }

    if (rows.empty())
    {
        g.setFont (title (15.f));
        g.setColour (col::inkSoft);
        g.drawText (juce::String (juce::CharPointer_UTF8 ("No lanes yet \xe2\x80\x94 add one with + LANE, then draw its path.")), p,
                    juce::Justification::centred, false);
        return;
    }

    for (int r = 0; r < (int) rows.size(); ++r) paintRow (g, r, along, played);

    // The playhead through every row.
    g.setColour (col::accent);
    g.fillRect (playX - 0.5f, kRuler - 4.f, 1.f, p.getBottom() - kRuler + 4.f);
}

void LaneEditor::paintRow (juce::Graphics& g, int r, const std::vector<ba::Settings>& along, const std::vector<ba::Settings>& played)
{
    const auto rr = row (r);
    const auto id = laneOf (r);
    const auto& l = laneAt (r);
    const float xEnd = xOfPath (1.f), playX = xOfBuild (model.build());
    const auto paper = rr.withTrimmedBottom (1.f).withLeft (0.f);   // from the left edge: keys at 0 % show whole
    g.setColour (col::hairline);
    g.fillRect (0.f, rr.getBottom() - 1.f, (float) getWidth(), 1.f);

    // Graph paper: the well, the grid along the path, the tail hatched, guides.
    g.setColour (col::well);
    g.fillRect (paper);
    {
        juce::Path minor, major;
        const float stepX = xOfPath (gridStep()) - xOfPath (0.f);
        if (stepX >= 4.f)
            for (float t = gridStep(); t < 0.9999f; t += gridStep())
            {
                const float x = std::round (xOfPath (t)) + 0.5f;
                minor.startNewSubPath (x, paper.getY());
                minor.lineTo (x, paper.getBottom());
            }
        for (int q = 1; q < 4; ++q)
        {
            const float x = std::round (xOfPath ((float) q / 4.f)) + 0.5f;
            major.startNewSubPath (x, paper.getY());
            major.lineTo (x, paper.getBottom());
        }
        g.setColour (col::grid);
        g.strokePath (minor, juce::PathStrokeType (1.f));
        g.setColour (col::hairline);
        g.strokePath (major, juce::PathStrokeType (1.f));
    }
    const auto tail = paper.withLeft (xEnd);
    {
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (tail.toNearestInt());
        hatch (g, tail, col::hairline, 5.f, 1.f);
    }
    g.setColour (col::hairline);
    g.fillRect (xEnd, paper.getY(), 1.f, paper.getHeight());
    const auto marks = guides (id);
    for (const auto& [v, text] : marks)
    {
        const float y = std::round (yOf (r, v)) + 0.5f;
        if (y < paper.getY() + 2.f || y > paper.getBottom() - 2.f) continue;
        g.setColour (id == ba::LaneId::level && v == 0.f ? col::hairline : col::grid);
        g.fillRect (paper.getX(), y - 0.5f, xEnd - paper.getX(), 1.f);
    }

    // The time selection, behind the line.
    if (sel.covers (r))
    {
        const auto s = juce::Rectangle<float>::leftTopRightBottom (xOfPath (sel.t0), paper.getY(), xOfPath (sel.t1), paper.getBottom());
        g.setColour (col::ink.withAlpha (0.07f));
        g.fillRect (s);
        g.setColour (col::inkSoft);
        g.fillRect (s.getX(), s.getY(), 1.f, s.getHeight());
        g.fillRect (s.getRight() - 1.f, s.getY(), 1.f, s.getHeight());
    }

    {
    juce::Graphics::ScopedSaveState clip (g);
    g.reduceClipRegion (paper.toNearestInt());

    // The path as edited: each segment exactly, steps vertical.
    juce::Path line;
    if (l.count > 0)
    {
        line.startNewSubPath (paper.getX(), yOf (r, l.keys[0].value));
        line.lineTo (xOfPath (l.keys[0].at), yOf (r, l.keys[0].value));
        for (int i = 1; i < l.count; ++i)
        {
            const auto &a = l.keys[(size_t) i - 1], &z = l.keys[(size_t) i];
            const float xa = xOfPath (a.at), xz = xOfPath (z.at);
            const int n = z.curve == 1.f ? 1 : std::max (2, (int) ((xz - xa) / 2.f));
            for (int j = 1; j <= n; ++j)
            {
                const float u = (float) j / (float) n;
                line.lineTo (xa + (xz - xa) * u, yOf (r, edit::segment (a, z, u)));
            }
        }
        line.lineTo (xEnd, yOf (r, l.keys[(size_t) l.count - 1].value));
    }

    // What plays along BUILD (the lane's stairs, the global tail), and as the
    // INTENSITY knob has it.
    const auto trace = [&] (const std::vector<ba::Settings>& s, float fromBuild) {
        juce::Path t;
        const int n = (int) s.size() - 1, k0 = (int) std::floor (fromBuild * (float) n);
        for (int k = k0; k <= n; ++k)
        {
            const juce::Point<float> pt (xOfBuild ((float) k / (float) n), yOf (r, settingOf (id, s[(size_t) k])));
            if (k == k0) t.startNewSubPath (pt);
            else t.lineTo (pt);
        }
        return t;
    };
    g.setColour (col::inkFaint);
    g.strokePath (trace (along, ba::kPathEnd), juce::PathStrokeType (1.2f));
    bool turned = false;
    for (size_t k = 0; k < played.size() && ! turned; ++k) turned = std::abs (settingOf (id, played[k]) - settingOf (id, along[k])) > 1e-5f;
    if (turned)
    {
        juce::Path dashed;
        const float dashes[] = { 3.f, 3.f };
        juce::PathStrokeType (1.2f).createDashedStroke (dashed, trace (played, 0.f), dashes, 2);
        g.setColour (col::inkSoft);
        g.fillPath (dashed);
    }
    if (l.step > 0.f && l.count > 0)
    {
        juce::Path stairs;
        for (float x = paper.getX(); x <= xEnd; x += 1.f)
        {
            const juce::Point<float> pt (x, yOf (r, l (juce::jlimit (0.f, 1.f, pathOfX (x)))));
            if (x == paper.getX()) stairs.startNewSubPath (pt);
            else stairs.lineTo (pt);
        }
        g.setColour (col::inkSoft);
        g.strokePath (stairs, juce::PathStrokeType (1.f));
    }

    const bool bending = (hover.row == r && hover.part != Part::key && juce::ModifierKeys::getCurrentModifiers().isAltDown()
                          && (hover.part == Part::line || hover.part == Part::nearLine)) || (gesture == Gesture::bend && start.row == r);
    g.setColour (col::ink);
    g.strokePath (line, juce::PathStrokeType (1.4f));

    // Lit from where it starts moving up to the playhead, filled down to its
    // start, as on the timeline.
    if (l.count > 0)
    {
        const float startValue = l.keys[0].value;
        float leaves = 2.f;
        for (int i = 0; i < l.count && leaves > 1.f; ++i)
            if (std::abs (l.keys[(size_t) i].value - startValue) > 1e-6f) leaves = i > 0 ? l.keys[(size_t) i - 1].at : 0.f;
        const float from = xOfPath (std::min (leaves, 1.f));
        if (playX > from && leaves <= 1.f)
        {
            juce::Graphics::ScopedSaveState lit (g);
            g.reduceClipRegion (juce::Rectangle<float>::leftTopRightBottom (from, paper.getY(), std::min (playX, xEnd), paper.getBottom()).toNearestInt());
            auto fill = line;
            fill.lineTo (xEnd, yOf (r, startValue));
            fill.lineTo (paper.getX(), yOf (r, startValue));
            fill.closeSubPath();
            g.setColour (col::accent.withAlpha (0.1f));
            g.fillPath (fill, {});
            g.setColour (col::accent);
            g.strokePath (line, juce::PathStrokeType (1.8f));
        }
    }
    if (bending)
    {
        const int k = gesture == Gesture::bend ? start.key : hover.key;
        if (k > 0 && k < l.count)
        {
            juce::Graphics::ScopedSaveState seg (g);
            g.reduceClipRegion (juce::Rectangle<float>::leftTopRightBottom (xOfPath (l.keys[(size_t) k - 1].at), paper.getY(),
                                                                           xOfPath (l.keys[(size_t) k].at), paper.getBottom()).toNearestInt());
            g.strokePath (line, juce::PathStrokeType (2.6f));
        }
    }

    // The guides' words, on the row under the mouse only, each on a patch of well.
    if (hover.row == r || (gesture != Gesture::none && start.row == r))
    {
        g.setFont (mono (8.5f));
        for (const auto& [v, text] : marks)
        {
            const float y = std::round (yOf (r, v)) + 0.5f;
            if (text.isEmpty() || y < paper.getY() + 12.f || y > paper.getBottom() - 2.f) continue;
            const auto box = juce::Rectangle<float> (textWidth (g.getCurrentFont(), text) + 6.f, 11.f).withPosition (kPad + 6.f, y - 12.f);
            g.setColour (col::well);
            g.fillRect (box);
            g.setColour (col::inkSoft);
            g.drawText (text, box, juce::Justification::centred, false);
        }
    }

    // Keys: hollow, inked when selected; the hovered or dragged one larger.
    const bool selected = sel.covers (r);
    for (int i = 0; i < l.count; ++i)
    {
        const auto& k = l.keys[(size_t) i];
        const bool in = selected && k.at >= sel.t0 && k.at <= sel.t1;
        const bool hot = (hover.row == r && hover.part == Part::key && hover.key == i) || (start.row == r && dragKey == i && gesture != Gesture::none);
        const auto b = juce::Rectangle<float> (hot ? 9.f : 7.f, hot ? 9.f : 7.f).withCentre ({ xOfPath (k.at), yOf (r, k.value) });
        g.setColour (in || hot ? col::ink : col::well);
        g.fillRoundedRectangle (b, kRadius);
        g.setColour (col::ink);
        g.drawRoundedRectangle (b.reduced (0.5f), kRadius, 1.2f);
    }
    // Where a click on the line puts a key.
    if (! drawMode() && hover.row == r && hover.part == Part::line && gesture == Gesture::none && ! juce::ModifierKeys::getCurrentModifiers().isAltDown())
    {
        const auto b = juce::Rectangle<float> (7.f, 7.f).withCentre ({ xOfPath (hover.at), yOf (r, edit::valueAfter (l, hover.at)) });
        g.setColour (col::inkSoft);
        g.drawRoundedRectangle (b.reduced (0.5f), kRadius, 1.f);
    }
    // The selection's handles, while the mouse is on it.
    if (const auto b = handleBox (r); b && (gesture == Gesture::handle || (hover.row == r && hover.at >= sel.t0 && hover.at <= sel.t1)))
    {
        g.setColour (col::inkSoft);
        g.drawRect (*b, 1.f);
        for (const auto& spot : { b->getTopLeft(), juce::Point<float> (b->getCentreX(), b->getY()), b->getTopRight(),
                                  juce::Point<float> (b->getX(), b->getCentreY()), juce::Point<float> (b->getRight(), b->getCentreY()),
                                  b->getBottomLeft(), juce::Point<float> (b->getCentreX(), b->getBottom()), b->getBottomRight() })
        {
            const auto h = juce::Rectangle<float> (6.f, 6.f).withCentre (spot);
            g.setColour (col::ink);
            g.fillRect (h);
        }
    }
    // A value box by the key under the mouse, in the knob's words.
    const int boxKey = gesture != Gesture::none && start.row == r ? dragKey : hover.row == r && hover.part == Part::key ? hover.key : -1;
    if (boxKey >= 0 && boxKey < l.count)
    {
        const auto& k = l.keys[(size_t) boxKey];
        const auto text = model.words (id, k.value);
        g.setFont (mono (10.f));
        const float w = textWidth (g.getCurrentFont(), text) + 12.f;
        const juce::Point<float> at (xOfPath (k.at), yOf (r, k.value));
        auto b = juce::Rectangle<float> (w, 16.f).withPosition (at.x + 8.f, at.y - 22.f);
        if (b.getRight() > xEnd) b.setX (at.x - 8.f - w);
        if (b.getY() < paper.getY()) b.setY (at.y + 6.f);
        g.setColour (col::sheet);
        g.fillRoundedRectangle (b, kRadius);
        g.setColour (col::inkSoft);
        g.drawRoundedRectangle (b.reduced (0.5f), kRadius, 1.f);
        g.setColour (col::ink);
        g.drawText (text, b, juce::Justification::centred, false);
    }
    }

    // On the right, what it plays at the playhead, in the knob's words; at rest it steps back.
    const float now = settingOf (id, model.run (model.build(), false)), rest = settingOf (id, model.run (0.f, false));
    g.setFont (mono (10.f));
    g.setColour (std::abs (now - rest) > 1e-5f ? col::ink : col::inkFaint);
    g.drawText (model.words (id, now), juce::Rectangle<float> (rr.getRight() + kPad, rr.getY(), kValueW, rr.getHeight()),
                juce::Justification::centredLeft, false);
}
