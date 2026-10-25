#pragma once

#include "Theme.h"
#include "fx/LaneEdit.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>
#include <vector>

// What the lane editor edits (the preset editor implements it). Lanes are
// in path terms (0..1 = BUILD 0..kPathEnd); every change of a gesture goes to
// `change` at once, so the sound follows the mouse, and beginEdit/endEdit
// make the gesture one undo step (cancelEdit puts it back, no undo step).
struct LaneModel
{
    virtual ~LaneModel() = default;
    virtual const ba::Preset& preset() const = 0;          // as it plays
    virtual std::vector<ba::LaneId> lanes() const = 0;     // the rows, top to bottom
    virtual void beginEdit() = 0;
    virtual void change (ba::LaneId, const ba::Lane&) = 0;
    virtual void endEdit (const juce::String& what) = 0;
    virtual void cancelEdit() = 0;
    virtual void removeLane (ba::LaneId) = 0;              // the control goes back to its knob
    virtual float build() const = 0;                       // BUILD 0..1
    virtual void scrub (float build, int phase) = 0;       // phase: -1 begin, 0 drag, 1 end
    virtual ba::Settings run (float build, bool designed) const = 0;   // what the chain runs (designed: every intensity at 100 %)
    virtual juce::String words (ba::LaneId, float value) const = 0;    // the knob's words for a lane value
    virtual std::optional<float> parse (ba::LaneId, const juce::String&) const = 0;
    virtual void say (const juce::String& line) = 0;       // the footer; empty: its default
};

// The preset editor's lanes, all in one component: a ruler over BUILD (drag
// it to scrub), then a band per lane, its graph paper (a well) with a gap
// above and below. The editable path is solid, the part already built lit,
// the tail past kPathEnd hatched (global, shown, not editable), and the
// played path dashed where an INTENSITY knob turns it down. The bands share
// the height; below kMinRow each they scroll (the wheel). The headers beside
// them are the preset editor's, laid out from rowTop / rowHeight.
//
// Mouse and keys work like Ableton Live's automation lanes (LaneEdit.h does
// the editing, on the lane as the gesture found it):
//   Draw Mode (B; held, for one gesture): drag draws steps one grid cell
//   wide, Shift finer, Cmd (Mac) / Alt (Windows) freehand.
//   Otherwise: click the line for a breakpoint, double-click off it for one,
//   click one to delete it, drag them (the selected ones follow, a guide
//   shows where on the grid; Cmd / Alt: no snap; Shift: one axis, finer),
//   click near a segment or Shift-click it and drag it (in a selection the
//   selected part moves as one), Option/Alt-drag a segment to bend it,
//   Option/Alt-double-click to straighten it, drag the background for a
//   time selection (across rows; Shift-click extends it), its handles to
//   stretch and skew, right-click for Edit Value (the selected breakpoints
//   move with it), Add Value, shapes, Simplify Envelope, the edit commands
//   and the grid. The grid: Cmd 1 / 2 narrower / wider, Cmd 3 triplets,
//   Cmd 4 snap. Cmd C / X / V / D, Delete, Cmd A, Esc, Cmd Delete removes
//   the lane.
//   The keyboard (Live's breakpoint keys): a click on the background puts
//   the insert marker there; left / right move it a grid step (the
//   selection, when there is one; Shift resizes it), Option / Alt jumps to
//   the previous / next breakpoint, up / down (or Option / Alt) the lane.
//   Enter selects the breakpoint at the marker or adds one; then left /
//   right move it, up / down or typing a number change its value (Shift
//   finer), Delete removes it, Enter confirms, Esc takes it back. Tab /
//   Shift-Tab select the next / previous breakpoint; Option/Alt-click one
//   selects it.
class LaneEditor : public juce::Component
{
public:
    explicit LaneEditor (LaneModel& m);

    // ---- What the toolbar shows ---------------------------------------------------
    static constexpr int kGridCount = 7;   // 1/4 .. 1/256 of the path
    static juce::String gridName (int g);  // "1/16"
    bool drawMode() const { return draw != heldB; }   // B held flips it
    void setDrawMode (bool on);
    int grid() const;                      // the grid in use (adaptive: the one the zoom gives)
    void setGrid (int g);                  // a fixed grid of that size
    void narrower();                       // Cmd 1: a finer grid (adaptive: denser)
    void wider();                          // Cmd 2
    bool adaptive() const { return adaptiveGrid; }
    void setAdaptive (bool on);            // Cmd 5: the grid follows the zoom (Live's default) or stays fixed
    bool snaps() const { return snap; }
    void setSnap (bool on);
    bool triplets() const { return triplet; }
    void setTriplets (bool on);
    std::function<void()> onModeChange;   // draw, grid, snap or triplet changed
    std::function<void()> onScroll;       // the bands moved (the headers follow)

