#pragma once

#include "Theme.h"
#include "fx/LaneEdit.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>
#include <vector>

// What the lane editor edits (the preset editor implements it). Lanes are
// in path terms (0..1 = BUILD 0..kPathEnd); every change of a gesture goes to
// `change` at once, so the sound follows the mouse, and beginEdit/endEdit
// make the gesture one undo step.
struct LaneModel
{
    virtual ~LaneModel() = default;
    virtual const ba::Preset& preset() const = 0;          // as it plays
    virtual std::vector<ba::LaneId> lanes() const = 0;     // the rows, top to bottom
    virtual void beginEdit() = 0;
    virtual void change (ba::LaneId, const ba::Lane&) = 0;
    virtual void endEdit (const juce::String& what) = 0;
    virtual float build() const = 0;                       // BUILD 0..1
    virtual void scrub (float build, int phase) = 0;       // phase: -1 begin, 0 drag, 1 end
    virtual ba::Settings run (float build, bool designed) const = 0;   // what the chain runs (designed: every intensity at 100 %)
    virtual juce::String words (ba::LaneId, float value) const = 0;    // the knob's words for a lane value
    virtual std::optional<float> parse (ba::LaneId, const juce::String&) const = 0;
    virtual void say (const juce::String& line) = 0;       // the footer; empty: its default
};

// The preset editor's lanes, all in one component: a ruler over BUILD (drag
// it to scrub), then a row per lane on graph paper, the editable path solid,
// the part already built lit, the tail past kPathEnd hatched (global, shown,
// not editable), the played path dashed where an INTENSITY knob turns it
// down, and each row's value at the playhead on the right.
//
// Mouse and keys work like Ableton Live's automation lanes (LaneEdit.h does
// the editing, on the lane as the gesture found it):
//   Draw Mode (B; held, for one gesture): drag draws steps one grid cell
//   wide, Shift finer, Cmd (Mac) / Alt (Windows) freehand. Otherwise: click
//   the line for a key, double-click off it for one, click a key to delete
//   it, drag keys (the selected ones follow; Cmd / Alt: no snap; Shift: one
//   axis, finer), click near a segment or Shift-click it and drag it (in a
//   selection the selected part moves as one), Option/Alt-drag a segment to
//   bend it, Option/Alt-double-click to straighten it, drag the background
//   for a time selection (across rows), its handles to stretch and skew,
//   right-click for Edit Value, shapes, Simplify Envelope and the edit
//   commands. The grid: Cmd 1 / 2 narrower / wider, Cmd 3 triplets, Cmd 4
//   snap. Cmd C / X / V / D, Delete, Cmd A, Esc.
class LaneEditor : public juce::Component
{
public:
    explicit LaneEditor (LaneModel& m);

    // ---- What the toolbar shows ---------------------------------------------------
    static constexpr int kGridCount = 5;   // 1/4 .. 1/64 of the path
    static juce::String gridName (int g);  // "1/16"
    bool drawMode() const { return draw != heldB; }   // B held flips it
    void setDrawMode (bool on);
    int grid() const { return gridIndex; }
    void setGrid (int g);
    bool snaps() const { return snap; }
    void setSnap (bool on);
    bool triplets() const { return triplet; }
    void setTriplets (bool on);
    std::function<void()> onModeChange;   // draw, grid, snap or triplet changed

    // ---- From the editor --------------------------------------------------------------
    void refresh();                       // once per tick: the playhead
    void presetChanged();                 // another preset, or one undone: drop the selection
    void contentChanged() { rows = model.lanes(); repaint(); }   // the preset changed (an edit, lanes added or gone)
    bool keyPressed (const juce::KeyPress&) override;   // the editor forwards its keys
    bool keyStateChanged (bool isKeyDown) override;
    float rowTop (int row) const;         // for the headers next to the rows
    float rowHeight() const;
    static constexpr float kRuler = 22.f, kValueW = 70.f, kPad = 8.f;

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void modifierKeysChanged (const juce::ModifierKeys&) override;   // Option / Alt over a segment: it shows it bends

private:
    // What is under the mouse.
    enum class Part { none, ruler, key, line, nearLine, background, handle, tail };
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
    enum class Gesture { none, scrub, moveKeys, moveSegment, shiftRange, bend, select, handle, drawSteps, drawFree, maybeDelete };

    // Geometry.
    juce::Rectangle<float> plot() const;   // every row's plot area, BUILD 0..1 left to right
    juce::Rectangle<float> row (int r) const;
    float xOfBuild (float b) const;
    float xOfPath (float p) const { return xOfBuild (p * ba::kPathEnd); }
    float pathOfX (float x) const;
    float yOf (int r, float value) const;
    float valueOf (int r, float y) const;
    float gridStep() const;                // path units, triplets included
    float pathPerPixel() const;

    Hit hitAt (juce::Point<float>) const;
    std::optional<juce::Rectangle<float>> handleBox (int r) const;   // the selection's box on that row
    Handle handleAt (int r, juce::Point<float>) const;
    ba::LaneId laneOf (int r) const { return rows[(size_t) r]; }
    const ba::Lane& laneAt (int r) const { return ba::lane (model.preset(), laneOf (r)); }
    ba::LaneRange rangeOf (int r) const { return ba::laneRange (laneOf (r)); }
    std::pair<int, int> selectedKeys (int r) const;   // the run of keys inside the selection, first > last if none
    // `at` snapped to the grid and to the times of the lane's keys that aren't `moving`.
    float snapAt (float at, const ba::Lane&, std::span<const int> moving, const juce::ModifierKeys&) const;

    // Edits.
    void apply (int r, const ba::Lane& l) { model.change (laneOf (r), l); }
    void beginGesture (Gesture g, const Hit& h, const juce::MouseEvent& e);
    void dragGesture (const juce::MouseEvent& e);
    void drawCellsTo (float at, float value);
    void command (int what);   // the context menu's and the keys' edit commands
    void showMenu (const Hit& h);
    void editValue (const Hit& h, bool addNew);
    void insertShape (ba::edit::Shape);
    void updateCursor (const Hit& h, const juce::ModifierKeys&);
    void hoverTo (const Hit& h);
    juce::String describe (const Hit& h) const;   // the footer while hovering
    std::pair<float, float> selectionOrCell (float at) const;

    void paintRow (juce::Graphics&, int r, const std::vector<ba::Settings>& along, const std::vector<ba::Settings>& played);

    LaneModel& model;
    std::vector<ba::LaneId> rows;
    bool draw = false, heldB = false, editedWhileB = false, snap = true, triplet = false;
    int gridIndex = 2;   // 1/16
    Selection sel;
    float marker = 0.f;  // where Paste lands
    int markerRow = -1;
    Hit hover;
    float shownBuild = -1.f;

    // The gesture in progress.
    Gesture gesture = Gesture::none;
    Hit start;
    juce::Point<float> downAt;
    ba::Lane before;      // the lane as the gesture found it
    int runFirst = -1, runLast = -1;
    float grabValue = 0.f, grabU = 0.f, dragValueBase = 0.f;
    std::optional<bool> horizontal;   // Shift's axis, once it's clear
    std::vector<ba::edit::Cell> cells;
    std::vector<ba::Key> stroke;
    Selection selBefore;
    std::pair<float, float> box;   // a handle's: the content's bottom and top
    juce::String gestureName;
    int dragKey = -1;              // the key under the mouse while dragging (its value box)

    std::unique_ptr<juce::TextEditor> entry;
    juce::MouseCursor pencil;
};
