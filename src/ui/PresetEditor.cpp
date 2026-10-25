#include "PresetEditor.h"

#include "PresetMenu.h"
#include "fx/CutFilters.h"
#include "plugin/PluginProcessor.h"

using namespace theme;
namespace edit = ba::edit;
using L = ba::LaneId;
using P = BuildAppProcessor;

namespace
{
    // The window's layout, top to bottom (logical pixels, unscaled): the
    // masthead, section A's two rows (the preset, its description), a rule,
    // section B's row, the lanes, the footer. Rows are 22 high, 8 apart; 12
    // from a rule to a row and from a row to what follows.
    constexpr float kMastRuleY = 62.f, kRowA = 74.f, kRowA2 = 104.f, kRuleY = 138.f, kRowB = 150.f, kLanesY = 184.f;
    constexpr int kRowH = 22, kFooter = 46, kKeyGap = 6, kGroupGap = 24;

    const char* laneName (L id)
    {
        constexpr const char* names[] = { "Low cut", "Low reso", "High cut", "High reso", "Pitch", "Noise", "Noise pitch", "Noise cut", "Reverb", "Stereo", "Level" };
        return names[(int) id];
    }

    juce::String laneHelp (L id)
    {
        switch (id)
        {
            case L::lowCut:      return "The low cut along the build, 20 Hz to 8 kHz (all the way down: off), in the type under its name.";
            case L::lowReso:     return "The low cut's resonance along the build; AUTO on the low cut hands it back to auto resonance.";
            case L::highCut:     return "The high cut along the build, 20 kHz down to 100 Hz (all the way up: off), in the type under its name.";
            case L::highReso:    return "The high cut's resonance along the build; AUTO on the high cut hands it back to auto resonance.";
            case L::pitch:       return "The pitch along the build, 0 to +12 semitones; STEPS keeps it on whole semitones (stairs).";
            case L::noise:       return "The noise's volume along the build (all the way down: off).";
            case L::noisePitch:  return "Where the noise band sits along the build, 80 Hz to 15 kHz.";
            case L::noiseLowCut: return "The noise's own low cut along the build (all the way down: off).";
            case L::reverb:      return "The reverb along the build; past 75 % every preset brings it up for the tail.";
            case L::stereo:      return "The image along the build: bottom is the full width, top is mono.";
            case L::level:       return juce::String (juce::CharPointer_UTF8 ("Makeup gain on the wet signal along the build, \xe2\x88\x92" "12 to +12 dB: it holds the loudness when the cuts take more away than the noise and reverb fill."));
        }
        return {};
    }

    const char* paramOf (L id)
    {
        switch (id)
        {
            case L::lowCut:      return ids::lowCut;
            case L::lowReso:     return ids::lowCutReso;
            case L::highCut:     return ids::highCut;
            case L::highReso:    return ids::highCutReso;
            case L::pitch:       return ids::pitch;
            case L::noise:       return ids::noise;
            case L::noisePitch:  return ids::noisePitch;
            case L::noiseLowCut: return ids::noiseLowCut;
            case L::reverb:      return ids::reverb;
            case L::stereo:      return ids::stereo;
            case L::level:       return nullptr;
        }
        return nullptr;
    }

    float (*const kIntensity[]) (const ba::Intensity&) = {
        [] (const ba::Intensity& k) { return k.lowCut; },   [] (const ba::Intensity& k) { return k.lowReso; },
        [] (const ba::Intensity& k) { return k.highCut; },  [] (const ba::Intensity& k) { return k.highReso; },
        [] (const ba::Intensity& k) { return k.pitch; },    [] (const ba::Intensity& k) { return k.noise; },
        [] (const ba::Intensity& k) { return k.noisePitch; }, [] (const ba::Intensity& k) { return k.noiseLowCut; },
        [] (const ba::Intensity& k) { return k.reverb; },   [] (const ba::Intensity& k) { return k.stereo; },
        [] (const ba::Intensity& k) { return k.level; },
    };

    std::vector<TypeSelector::Choice> cutChoices (bool lowpass)
    {
        std::vector<TypeSelector::Choice> out;
        for (const auto& c : ba::characters (lowpass)) out.push_back ({ c.name, c.help, c.build ? "Buildup" : "Breakdown" });
        return out;
    }

    std::vector<TypeSelector::Choice> gridChoices()
    {
        constexpr const char* what[] = { "four bars", "two bars", "a bar", "half a bar", "a beat", "an eighth", "a sixteenth" };
        static_assert (std::size (what) == LaneEditor::kGridCount);
        std::vector<TypeSelector::Choice> out;
        for (int g = 0; g < LaneEditor::kGridCount; ++g)
            out.push_back ({ LaneEditor::gridName (g), "The grid: " + LaneEditor::gridName (g) + " of the path, " + what[g] + " of a 16-bar build.", {} });
        return out;
    }

    juce::juce_wchar upper (int code) { return juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) code); }

    // Live's modifier names on this platform.
   #if JUCE_MAC
    const juce::String kCmd ("Cmd"), kOpt ("Option"), kNoSnap ("Cmd");
   #else
    const juce::String kCmd ("Ctrl"), kOpt ("Alt"), kNoSnap ("Alt");
   #endif
    juce::String u8 (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); }
} // namespace

// ---- A lane's header ----------------------------------------------------------------

// Beside its row: the lane's name and what it plays at BUILD (dim at rest),
// the x that removes it; under them its cut type and AUTO (cuts) or STEPS
// (pitch); its INTENSITY when that's turned down.
struct PresetEditorView::Header : juce::Component
{
    static constexpr int kTop = (int) (LaneEditor::kRowGap * 0.5f) + 2, kLine2 = 25, kLine3 = 46;

    Header (PresetEditorView& v, L lane) : view (v), id (lane)
    {
        setOpaque (true);
        const bool cut = id == L::lowCut || id == L::highCut;
        if (cut)
        {
            type = std::make_unique<TypeSelector> ("", cutChoices (id == L::highCut));
            type->onChoose = [this] (int t) { view.setCutType (id == L::highCut, t); };
            type->onHover = [this] (TypeSelector* s, bool on) { view.say (on ? s->getHelp() : juce::String()); };
            addAndMakeVisible (*type);
            option.setText ("Auto");
            option.setHelp ("AUTO on: the resonance follows the cut the way its type wants. Off: a resonance lane of its own, starting where auto had it.");
            option.onClick = [this] { view.setAutoReso (id == L::highCut, ! option.isOn()); };
            addAndMakeVisible (option);
        }
        else if (id == L::pitch)
        {
            option.setText ("Steps");
            option.setHelp ("STEPS on: the pitch moves a whole semitone at a time (Goa stairs); off: it glides.");
            option.onClick = [this] { view.setSteps (! option.isOn()); };
            addAndMakeVisible (option);
        }
        remove.setHelp ("Removes this lane from the preset: the control goes back to your knob (" + kCmd + " Delete on its row).");
        remove.onClick = [this] { view.removeLane (id); };
        addAndMakeVisible (remove);
        for (auto* k : { &option, &remove })
            k->onHover = [this] (TextKey* key, bool on) { view.say (on ? key->getHelp() : juce::String()); };
        refresh();
    }

