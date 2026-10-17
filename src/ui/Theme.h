#pragma once

#include "plugin/Format.h"

#include <juce_gui_basics/juce_gui_basics.h>

// "Night sheet": PhaseFucker's drawing-sheet language on warm black. Bone
// ink, hairlines, one signal colour for what the effect is doing. No
// gradients, no shadows, no glow; type and lines do the work.
namespace theme
{
    namespace col
    {
        inline const juce::Colour sheet     { 0xff131211 };
        inline const juce::Colour well      { 0xff0c0b0a };   // inside the display
        inline const juce::Colour raised    { 0xff1b1a18 };   // knob discs
        inline const juce::Colour ink       { 0xffece7db };   // bone
        inline const juce::Colour inkSoft   { 0xffa39d91 };   // secondary text
        inline const juce::Colour inkFaint  { 0xff615c54 };   // tertiary text, ticks
        inline const juce::Colour hairline  { 0xff2f2c28 };
        inline const juce::Colour grid      { 0xff1d1b19 };
        inline const juce::Colour accent    { 0xffc8f03c };   // acid
        inline const juce::Colour accentDim { 0xff4d5c1a };   // accent at ~35 % on the sheet
    }

    // IBM Plex Sans Condensed for labels, IBM Plex Mono for numbers.
    juce::Font label (float height = 10.f);        // uppercase captions, tracked
    juce::Font title (float height = 22.f);
    juce::Font mono (float height = 11.f);

    // Uppercase caption with tracking.
    void caption (juce::Graphics& g, const juce::String& s, juce::Rectangle<float> area,
                  juce::Justification j = juce::Justification::centredLeft, juce::Colour c = col::inkSoft, float height = 10.f);

    // Section tag like a drawing callout: a boxed letter and a name, "[A] SPECTRUM".
    void sectionTag (juce::Graphics& g, juce::String letter, const juce::String& name, juce::Point<float> topLeft);

    // Crop marks in the four corners of r, as on a printed sheet.
    void cropMarks (juce::Graphics& g, juce::Rectangle<float> r, float len = 6.f);

    // Diagonal hatching inside r.
    void hatch (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, float spacing = 4.f, float thickness = 1.f);

    float textWidth (const juce::Font& f, const juce::String& s);

    // Draws `draw` into `buffer` with JUCE's software renderer at the
    // screen's pixel density, then blits it. For busy plots this is 2-3x
    // faster than drawing the same paths straight through CoreGraphics.
    void paintOffscreen (juce::Graphics& g, juce::Rectangle<int> bounds, juce::Image& buffer,
                         const std::function<void (juce::Graphics&)>& draw);

    inline const juce::String dot (juce::CharPointer_UTF8 (" \xc2\xb7 "));

    constexpr float kRadius = 2.f;   // the only corner radius anywhere
}
