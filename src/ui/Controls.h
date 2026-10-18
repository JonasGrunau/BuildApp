#pragma once

#include "Theme.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <optional>

// Text entry, menus and corner resizer on the night sheet.
class NightLookAndFeel : public juce::LookAndFeel_V4
{
public:
    NightLookAndFeel();
    void drawCornerResizer (juce::Graphics&, int w, int h, bool isMouseOver, bool isMouseDragging) override;

    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator, bool isActive, bool isHighlighted,
                            bool isTicked, bool hasSubMenu, const juce::String& text, const juce::String& shortcutKeyText,
                            const juce::Drawable* icon, const juce::Colour* textColour) override;
    void drawPopupMenuSectionHeader (juce::Graphics&, const juce::Rectangle<int>& area, const juce::String& sectionName) override;
    juce::Font getPopupMenuFont() override;
};

// Every control paints its own sheet and is opaque, so repainting one never
// repaints the sheet behind it.

// A knob on a parameter: a raised disc in a ring of 41 ticks. How the ring
// lights says what kind of knob it is:
//   build      signal colour from the rest position to the value: how much
//              tension this knob adds (the high cut rests on the right, so it
//              lights leftwards); dark at rest
//   position   one bone tick where the value is (noise pitch: a place, not an amount)
//   bipolar    bone from the default to the value (level around 0 dB)
//   plain      grey from the left to the value (dry/wet)
// Drag up/down (Shift = fine), wheel, double-click to type a value,
// Alt/Cmd-click to reset.
//
// A knob something else drives (auto resonance) is disabled: dimmed, it
// shows that value, marked AUTO, and ignores the mouse until it is handed
// back (its help line still shows). A knob a preset drives is locked: its
// ring still shows the driven value, but the disc is flat and the mouse
// can't turn it.
class Dial : public juce::Component
{
public:
    enum class Ring { build, position, bipolar, plain };

    Dial (juce::RangedAudioParameter& p, juce::String name, juce::String help);

    void setRing (Ring r, bool restsAtEnd = false) { ring = r; restAtEnd = restsAtEnd; repaint(); }
    void setAutoValue (std::optional<float> plain);   // nullopt: the knob is the parameter again
    void setLocked (std::optional<float> plain);      // nullopt: the knob is the user's again
    bool isLocked() const { return lockedValue.has_value(); }
    void setMaxDisc (float diameter) { maxDisc = diameter; repaint(); }
    juce::String getHelp() const { return help; }
    std::function<void (Dial*, bool)> onHover;   // the editor shows the help line while hovering

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> discArea() const;
    void nudge (float deltaNorm, bool asGesture);
    juce::String valueText() const { return param.getText (param.convertTo0to1 (shown()), 0); }
    float shown() const { return lockedValue ? *lockedValue : autoValue.value_or (value); }
    bool disabled() const { return autoValue.has_value(); }
    bool untouchable() const { return disabled() || isLocked(); }
    bool showsDifferent (std::optional<float> a, std::optional<float> b) const;
    void updateCursor();

    juce::RangedAudioParameter& param;
    juce::String name, help;
    juce::ParameterAttachment attachment;
    float value = 0.f;
    std::optional<float> autoValue, lockedValue;
    float lastY = 0.f, maxDisc = 66.f;
    bool dragging = false, restAtEnd = false;
    Ring ring = Ring::build;
    std::unique_ptr<juce::TextEditor> entry;
};

// A small knob on a parameter, for a row of a display (the timeline's lane
// intensities): the Dial in miniature, a grey ring lit from the left to the
// value around a disc and its pointer, no text. Its words go to the footer:
// getHelp() is its name and value, then the help line, and onHover runs
// again as the value moves under the mouse. Drag up/down (Shift = fine),
// wheel; double-click or Alt/Cmd-click resets it.
class SmallDial : public juce::Component
{
public:
    SmallDial (juce::RangedAudioParameter& p, juce::String name);

    void setHelp (juce::String line) { help = std::move (line); }
    juce::String getHelp() const;
    std::function<void (SmallDial*, bool)> onHover;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    void nudge (float deltaNorm, bool asGesture);
    void reset();

    juce::RangedAudioParameter& param;
    juce::String name, help;
    juce::ParameterAttachment attachment;
    float value = 0.f, lastY = 0.f;
    bool dragging = false;
};

// A choice parameter as a line of text: its name, then a fixed-width field
// with the current choice and a chevron at its right end. Click for a menu
// of the choices (in sections), wheel to step through them; hovering shows
// the current choice's help line. With steppers, a < and a > key right after
// the chevron step to the previous / next choice, wrapping at the ends; the
// field keeps them in one place whatever the choice. A preset can drive it: then it shows the
// preset's choice, without the chevron, its keys dimmed, and can't be changed.
class TypeSelector : public juce::Component
{
public:
    struct Choice
    {
        juce::String name, help, section;
    };

    TypeSelector (juce::RangedAudioParameter& p, juce::String name, std::vector<Choice> choices);

    juce::String getHelp() const;
    void setDriven (std::optional<int> choice);
    bool isDriven() const { return driven.has_value(); }
    void setSteppers (bool on) { steppers = on; repaint(); }
    float widestChoice() const { return widest; }
    float choiceX() const;   // where the field (the choice text) starts
    // Lines the field up with other selectors: it starts at x at the
    // earliest and is w wide, never closer to the name or narrower than the widest choice.
    void setField (float x, float w) { fieldX = x; field = std::max (widest, w); repaint(); }
    int idealWidth() const;   // room for the field, its chevron and the keys
    std::function<void (TypeSelector*, bool)> onHover;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    enum class Part { none, prev, next, menu };

    int index() const { return juce::jlimit (0, (int) choices.size() - 1, driven.value_or (juce::roundToInt (value))); }
    void choose (int i);
    juce::Rectangle<float> stepKey (int direction) const;   // -1: <, +1: >
    Part partAt (juce::Point<float> p) const;
    void setHover (Part p);

    juce::RangedAudioParameter& param;
    juce::String name;
    std::vector<Choice> choices;
    juce::ParameterAttachment attachment;
    float value = 0.f, wheel = 0.f, widest = 0.f, fieldX = 0.f, field = 0.f;   // field: the choice text's room, >= widest
    std::optional<int> driven;
    bool open = false, steppers = false;
    Part hover = Part::none, pressed = Part::none;
};

// A two-state parameter as a small box and a word: filled when on. Click
// toggles; beforeToggle runs first (with the state it is about to take). A
// preset can drive it: then it shows the preset's state and ignores clicks.
class Switch : public juce::Component
{
public:
    Switch (juce::RangedAudioParameter& p, juce::String text, juce::String help);

    bool isOn() const { return driven.value_or (value >= 0.5f); }
    void setDriven (std::optional<bool> on);
    bool isDriven() const { return driven.has_value(); }
    juce::String getHelp() const { return help; }
    std::function<void (bool turningOn)> beforeToggle;
    std::function<void (Switch*, bool)> onHover;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    juce::RangedAudioParameter& param;
    juce::String text, help;
    juce::ParameterAttachment attachment;
    float value = 0.f;
    std::optional<bool> driven;
};

// Horizontal level meter, -60..0 dBFS, with a peak tick.
class LevelMeter : public juce::Component
{
public:
    explicit LevelMeter (juce::String n) : name (std::move (n)) { setOpaque (true); }
    void push (float peakLinear);   // once per UI tick
    void paint (juce::Graphics&) override;

private:
    juce::String name;
    float levelDb = -100.f, holdDb = -100.f, shownLevel = -100.f, shownHold = -100.f;
    int holdTicks = 0;
};
