#include "LaneEditor.h"

#include "fx/CutFilters.h"
#include "fx/Mapping.h"

#include <cmath>

using namespace theme;
namespace edit = ba::edit;
namespace map = ba::map;

namespace
{
    // The grids, in path terms: on a 16-bar build 1/16 is a bar, 1/64 a beat,
    // 1/256 a sixteenth.
    constexpr float kGrids[] = { 1.f / 4.f, 1.f / 8.f, 1.f / 16.f, 1.f / 32.f, 1.f / 64.f, 1.f / 128.f, 1.f / 256.f };
    static_assert (std::size (kGrids) == LaneEditor::kGridCount);
    // The adaptive grid's densities, Narrowest to Widest: the least room a cell gets.
    constexpr float kDensities[] = { 6.f, 12.f, 24.f, 48.f, 96.f };
    // The narrowest view: 2 % of BUILD (a third of a bar on a 16-bar build).
    constexpr float kMinSpan = 0.02f;

    // Live's modifiers: Cmd (Mac) / Alt (Windows) takes the snap off; Option /
    // Alt bends a segment. Names for the footer and the menu.
   #if JUCE_MAC
    bool noSnap (const juce::ModifierKeys& m) { return m.isCommandDown(); }
    const juce::String kNoSnap ("Cmd"), kBend ("Option"), kCmd ("Cmd ");
   #else
    bool noSnap (const juce::ModifierKeys& m) { return m.isAltDown(); }
    const juce::String kNoSnap ("Alt"), kBend ("Alt"), kCmd ("Ctrl ");
   #endif

    // One clipboard for every editor window: copy in one, paste in another.
    std::vector<edit::Clip>& clipboard()
    {
        static std::vector<edit::Clip> c;
        return c;
    }

    enum Command { cmdEditValue = 1, cmdDeleteKey, cmdAddValue, cmdStraighten, cmdCut, cmdCopy, cmdPaste, cmdDuplicate, cmdDelete,
                   cmdSelectAll, cmdSimplify, cmdDraw, cmdNarrow, cmdWiden, cmdTriplet, cmdSnap, cmdAdaptive, cmdRemoveLane,
                   cmdZoomSelection, cmdZoomBack, cmdZoomIn, cmdZoomOut, cmdZoomAll, cmdShape = 100 };

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

    // A value box: the words on a patch of sheet, outlined.
    void valueBox (juce::Graphics& g, const juce::String& text, juce::Point<float> at, juce::Rectangle<float> within)
    {
        g.setFont (mono (10.f));
        const float w = textWidth (g.getCurrentFont(), text) + 12.f;
        auto b = juce::Rectangle<float> (w, 16.f).withPosition (at.x + 8.f, at.y - 22.f);
        if (b.getRight() > within.getRight()) b.setX (at.x - 8.f - w);
        if (b.getY() < within.getY()) b.setY (at.y + 6.f);
        g.setColour (col::sheet);
        g.fillRoundedRectangle (b, kRadius);
        g.setColour (col::inkSoft);
        g.drawRoundedRectangle (b.reduced (0.5f), kRadius, 1.f);
        g.setColour (col::ink);
        g.drawText (text, b, juce::Justification::centred, false);
    }
} // namespace

LaneEditor::LaneEditor (LaneModel& m) : model (m), pencil (makePencil())
{
    setOpaque (true);
    setWantsKeyboardFocus (true);
    rows = model.lanes();
}

juce::String LaneEditor::gridName (int g)
{
    constexpr const char* names[] = { "1/4", "1/8", "1/16", "1/32", "1/64", "1/128", "1/256" };
    return names[juce::jlimit (0, kGridCount - 1, g)];
}

int LaneEditor::grid() const
{
    if (! adaptiveGrid) return gridIndex;
    // The finest grid whose cells still get the density's room at this zoom.
    const float pxPerPath = plot().getWidth() * ba::kPathEnd / (view1 - view0) * (triplet ? 2.f / 3.f : 1.f);
    int g = 0;
    while (g + 1 < kGridCount && kGrids[g + 1] * pxPerPath >= kDensities[density]) ++g;
    return g;
}

void LaneEditor::narrower()
{
    if (adaptiveGrid)
    {
        if (density == 0) return;
        --density;
        repaint();
        if (onModeChange) onModeChange();
    }
    else setGrid (gridIndex + 1);
}

void LaneEditor::wider()
{
    if (adaptiveGrid)
    {
        if (density == (int) std::size (kDensities) - 1) return;
        ++density;
        repaint();
        if (onModeChange) onModeChange();
    }
    else setGrid (gridIndex - 1);
}

void LaneEditor::setAdaptive (bool on)
{
    if (on == adaptiveGrid) return;
    if (! on) gridIndex = grid();   // fixed where it was
    adaptiveGrid = on;
    repaint();
    if (onModeChange) onModeChange();
}

float LaneEditor::valueIn (ba::LaneId id, const ba::Settings& s)
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
    if (g == gridIndex && ! adaptiveGrid) return;
    gridIndex = g;
    adaptiveGrid = false;
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
    // The playhead's strip: what it lit or unlit on the way; its tick on the overview.
    repaint (juce::Rectangle<float> (std::min (x0, x1) - 4.f, 0.f, std::abs (x1 - x0) + 8.f, (float) getHeight()).getSmallestIntegerContainer());
    repaint (0, 0, getWidth(), (int) kOverview);
}

void LaneEditor::presetChanged()
{
    rows = model.lanes();
    sel = {};
    gesture = Gesture::none;
    hover = {};
    entry.reset();
    keyEditing = false;   // the preset it belonged to is gone
    dropKeys();
    if (cursorRow >= (int) rows.size()) cursorRow = -1;   // the same lanes after an undo or a copy: the keys stay on theirs
    scrollTo (scroll);
    if (onScroll) onScroll();
    repaint();
}

void LaneEditor::contentChanged()
{
    const bool changed = rows != model.lanes();
    rows = model.lanes();
    if (cursorRow >= (int) rows.size()) cursorRow = (int) rows.size() - 1;
    if (keyKey >= 0 && (cursorRow < 0 || keyKey >= laneAt (cursorRow).count)) dropKeys();
    if (changed)
    {
        sel = {};
        scrollTo (scroll);
        if (onScroll) onScroll();
    }
    repaint();
}

void LaneEditor::resized()
{
    scrollTo (scroll);
    if (onScroll) onScroll();
    if (onModeChange) onModeChange();   // an adaptive grid follows the width too
}

// ---- Geometry ----------------------------------------------------------------------

juce::Rectangle<float> LaneEditor::rowsArea() const { return getLocalBounds().toFloat().withTrimmedTop (kTop); }

juce::Rectangle<float> LaneEditor::plot() const { return rowsArea().withTrimmedLeft (kGap + kPad).withTrimmedRight (kPad); }

// The overview: the whole build along the same span as the rows' time axis, 10 px high.
juce::Rectangle<float> LaneEditor::overview() const
{
    const auto p = plot();
    return { p.getX(), 0.f, p.getWidth(), kOverview - 6.f };
}

float LaneEditor::buildOfX (float x) const
{
    const auto p = plot();
    return view0 + (x - p.getX()) / p.getWidth() * (view1 - view0);
}

float LaneEditor::buildOfOverviewX (float x) const
{
    const auto o = overview();
    return juce::jlimit (0.f, 1.f, (x - o.getX()) / o.getWidth());
}