    bool hasOptions() const { return type != nullptr || id == L::pitch; }

    void refresh()
    {
        const auto& p = view.working;
        if (type != nullptr) type->setIndex (id == L::highCut ? std::max (0, p.highCutType) : std::max (0, p.lowCutType));
        if (id == L::lowCut) option.setOn (! p.lowReso.driven());
        if (id == L::highCut) option.setOn (! p.highReso.driven());
        if (id == L::pitch) option.setOn (p.pitch.step > 0.f);
        const float k = kIntensity[(int) id] (view.proc.intensity());
        if (k != intensity)
        {
            intensity = k;
            repaint();
        }
    }

    void resized() override
    {
        const int w = getWidth() - 12;   // the right edge, clear of the rows
        remove.setBounds (w - 16, kTop - 1, 16, 16);
        if (hasOptions())
        {
            const int ow = option.idealWidth();
            option.setBounds (w - ow, kLine2, ow, 17);
            // The type's words start where the name does (the selector keeps 9 px for a name it hasn't got).
            if (type != nullptr) type->setBounds (-9, kLine2, w - ow - 8 + 9, 17);
        }
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (col::sheet);
        const float w = (float) getWidth() - 12.f;
        caption (g, laneName (id), { 0.f, (float) kTop, w - 100.f, 14.f }, juce::Justification::centredLeft, col::ink, 10.f);
        // What it plays at BUILD, in the knob's words; at rest it steps back.
        const float now = LaneEditor::valueIn (id, view.run (view.build(), false)), rest = LaneEditor::valueIn (id, view.run (0.f, false));
        g.setFont (mono (11.f));
        g.setColour (std::abs (now - rest) > 1e-5f ? col::ink : col::inkFaint);
        g.drawText (view.words (id, now), juce::Rectangle<float> (w - 16.f - 8.f - 96.f, (float) kTop, 96.f, 14.f), juce::Justification::centredRight, false);
        if (intensity < 1.f)
        {
            const float y = hasOptions() ? (float) kLine3 : (float) kLine2;
            if (y + 13.f <= (float) getHeight() - LaneEditor::kRowGap * 0.5f)
                caption (g, "Intensity " + fmt::fixed (intensity * 100.0, 0) + " %", { 0.f, y, w, 13.f }, juce::Justification::centredLeft, col::inkFaint, 9.f);
        }
        if (const auto rows = view.lanes(); ! rows.empty() && rows.back() != id)
        {
            g.setColour (col::hairline);
            g.fillRect (0.f, (float) getHeight() - 1.f, (float) getWidth(), 1.f);
        }
    }

    void mouseEnter (const juce::MouseEvent&) override { view.say (juce::String (laneName (id)) + theme::dot + laneHelp (id)); }
    void mouseExit (const juce::MouseEvent&) override { view.say ({}); }

    PresetEditorView& view;
    const L id;
    std::unique_ptr<TypeSelector> type;
    TextKey option { "" }, remove { u8 ("\xc3\x97") };
    float intensity = 1.f;
};

// ---- The gestures and keys, at a glance -----------------------------------------------

// Over the lanes while SHORTCUTS is on: Live's automation gestures and keys
// as this editor has them, in groups. A click anywhere (or Esc) closes it.
struct PresetEditorView::Shortcuts : juce::Component
{
    struct Line
    {
        juce::String keys, does;
    };
    struct Group
    {
        juce::String name;
        std::vector<Line> lines;
    };