    // ---- Zoom (Live's: pinch or Cmd-wheel, + / -, Z to the selection, X back,
    // W the whole build; the overview strip and a two-finger swipe or
    // Shift-wheel scroll). The view is BUILD view0..view1.
    void zoomBy (float factor, float aroundBuild);
    void zoomAll();
    float viewStart() const { return view0; }
    float viewEnd() const { return view1; }

    // ---- From the editor --------------------------------------------------------------
    void refresh();                       // once per tick: the playhead
    void presetChanged();                 // another preset, or one undone: drop the selection
    void contentChanged();                // the preset changed (an edit, lanes added or gone)
    bool keyPressed (const juce::KeyPress&) override;   // the editor forwards its keys
    bool keyStateChanged (bool isKeyDown) override;
    void commitKeys();                    // a breakpoint the keys are editing: keep it as it is

    // ---- Geometry, for the headers beside the bands and the snapshot tool ----------
    float rowTop (int row) const;         // the band's top, scrolled
    float rowHeight() const;              // the band's height (the gap included)
    juce::Rectangle<float> rowsArea() const;   // under the ruler: where the bands show
    juce::Point<float> pointOf (int row, float path, float value) const;   // a lane value on its row
    int focusedRow() const { return cursorRow; }
    static float valueIn (ba::LaneId, const ba::Settings&);   // what the chain runs for a lane's control, in lane units
    // Top to bottom: the overview strip (and a gap), the ruler, the bands.
    static constexpr float kOverview = 16.f, kRuler = 22.f, kTop = kOverview + kRuler, kGap = 8.f, kPad = 8.f, kRowGap = 6.f, kMinRow = 48.f;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify (const juce::MouseEvent&, float scaleFactor) override;   // a trackpad pinch
    void modifierKeysChanged (const juce::ModifierKeys&) override;   // Option / Alt over a segment: it shows it bends
    void focusLost (FocusChangeType) override;

private:
    // What is under the mouse.
    enum class Part { none, overview, ruler, key, line, nearLine, background, handle, tail };
    enum class Handle { none, top, bottom, left, right, topLeft, topRight, bottomLeft, bottomRight };
    struct Hit
    {
        Part part = Part::none;
        int row = -1;
        int key = -1;       // Part::key: its index; line / nearLine: the key the segment ends at (0: the hold before the first, count: after the last)
        float at = 0.f;     // path position under the mouse
        float value = 0.f;  // lane value under the mouse
        Handle handle = Handle::none;
    };
    struct Selection
    {
        float t0 = 0.f, t1 = 0.f;   // path
        int first = -1, last = -1;  // rows
        bool active() const { return first >= 0 && t1 > t0; }
        bool covers (int row) const { return active() && row >= first && row <= last; }
    };
    enum class Gesture { none, scrub, view, moveKeys, moveSegment, shiftRange, bend, select, handle, drawSteps, drawFree, maybeDelete, maybePick };

    // Geometry.
    juce::Rectangle<float> plot() const;   // the time axis (BUILD view0..view1 left to right) over the rows area
    juce::Rectangle<float> overview() const;   // the whole build, the view outlined on it
    float buildOfX (float x) const;
    float buildOfOverviewX (float x) const;
    void setView (float v0, float v1);     // clamped; the adaptive grid follows
    void showBuild (float b);              // scrolled so b is in view
    juce::Rectangle<float> band (int r) const;
    juce::Rectangle<float> row (int r) const;   // the band's well (the graph paper)
    float xOfBuild (float b) const;
    float xOfPath (float p) const { return xOfBuild (p * ba::kPathEnd); }
    float pathOfX (float x) const;
    float yOf (int r, float value) const;
    float valueOf (int r, float y) const;
    float gridStep() const;                // path units, triplets included
    float pathPerPixel() const;
    float maxScroll() const;
    void scrollTo (float y);
    void showRow (int r);                  // scrolled into view