void LaneEditor::setView (float v0, float v1)
{
    float span = juce::jlimit (kMinSpan, 1.f, v1 - v0);
    v0 = juce::jlimit (0.f, 1.f - span, v0);
    if (v0 == view0 && v0 + span == view1) return;
    const int was = grid();
    view0 = v0;
    view1 = v0 + span;
    repaint();
    if (grid() != was && onModeChange) onModeChange();
}

void LaneEditor::zoomBy (float factor, float around)
{
    const float span = view1 - view0, next = juce::jlimit (kMinSpan, 1.f, span / factor);
    const float a = juce::jlimit (view0, view1, around);
    setView (a - (a - view0) * next / span, a - (a - view0) * next / span + next);
}

void LaneEditor::zoomAll()
{
    zoomedFrom.clear();
    setView (0.f, 1.f);
}

void LaneEditor::showBuild (float b)
{
    const float span = view1 - view0, margin = span * 0.05f;
    if (b < view0 + margin) setView (b - margin, b - margin + span);
    else if (b > view1 - margin) setView (b + margin - span, b + margin);
}

// The bands share the height in whole pixels (sharp edges), never less than kMinRow.
float LaneEditor::rowHeight() const
{
    return rows.empty() ? 0.f : std::max (kMinRow, std::floor (rowsArea().getHeight() / (float) rows.size()));
}

float LaneEditor::rowTop (int r) const { return rowsArea().getY() + rowHeight() * (float) r - scroll; }

juce::Rectangle<float> LaneEditor::band (int r) const { return { 0.f, rowTop (r), (float) getWidth(), rowHeight() }; }

// The well: the band less the gap, which centres the band's rule (its last
// pixel row) between two wells, 3 px either side.
juce::Rectangle<float> LaneEditor::row (int r) const
{
    return band (r).withTrimmedLeft (kGap).withTrimmedTop (kRowGap * 0.5f).withTrimmedBottom (kRowGap * 0.5f + 1.f);
}

juce::Point<float> LaneEditor::pointOf (int r, float path, float value) const
{
    if (r < 0 || r >= (int) rows.size()) return { xOfPath (path), rowsArea().getCentreY() };
    return { xOfPath (path), yOf (r, value) };
}

float LaneEditor::xOfBuild (float b) const
{
    const auto p = plot();
    return p.getX() + p.getWidth() * (b - view0) / (view1 - view0);
}

float LaneEditor::pathOfX (float x) const { return buildOfX (x) / ba::kPathEnd; }

float LaneEditor::pathPerPixel() const { return (view1 - view0) / std::max (1.f, plot().getWidth() * ba::kPathEnd); }

// Values fill the well 8 px clear of its edges (room for the keys).
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

float LaneEditor::maxScroll() const { return std::max (0.f, rowHeight() * (float) rows.size() - rowsArea().getHeight()); }

void LaneEditor::scrollTo (float y)
{
    y = juce::jlimit (0.f, maxScroll(), y);
    if (y == scroll) return;
    scroll = y;
    repaint();
    if (onScroll) onScroll();
}

void LaneEditor::showRow (int r)
{
    if (r < 0 || r >= (int) rows.size()) return;
    const auto area = rowsArea();
    const float top = rowHeight() * (float) r;
    if (top < scroll) scrollTo (top);
    else if (top + rowHeight() > scroll + area.getHeight()) scrollTo (top + rowHeight() - area.getHeight());
}

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