    explicit Shortcuts (PresetEditorView& v) : view (v)
    {
        setOpaque (true);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        const auto lr = u8 ("\xe2\x86\x90 \xe2\x86\x92"), ud = u8 ("\xe2\x86\x91 \xe2\x86\x93");
        columns = {
            { { "Draw Mode",
                { { "B", "Draw Mode on or off" },
                  { "hold B", "Draw Mode for one gesture" },
                  { "drag", "draws steps one grid cell wide" },
                  { "Shift-drag", "finer values" },
                  { kNoSnap + "-drag", "freehand, off the grid" } } },
              { "Breakpoints",
                { { "click the line", "adds one" },
                  { "double-click off it", "adds one there" },
                  { "click one", "deletes it" },
                  { "drag one", "moves it (the selected ones follow)" },
                  { "Shift-drag", "one direction, finer" },
                  { kNoSnap + "-drag", "no snap" },
                  { "right-click", "Edit Value, Add Value" } } },
              { "Segments",
                { { "click near, Shift-click", "grabs it: drag to move it" },
                  { kOpt + "-drag", "curves it" },
                  { kOpt + "-double-click", "straightens it" } } } },
            { { "Time selection",
                { { "drag the background", "selects (across lanes)" },
                  { "Shift-click", "extends it" },
                  { "side handles", "stretch in time (Shift: the rest moves)" },
                  { "top, bottom handles", "stretch the values" },
                  { "corner handles", "skew (" + kOpt + ": mirrored)" },
                  { "right-click", "shapes, Simplify Envelope" },
                  { kCmd + " C X V D", "copy, cut, paste, duplicate" },
                  { "Delete, " + kCmd + " A", "clear it, select all" } } },
              { "Zoom and scroll",
                { { "pinch, " + kCmd + "-scroll", "zoom in and out" },
                  { "swipe, Shift-scroll", "scroll along the build" },
                  { "Z, X", "zoom to the selection, and back" },
                  { "+, -, W", "zoom in, out, all of it" },
                  { "drag the overview", "scroll; down or up zooms" },
                  { "drag the ruler", "scrubs BUILD" } } } },
            { { "Keyboard",
                { { "click the background", "puts the insert marker there" },
                  { lr, "marker or selection, a grid step" },
                  { "Shift " + lr, "resizes the selection" },
                  { kOpt + " " + lr, "previous, next breakpoint" },
                  { ud, "previous, next lane" },
                  { "Enter", "selects or adds a breakpoint" },
                  { "Tab, Shift-Tab", "next, previous breakpoint" },
                  { lr + " " + ud + ", 0-9", "move it, change its value" },
                  { "Enter, Esc, Delete", "keep, take back, remove" } } },
              { "Grid and more",
                { { kCmd + " 1, " + kCmd + " 2", "narrower, wider grid" },
                  { kCmd + " 3, 4, 5", "triplets, snap, adaptive" },
                  { kCmd + " Delete", "removes the lane" },
                  { kCmd + " Z, " + kCmd + " Shift Z", "undo, redo" },
                  { kCmd + " S, " + kCmd + " Shift S", "save, save as" } } } },
        };
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (col::sheet);
        const auto b = getLocalBounds().toFloat();
        g.setColour (col::hairline);
        g.drawRoundedRectangle (b.reduced (0.5f), kRadius, 1.f);
        const auto inner = b.reduced (20.f, 16.f);
        caption (g, "Mouse and keys: Ableton Live's automation editing", inner.withHeight (15.f), juce::Justification::centredLeft, col::ink, 10.f);
        caption (g, "Click anywhere or Esc to close", inner.withHeight (15.f), juce::Justification::centredRight, col::inkFaint, 9.f);

        // Rows as tall as the room allows, 15 to 19 px.
        size_t most = 0;
        for (const auto& c : columns)
        {
            size_t n = 0;
            for (const auto& grp : c) n += grp.lines.size();
            most = std::max (most, n);
        }
        size_t groups = 0;
        for (const auto& c : columns) groups = std::max (groups, c.size());
        const float top = inner.getY() + 34.f, heads = (float) groups * 36.f;   // a group's name and rule, the gap after it
        const float lineH = juce::jlimit (13.f, 19.f, (inner.getBottom() - top - heads) / (float) most);
        const float colGap = 28.f, colW = (inner.getWidth() - colGap * (float) (columns.size() - 1)) / (float) columns.size();
        for (size_t c = 0; c < columns.size(); ++c)
        {
            const float x = inner.getX() + (float) c * (colW + colGap);
            float y = top;
            const float keysW = std::min (132.f, colW * 0.42f);
            for (const auto& grp : columns[c])
            {
                caption (g, grp.name, { x, y, colW, 14.f }, juce::Justification::centredLeft, col::inkSoft, 9.5f);
                g.setColour (col::hairline);
                g.fillRect (x, y + 17.f, colW, 1.f);
                y += 24.f;
                for (const auto& l : grp.lines)
                {
                    g.setFont (mono (10.f));
                    g.setColour (col::ink);
                    g.drawText (l.keys, juce::Rectangle<float> (x, y, keysW - 8.f, lineH), juce::Justification::centredLeft, true);
                    g.setFont (title (12.5f));
                    g.setColour (col::inkSoft);
                    g.drawText (l.does, juce::Rectangle<float> (x + keysW, y, colW - keysW, lineH), juce::Justification::centredLeft, true);
                    y += lineH;
                }
                y += 12.f;
            }
        }
    }

    void mouseDown (const juce::MouseEvent&) override { view.showShortcuts (false); }

    PresetEditorView& view;
    std::vector<std::vector<Group>> columns;
};

// One undo step: the preset that was on, and its slot, before and after.
struct PresetEditorView::Step : juce::UndoableAction
{
    Step (PresetEditorView& v, Snapshot b, Snapshot a) : view (v), was (std::move (b)), now (std::move (a)) {}
    bool perform() override { view.restore (now); return true; }
    bool undo() override { view.restore (was); return true; }
    int getSizeInUnits() override { return 10; }

    PresetEditorView& view;
    Snapshot was, now;
};

// ---- The view --------------------------------------------------------------------------

