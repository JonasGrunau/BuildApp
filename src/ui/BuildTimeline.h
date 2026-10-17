#pragma once

#include "Theme.h"
#include "fx/Presets.h"

#include <juce_audio_processors/juce_audio_processors.h>

class BuildAppProcessor;

// Section A's timeline: what the preset does over BUILD's travel. One lane
// per control it drives, in the knob row's order: the control's path over
// the build, scaled to its own range so you see when it moves (gain keeps
// 0 dB in view), with the part already built in the signal colour. A
// playhead marks BUILD; each lane ends in the control's value there, in the
// knob's own words. Click or drag to scrub BUILD, double-click to drop it
// back to 0. Hidden with no preset (the editor folds the band away).
class BuildTimeline : public juce::Component
{
public:
    explicit BuildTimeline (BuildAppProcessor& p);

    void refresh();   // once per UI tick
    std::function<void (bool)> onHover;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { if (onHover) onHover (true); }
    void mouseExit (const juce::MouseEvent&) override { if (onHover) onHover (false); }

    static constexpr float kLabelW = 86.f, kValueW = 64.f, kAxis = 16.f;

private:
    struct Row
    {
        juce::String label;
        const ba::Lane& lane;
        std::function<float (float)> height;        // value -> 0..1 of the lane
        std::function<juce::String (float)> text;   // value -> the knob's words
        bool bipolar = false;                       // drawn around the middle (gain)
    };

    std::vector<Row> rows (const ba::Preset&) const;
    juce::Rectangle<float> plot() const;   // where the lanes run, left to right = BUILD 0..1
    float xFor (float build) const;
    void scrub (float x);

    BuildAppProcessor& proc;
    const ba::Preset* shownPreset = nullptr;
    float shownBuild = -1.f;
    bool dragging = false;
};