int LaneEditor::closestKey (int r, int a, int z, float at) const
{
    const auto& l = laneAt (r);
    a = juce::jlimit (0, l.count - 1, a);
    z = juce::jlimit (0, l.count - 1, z);
    return std::abs (l.keys[(size_t) z].at - at) < std::abs (l.keys[(size_t) a].at - at) ? z : a;
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
    if (pt.y < kTop)
    {
        if (pt.x >= p.getX() - kPad && pt.x <= p.getRight() + kPad) h.part = pt.y < kOverview ? Part::overview : Part::ruler;
        return h;
    }
    if (rows.empty() || pt.x < kGap) return h;
    const int r = (int) std::floor ((pt.y - rowsArea().getY() + scroll) / rowHeight());
    if (r < 0 || r >= (int) rows.size()) return h;
    h.row = r;
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
    downAt = mouseAt = e.position;
    horizontal.reset();
    dragKey = -1;
    if (g == Gesture::scrub)
    {
        model.scrub (juce::jlimit (0.f, 1.f, buildOfX (e.position.x)), -1);
        return;
    }
    if (g == Gesture::view)
    {
        // A click beside the view on the overview brings it there first (as Live's).
        const float b = buildOfOverviewX (e.position.x), span = view1 - view0;
        if (b < view0 || b > view1) setView (b - span * 0.5f, b + span * 0.5f);
        grabView0 = view0;
        grabSpan = view1 - view0;
        return;
    }
    if (g == Gesture::select)
    {
        selBefore = sel;
        const float at = snapAt (h.at, laneAt (h.row), {}, e.mods);
        sel = { at, at, h.row, h.row };
        growRight = true;
        repaint();
        return;
    }
    before = laneAt (h.row);
    model.beginEdit();
    const auto& l = before;
    switch (g)
    {
        case Gesture::maybeDelete:
        case Gesture::maybePick:
        case Gesture::moveKeys:
        {
            gestureName = "Move breakpoints";
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
            gestureName = "Curve";
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
        case Gesture::view:
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
    mouseAt = e.position;
    const auto d = e.position - downAt;
    const bool shift = e.mods.isShiftDown();
    if (gesture == Gesture::scrub)
    {
        model.scrub (juce::jlimit (0.f, 1.f, buildOfX (e.position.x)), 0);
        return;
    }
    if (gesture == Gesture::view)
    {
        // Sideways scrolls the view along the overview; down zooms in, up out (Live's ruler).
        const float span = juce::jlimit (kMinSpan, 1.f, grabSpan * std::exp (-d.y * 0.02f));
        const float centre = grabView0 + grabSpan * 0.5f + d.x / overview().getWidth();
        setView (centre - span * 0.5f, centre + span * 0.5f);
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
    const float mouseX = juce::jlimit (0.f, 1.f, pathOfX (e.position.x));

    switch (gesture)
    {
        case Gesture::maybeDelete:
        case Gesture::maybePick:
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
            const int first = movedFirst = edit::move (l, runFirst, runLast, dAt, dValue, range);
            apply (r, l);
            // The value box: the dragged key, or the segment's key nearer the mouse (as Live).
            dragKey = gesture == Gesture::moveKeys ? first + (start.key - runFirst) : closestKey (r, first, first + (runLast - runFirst), mouseX);
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
            const float at = snapAt (mouseX, laneAt (r), {}, e.mods);
            const int rowNow = rows.empty() ? r
                                            : juce::jlimit (0, (int) rows.size() - 1, (int) std::floor ((e.position.y - rowsArea().getY() + scroll) / rowHeight()));
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
            drawCellsTo (mouseX, v);
            ba::Lane l = before;
            edit::drawSteps (l, cells, range);
            apply (r, l);
            break;
        }
        case Gesture::drawFree:
        {
            stroke.push_back ({ mouseX, mouseValue, 1.f, 0.f });
            ba::Lane l = before;
            edit::drawFreehand (l, stroke, 0.0025f * (range.hi - range.lo), range);
            apply (r, l);
            break;
        }
        case Gesture::none:
        case Gesture::scrub:
        case Gesture::view: break;
    }
    repaint();
}

void LaneEditor::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    entry.reset();
    commitKeys();
    dropKeys();
    markerByKeys = false;
    const auto h = hitAt (e.position);
    if (h.row >= 0) cursorRow = h.row;
    if (e.mods.isPopupMenu())
    {
        showMenu (h);
        return;
    }
    if (h.part == Part::ruler || h.part == Part::overview)
    {
        beginGesture (h.part == Part::ruler ? Gesture::scrub : Gesture::view, h, e);
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
        // A click deletes it, Option/Alt-click selects it for the keys; a drag moves it.
        case Part::key: beginGesture (e.mods.isAltDown() ? Gesture::maybePick : Gesture::maybeDelete, h, e); break;
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
                    model.say ("This lane is full (" + juce::String (ba::Lane::kMaxKeys) + " breakpoints): Simplify Envelope makes room.");
                    return;
                }
                apply (h.row, l);
                model.endEdit ("Add breakpoint");
                Hit k = h;
                k.part = Part::key;
                k.key = i;
                beginGesture (Gesture::moveKeys, k, e);
                gestureName = "Add breakpoint";
            }
            break;
        case Part::nearLine:
            if (e.mods.isAltDown()) beginGesture (Gesture::bend, h, e);
            else beginGesture (inSelection ? Gesture::shiftRange : Gesture::moveSegment, h, e);
            break;
        case Part::background:
            if (e.mods.isShiftDown() && sel.active())
            {
                // Shift-click: the selection reaches here from its far edge.
                Hit anchor = h;
                anchor.at = h.at >= sel.t0 ? sel.t0 : sel.t1;
                anchor.row = h.row >= sel.first ? sel.first : sel.last;
                beginGesture (Gesture::select, anchor, e);
                growRight = h.at >= anchor.at;
                dragGesture (e);
            }
            else beginGesture (Gesture::select, h, e);
            break;
        case Part::none:
        case Part::overview:
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
    else if (g == Gesture::view) {}
    else if (g == Gesture::maybeDelete)
    {
        // A click on a key: it goes (but a lane keeps its last one, as in Live).
        ba::Lane l = before;
        if (l.count > 1)
        {
            edit::remove (l, start.key);
            apply (start.row, l);
            model.endEdit ("Delete breakpoint");
        }
        else model.endEdit ({});
    }
    else if (g == Gesture::maybePick)
    {
        model.endEdit ({});
        pickKey (start.row, start.key);
    }
    else if (g == Gesture::select)
    {
        if (! sel.active())
        {
            // A click: the insert marker goes there.
            sel = {};
            marker = snapAt (start.at, laneAt (start.row), {}, e.mods);
            cursorRow = start.row;
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
    if (h.part == Part::overview)
    {
        zoomAll();
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
        model.endEdit ("Add breakpoint");
        sel = {};
    }
    repaint();
}

// As Live: Cmd (Ctrl) and the wheel zoom around the mouse; a two-finger swipe
// sideways or Shift and the wheel scroll along the build; up and down scroll
// the lanes when they don't all fit. The deltas as given: natural scrolling
// is the system's.
void LaneEditor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
    {
        zoomBy (std::exp (w.deltaY * 2.f), buildOfX (e.position.x));
        return;
    }
    float dx = w.deltaX, dy = w.deltaY;
    if (e.mods.isShiftDown() && dx == 0.f) std::swap (dx, dy);
    constexpr float kPixels = 224.f;   // per wheel unit, as JUCE's viewports
    bool used = false;
    if (dx != 0.f && view1 - view0 < 1.f)
    {
        const float by = -dx * kPixels / plot().getWidth() * (view1 - view0);
        setView (view0 + by, view1 + by);
        used = true;
    }
    if (dy != 0.f && maxScroll() > 0.f)
    {
        scrollTo (scroll - dy * kPixels);
        used = true;
    }
    if (! used)
    {
        Component::mouseWheelMove (e, w);
        return;
    }
    hoverTo (hitAt (e.position));
}

void LaneEditor::mouseMagnify (const juce::MouseEvent& e, float scaleFactor)
{
    zoomBy (scaleFactor, buildOfX (e.position.x));
    hoverTo (hitAt (e.position));
}

void LaneEditor::focusLost (FocusChangeType)
{
    if (entry != nullptr) return;   // the value box took it
    commitKeys();
    dropKeys();
    repaint();
}

// ---- Commands ----------------------------------------------------------------------

void LaneEditor::command (int what)
{
    switch (what)
    {
        case cmdDraw: setDrawMode (! draw); return;
        case cmdNarrow: narrower(); return;
        case cmdWiden: wider(); return;
        case cmdTriplet: setTriplets (! triplet); return;
        case cmdSnap: setSnap (! snap); return;
        case cmdAdaptive: setAdaptive (! adaptiveGrid); return;
        case cmdZoomSelection:
            // Live's Z: the selection fills the view; X goes back.
            if (sel.active())
            {
                zoomedFrom.push_back ({ view0, view1 });
                const float a = sel.t0 * ba::kPathEnd, b = sel.t1 * ba::kPathEnd, margin = (b - a) * 0.04f;
                setView (a - margin, b + margin);
            }
            return;
        case cmdZoomBack:
            if (zoomedFrom.empty()) zoomBy (0.5f, 0.5f * (view0 + view1));
            else
            {
                setView (zoomedFrom.back().first, zoomedFrom.back().second);
                zoomedFrom.pop_back();
            }
            return;
        case cmdZoomIn:
        case cmdZoomOut:
        {
            // Around the selection, else the insert marker, else the view's middle.
            const float at = sel.active() ? 0.5f * (sel.t0 + sel.t1) * ba::kPathEnd
                           : cursorRow >= 0 ? marker * ba::kPathEnd : 0.5f * (view0 + view1);
            zoomBy (what == cmdZoomIn ? 2.f : 0.5f, at);
            return;
        }
        case cmdZoomAll: zoomAll(); return;
        case cmdRemoveLane:
            if (const int r = keyRow(); r >= 0)
            {
                commitKeys();
                dropKeys();
                model.removeLane (laneOf (r));
            }
            return;
        default: break;
    }
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
        const int r0 = sel.active() ? sel.first : keyRow();
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
    if (what == cmdSimplify && ! sel.active() && keyRow() >= 0)
    {
        const int r = keyRow();
        model.beginEdit();
        ba::Lane l = laneAt (r);
        edit::simplify (l, 0.f, 1.f, 0.004f * (rangeOf (r).hi - rangeOf (r).lo), rangeOf (r));
        apply (r, l);
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
    const auto item = [&m] (int id, const juce::String& text, const juce::String& keys, bool enabled = true, bool ticked = false) {
        juce::PopupMenu::Item i (text);
        i.itemID = id;
        i.isEnabled = enabled;
        i.isTicked = ticked;
        i.shortcutKeyDescription = keys;
        m.addItem (i);
    };
    const auto& l = laneAt (h.row);
    if (h.part == Part::key)
    {
        const auto [f, last] = selectedKeys (h.row);
        const bool many = sel.covers (h.row) && h.key >= f && h.key <= last && last > f;
        item (cmdEditValue, juce::String (juce::CharPointer_UTF8 (many ? "Edit Value (all selected)\xe2\x80\xa6" : "Edit Value\xe2\x80\xa6")), {});
        item (cmdDeleteKey, "Delete Breakpoint", {}, l.count > 1);
        m.addSeparator();
    }
    else if (h.part == Part::line || h.part == Part::nearLine)
    {
        item (cmdAddValue, juce::String (juce::CharPointer_UTF8 ("Add Value\xe2\x80\xa6")), {});
        if (h.key > 0 && h.key < l.count && l.keys[(size_t) h.key].curve != 1.f) item (cmdStraighten, "Straighten Segment", kBend + " double-click");
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
    item (cmdCut, "Cut", kCmd + "X", sel.active());
    item (cmdCopy, "Copy", kCmd + "C", sel.active());
    item (cmdPaste, "Paste", kCmd + "V", ! clipboard().empty());
    item (cmdDuplicate, "Duplicate", kCmd + "D", sel.active());
    item (cmdDelete, "Delete", "Del", sel.active());
    item (cmdSelectAll, "Select All", kCmd + "A");
    m.addSeparator();
    item (cmdDraw, "Draw Mode", "B", true, draw);
    item (cmdNarrow, "Narrow Grid", kCmd + "1", adaptiveGrid ? density > 0 : gridIndex < kGridCount - 1);
    item (cmdWiden, "Widen Grid", kCmd + "2", adaptiveGrid ? density < (int) std::size (kDensities) - 1 : gridIndex > 0);
    item (cmdTriplet, "Triplet Grid", kCmd + "3", true, triplet);
    item (cmdSnap, "Snap to Grid", kCmd + "4", true, snap);
    item (cmdAdaptive, "Zoom-Adaptive Grid", kCmd + "5", true, adaptiveGrid);
    m.addSeparator();
    item (cmdZoomSelection, "Zoom to Selection", "Z", sel.active());
    item (cmdZoomBack, "Zoom Back", "X", view1 - view0 < 1.f);
    item (cmdZoomAll, "Show the Whole Build", "W", view1 - view0 < 1.f);
    m.addSeparator();
    item (cmdRemoveLane, "Remove Lane", kCmd + juce::String (juce::CharPointer_UTF8 ("\xe2\x8c\xab")));
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
                             self.model.endEdit (id == cmdDeleteKey ? "Delete breakpoint" : "Straighten");
                         }
                         else self.command (id);
                     });
}