PresetEditorView::PresetEditorView (BuildAppProcessor& p)
    : proc (p),
      presetMenu (p.param (ids::preset), "", presetmenu::choices()),
      gridMenu ("Grid", gridChoices()),
      undoKey ("Undo", "Undoes the last change to the preset (" + kCmd + " Z)."),
      redoKey ("Redo", "Does it again (" + kCmd + " Shift Z)."),
      keysKey ("Shortcuts", "Every gesture and key of the lanes, as in Ableton Live's automation lanes."),
      saveKey ("Save", "Writes the preset to its file in your library (" + kCmd + " S)."),
      saveAsKey (u8 ("Save as\xe2\x80\xa6"), "Keeps it in your library under a name of its own (" + kCmd + " Shift S); it then shows under USER in the preset menu of every project."),
      revertKey ("Revert", "Back to the preset as it was saved (or to the factory preset it was copied from)."),
      drawKey ("Draw", "Draw Mode (B, or hold B while you edit): drag to draw steps one grid cell wide; Shift finer; " + kNoSnap + " freehand."),
      adaptiveKey ("Adaptive", "On: the grid follows the zoom, as Live's zoom-adaptive grid (" + kCmd + " 5); " + kCmd + " 1 / " + kCmd
                                   + " 2 or < > make it denser or wider. Off: the grid stays at the size you pick."),
      snapKey ("Snap", "Breakpoints and drawing snap to the grid (" + kCmd + " 4); " + kNoSnap + " bypasses it while you drag."),
      tripletKey ("Triplet", "A triplet grid: two thirds of each step (" + kCmd + " 3)."),
      addKey ("+ Add lane", "Gives the preset a lane for another control: it then follows that lane instead of your knob.")
{
    setLookAndFeel (&lnf);
    setOpaque (true);
    setWantsKeyboardFocus (true);

    presetmenu::attach (presetMenu, proc, [this] (const juce::String& line) { say (line); });
    presetMenu.setSteppers (true);
    presetMenu.setField (0.f, std::max (presetMenu.widestChoice(), 170.f));
    presetMenu.onHover = [this] (TypeSelector* s, bool on) { say (on ? s->getHelp() : juce::String()); };
    gridMenu.setSteppers (true);
    gridMenu.setIndex (lanesView.grid());
    gridMenu.onChoose = [this] (int g) { lanesView.setGrid (g); };      // a size from the menu: a fixed grid
    gridMenu.step = [this] (int dir) { dir > 0 ? lanesView.narrower() : lanesView.wider(); };   // < >: as Cmd 2 / Cmd 1
    gridMenu.onHover = [this] (TypeSelector* s, bool on) { say (on ? s->getHelp() : juce::String()); };

    undoKey.onClick = [this] { undoOrRedo (false); };
    redoKey.onClick = [this] { undoOrRedo (true); };
    keysKey.onClick = [this] { showShortcuts (shortcuts == nullptr); };
    saveKey.onClick = [this] { save(); };
    saveAsKey.onClick = [this] { saveAs(); };
    revertKey.onClick = [this] { revert(); };
    drawKey.onClick = [this] { lanesView.setDrawMode (! lanesView.drawMode()); };
    adaptiveKey.onClick = [this] { lanesView.setAdaptive (! lanesView.adaptive()); };
    snapKey.onClick = [this] { lanesView.setSnap (! lanesView.snaps()); };
    tripletKey.onClick = [this] { lanesView.setTriplets (! lanesView.triplets()); };
    addKey.onClick = [this] { showLaneMenu(); };
    for (auto* k : { &undoKey, &redoKey, &keysKey, &saveKey, &saveAsKey, &revertKey, &drawKey, &adaptiveKey, &snapKey, &tripletKey, &addKey })
    {
        k->onHover = [this] (TextKey* key, bool on) { say (on ? key->getHelp() : juce::String()); };
        addAndMakeVisible (*k);
    }
    lanesView.onModeChange = [this] {
        gridMenu.setIndex (lanesView.grid());
        updateKeys();
    };
    lanesView.onScroll = [this] { layoutHeaders(); };

    for (auto* t : { &description, &nameEntry })
    {
        t->setFont (mono (10.5f));
        t->setIndents (6, 4);
        t->setColour (juce::TextEditor::backgroundColourId, col::sheet);
        t->setColour (juce::TextEditor::outlineColourId, col::hairline);
    }
    description.setTextToShowWhenEmpty ("What this preset does, in one line (the main window's footer shows it)", col::inkFaint);
    description.onTextChange = [this] {
        if (! before) beginEdit();
        if (! ensureSlot())
        {
            description.setText (meta.help, false);
            before.reset();
            return;
        }
        meta.help = description.getText();
        publish();
    };
    description.onReturnKey = description.onFocusLost = [this] {
        endEdit ("Edit help line");
        grabKeyboardFocus();
    };
    description.onEscapeKey = [this] { grabKeyboardFocus(); };
    nameEntry.onReturnKey = [this] { finishName (true); };
    nameEntry.onEscapeKey = [this] { finishName (false); };
    nameEntry.onFocusLost = [this] { finishName (false); };
    nameEntry.setVisible (false);

    headerColumn.setInterceptsMouseClicks (false, true);
    for (juce::Component* c : std::initializer_list<juce::Component*> { &presetMenu, &gridMenu, &description, &lanesView, &headerColumn })
        addAndMakeVisible (c);
    addChildComponent (nameEntry);

    help = defaultHelp();
    load();
    setSize (kWidth, kHeight);
}

PresetEditorView::~PresetEditorView()
{
    shortcuts.reset();
    headers.clear();
    setLookAndFeel (nullptr);
}

void PresetEditorView::visibilityChanged()
{
    if (isShowing()) grabKeyboardFocus();
}

// ---- LaneModel --------------------------------------------------------------------------

std::vector<L> PresetEditorView::lanes() const
{
    std::vector<L> out;
    for (const auto id : ba::kLaneIds)
        if (ba::hasLane (working, id)) out.push_back (id);
    return out;
}

void PresetEditorView::beginEdit() { before = snapshot(); }

void PresetEditorView::change (L id, const ba::Lane& l)
{
    if (! ensureSlot()) return;
    ba::lane (working, id) = l;
    publish();
    lanesView.contentChanged();
    for (auto& h : headers) h->repaint();   // their values
}

void PresetEditorView::endEdit (const juce::String& what)
{
    if (! before) return;
    auto was = std::move (*before);
    before.reset();
    if (what.isEmpty()) return;
    auto now = snapshot();
    const bool same = was.preset == now.preset && was.slot == now.slot && was.content.used == now.content.used
                      && UserPresets::hashOf (was.content) == UserPresets::hashOf (now.content);
    if (same) return;
    undo.beginNewTransaction (what);
    undo.perform (new Step (*this, std::move (was), std::move (now)));
    updateKeys();
}

void PresetEditorView::cancelEdit()
{
    if (! before) return;
    auto was = std::move (*before);
    before.reset();
    restore (was);
}

float PresetEditorView::build() const { return proc.buildAmount(); }

void PresetEditorView::scrub (float b, int phase)
{
    auto& prm = proc.param (ids::build);
    if (phase < 0) prm.beginChangeGesture();
    prm.setValueNotifyingHost (juce::jlimit (0.f, 1.f, b));
    if (phase > 0) prm.endChangeGesture();
}

ba::Settings PresetEditorView::run (float b, bool designed) const
{
    return ba::applyPreset (working, b, proc.ownSettings(), designed ? ba::Intensity {} : proc.intensity());
}

juce::String PresetEditorView::words (L id, float v) const
{
    if (id == L::level) return fmt::signedNumber (v, 1) + " dB";
    auto& prm = proc.param (paramOf (id));
    const float plain = id == L::lowReso || id == L::highReso ? v * 100.f : v;
    return prm.getText (prm.convertTo0to1 (plain), 0);
}

std::optional<float> PresetEditorView::parse (L id, const juce::String& text) const
{
    if (text.trim().isEmpty()) return std::nullopt;
    if (id == L::level) return (float) fmt::parse (text);
    auto& prm = proc.param (paramOf (id));
    const float plain = prm.convertFrom0to1 (prm.getValueForText (text));
    return id == L::lowReso || id == L::highReso ? plain / 100.f : plain;
}

void PresetEditorView::say (const juce::String& line)
{
    helpIsDefault = line.isEmpty();
    const auto next = helpIsDefault ? defaultHelp() : line;
    if (next == help) return;
    help = next;
    repaint (footerArea());
}

// ---- The preset --------------------------------------------------------------------------

void PresetEditorView::load()
{
    const int i = proc.presetIndex();
    const bool same = i == shownPreset;
    shownPreset = i;
    shownGen = proc.user.generation();
    slot = P::slotOf (i);
    if (slot >= 0 && proc.user[slot].used)
    {
        meta = proc.user[slot];
        working = meta.path;
    }
    else
    {
        slot = -1;
        working = ba::Preset {};
        meta = {};
        if (i >= 1 && i <= P::kFactory)
        {
            working = ba::kPresets[(size_t) i - 1];
            meta.name = working.name;
            meta.help = working.help;
        }
    }
    if (! description.hasKeyboardFocus (true))
    {
        description.setText (meta.help, false);
        description.moveCaretToTop (false);
    }
    rebuildHeaders();
    if (same) lanesView.contentChanged();
    else lanesView.presetChanged();
    if (helpIsDefault) help = defaultHelp();
    updateKeys();
    repaint();
}

