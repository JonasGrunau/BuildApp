#pragma once

#include "Controls.h"
#include "Theme.h"
#include "fx/Presets.h"

#include <juce_audio_processors/juce_audio_processors.h>

class BuildAppProcessor;

// Section A's timeline: what the preset does over BUILD's travel, as the
// chain runs it (the path, then the fade into the tail past it). One lane
// per control it drives, in the knob row's order, plus the reverb (every
// preset ends in a tail) and the level (its makeup gain and the fade): the
// control's path over the build, with the part already built in the signal
// colour. A playhead marks BUILD; each lane ends in a small INTENSITY knob
// (how much of that lane lands, ba::Intensity) and the control's value at
// BUILD, in the knob's own words.
//
// A lane spans the range between where its control rests (the hairline it
// shrinks towards as its intensity goes down: the bottom, 0 dB for level,
// the default for the noise band, auto resonance for a resonance) and the
// preset's full path, so turning a lane down flattens it in place; the full
// path stays as a fainter line behind it. Click or drag to scrub BUILD,
// double-click to drop it back to 0. Hidden with no preset (the editor
// folds the band away).
class BuildTimeline : public juce::Component
{
public:
    explicit BuildTimeline (BuildAppProcessor& p);

    void refresh();   // once per UI tick
    // What the footer should say (the hovered knob's words, or the band's);
    // empty: the mouse left.
    std::function<void (const juce::String&)> onHelp;

    void paint (juce::Graphics&) override;
    void resized() override { placeKnobs(); }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    // Label | plot | gap | knob | value.
    static constexpr float kLabelW = 86.f, kKnobW = 18.f, kGap = 10.f, kValueW = 64.f, kAxis = 16.f;

private:
    // The lanes there can be, in the knob row's order; each has a knob.
    enum Lane { lowCut, lowReso, highCut, highReso, pitch, noise, noisePitch, noiseLowCut, reverb, stereo, level, kLanes };
    static constexpr float ba::Intensity::*kIntensityOf[kLanes] = {
        &ba::Intensity::lowCut, &ba::Intensity::lowReso, &ba::Intensity::highCut, &ba::Intensity::highReso, &ba::Intensity::pitch,
        &ba::Intensity::noise, &ba::Intensity::noisePitch, &ba::Intensity::noiseLowCut, &ba::Intensity::reverb, &ba::Intensity::stereo,
        &ba::Intensity::level,
    };

    struct Row
    {
        Lane lane;
        juce::String label;
        std::function<float (const ba::Settings&)> value;
        std::function<float (float)> height;        // value -> 0..1 of the lane
        std::function<juce::String (float)> text;   // value -> the knob's words
        bool bipolar = false;                       // drawn around the middle (level)
    };

    std::vector<Row> rows (const ba::Preset&) const;
    juce::Rectangle<float> plot() const;   // where the lanes run, left to right = BUILD 0..1
    float xFor (float build) const;
    void scrub (float x);
    void placeKnobs();                     // one knob per shown lane, the rest hidden
    juce::String knobHelp (Lane, const ba::Preset&) const;

    BuildAppProcessor& proc;
    std::array<std::unique_ptr<SmallDial>, kLanes> knobs;
    const ba::Preset* shownPreset = nullptr;
    float shownBuild = -1.f, shownReverb = -1.f;
    uint32_t shownGen = 0;
    ba::Intensity shownIntensity;
    bool dragging = false;
};