// Edit Value on a key (the selected ones move with it, by as much) or Add
// Value on the line; `typed` starts the text (a number typed at a key).
void LaneEditor::editValue (const Hit& h, bool addNew, const juce::String& typed)
{
    const auto& l = laneAt (h.row);
    if (! addNew && (h.key < 0 || h.key >= l.count)) return;
    const float at = addNew ? h.at : l.keys[(size_t) h.key].at;
    const float v = addNew ? edit::valueAfter (l, h.at) : l.keys[(size_t) h.key].value;
    entry = std::make_unique<juce::TextEditor>();
    entry->setFont (mono (10.5f));
    entry->setJustification (juce::Justification::centred);
    entry->setIndents (2, 1);
    entry->setText (typed.isNotEmpty() ? typed : model.words (laneOf (h.row), v), false);
    const auto spot = juce::Point<float> (xOfPath (at), yOf (h.row, v));
    entry->setBounds (juce::Rectangle<float> (88.f, 18.f).withCentre (spot.translated (0.f, spot.y - 20.f < row (h.row).getY() ? 18.f : -18.f))
                          .constrainedWithin (getLocalBounds().toFloat())
                          .toNearestInt());
    const auto done = [safe = juce::Component::SafePointer<LaneEditor> (this)] {
        juce::MessageManager::callAsync ([safe] {
            if (safe == nullptr) return;
            safe->entry.reset();
            safe->grabKeyboardFocus();
        });
    };
    entry->onReturnKey = [this, h, at, v, addNew, done] {
        if (const auto value = model.parse (laneOf (h.row), entry->getText()))
        {
            model.beginEdit();
            ba::Lane edited = laneAt (h.row);
            const auto range = rangeOf (h.row);
            if (addNew) edit::insert (edited, at, *value, range);
            else if (h.key < edited.count)
            {
                const auto [f, last] = selectedKeys (h.row);
                const bool many = sel.covers (h.row) && h.key >= f && h.key <= last;
                const float by = range.clamp (*value) - v;
                for (int i = many ? f : h.key; i <= (many ? last : h.key); ++i)
                    edited.keys[(size_t) i].value = range.clamp (edited.keys[(size_t) i].value + by);
            }
            apply (h.row, edited);
            model.endEdit (addNew ? "Add value" : "Edit value");
        }
        done();
    };
    entry->onEscapeKey = done;
    entry->onFocusLost = done;
    addAndMakeVisible (*entry);
    if (typed.isNotEmpty()) entry->moveCaretToEnd();
    else entry->selectAll();
    entry->grabKeyboardFocus();
}

// ---- The keyboard ---------------------------------------------------------------------

int LaneEditor::keyRow() const
{
    if (rows.empty()) return -1;
    return cursorRow >= 0 && cursorRow < (int) rows.size() ? cursorRow : 0;
}

void LaneEditor::beginKeyEdit (const juce::String& what)
{
    if (keyEditing) return;
    model.beginEdit();
    keyEditing = true;
    keyEditName = what;
}

void LaneEditor::commitKeys()
{
    if (! keyEditing) return;
    keyEditing = false;
    model.endEdit (keyEditName);
}

void LaneEditor::dropKeys()
{
    keyKey = -1;
    focusKey = -1;
}

void LaneEditor::pickKey (int r, int key)
{
    if (r < 0 || r >= (int) rows.size() || key < 0 || key >= laneAt (r).count) return;
    cursorRow = r;
    keyKey = key;
    focusKey = -1;
    marker = laneAt (r).keys[(size_t) key].at;
    markerByKeys = true;
    sel = {};
    showRow (r);
    showBuild (marker * ba::kPathEnd);
    model.say (juce::String (juce::CharPointer_UTF8 ("Breakpoint selected: \xe2\x86\x90 \xe2\x86\x92 move it")) + dot
               + juce::String (juce::CharPointer_UTF8 ("\xe2\x86\x91 \xe2\x86\x93 or type a value (Shift finer)")) + dot + "Enter: done" + dot
               + "Esc: take it back" + dot + "Delete: remove it.");
    repaint();
}

void LaneEditor::enter()
{
    if (keyKey >= 0)
    {
        commitKeys();
        dropKeys();
        model.say ({});
        repaint();
        return;
    }
    const int r = keyRow();
    if (r < 0) return;
    const auto& l = laneAt (r);
    for (int i = 0; i < l.count; ++i)
        if (std::abs (l.keys[(size_t) i].at - marker) <= 0.5f * pathPerPixel())
        {
            pickKey (r, i);
            return;
        }
    beginKeyEdit ("Add breakpoint");
    ba::Lane e = l;
    const int i = edit::insert (e, marker, rangeOf (r));
    if (i < 0)
    {
        keyEditing = false;
        model.cancelEdit();
        model.say ("This lane is full (" + juce::String (ba::Lane::kMaxKeys) + " breakpoints): Simplify Envelope makes room.");
        return;
    }
    apply (r, e);
    pickKey (r, i);
}