bool PresetEditorView::ensureSlot()
{
    if (slot >= 0) return true;
    const int i = proc.presetIndex();
    const int s = proc.user.freeSlot();
    if (s < 0)
    {
        say ("All " + juce::String (UserPresets::kSlots) + " user slots hold unsaved presets: save or revert one to edit another.");
        return false;
    }
    if (before && before->slot < 0)
    {
        before->slot = s;   // undo gives that slot back as it was
        before->content = proc.user[s];
    }
    UserPresets::Slot copy;
    copy.used = true;
    if (i >= 1 && i <= P::kFactory)
    {
        const auto& f = ba::kPresets[(size_t) i - 1];
        copy.name = presetmenu::freeName (proc, juce::String (f.name) + " copy");
        copy.help = f.help;
        copy.copiedFrom = i;
        copy.path = f;
    }
    else copy.name = presetmenu::freeName (proc, "New preset");
    proc.user.set (s, copy);
    proc.selectPreset (P::presetOf (s), false);   // the same preset (a copy): the intensities stay
    slot = s;
    shownPreset = P::presetOf (s);
    meta = proc.user[s];
    shownGen = proc.user.generation();
    return true;
}

void PresetEditorView::publish()
{
    if (slot < 0) return;
    auto s = meta;
    s.path = working;
    proc.user.set (slot, s);
    meta = proc.user[slot];
    shownGen = proc.user.generation();
    repaint (0, 0, getWidth(), (int) kRuleY);   // the status beside the menu
    updateKeys();
}

PresetEditorView::Snapshot PresetEditorView::snapshot() const
{
    Snapshot s;
    s.preset = proc.presetIndex();
    s.slot = slot;
    if (slot >= 0) s.content = proc.user[slot];
    return s;
}

void PresetEditorView::restore (const Snapshot& s)
{
    if (s.slot >= 0)
    {
        auto c = s.content;
        const auto& now = proc.user[s.slot];
        if (c.used && now.used && now.id == c.id)
        {
            // The same preset: what saving did since stays (its name, its file).
            c.name = now.name;
            c.file = now.file;
            c.savedHash = now.savedHash;
        }
        if (c.used) proc.user.set (s.slot, c);
        else proc.user.release (s.slot);
    }
    if (proc.presetIndex() != s.preset) proc.selectPreset (s.preset, false);
    load();
}

void PresetEditorView::edit (const juce::String& what, const std::function<void()>& change)
{
    lanesView.commitKeys();
    beginEdit();
    if (! ensureSlot())
    {
        before.reset();
        return;
    }
    change();
    publish();
    endEdit (what);
    rebuildHeaders();
    lanesView.contentChanged();
}

void PresetEditorView::addLane (L id)
{
    if (ba::hasLane (working, id)) return;
    const auto own = proc.ownSettings();
    edit ("Add lane", [&] {
        auto& p = working;
        switch (id)
        {
            case L::lowCut:   p.lowCutType = own.lowCutType; p.lowCut = edit::flat (0.f); break;   // off: BUILD 0 stays the drop
            case L::highCut:  p.highCutType = own.highCutType; p.highCut = edit::flat (1.f); break;
            case L::noisePitch:  p.noisePitch = edit::flat (own.noisePitch); break;
            case L::noiseLowCut: p.noiseLowCut = edit::flat (own.noiseLowCut); break;
            case L::lowReso:
            case L::highReso: break;   // through AUTO, below
            default: ba::lane (p, id) = edit::flat (ba::laneRange (id).clamp (0.f)); break;
        }
    });
    if (id == L::lowReso || id == L::highReso) setAutoReso (id == L::highReso, false);
}

void PresetEditorView::removeLane (L id)
{
    edit ("Remove lane", [&] {
        auto& p = working;
        if (id == L::lowCut) { p.lowCutType = -1; p.lowCut = p.lowReso = {}; }
        else if (id == L::highCut) { p.highCutType = -1; p.highCut = p.highReso = {}; }
        else ba::lane (p, id) = {};
    });
}

void PresetEditorView::setCutType (bool lowpass, int type)
{
    edit ("Cut type", [&] { (lowpass ? working.highCutType : working.lowCutType) = type; });
}

void PresetEditorView::setAutoReso (bool lowpass, bool on)
{
    edit (on ? "Auto resonance" : "Resonance lane", [&] {
        auto& reso = lowpass ? working.highReso : working.lowReso;
        if (on)
        {
            reso = {};
            return;
        }
        // A lane of its own, starting where auto resonance had it along the cut.
        const auto& cut = lowpass ? working.highCut : working.lowCut;
        const int type = std::max (0, lowpass ? working.highCutType : working.lowCutType);
        std::vector<ba::Key> keys;
        for (int i = 0; i <= 64; ++i)
        {
            const float at = (float) i / 64.f;
            const ba::CutKnobs k { cut (at), type, 0.3f, true };
            keys.push_back ({ at, (float) ba::CutFilters::resonance (lowpass, k), 1.f, 0.f });
        }
        edit::assign (reso, keys, {});
        edit::simplify (reso, 0.f, 1.f, 0.004f, {});
    });
}

void PresetEditorView::setSteps (bool on)
{
    edit (on ? "Pitch steps" : "Pitch glides", [&] { working.pitch.step = on ? 1.f : 0.f; });
}

void PresetEditorView::showLaneMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader ("Add a lane");
    for (const auto id : ba::kLaneIds)
    {
        const bool reso = id == L::lowReso || id == L::highReso;
        const bool cutOn = id == L::lowReso ? working.lowCutType >= 0 : id == L::highReso ? working.highCutType >= 0 : true;
        if (! ba::hasLane (working, id)) m.addItem (1 + (int) id, laneName (id), ! reso || cutOn, false);
    }
    m.addSeparator();
    m.addItem (-1, "Level and dry/wet stay yours: a preset never takes them", false, false);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addKey).withStandardItemHeight (22),
                     [safe = juce::Component::SafePointer<PresetEditorView> (this)] (int picked) {
                         if (safe != nullptr && picked > 0) safe->addLane ((L) (picked - 1));
                     });
}

