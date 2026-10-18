#pragma once

#include "Controls.h"
#include "LaneEditor.h"
#include "plugin/UserPresets.h"

class BuildAppProcessor;

// The preset editor: every lane of the preset that's on, to draw and bend
// with Live's gestures (LaneEditor), a header per lane (its cut type, auto
// resonance, pitch stairs, remove), + LANE for the controls it doesn't take
// yet, and the preset itself: the PRESET menu, its help line, UNDO / REDO,
// SAVE, SAVE AS and REVERT. It shows whatever PRESET is on and follows it.
//
// Factory presets stay as they are: the first change to one makes a copy in
// a user slot ("Full-On Rise copy"), turns PRESET to it and edits that; with
// PRESET off the first lane makes a new preset. Every change plays at once;
// one gesture is one undo step (an undo takes PRESET back too). SAVE writes
// the preset's file in the library, SAVE AS asks for a name in place.
//
// A tool window: it reflows with its size (the lanes get the room) instead
// of scaling a fixed sheet.
class PresetEditorView : public juce::Component, private LaneModel
{
public:
    explicit PresetEditorView (BuildAppProcessor&);
    ~PresetEditorView() override;

    void tick();   // the main editor's timer
    LaneEditor& laneEditor() { return lanesView; }

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;
    void visibilityChanged() override;

    static constexpr int kWidth = 1120, kHeight = 700, kMinWidth = 860, kMinHeight = 560;

private:
    struct Header;
    struct Snapshot
    {
        int preset = 0, slot = -1;
        UserPresets::Slot content;   // the slot's, when slot >= 0
    };
    struct Step;

    // ---- LaneModel --------------------------------------------------------------
    const ba::Preset& preset() const override { return working; }
    std::vector<ba::LaneId> lanes() const override;
    void beginEdit() override;
    void change (ba::LaneId, const ba::Lane&) override;
    void endEdit (const juce::String& what) override;
    float build() const override;
    void scrub (float build, int phase) override;
    ba::Settings run (float build, bool designed) const override;
    juce::String words (ba::LaneId, float value) const override;
    std::optional<float> parse (ba::LaneId, const juce::String&) const override;
    void say (const juce::String& line) override;

    // ---- The preset ---------------------------------------------------------------
    void load();                       // what PRESET has on now
    bool ensureSlot();                 // the first change: a factory preset (or nothing) becomes a user preset
    void publish();                    // the slot plays `working`
    Snapshot snapshot() const;
    void restore (const Snapshot&);    // undo / redo
    void edit (const juce::String& what, const std::function<void()>& change);   // one undo step around a change of the header's

    void addLane (ba::LaneId);
    void removeLane (ba::LaneId);
    void setCutType (bool lowpass, int type);
    void setAutoReso (bool lowpass, bool on);
    void setSteps (bool on);

    void save();
    void saveAs();
    void finishName (bool keep);
    void revert();

    void rebuildHeaders();
    void layoutHeaders();
    void updateKeys();
    void showLaneMenu();
    juce::String defaultHelp() const;
    juce::Rectangle<int> footerArea() const { return { 24, getHeight() - 30, getWidth() - 48, 22 }; }

    BuildAppProcessor& proc;
    NightLookAndFeel lnf;
    LaneEditor lanesView { *this };
    juce::UndoManager undo { 1000, 50 };

    ba::Preset working {};
    UserPresets::Slot meta;            // its words, file and state (a factory preset's words until it's copied)
    int shownPreset = -1, slot = -1;
    uint32_t shownGen = 0;
    std::optional<Snapshot> before;    // the gesture's start
    std::array<float, 16> shownRun {};

    TypeSelector presetMenu, gridMenu;
    TextKey undoKey, redoKey, saveKey, saveAsKey, revertKey, drawKey, snapKey, tripletKey, addKey;
    juce::TextEditor description, nameEntry;
    bool confirmReplace = false;
    std::vector<std::unique_ptr<Header>> headers;
    juce::String help, state;
    bool helpIsDefault = true;
};

// The window around it: a native title bar, its own size and place
// (remembered on the processor), floating over the host while the host is in
// front, as its plugin windows do, and never over other apps.
class PresetEditorWindow : public juce::DocumentWindow
{
public:
    PresetEditorWindow (BuildAppProcessor&, std::function<void()> onClose);
    ~PresetEditorWindow() override;

    void tick();
    void closeButtonPressed() override;
    PresetEditorView& view() { return *dynamic_cast<PresetEditorView*> (getContentComponent()); }

private:
    BuildAppProcessor& proc;
    std::function<void()> onClose;
    bool floating = false;
};