void LaneEditor::moveMarker (int direction, bool resize)
{
    commitKeys();
    dropKeys();
    const int r = keyRow();
    if (r < 0) return;
    cursorRow = r;
    const float g = gridStep();
    // The next grid line that way.
    const auto next = [g, direction] (float t) {
        return juce::jlimit (0.f, 1.f, direction > 0 ? (std::floor (t / g + 1e-4f) + 1.f) * g : (std::ceil (t / g - 1e-4f) - 1.f) * g);
    };
    if (resize)
    {
        if (! sel.active())
        {
            sel = { marker, marker, r, r };
            growRight = direction > 0;
        }
        if (growRight) sel.t1 = std::max (sel.t0, next (sel.t1));
        else sel.t0 = std::min (sel.t1, next (sel.t0));
        if (! sel.active())
        {
            marker = sel.t0;
            sel = {};
        }
    }
    else if (sel.active())
    {
        const float len = sel.t1 - sel.t0;
        sel.t0 = juce::jlimit (0.f, 1.f - len, next (sel.t0));
        sel.t1 = sel.t0 + len;
    }
    else
    {
        marker = next (marker);
        markerByKeys = true;
    }
    showRow (r);
    // What moved stays in view.
    showBuild ((! sel.active() ? marker : resize ? (growRight ? sel.t1 : sel.t0) : direction > 0 ? sel.t1 : sel.t0) * ba::kPathEnd);
    repaint();
}

void LaneEditor::jumpToKey (int direction)
{
    commitKeys();
    dropKeys();
    const int r = keyRow();
    if (r < 0) return;
    cursorRow = r;
    const auto& l = laneAt (r);
    int found = -1;
    if (direction > 0)
    {
        for (int i = 0; i < l.count && found < 0; ++i)
            if (l.keys[(size_t) i].at > marker + 1e-6f) found = i;
    }
    else
        for (int i = l.count - 1; i >= 0 && found < 0; --i)
            if (l.keys[(size_t) i].at < marker - 1e-6f) found = i;
    if (found < 0) return;
    marker = l.keys[(size_t) found].at;
    focusKey = found;
    markerByKeys = true;
    sel = {};
    showRow (r);
    showBuild (marker * ba::kPathEnd);
    repaint();
}

void LaneEditor::tabToKey (int direction)
{
    const int r = keyRow();
    if (r < 0) return;
    commitKeys();
    const auto& l = laneAt (r);
    int i = -1;
    if (keyKey >= 0) i = keyKey + direction;
    else if (direction > 0)
    {
        for (int k = 0; k < l.count && i < 0; ++k)
            if (l.keys[(size_t) k].at >= marker - 1e-6f) i = k;
    }
    else
        for (int k = l.count - 1; k >= 0 && i < 0; --k)
            if (l.keys[(size_t) k].at <= marker + 1e-6f) i = k;
    if (i >= 0 && i < l.count) pickKey (r, i);
}

void LaneEditor::moveKey (int direction)
{
    const int r = keyRow();
    if (r < 0 || keyKey < 0) return;
    beginKeyEdit ("Move breakpoint");
    ba::Lane l = laneAt (r);
    const float g = gridStep(), at = l.keys[(size_t) keyKey].at;
    const float to = juce::jlimit (0.f, 1.f, direction > 0 ? (std::floor (at / g + 1e-4f) + 1.f) * g : (std::ceil (at / g - 1e-4f) - 1.f) * g);
    keyKey = edit::move (l, keyKey, keyKey, to - at, 0.f, rangeOf (r));
    apply (r, l);
    marker = laneAt (r).keys[(size_t) keyKey].at;
    showBuild (marker * ba::kPathEnd);
    repaint();
}

float LaneEditor::valueStep (int r, bool fine) const
{
    if (laneOf (r) == ba::LaneId::pitch) return fine ? 0.1f : 1.f;    // semitones
    if (laneOf (r) == ba::LaneId::level) return fine ? 0.1f : 0.5f;   // dB
    const auto range = rangeOf (r);
    return (range.hi - range.lo) * (fine ? 0.001f : 0.01f);
}

void LaneEditor::stepValue (int direction, bool fine)
{
    const int r = keyRow();
    if (r < 0 || keyKey < 0) return;
    beginKeyEdit ("Edit value");
    ba::Lane l = laneAt (r);
    auto& k = l.keys[(size_t) keyKey];
    k.value = rangeOf (r).clamp (k.value + (float) direction * valueStep (r, fine));
    apply (r, l);
    repaint();
}

void LaneEditor::moveRow (int direction)
{
    commitKeys();
    dropKeys();
    if (rows.empty()) return;
    const int to = juce::jlimit (0, (int) rows.size() - 1, keyRow() + direction);
    if (sel.active())
    {
        const int n = sel.last - sel.first;
        sel.first = juce::jlimit (0, (int) rows.size() - 1 - n, sel.first + direction);
        sel.last = sel.first + n;
    }
    cursorRow = to;
    showRow (to);
    repaint();
}

bool LaneEditor::keyPressed (const juce::KeyPress& k)
{
    const auto mods = k.getModifiers();
    const auto key = upper (k.getKeyCode());
    const bool cmd = mods.isCommandDown(), alt = mods.isAltDown(), shift = mods.isShiftDown();
    if (key == 'B' && ! cmd && ! alt && ! mods.isCtrlDown())
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
    const bool erase = k.isKeyCode (juce::KeyPress::deleteKey) || k.isKeyCode (juce::KeyPress::backspaceKey);
    if (cmd && ! alt)
    {
        switch (key)
        {
            case '1': command (cmdNarrow); return true;
            case '2': command (cmdWiden); return true;
            case '3': command (cmdTriplet); return true;
            case '4': command (cmdSnap); return true;
            case '5': command (cmdAdaptive); return true;
            case 'A': command (cmdSelectAll); return true;
            case 'C': command (cmdCopy); return true;
            case 'X': command (cmdCut); return true;
            case 'V': command (cmdPaste); return true;
            case 'D': command (cmdDuplicate); return true;
            default: break;
        }
        if (erase)
        {
            command (cmdRemoveLane);   // Live: Delete Automation
            return true;
        }
    }
    if (rows.empty()) return false;
    if (erase)
    {
        if (keyKey >= 0)
        {
            const int r = keyRow();
            if (laneAt (r).count > 1)
            {
                beginKeyEdit ("Delete breakpoint");
                ba::Lane l = laneAt (r);
                edit::remove (l, keyKey);
                apply (r, l);
                commitKeys();
            }
            dropKeys();
            repaint();
        }
        else command (cmdDelete);
        return true;
    }
    if (k.isKeyCode (juce::KeyPress::escapeKey))
    {
        if (keyKey >= 0)
        {
            if (keyEditing)
            {
                keyEditing = false;
                model.cancelEdit();
            }
            dropKeys();
            model.say ({});
        }
        else if (sel.active()) sel = {};
        else return false;
        repaint();
        return true;
    }
    if (k.isKeyCode (juce::KeyPress::returnKey))
    {
        enter();
        return true;
    }
    if (k.isKeyCode (juce::KeyPress::tabKey))
    {
        tabToKey (shift ? -1 : 1);
        return true;
    }
    const bool left = k.isKeyCode (juce::KeyPress::leftKey), right = k.isKeyCode (juce::KeyPress::rightKey);
    if (left || right)
    {
        const int dir = right ? 1 : -1;
        if (alt) jumpToKey (dir);
        else if (keyKey >= 0) moveKey (dir);
        else moveMarker (dir, shift);
        return true;
    }
    const bool up = k.isKeyCode (juce::KeyPress::upKey), down = k.isKeyCode (juce::KeyPress::downKey);
    if (up || down)
    {
        if (keyKey >= 0 && ! alt) stepValue (up ? 1 : -1, shift);
        else moveRow (up ? -1 : 1);
        return true;
    }
    // A number typed at a selected breakpoint: its value box, with what was typed.
    const auto c = k.getTextCharacter();
    if (keyKey >= 0 && ! cmd && (juce::CharacterFunctions::isDigit (c) || c == '-' || c == '+' || c == '.'))
    {
        commitKeys();
        Hit h;
        h.row = keyRow();
        h.key = keyKey;
        h.part = Part::key;
        editValue (h, false, juce::String::charToString (c));
        return true;
    }
    // Live's zoom keys.
    if (! cmd && ! alt && ! mods.isCtrlDown())
    {
        const int zoom = key == 'Z' ? cmdZoomSelection : key == 'X' ? cmdZoomBack : key == 'W' ? cmdZoomAll
                       : c == '+' || c == '=' ? cmdZoomIn : c == '-' ? cmdZoomOut : 0;
        if (zoom != 0)
        {
            command (zoom);
            return true;
        }
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
    else if (h.part == Part::overview) c = C::DraggingHandCursor;
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
            case Part::overview:
            case Part::ruler:
            case Part::tail: break;
        }
    setMouseCursor (c);
}