void PresetEditorView::showShortcuts (bool on)
{
    if (on == (shortcuts != nullptr)) return;
    if (on)
    {
        shortcuts = std::make_unique<Shortcuts> (*this);
        addAndMakeVisible (*shortcuts);
        shortcuts->setBounds (lanesArea().withTrimmedLeft (0));
    }
    else shortcuts.reset();
    keysKey.setOn (on);
    grabKeyboardFocus();
}

// ---- Saving ------------------------------------------------------------------------------

void PresetEditorView::save()
{
    lanesView.commitKeys();
    if (slot < 0)
    {
        say ("A factory preset is kept as it is: change it, then SAVE AS keeps your version.");
        return;
    }
    if (meta.file == juce::File())
    {
        saveAs();
        return;
    }
    auto s = proc.user[slot];
    s.savedHash = UserPresets::hashOf (s);
    if (! UserPresets::write (s.file, s))
    {
        say ("Couldn't write " + s.file.getFullPathName());
        return;
    }
    proc.user.set (slot, s);
    meta = proc.user[slot];
    shownGen = proc.user.generation();
    say ("Saved: " + s.file.getFullPathName());
    updateKeys();
    repaint();
}

void PresetEditorView::saveAs()
{
    lanesView.commitKeys();
    nameEntry.setBounds (presetMenu.getBounds().withX (presetMenu.getX() + (int) presetMenu.choiceX() - 6).withWidth ((int) presetMenu.widestChoice() + 40).expanded (0, 3));
    nameEntry.setText (meta.name.isNotEmpty() ? meta.name : "My preset", false);
    nameEntry.setVisible (true);
    nameEntry.selectAll();
    nameEntry.grabKeyboardFocus();
    confirmReplace = false;
    say ("Name it, then Enter keeps it in your library (Esc: cancel).");
}

void PresetEditorView::finishName (bool keep)
{
    if (! nameEntry.isVisible()) return;
    const auto name = nameEntry.getText().trim();
    if (! keep || name.isEmpty())
    {
        nameEntry.setVisible (false);
        say ({});
        grabKeyboardFocus();
        return;
    }
    if (slot < 0)
    {
        beginEdit();
        const bool ok = ensureSlot();
        endEdit (ok ? "Copy preset" : juce::String());
        if (! ok) return;
    }
    const auto file = UserPresets::fileFor (name);
    if (file.existsAsFile() && file != meta.file && ! confirmReplace)
    {
        confirmReplace = true;
        say ("Your library has a preset called " + name + " already: Enter again replaces it.");
        return;
    }
    auto s = proc.user[slot];
    s.name = name;
    s.file = file;
    s.savedHash = UserPresets::hashOf (s);
    if (! UserPresets::write (file, s))
    {
        say ("Couldn't write " + file.getFullPathName());
        return;
    }
    if (const int other = proc.user.holding (file); other >= 0 && other != slot) proc.user.release (other);   // replaced: one slot per file
    proc.user.set (slot, s);
    nameEntry.setVisible (false);
    load();
    say ("Saved as " + name + ": " + file.getFullPathName());
    grabKeyboardFocus();
}

void PresetEditorView::revert()
{
    if (slot < 0 || ! meta.unsaved) return;
    edit ("Revert", [&] {
        UserPresets::Slot saved;
        if (meta.file.existsAsFile() && UserPresets::read (meta.file, saved))
        {
            working = saved.path;
            meta.help = saved.help;
        }
        else if (meta.copiedFrom > 0)
        {
            working = ba::kPresets[(size_t) meta.copiedFrom - 1];
            meta.help = working.help;
        }
        else working = ba::Preset {};
    });
    description.setText (meta.help, false);
}

void PresetEditorView::undoOrRedo (bool redo)
{
    lanesView.commitKeys();
    if (redo) undo.redo();
    else undo.undo();
    updateKeys();
}

// ---- Headers, keys, the tick ----------------------------------------------------------------

void PresetEditorView::rebuildHeaders()
{
    const auto now = lanes();
    bool same = now.size() == headers.size();
    for (size_t i = 0; same && i < now.size(); ++i) same = headers[i]->id == now[i];
    if (same)
    {
        for (auto& h : headers) h->refresh();
        return;
    }
    // Not from inside a header's own callback: the click that removed a lane
    // is still running in it.
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<PresetEditorView> (this)] {
        if (safe == nullptr) return;
        auto& v = *safe;
        const auto rows = v.lanes();
        bool still = rows.size() == v.headers.size();
        for (size_t i = 0; still && i < rows.size(); ++i) still = v.headers[i]->id == rows[i];
        if (still) return;
        v.headers.clear();
        for (const auto id : rows)
        {
            v.headers.push_back (std::make_unique<Header> (v, id));
            v.headerColumn.addAndMakeVisible (*v.headers.back());
        }
        v.layoutHeaders();
    });
}

void PresetEditorView::layoutHeaders()
{
    const float top = (float) lanesView.getY() + LaneEditor::kTop;
    for (size_t i = 0; i < headers.size(); ++i)
        headers[i]->setBounds (juce::Rectangle<float> (0.f, (float) lanesView.getY() + lanesView.rowTop ((int) i) - top, (float) kHeaderW, lanesView.rowHeight())
                                   .toNearestInt());
}

void PresetEditorView::updateKeys()
{
    undoKey.setUsable (undo.canUndo());
    redoKey.setUsable (undo.canRedo());
    saveKey.setUsable (slot >= 0 && (meta.unsaved || meta.file == juce::File()));
    saveAsKey.setUsable (slot >= 0 || shownPreset > 0);
    revertKey.setUsable (slot >= 0 && meta.unsaved);
    drawKey.setOn (lanesView.drawMode());
    adaptiveKey.setOn (lanesView.adaptive());
    snapKey.setOn (lanesView.snaps());
    tripletKey.setOn (lanesView.triplets());
    addKey.setUsable ((int) lanes().size() < ba::kLaneCount);
    addKey.setOn (lanes().empty());   // where to start
    if (const auto s = startsAt(); s != state)
    {
        state = s;
        repaint (footerArea());
    }
}

juce::String PresetEditorView::status() const
{
    if (slot >= 0) return meta.file == juce::File() ? "Not saved" : meta.unsaved ? "Unsaved" : "Saved";
    const int i = proc.presetIndex();
    return i >= 1 && i <= P::kFactory ? "Factory" : juce::String();
}

juce::String PresetEditorView::startsAt() const
{
    if (lanes().empty()) return {};
    return ba::restsAtZero (working) ? "BUILD 0 is the drop" : "Starts at a setting";
}

