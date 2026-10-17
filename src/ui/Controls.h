#pragma once

#include "Theme.h"

#include <juce_audio_processors/juce_audio_processors.h>

// Text entry and corner resizer on the night sheet.
class NightLookAndFeel : public juce::LookAndFeel_V4
{
public:
    NightLookAndFeel();
    void drawCornerResizer (juce::Graphics&, int w, int h, bool isMouseOver, bool isMouseDragging) override;
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
class Dial : public juce::Component
{
public:
    enum class Ring { build, position, bipolar, plain };

    Dial (juce::RangedAudioParameter& p, juce::String name, juce::String help);

    void setRing (Ring r, bool restsAtEnd = false) { ring = r; restAtEnd = restsAtEnd; repaint(); }
    juce::String getHelp() const { return help; }
    std::function<void (Dial*, bool)> onHover;   // the editor shows the help line while hovering
    std::function<void()> onValueChange;

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
    juce::String valueText() const { return param.getText (param.convertTo0to1 (value), 0); }

    juce::RangedAudioParameter& param;
    juce::String name, help;
    juce::ParameterAttachment attachment;
    float value = 0.f;
    float lastY = 0.f;
    bool dragging = false, restAtEnd = false;
    Ring ring = Ring::build;
    std::unique_ptr<juce::TextEditor> entry;
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