juce::String LaneEditor::describe (const Hit& h) const
{
    if (h.part == Part::ruler) return "BUILD: drag to scrub it" + dot + "double-click: back to 0" + dot + "pinch or " + kCmd.trimEnd() + "-scroll: zoom" + dot + "swipe: scroll along.";
    if (h.part == Part::overview)
        return "The whole build, what you see outlined: drag sideways to scroll, down or up to zoom in or out" + dot + "double-click: all of it" + dot
               + "Z / X: zoom to the selection and back.";
    if (h.part == Part::tail)
        return juce::String (juce::CharPointer_UTF8 ("Past 75 % every preset fades into the same tail: the reverb comes up, the level goes to \xe2\x88\x92" "20 dB. Not editable."));
    if (h.row < 0) return {};
    const auto id = laneOf (h.row);
    if (drawMode())
        return "Draw Mode: drag to draw steps " + gridName (gridIndex) + (triplet ? " T" : "") + " wide" + dot + "Shift: finer" + dot + kNoSnap + ": freehand"
               + dot + "B: back to breakpoints.";
    const auto& l = laneAt (h.row);
    switch (h.part)
    {
        case Part::key:
        {
            const auto& k = l.keys[(size_t) h.key];
            return model.words (id, k.value) + " at " + fmt::fixed (k.at * ba::kPathEnd * 100.f, 1) + " % BUILD" + dot + "drag: move (Shift one axis, " + kNoSnap
                   + " no snap)" + dot + "click: delete" + dot + kBend + "-click: select" + dot + "right-click: Edit Value.";
        }
        case Part::line: return "Click: add a breakpoint" + dot + "Shift-drag: move this segment" + dot + kBend + "-drag: curve it" + dot + "right-click: Add Value.";
        case Part::nearLine: return "Drag: move this segment" + dot + kBend + "-drag: curve it" + dot + kBend + "-double-click: straighten it.";
        case Part::handle:
            return h.handle == Handle::left || h.handle == Handle::right ? "Drag: stretch the selection in time (Shift: the rest moves along, " + kBend + ": both sides)."
                 : h.handle == Handle::top || h.handle == Handle::bottom ? "Drag: stretch the selection up or down (Shift: finer, " + kBend + ": both edges)."
                                                                          : "Drag: skew the selection (Shift: finer, " + kBend + ": the other corner too).";
        case Part::background:
            return "Drag: select time (across lanes)" + dot + "Shift-click: extend it" + dot + "double-click: add a breakpoint" + dot
                   + "click: insert marker for the keys.";
        case Part::none:
        case Part::overview:
        case Part::ruler:
        case Part::tail: break;
    }
    return {};
}

void LaneEditor::hoverTo (const Hit& h)
{
    const bool same = h.part == hover.part && h.row == hover.row && h.key == hover.key && h.handle == hover.handle;
    const bool lineMoved = (h.part == Part::line || hover.part == Part::line || h.part == Part::nearLine) && std::abs (h.at - hover.at) > 1e-6f;
    hover = h;
    updateCursor (h, juce::ModifierKeys::getCurrentModifiers());
    model.say (describe (h));   // a key's words carry its value, which a drag changes
    if (! same || lineMoved) repaint();
}