juce::String PresetEditorView::defaultHelp() const
{
    const int i = proc.presetIndex();
    if (slot >= 0)
    {
        if (meta.file == juce::File()) return meta.name + ": not in your library yet" + theme::dot + "SAVE AS keeps it there.";
        if (meta.unsaved) return meta.name + ": changed since you saved it" + theme::dot + "SAVE writes it to " + meta.file.getFileName() + ", REVERT goes back.";
        return meta.name + ": in your library (" + meta.file.getFileName() + ").";
    }
    if (i >= 1 && i <= P::kFactory)
        return juce::String (ba::kPresets[(size_t) i - 1].name) + " is a factory preset: your first change makes a copy of it to edit.";
    return "No preset is on: + ADD LANE starts a new one, or pick one in the menu to edit it.";
}

void PresetEditorView::tick()
{
    if (proc.presetIndex() != shownPreset || proc.user.generation() != shownGen) load();
    lanesView.refresh();
    if (const float b = proc.buildAmount(); b != shownBuild)
    {
        shownBuild = b;
        for (auto& h : headers) h->repaint();   // their values at BUILD
    }
    // The knobs under it and the intensities change what plays: redraw then.
    const auto k = proc.intensity();
    const auto own = proc.ownSettings();
    const std::array<float, 16> now { k.lowCut, k.lowReso, k.highCut, k.highReso, k.pitch, k.noise, k.noisePitch, k.noiseLowCut, k.reverb, k.stereo,
                                      k.level, own.reverb, own.noisePitch, own.noiseLowCut, (float) own.lowCutType, (float) own.highCutType };
    if (now != shownRun)
    {
        shownRun = now;
        lanesView.contentChanged();
        for (auto& h : headers)
        {
            h->refresh();
            h->repaint();
        }
    }
    if (helpIsDefault && help != defaultHelp())
    {
        help = defaultHelp();
        repaint (footerArea());
    }
}

bool PresetEditorView::keyPressed (const juce::KeyPress& k)
{
    const auto mods = k.getModifiers();
    const auto c = upper (k.getKeyCode());
    if (shortcuts != nullptr && k.isKeyCode (juce::KeyPress::escapeKey))
    {
        showShortcuts (false);
        return true;
    }
    if (mods.isCommandDown() && c == 'Z')
    {
        undoOrRedo (mods.isShiftDown());
        return true;
    }
    if (mods.isCommandDown() && c == 'Y')
    {
        undoOrRedo (true);
        return true;
    }
    if (mods.isCommandDown() && c == 'S')
    {
        if (mods.isShiftDown()) saveAs();
        else save();
        return true;
    }
    return lanesView.keyPressed (k);
}

bool PresetEditorView::keyStateChanged (bool down) { return lanesView.keyStateChanged (down); }

// ---- Layout and painting --------------------------------------------------------------------

juce::Rectangle<int> PresetEditorView::lanesArea() const
{
    return { kLeft, (int) kLanesY, getWidth() - 2 * kLeft, getHeight() - kFooter - (int) kLanesY };
}

void PresetEditorView::resized()
{
    const int w = getWidth();
    const int content = kLeft + kHeaderW + (int) LaneEditor::kGap;   // where the rows' paper starts: everything the left column labels lines up there

    // Masthead: the shortcuts, then undo / redo, right-aligned.
    int x = w - kLeft;
    for (auto* k : { &redoKey, &undoKey })
    {
        x -= k->idealWidth();
        k->setBounds (x, 20, k->idealWidth(), kRowH);
        x -= kKeyGap;
    }
    x -= kGroupGap - kKeyGap;
    keysKey.setBounds (x - keysKey.idealWidth(), 20, keysKey.idealWidth(), kRowH);

    // [A] Preset: the menu (its words where the rows start), the status, the help line, the file keys.
    const int ya = (int) kRowA;
    x = w - kLeft;
    for (auto* k : { &saveKey, &saveAsKey, &revertKey })
    {
        x -= k->idealWidth();
        k->setBounds (x, ya, k->idealWidth(), kRowH);
        x -= kKeyGap;
    }
    presetMenu.setBounds (content - (int) presetMenu.choiceX(), ya + 2, presetMenu.idealWidth(), 18);
    description.setBounds (content, (int) kRowA2, w - kLeft - content, kRowH);

    // [B] Lanes: the tools over the rows, in groups.
    const int yb = (int) kRowB;
    x = content;
    drawKey.setBounds (x, yb, drawKey.idealWidth(), kRowH);
    x = drawKey.getRight() + kGroupGap;
    gridMenu.setBounds (x, yb + 2, gridMenu.idealWidth(), 18);
    x = gridMenu.getRight() + 10;
    adaptiveKey.setBounds (x, yb, adaptiveKey.idealWidth(), kRowH);
    x = adaptiveKey.getRight() + kKeyGap;
    tripletKey.setBounds (x, yb, tripletKey.idealWidth(), kRowH);
    x = tripletKey.getRight() + kKeyGap;
    snapKey.setBounds (x, yb, snapKey.idealWidth(), kRowH);

    // The lanes: + ADD LANE over the headers, the headers beside the rows.
    const auto lanes = lanesArea();
    addKey.setBounds (kLeft, lanes.getY() + (int) LaneEditor::kOverview, addKey.idealWidth(), 18);   // beside the ruler
    lanesView.setBounds (lanes.withTrimmedLeft (kHeaderW));
    headerColumn.setBounds (kLeft, lanes.getY() + (int) LaneEditor::kTop, kHeaderW, lanes.getHeight() - (int) LaneEditor::kTop);
    layoutHeaders();
    if (shortcuts != nullptr) shortcuts->setBounds (lanes);
}

