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
    // The window's layout, top to bottom (logical pixels, unscaled).
    constexpr int kLeft = 24, kHeaderW = 196, kHeaderGap = 6;
    constexpr float kMastRuleY = 62.f, kPresetY = 74.f, kRuleY = 102.f, kTagY = 114.f, kLanesY = 139.f, kFooter = 44.f;

    const char* laneName (L id)
    {
        constexpr const char* names[] = { "Low cut", "Low reso", "High cut", "High reso", "Pitch", "Noise", "Noise pitch", "Noise cut", "Reverb", "Stereo", "Level" };
        return names[(int) id];
    }

    juce::String laneHelp (L id)
    {
        switch (id)
        {
            case L::lowCut:      return "The low cut along the build, 20 Hz to 8 kHz (all the way down: off), in the type beside it.";
            case L::lowReso:     return "The low cut's resonance along the build; switch it off with AUTO and auto resonance takes over again.";
            case L::highCut:     return "The high cut along the build, 20 kHz down to 100 Hz (all the way up: off), in the type beside it.";
            case L::highReso:    return "The high cut's resonance along the build; switch it off with AUTO and auto resonance takes over again.";
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
        constexpr const char* what[] = { "four bars", "two bars", "a bar", "half a bar", "a beat" };
        std::vector<TypeSelector::Choice> out;
        for (int g = 0; g < LaneEditor::kGridCount; ++g)
            out.push_back ({ LaneEditor::gridName (g),
                             "The grid: " + LaneEditor::gridName (g) + " of the path, " + what[g] + " of a 16-bar build (Cmd 1 narrower, Cmd 2 wider).", {} });
        return out;
    }

    juce::juce_wchar upper (int code) { return juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) code); }
} // namespace

// ---- A lane's header ----------------------------------------------------------------

// Next to its row: the lane's name, its cut type and AUTO (cuts), STEPS
// (pitch), the x that removes it, and its INTENSITY when that's turned down.
struct PresetEditorView::Header : juce::Component
{
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
        remove.setHelp (juce::String ("Takes this lane out of the preset: ") + (cut ? "the cut goes back to your knob." : "the control goes back to your knob."));
        remove.onClick = [this] { view.removeLane (id); };
        addAndMakeVisible (remove);
        for (auto* k : { &option, &remove })
            k->onHover = [this] (TextKey* key, bool on) { view.say (on ? key->getHelp() : juce::String()); };
        refresh();
    }

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
        const int w = getWidth();
        remove.setBounds (w - 16, 4, 16, 16);
        option.setBounds (w - 16 - 6 - option.idealWidth(), 4, option.idealWidth(), 16);
        if (type != nullptr) type->setBounds (0, 24, w, 17);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (col::sheet);
        caption (g, laneName (id), { 0.f, 4.f, (float) getWidth() - 80.f, 16.f }, juce::Justification::centredLeft, hot ? col::ink : col::inkSoft, 9.5f);
        if (intensity < 1.f)
        {
            const float y = type != nullptr ? 44.f : 24.f;
            if (y + 14.f <= (float) getHeight())
                caption (g, "Intensity " + fmt::fixed (intensity * 100.0, 0) + " %", { 0.f, y, (float) getWidth(), 14.f }, juce::Justification::centredLeft,
                         col::inkFaint, 9.f);
        }
        g.setColour (col::hairline);
        g.fillRect (0.f, (float) getHeight() - 1.f, (float) getWidth(), 1.f);
    }

    void mouseEnter (const juce::MouseEvent&) override
    {
        hot = true;
        repaint();
        view.say (juce::String (laneName (id)) + theme::dot + laneHelp (id));
    }
    void mouseExit (const juce::MouseEvent&) override
    {
        hot = false;
        repaint();
        view.say ({});
    }

    PresetEditorView& view;
    const L id;
    std::unique_ptr<TypeSelector> type;
    TextKey option { "" }, remove { juce::String (juce::CharPointer_UTF8 ("\xc3\x97")) };
    float intensity = 1.f;
    bool hot = false;
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
      presetMenu (p.param (ids::preset), "Preset", presetmenu::choices()),
      gridMenu ("Grid", gridChoices()),
      undoKey ("Undo", "Undoes the last change to the preset (Cmd Z)."),
      redoKey ("Redo", "Does it again (Cmd Shift Z)."),
      saveKey ("Save", "Writes the preset to its file in your library (Cmd S)."),
      saveAsKey (juce::String (juce::CharPointer_UTF8 ("Save as\xe2\x80\xa6")), "Keeps it in your library under a name of its own (Cmd Shift S); it then shows under USER in the preset menu of every project."),
      revertKey ("Revert", "Back to the preset as it was saved (or to the factory preset it was copied from)."),
      drawKey ("Draw", "Draw Mode (B, or hold B while you edit): drag to draw steps one grid cell wide; Shift finer; Cmd (Alt on Windows) freehand."),
      snapKey ("Snap", "Keys and drawing snap to the grid (Cmd 4); Cmd (Alt on Windows) bypasses it while you drag."),
      tripletKey ("Triplet", "A triplet grid: two thirds of each step (Cmd 3)."),
      addKey ("+ Lane", "Gives the preset a lane for another control.")
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
    gridMenu.onChoose = [this] (int g) { lanesView.setGrid (g); };
    gridMenu.onHover = [this] (TypeSelector* s, bool on) { say (on ? s->getHelp() : juce::String()); };

    undoKey.onClick = [this] { undo.undo(); updateKeys(); };
    redoKey.onClick = [this] { undo.redo(); updateKeys(); };
    saveKey.onClick = [this] { save(); };
    saveAsKey.onClick = [this] { saveAs(); };
    revertKey.onClick = [this] { revert(); };
    drawKey.onClick = [this] { lanesView.setDrawMode (! lanesView.drawMode()); };
    snapKey.onClick = [this] { lanesView.setSnap (! lanesView.snaps()); };
    tripletKey.onClick = [this] { lanesView.setTriplets (! lanesView.triplets()); };
    addKey.onClick = [this] { showLaneMenu(); };
    for (auto* k : { &undoKey, &redoKey, &saveKey, &saveAsKey, &revertKey, &drawKey, &snapKey, &tripletKey, &addKey })
    {
        k->onHover = [this] (TextKey* key, bool on) { say (on ? key->getHelp() : juce::String()); };
        addAndMakeVisible (*k);
    }
    lanesView.onModeChange = [this] {
        gridMenu.setIndex (lanesView.grid());
        updateKeys();
    };

    for (auto* t : { &description, &nameEntry })
    {
        t->setFont (mono (10.5f));
        t->setIndents (6, 3);
        t->setColour (juce::TextEditor::backgroundColourId, col::sheet);
        t->setColour (juce::TextEditor::outlineColourId, col::hairline);
    }
    description.setTextToShowWhenEmpty ("One line on what this preset does (it shows in the footer)", col::inkFaint);
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

    for (juce::Component* c : std::initializer_list<juce::Component*> { &presetMenu, &gridMenu, &description, &lanesView })
        addAndMakeVisible (c);
    addChildComponent (nameEntry);

    help = defaultHelp();
    load();
    setSize (kWidth, kHeight);
}