void LaneEditor::mouseMove (const juce::MouseEvent& e)
{
    mouseAt = e.position;
    hoverTo (hitAt (e.position));
}

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
    const auto area = rowsArea();
    const float playX = xOfBuild (model.build());
    const bool playInView = playX >= p.getX() - kPad && playX <= p.getRight() + kPad;
    const bool zoomed = view1 - view0 < 1.f;

    // Overview: the whole build on a strip of well, the tail hatched, the
    // playhead, and the view outlined (the rest dimmed while zoomed in).
    {
        const auto o = overview();
        const auto ox = [&o] (float b) { return o.getX() + o.getWidth() * b; };
        g.setColour (col::well);
        g.fillRect (o);
        {
            juce::Graphics::ScopedSaveState s (g);
            const auto t = o.withLeft (ox (ba::kPathEnd));
            g.reduceClipRegion (t.toNearestInt());
            hatch (g, t, col::hairline, 4.f, 1.f);
        }
        g.setColour (col::hairline);
        for (int q = 1; q < 4; ++q) g.fillRect (std::round (ox ((float) q / 4.f)), o.getY(), 1.f, o.getHeight());
        g.setColour (col::accent);
        g.fillRect (ox (model.build()) - 0.5f, o.getY(), 1.f, o.getHeight());
        const auto v = juce::Rectangle<float>::leftTopRightBottom (ox (view0), o.getY(), ox (view1), o.getBottom());
        if (zoomed)
        {
            g.setColour (col::sheet.withAlpha (0.6f));
            g.fillRect (o.withRight (v.getX()));
            g.fillRect (o.withLeft (v.getRight()));
        }
        g.setColour (hover.part == Part::overview || gesture == Gesture::view ? col::ink : zoomed ? col::inkSoft : col::hairline);
        g.drawRect (v, 1.f);
    }

    // Ruler: BUILD in steps that fit the zoom (quarters when it shows it all),
    // the grid along the path, the tail named.
    {
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (juce::Rectangle<float> (p.getX() - kPad, kOverview, p.getWidth() + 2.f * kPad, kRuler).toNearestInt());
        g.setFont (mono (9.f));
        const float base = kTop - 4.f, top = kOverview + 2.f;
        g.setColour (col::hairline);
        g.fillRect (p.getX(), base, p.getWidth(), 1.f);
        const float stepX = xOfPath (gridStep()) - xOfPath (0.f);
        if (stepX >= 4.f)
            for (float t = std::floor (view0 / ba::kPathEnd / gridStep()) * gridStep(); t <= std::min (1.0001f, view1 / ba::kPathEnd + gridStep()); t += gridStep())
            {
                g.setColour (col::inkFaint);
                g.fillRect (xOfPath (t) - 0.5f, base - 3.f, 1.f, 3.f);
            }
        const float pxPerBuild = p.getWidth() / (view1 - view0);
        float step = 0.25f;
        for (const float s2 : { 0.05f, 0.025f, 0.01f, 0.005f })
            if (s2 * pxPerBuild >= 70.f) step = s2;
        for (float b = std::ceil (view0 / step - 1e-4f) * step; b <= view1 + 1e-4f; b += step)
        {
            const float x = xOfBuild (b);
            g.setColour (col::inkSoft);
            g.fillRect (x - 0.5f, base - 6.f, 1.f, 6.f);
            const bool end = b > 0.9999f;
            const float pc = b * 100.f;
            const auto t = fmt::fixed (pc, std::abs (pc - std::round (pc)) > 1e-3f ? 1 : 0) + (end ? " % BUILD" : " %");   // 25 %, 27.5 %
            const float w = end ? 80.f : 60.f;
            // Centred on the tick, kept inside the ruler at its ends.
            const auto r = juce::Rectangle<float> (x - w * 0.5f, top, w, 12.f);
            const auto j = r.getX() < p.getX() - kPad ? juce::Justification::centredLeft
                         : r.getRight() > p.getRight() + kPad ? juce::Justification::centredRight : juce::Justification::centred;
            g.drawText (t, j == juce::Justification::centredLeft ? r.withX (x + 3.f) : j == juce::Justification::centredRight ? r.withRightX (x - 3.f) : r, j, false);
        }
        if (step == 0.25f)
            caption (g, "Tail", { xOfPath (1.f), top, xOfBuild (1.f) - xOfPath (1.f) - 84.f, 12.f }, juce::Justification::centred, col::inkFaint, 9.f);
        if (playInView)
        {
            g.setColour (col::accent);
            juce::Path head;
            head.addTriangle (playX - 4.f, base - 7.f, playX + 4.f, base - 7.f, playX, base);
            g.fillPath (head);
        }
    }

    if (rows.empty())
    {
        const auto empty = area.withTrimmedLeft (kGap);
        g.setColour (col::well);
        g.fillRect (empty);
        g.setFont (title (15.f));
        g.setColour (col::ink);
        g.drawText ("This preset doesn't drive anything yet.",
                    empty.withTrimmedBottom (empty.getHeight() * 0.5f - 2.f).withTrimmedTop (empty.getHeight() * 0.5f - 24.f),
                    juce::Justification::centredBottom, false);
        g.setFont (mono (10.5f));
        g.setColour (col::inkSoft);
        g.drawText ("+ ADD LANE (top left) picks a control, then draw its path the way you draw automation in Live.",
                    empty.withTrimmedTop (empty.getHeight() * 0.5f + 6.f).withHeight (16.f), juce::Justification::centredTop, false);
        return;
    }

    // What the chain runs along the BUILD in view: as designed, and as the intensities have it.
    constexpr int kSteps = 240;
    std::vector<ba::Settings> along ((size_t) kSteps + 1), played ((size_t) kSteps + 1);
    for (int k = 0; k <= kSteps; ++k)
    {
        const float b = view0 + (view1 - view0) * (float) k / kSteps;
        along[(size_t) k] = model.run (b, true);
        played[(size_t) k] = model.run (b, false);
    }

    {
        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (area.toNearestInt());
        const auto clipBox = g.getClipBounds().toFloat();
        for (int r = 0; r < (int) rows.size(); ++r)
            if (band (r).intersects (clipBox)) paintRow (g, r, along, played);

        // The playhead through every row.
        if (playInView)
        {
            g.setColour (col::accent);
            g.fillRect (playX - 0.5f, area.getY(), 1.f, std::min (area.getBottom(), rowTop ((int) rows.size())) - area.getY());
        }

        // More rows than room: where the view is, a thumb at the right edge.
        if (maxScroll() > 0.f)
        {
            const float all = rowHeight() * (float) rows.size();
            const auto thumb = juce::Rectangle<float> (3.f, area.getHeight() * area.getHeight() / all)
                                   .withPosition ((float) getWidth() - 4.f, area.getY() + area.getHeight() * scroll / all);
            g.setColour (col::inkSoft);
            g.fillRoundedRectangle (thumb, 1.5f);
        }
    }
    if (playInView)
    {
        g.setColour (col::accent);
        g.fillRect (playX - 0.5f, kTop - 4.f, 1.f, 4.f);
    }
}