void PresetEditorView::paint (juce::Graphics& g)
{
    g.fillAll (col::sheet);
    const auto b = getLocalBounds().toFloat();
    const float w = b.getWidth();
    cropMarks (g, b.reduced (8.f), 7.f);

    // Wordmark and what this window is.
    g.setFont (title (22.f));
    float x = (float) kLeft;
    for (auto [s, c] : { std::pair { juce::String ("BUILD"), col::ink }, { juce::String ("/"), col::accent }, { juce::String ("APP"), col::ink } })
    {
        g.setColour (c);
        const float tw = textWidth (g.getCurrentFont(), s);
        g.drawText (s, juce::Rectangle<float> (x, 14.f, tw + 2.f, 26.f), juce::Justification::centredLeft, false);
        x += tw + (s == "/" ? 1.f : 0.f);
    }
    g.setFont (mono (9.5f));
    g.setColour (col::inkSoft);
    g.drawText (u8 ("PRESET EDITOR \xc2\xb7 EVERY LANE ALONG BUILD \xc2\xb7 EDITED LIKE AUTOMATION IN ABLETON LIVE"),
                juce::Rectangle<float> ((float) kLeft, 40.f, 560.f, 14.f), juce::Justification::centredLeft, false);
    g.setColour (col::inkFaint);
    g.fillRect ((float) kLeft, kMastRuleY, w - 2.f * kLeft, 1.5f);

    // [A] Preset, and its status beside the menu.
    const float rowMid = (float) kRowH * 0.5f;
    sectionTag (g, "A", "Preset", { (float) kLeft, kRowA + rowMid - 6.5f });
    if (const auto s = status(); s.isNotEmpty())
    {
        const bool open = s == "Unsaved" || s == "Not saved";
        float sx = (float) presetMenu.getRight() + 14.f;
        if (open)
        {
            g.setColour (col::ink);
            g.fillEllipse (juce::Rectangle<float> (5.f, 5.f).withCentre ({ sx + 2.5f, kRowA + rowMid }));
            sx += 11.f;
        }
        caption (g, s, { sx, kRowA, 90.f, (float) kRowH }, juce::Justification::centredLeft, open ? col::ink : col::inkFaint, 9.5f);
    }
    caption (g, "Description", { (float) kLeft + 19.f, kRowA2, (float) kHeaderW - 19.f, (float) kRowH }, juce::Justification::centredLeft, col::inkSoft, 9.5f);
    g.setColour (col::hairline);
    g.fillRect ((float) kLeft, kRuleY, w - 2.f * kLeft, 1.f);

    // [B] Lanes, a hairline between its groups of tools, the legend right.
    sectionTag (g, "B", "Lanes", { (float) kLeft, kRowB + rowMid - 6.5f });
    g.setColour (col::hairline);
    for (const float gx : { (float) drawKey.getRight() + (float) kGroupGap * 0.5f })
        g.fillRect (gx, kRowB + 3.f, 1.f, (float) kRowH - 6.f);
    {
        struct Item
        {
            const char* name;
            std::function<void (juce::Rectangle<float>)> swatch;
        };
        const Item items[] = {
            { "Path", [&] (auto r) { g.setColour (col::ink); g.fillRect (r.withHeight (1.4f).translated (0.f, -0.5f)); } },
            { "Built so far", [&] (auto r) { g.setColour (col::accent); g.fillRect (r.withHeight (1.8f).translated (0.f, -0.5f)); } },
            { "At intensity", [&] (auto r) {
                  g.setColour (col::inkSoft);
                  for (float dx = 0.f; dx < 16.f; dx += 6.f) g.fillRect (r.getX() + dx, r.getY() - 0.5f, 3.f, 1.2f);
              } },
            { "Tail", [&] (auto r) {
                  juce::Graphics::ScopedSaveState s (g);
                  const auto box = r.withTop (r.getY() - 4.f).withHeight (9.f);
                  g.reduceClipRegion (box.toNearestInt());
                  hatch (g, box, col::inkFaint, 4.f, 1.f);
              } },
        };
        // As many as fit after the tools, from the left: the tail is named on the ruler anyway.
        const float room = w - (float) kLeft - ((float) snapKey.getRight() + (float) kGroupGap);
        const auto widthOf = [] (const char* n) { return textWidth (label (9.f), juce::String (n).toUpperCase()) + 22.f; };
        size_t n = std::size (items);
        float need = 0.f;
        for (size_t i = 0; i < n; ++i) need += widthOf (items[i].name) + (i > 0 ? 18.f : 0.f);
        while (n > 0 && need > room)
        {
            --n;
            need -= widthOf (items[n].name) + (n > 0 ? 18.f : 0.f);
        }
        float lx = w - (float) kLeft - need;
        const float ly = kRowB + rowMid;
        for (size_t i = 0; i < n; ++i)
        {
            items[i].swatch ({ lx, ly, 16.f, 1.f });
            const float tw = widthOf (items[i].name) - 22.f;
            caption (g, items[i].name, { lx + 22.f, ly - 7.f, tw + 2.f, 14.f }, juce::Justification::centredLeft, col::inkSoft, 9.f);
            lx += tw + 22.f + 18.f;
        }
    }

    // Footer: the help line, and what BUILD 0 does with this preset.
    g.setColour (col::hairline);
    g.fillRect ((float) kLeft, b.getHeight() - 34.f, w - 2.f * kLeft, 1.f);
    const auto foot = footerArea().toFloat();
    g.setFont (mono (10.5f));
    g.setColour (col::ink);
    g.drawText (help, foot.withTrimmedRight (state.isEmpty() ? 0.f : 170.f), juce::Justification::centredLeft, true);
    if (state.isNotEmpty())
        caption (g, state, foot, juce::Justification::centredRight, state == "BUILD 0 is the drop" ? col::inkSoft : col::inkFaint, 9.5f);
}

// ---- The window ---------------------------------------------------------------------------------

PresetEditorWindow::PresetEditorWindow (BuildAppProcessor& p, std::function<void()> closed)
    : juce::DocumentWindow ("BuildApp Preset Editor", col::sheet, juce::DocumentWindow::closeButton), proc (p), onClose (std::move (closed))
{
    setUsingNativeTitleBar (true);
    setContentOwned (new PresetEditorView (p), true);
    setResizable (true, false);
    setResizeLimits (PresetEditorView::kMinWidth, PresetEditorView::kMinHeight, 10000, 10000);
    auto at = proc.presetEditorBounds;
    if (at.isEmpty()) centreWithSize (getWidth(), getHeight());
    else
    {
        if (const auto* d = juce::Desktop::getInstance().getDisplays().getDisplayForRect (at)) at = at.constrainedWithin (d->userBounds.toNearestInt());
        setBounds (at);
    }
    setVisible (true);
    toFront (true);
}

PresetEditorWindow::~PresetEditorWindow() { proc.presetEditorBounds = getBounds(); }

void PresetEditorWindow::closeButtonPressed()
{
    if (onClose) onClose();
}

void PresetEditorWindow::tick()
{
    view().tick();
    // Over the host's windows while the host is in front, like its plugin
    // windows; a normal window when another app is.
    const bool front = juce::Process::isForegroundProcess();
    if (front != floating)
    {
        floating = front;
        setAlwaysOnTop (front);
    }
}