    Hit hitAt (juce::Point<float>) const;
    std::optional<juce::Rectangle<float>> handleBox (int r) const;   // the selection's box on that row
    Handle handleAt (int r, juce::Point<float>) const;
    ba::LaneId laneOf (int r) const { return rows[(size_t) r]; }
    const ba::Lane& laneAt (int r) const { return ba::lane (model.preset(), laneOf (r)); }
    ba::LaneRange rangeOf (int r) const { return ba::laneRange (laneOf (r)); }
    std::pair<int, int> selectedKeys (int r) const;   // the run of keys inside the selection, first > last if none
    // `at` snapped to the grid and to the times of the lane's keys that aren't `moving`.
    float snapAt (float at, const ba::Lane&, std::span<const int> moving, const juce::ModifierKeys&) const;
    int closestKey (int r, int a, int z, float at) const;   // of keys a and z, the one nearer `at` (a valid one)

    // Edits.
    void apply (int r, const ba::Lane& l) { model.change (laneOf (r), l); }
    void beginGesture (Gesture g, const Hit& h, const juce::MouseEvent& e);
    void dragGesture (const juce::MouseEvent& e);
    void drawCellsTo (float at, float value);
    void command (int what);   // the context menu's and the keys' edit commands
    void showMenu (const Hit& h);
    void editValue (const Hit& h, bool addNew, const juce::String& typed = {});
    void insertShape (ba::edit::Shape);
    void updateCursor (const Hit& h, const juce::ModifierKeys&);
    void hoverTo (const Hit& h);
    juce::String describe (const Hit& h) const;   // the footer while hovering
    std::pair<float, float> selectionOrCell (float at) const;

    // The keyboard (Live's breakpoint keys).
    int keyRow() const;                    // the row the keys work on
    void beginKeyEdit (const juce::String& what);
    void pickKey (int r, int key);         // selected for the keys; the marker on it
    void dropKeys();                       // nothing selected for the keys
    void enter();
    void moveMarker (int direction, bool resize);
    void jumpToKey (int direction);
    void tabToKey (int direction);
    void moveKey (int direction);
    void stepValue (int direction, bool fine);
    void moveRow (int direction);
    float valueStep (int r, bool fine) const;

    void paintRow (juce::Graphics&, int r, const std::vector<ba::Settings>& along, const std::vector<ba::Settings>& played);

    LaneModel& model;
    std::vector<ba::LaneId> rows;
    bool draw = false, heldB = false, editedWhileB = false, snap = true, triplet = false, adaptiveGrid = true;
    int gridIndex = 2;   // fixed: 1/16
    int density = 2;     // adaptive: Live's Narrowest..Widest (the least room a cell gets)
    float view0 = 0.f, view1 = 1.f;
    std::vector<std::pair<float, float>> zoomedFrom;   // Z's, for X
    Selection sel;
    Hit hover;
    float shownBuild = -1.f, scroll = 0.f;

    // The insert marker and the breakpoint the keys work on.
    float marker = 0.f;
    int cursorRow = -1, keyKey = -1, focusKey = -1;   // keyKey: selected (Enter, Tab, Option-click); focusKey: jumped to
    bool keyEditing = false, markerByKeys = false, growRight = true;   // growRight: Shift-arrows move the selection's right edge
    juce::String keyEditName;

    // The gesture in progress.
    Gesture gesture = Gesture::none;
    Hit start;
    juce::Point<float> downAt, mouseAt;
    ba::Lane before;      // the lane as the gesture found it
    int runFirst = -1, runLast = -1, movedFirst = -1;   // the run as it was, and where it starts now
    float grabValue = 0.f, grabU = 0.f, dragValueBase = 0.f;
    std::optional<bool> horizontal;   // Shift's axis, once it's clear
    std::vector<ba::edit::Cell> cells;
    std::vector<ba::Key> stroke;
    Selection selBefore;
    std::pair<float, float> box;   // a handle's: the content's bottom and top
    float grabView0 = 0.f, grabSpan = 1.f;   // the overview drag's view as it began
    juce::String gestureName;
    int dragKey = -1;              // the key whose value box shows while dragging

    std::unique_ptr<juce::TextEditor> entry;
    juce::MouseCursor pencil;
};