void LaneEditor::paintRow (juce::Graphics& g, int r, const std::vector<ba::Settings>& along, const std::vector<ba::Settings>& played)
{
    const auto bb = band (r);
    const auto paper = row (r);
    const auto id = laneOf (r);
    const auto& l = laneAt (r);
    const float xEnd = xOfPath (1.f), playX = xOfBuild (model.build());

    // The rule between two bands, across the column (the header carries it on).
    g.setColour (col::hairline);
    if (r < (int) rows.size() - 1) g.fillRect (0.f, bb.getBottom() - 1.f, (float) getWidth(), 1.f);
    // The lane the keys work on: a bar in the gap beside its paper.
    if (r == cursorRow)
    {
        g.setColour (col::inkSoft);
        g.fillRect (2.f, paper.getY(), 2.f, paper.getHeight());
    }

    // Graph paper: the well, the grid along the path, the tail hatched, guides.
    g.setColour (col::well);
    g.fillRect (paper);
    {
        juce::Path minor, major;
        const auto onPaper = [&paper] (float x) { return x > paper.getX() && x < paper.getRight(); };
        const float stepX = xOfPath (gridStep()) - xOfPath (0.f);
        if (stepX >= 4.f)
            for (float t = std::ceil (view0 / ba::kPathEnd / gridStep()) * gridStep(); t < std::min (0.9999f, view1 / ba::kPathEnd + gridStep()); t += gridStep())
                if (const float x = std::round (xOfPath (t)) + 0.5f; t > 0.f && onPaper (x))
                {
                    minor.startNewSubPath (x, paper.getY());
                    minor.lineTo (x, paper.getBottom());
                }
        for (int q = 1; q < 4; ++q)
            if (const float x = std::round (xOfPath ((float) q / 4.f)) + 0.5f; onPaper (x))
            {
                major.startNewSubPath (x, paper.getY());
                major.lineTo (x, paper.getBottom());
            }
        g.setColour (col::grid);
        g.strokePath (minor, juce::PathStrokeType (1.f));
        g.setColour (col::hairline);
        g.strokePath (major, juce::PathStrokeType (1.f));
    }
    const float tailX = juce::jlimit (paper.getX(), paper.getRight(), xEnd);
    const auto tail = paper.withLeft (tailX);
    if (! tail.isEmpty())
    {
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (tail.toNearestInt());
        hatch (g, tail, col::hairline, 5.f, 1.f);
    }
    g.setColour (col::hairline);
    if (xEnd > paper.getX() && xEnd < paper.getRight()) g.fillRect (xEnd, paper.getY(), 1.f, paper.getHeight());
    const auto marks = guides (id);
    for (const auto& [v, text] : marks)
    {
        const float y = std::round (yOf (r, v)) + 0.5f;
        if (y < paper.getY() + 2.f || y > paper.getBottom() - 2.f || tailX <= paper.getX()) continue;
        g.setColour (id == ba::LaneId::level && v == 0.f ? col::hairline : col::grid);
        g.fillRect (paper.getX(), y - 0.5f, tailX - paper.getX(), 1.f);
    }

    // The time selection, behind the line.
    if (sel.covers (r))
    {
        const float xs0 = xOfPath (sel.t0), xs1 = xOfPath (sel.t1);
        const auto s = juce::Rectangle<float>::leftTopRightBottom (xs0, paper.getY(), xs1, paper.getBottom()).getIntersection (paper);
        g.setColour (col::ink.withAlpha (0.07f));
        g.fillRect (s);
        g.setColour (col::inkSoft);
        if (xs0 >= paper.getX()) g.fillRect (xs0, s.getY(), 1.f, s.getHeight());   // its edges, where they are in view
        if (xs1 <= paper.getRight()) g.fillRect (xs1 - 1.f, s.getY(), 1.f, s.getHeight());
    }

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
            if (z.curve == 1.f || xz - xa < 1.f)
            {
                line.lineTo (xz, yOf (r, z.value));
                continue;
            }
            // A curve: a point every 2 px of the part in view (zoomed in, the rest is off the paper).
            const float u0 = juce::jlimit (0.f, 1.f, (paper.getX() - 2.f - xa) / (xz - xa)), u1 = juce::jlimit (0.f, 1.f, (paper.getRight() + 2.f - xa) / (xz - xa));
            if (u0 > 0.f) line.lineTo (xa + (xz - xa) * u0, yOf (r, edit::segment (a, z, u0)));
            const int n = std::max (2, (int) ((u1 - u0) * (xz - xa) / 2.f));
            for (int j = 1; j <= n; ++j)
            {
                const float u = u0 + (u1 - u0) * (float) j / (float) n;
                line.lineTo (xa + (xz - xa) * u, yOf (r, edit::segment (a, z, u)));
            }
            if (u1 < 1.f) line.lineTo (xz, yOf (r, z.value));
        }
        line.lineTo (xEnd, yOf (r, l.keys[(size_t) l.count - 1].value));
    }

    // What plays along BUILD (the lane's stairs, the global tail), and as the
    // INTENSITY knob has it.
    const auto trace = [&] (const std::vector<ba::Settings>& s, float fromBuild) {
        juce::Path t;
        const int n = (int) s.size() - 1;   // samples over the BUILD in view
        const auto buildAt = [&] (int k) { return view0 + (view1 - view0) * (float) k / (float) n; };
        bool started = false;
        for (int k = 0; k <= n; ++k)
        {
            if (k < n && buildAt (k + 1) <= fromBuild) continue;
            const juce::Point<float> pt (xOfBuild (buildAt (k)), yOf (r, valueIn (id, s[(size_t) k])));
            if (! started) t.startNewSubPath (pt);
            else t.lineTo (pt);
            started = true;
        }
        return t;
    };
    g.setColour (col::inkFaint);
    g.strokePath (trace (along, ba::kPathEnd), juce::PathStrokeType (1.2f));
    bool turned = false;
    for (size_t k = 0; k < played.size() && ! turned; ++k) turned = std::abs (valueIn (id, played[k]) - valueIn (id, along[k])) > 1e-5f;
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

    // The segment a drag would take (near the line, or Option held over it),
    // or the one being bent or moved, drawn heavier.
    {
        const bool idle = gesture == Gesture::none && ! drawMode() && hover.row == r;
        const bool alt = juce::ModifierKeys::getCurrentModifiers().isAltDown();
        int k = -1;
        if (gesture == Gesture::bend && start.row == r) k = start.key;
        else if (gesture == Gesture::moveSegment && start.row == r && runFirst != runLast) k = movedFirst + 1;
        else if (idle && (hover.part == Part::nearLine || (alt && hover.part == Part::line))) k = hover.key;
        if (k > 0 && k < l.count && l.keys[(size_t) k].at > l.keys[(size_t) k - 1].at)
        {
            juce::Graphics::ScopedSaveState seg (g);
            g.reduceClipRegion (juce::Rectangle<float>::leftTopRightBottom (xOfPath (l.keys[(size_t) k - 1].at), paper.getY(),
                                                                           xOfPath (l.keys[(size_t) k].at), paper.getBottom()).toNearestInt());
            g.setColour (col::ink);
            g.strokePath (line, juce::PathStrokeType (2.6f));
        }
    }

    // The guides' words, on the row under the mouse only, each on a patch of
    // well at the right end of the tail (nothing to edit there).
    if (hover.row == r || (gesture != Gesture::none && start.row == r))
    {
        g.setFont (mono (8.5f));
        float last = -100.f;   // a word too close to the last one drawn stays out
        for (const auto& [v, text] : marks)
        {
            const float y = std::round (yOf (r, v)) + 0.5f;
            if (text.isEmpty() || y < paper.getY() + 12.f || y > paper.getBottom() - 2.f || std::abs (y - last) < 12.f) continue;
            last = y;
            const float tw = textWidth (g.getCurrentFont(), text) + 6.f;
            const auto patch = juce::Rectangle<float> (tw, 11.f).withPosition (paper.getRight() - 6.f - tw, y - 12.f);
            g.setColour (col::well);
            g.fillRect (patch);
            g.setColour (col::inkSoft);
            g.drawText (text, patch, juce::Justification::centred, false);
        }
    }

    // Dragging keys: a guide down the paper where the key is on the grid (as Live).
    if (gesture == Gesture::moveKeys && start.row == r && dragKey >= 0 && dragKey < l.count)
    {
        g.setColour (col::inkSoft);
        g.fillRect (std::round (xOfPath (l.keys[(size_t) dragKey].at)) - 0.5f, paper.getY(), 1.f, paper.getHeight());
    }

    // The insert marker, on the lane the keys work on.
    if (r == cursorRow && ! sel.active() && keyKey < 0)
    {
        g.setColour (col::ink.withAlpha (0.75f));
        g.fillRect (std::round (xOfPath (marker)) - 0.5f, paper.getY(), 1.f, paper.getHeight());
    }

    // Keys: hollow, inked when selected; the hovered, dragged or jumped-to one
    // larger; the one the keys edit ringed.
    const bool selected = sel.covers (r);
    for (int i = 0; i < l.count; ++i)
    {
        const auto& k = l.keys[(size_t) i];
        const bool in = selected && k.at >= sel.t0 && k.at <= sel.t1;
        const bool keyed = r == cursorRow && (i == keyKey || i == focusKey);
        const bool hot = keyed || (hover.row == r && hover.part == Part::key && hover.key == i) || (start.row == r && dragKey == i && gesture != Gesture::none);
        const juce::Point<float> c (xOfPath (k.at), yOf (r, k.value));
        const auto b = juce::Rectangle<float> (hot ? 9.f : 7.f, hot ? 9.f : 7.f).withCentre (c);
        g.setColour (in || hot ? col::ink : col::well);
        g.fillRoundedRectangle (b, kRadius);
        g.setColour (col::ink);
        g.drawRoundedRectangle (b.reduced (0.5f), kRadius, 1.2f);
        if (r == cursorRow && i == keyKey) g.drawRoundedRectangle (juce::Rectangle<float> (15.f, 15.f).withCentre (c).reduced (0.5f), kRadius, 1.f);
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
    // A value box by the key under the mouse, dragged, or the keys' one, in the
    // knob's words; near a segment, its key nearer the mouse (as Live). At the
    // insert marker the keys moved: the lane's value there.
    int boxKey = -1;
    if (gesture != Gesture::none && start.row == r) boxKey = dragKey;
    else if (gesture == Gesture::none && hover.row == r && hover.part == Part::key) boxKey = hover.key;
    else if (gesture == Gesture::none && hover.row == r && hover.part == Part::nearLine && l.count > 0 && ! drawMode())
        boxKey = closestKey (r, hover.key - 1, hover.key, hover.at);
    else if (r == cursorRow) boxKey = keyKey >= 0 ? keyKey : focusKey;
    if (boxKey >= 0 && boxKey < l.count)
    {
        const auto& k = l.keys[(size_t) boxKey];
        valueBox (g, model.words (id, k.value), { xOfPath (k.at), yOf (r, k.value) }, paper.withRight (xEnd));
    }
    else if (r == cursorRow && markerByKeys && ! sel.active())
    {
        const float v = edit::valueAfter (l, marker);
        valueBox (g, model.words (id, v), { xOfPath (marker), yOf (r, v) }, paper.withRight (xEnd));
    }
}
