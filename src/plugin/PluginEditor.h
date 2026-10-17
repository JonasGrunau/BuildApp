#pragma once

#include "PluginProcessor.h"
#include "ui/Controls.h"
#include "ui/SpectrumView.h"

// One sheet, laid out at 880 x 540 and scaled as a whole when the window is
// resized (fixed aspect):
//   header   wordmark, input and output meters
//   A        spectrum with the cuts and the noise band
//   B..F     filter, pitch, noise, space, output: the nine knobs
//   footer   what the hovered control does; the engine's rate on the right
//
// Nothing repaints on the timer unless what it shows changed: the controls
// are opaque and repaint themselves, the sheet only redraws the footer.
class BuildAppEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    static constexpr int kWidth = 880, kHeight = 540;

    explicit BuildAppEditor (BuildAppProcessor&);
    ~BuildAppEditor() override;

    void resized() override;
    void paint (juce::Graphics&) override {}

    void tick();   // one UI update (timer; the snapshot tool calls it directly)
    void showHelp (const juce::String& line);   // empty: back to the default line

private:
    struct Sheet : juce::Component
    {
        explicit Sheet (BuildAppEditor& e) : editor (e) { setOpaque (true); }
        void paint (juce::Graphics&) override;
        BuildAppEditor& editor;
    };

    struct Group
    {
        const char* letter;
        const char* name;
        int cells;
    };
    static constexpr Group kGroups[] = { { "B", "Filter", 2 }, { "C", "Pitch", 1 }, { "D", "Noise", 2 }, { "E", "Space", 2 }, { "F", "Output", 2 } };
    static float groupX (int group);   // left edge
    static float cellWidth();

    void timerCallback() override { tick(); }
    void layout();

    static juce::Rectangle<int> footerArea() { return { 24, 512, 832, 22 }; }

    BuildAppProcessor& proc;
    NightLookAndFeel lnf;
    Sheet sheet { *this };

    LevelMeter inMeter { "In" }, outMeter { "Out" };
    SpectrumView spectrum { proc };
    Dial lowCut, highCut, pitch, noise, noisePitch, reverb, stereo, level, mix;

    juce::String help, rateText;
};