PresetEditorView::~PresetEditorView()
{
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
    repaint (0, 0, getWidth(), (int) kRuleY);   // the unsaved mark and the badge
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

// ---- Saving ------------------------------------------------------------------------------

void PresetEditorView::save()
{
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
    nameEntry.setBounds (presetMenu.getBounds().withX (presetMenu.getX() + (int) presetMenu.choiceX() - 6).withWidth ((int) presetMenu.widestChoice() + 40).expanded (0, 2));
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
            v.addAndMakeVisible (*v.headers.back());
        }
        v.layoutHeaders();
    });
}

void PresetEditorView::layoutHeaders()
{
    for (size_t i = 0; i < headers.size(); ++i)
        headers[i]->setBounds (juce::Rectangle<float> ((float) kLeft, (float) lanesView.getY() + lanesView.rowTop ((int) i), (float) kHeaderW,
                                                       lanesView.rowHeight())
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
    snapKey.setOn (lanesView.snaps());
    tripletKey.setOn (lanesView.triplets());
    addKey.setUsable ((int) lanes().size() < ba::kLaneCount);
    const auto s = "Grid " + LaneEditor::gridName (lanesView.grid()) + (lanesView.triplets() ? " T" : "") + theme::dot
                   + (lanesView.snaps() ? "Snap" : "No snap") + theme::dot + (lanesView.drawMode() ? "Draw" : "Breakpoints");
    if (s != state)
    {
        state = s;
        repaint (footerArea());
    }
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
    return "No preset is on: + LANE starts a new one, or pick one in the menu to edit it.";
}

void PresetEditorView::tick()
{
    if (proc.presetIndex() != shownPreset || proc.user.generation() != shownGen) load();
    lanesView.refresh();
    // The knobs under it and the intensities change what plays: redraw then.
    const auto k = proc.intensity();
    const auto own = proc.ownSettings();
    const std::array<float, 16> now { k.lowCut, k.lowReso, k.highCut, k.highReso, k.pitch, k.noise, k.noisePitch, k.noiseLowCut, k.reverb, k.stereo,
                                      k.level, own.reverb, own.noisePitch, own.noiseLowCut, (float) own.lowCutType, (float) own.highCutType };
    if (now != shownRun)
    {
        shownRun = now;
        lanesView.contentChanged();
        for (auto& h : headers) h->refresh();
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
    if (mods.isCommandDown() && c == 'Z')
    {
        if (mods.isShiftDown()) undo.redo();
        else undo.undo();
        updateKeys();
        return true;
    }
    if (mods.isCommandDown() && c == 'Y')
    {
        undo.redo();
        updateKeys();
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

void PresetEditorView::resized()
{
    const int w = getWidth(), h = getHeight();
    int x = w - kLeft;
    for (auto* k : { &saveKey, &saveAsKey, &revertKey })
    {
        x -= k->idealWidth();
        k->setBounds (x, 20, k->idealWidth(), 22);
        x -= 6;
    }
    x -= 14;
    for (auto* k : { &redoKey, &undoKey })
    {
        x -= k->idealWidth();
        k->setBounds (x, 20, k->idealWidth(), 22);
        x -= 6;
    }
    presetMenu.setBounds (kLeft, (int) kPresetY - 2, presetMenu.idealWidth(), 17);
    const int badge = 190;
    description.setBounds (presetMenu.getRight() + 18, (int) kPresetY - 4, w - kLeft - badge - 12 - (presetMenu.getRight() + 18), 21);

    // The tag row: [A] LANES, then the tools.
    x = kLeft + 92;
    for (juce::Component* c : std::initializer_list<juce::Component*> { &drawKey, &gridMenu, &snapKey, &tripletKey, &addKey })
    {
        const int cw = c == &gridMenu ? gridMenu.idealWidth() : dynamic_cast<TextKey*> (c)->idealWidth();
        c->setBounds (x, (int) kTagY - 2, cw, 17);
        x += cw + (c == &gridMenu || c == &tripletKey ? 18 : 8);
    }

    lanesView.setBounds (kLeft + kHeaderW + kHeaderGap, (int) kLanesY, w - kLeft - (kLeft + kHeaderW + kHeaderGap), h - (int) kFooter - (int) kLanesY);
    layoutHeaders();
}

void PresetEditorView::paint (juce::Graphics& g)
{
    g.fillAll (col::sheet);
    const auto b = getLocalBounds().toFloat();
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
    g.drawText (juce::String (juce::CharPointer_UTF8 ("PRESET EDITOR \xc2\xb7 DRAW AND BEND EVERY LANE \xc2\xb7 LIVE'S GESTURES AND KEYS")),
                juce::Rectangle<float> ((float) kLeft, 40.f, 520.f, 14.f), juce::Justification::centredLeft, false);
    g.setColour (col::inkFaint);
    g.fillRect ((float) kLeft, kMastRuleY, b.getWidth() - 2.f * kLeft, 1.5f);

    // The preset row: unsaved mark, and whether BUILD 0 leaves the signal alone.
    if (slot >= 0 && meta.unsaved)
    {
        g.setColour (col::ink);
        g.fillEllipse (juce::Rectangle<float> (5.f, 5.f).withCentre ({ (float) presetMenu.getRight() + 8.f, kPresetY + 6.5f }));
    }
    const bool any = ! lanes().empty();
    const bool rests = ba::restsAtZero (working);
    caption (g, ! any ? "Nothing driven yet" : rests ? "BUILD 0 is the drop" : "Starts at a setting",
             { b.getWidth() - (float) kLeft - 190.f, kPresetY - 2.f, 190.f, 17.f }, juce::Justification::centredRight,
             any && rests ? col::ink : col::inkSoft, 9.5f);
    g.setColour (col::hairline);
    g.fillRect ((float) kLeft, kRuleY, b.getWidth() - 2.f * kLeft, 1.f);

    sectionTag (g, "A", "Lanes", { (float) kLeft, kTagY });

    // Legend, right-aligned on the tag row: what each line in the lanes is.
    {
        float lx = b.getWidth() - (float) kLeft;
        const auto item = [&] (const juce::String& name, const std::function<void (juce::Rectangle<float>)>& swatch) {
            g.setFont (label (9.f));
            const float w = textWidth (g.getCurrentFont(), name.toUpperCase());
            lx -= w;
            caption (g, name, { lx, kTagY, w + 2.f, 13.f }, juce::Justification::centredLeft, col::inkSoft, 9.f);
            lx -= 22.f;
            swatch ({ lx, kTagY + 6.f, 16.f, 1.f });
            lx -= 18.f;
        };
        item ("Tail", [&] (auto r) {
            juce::Graphics::ScopedSaveState s (g);
            const auto box = r.withTop (r.getY() - 4.f).withHeight (9.f);
            g.reduceClipRegion (box.toNearestInt());
            hatch (g, box, col::inkFaint, 4.f, 1.f);
        });
        item ("Played", [&] (auto r) {
            g.setColour (col::inkSoft);
            for (float dx = 0.f; dx < 16.f; dx += 6.f) g.fillRect (r.getX() + dx, r.getY() - 0.5f, 3.f, 1.2f);
        });
        item ("Built", [&] (auto r) { g.setColour (col::accent); g.fillRect (r.withHeight (1.8f).translated (0.f, -0.5f)); });
        item ("Path", [&] (auto r) { g.setColour (col::ink); g.fillRect (r.withHeight (1.4f).translated (0.f, -0.5f)); });
    }

    // Footer.
    g.setColour (col::hairline);
    g.fillRect ((float) kLeft, b.getHeight() - 34.f, b.getWidth() - 2.f * kLeft, 1.f);
    const auto foot = footerArea().toFloat();
    g.setFont (mono (10.5f));
    g.setColour (col::ink);
    g.drawText (help, foot.withTrimmedRight (240.f), juce::Justification::centredLeft, true);
    g.setFont (mono (9.5f));
    g.setColour (col::inkFaint);
    g.drawText (state.toUpperCase(), foot, juce::Justification::centredRight, false);
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
