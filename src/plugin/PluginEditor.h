#pragma once

#include "PluginProcessor.h"
#include "ui/BuildTimeline.h"
#include "ui/Controls.h"
#include "ui/SpectrumView.h"

// One sheet, laid out at 880 x 666 and scaled as a whole when the window is
// resized (fixed aspect); with no preset the build band folds away to its
// menu and the sheet is 880 x 548:
//   header   wordmark, input and output meters
//   A        build: the preset menu, the timeline of what it drives, the BUILD knob
//   B        spectrum with the cuts and the noise band; the cut types in its header
//   C..G     filter (cuts, their resonance and auto switches), pitch, noise,
//            space, output; what the preset drives is locked
//   footer   what the hovered control does (or what the preset does); the
//            engine's rate on the right
//
// Nothing repaints on the timer unless what it shows changed: the controls
// are opaque and repaint themselves, the sheet only redraws the footer.
class BuildAppEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    static constexpr int kWidth = 880, kHeight = 666;   // with a preset
    static constexpr int kFold = 118;                   // what the build band gives back without one

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
    static constexpr Group kGroups[] = { { "C", "Filter", 4 }, { "D", "Pitch", 1 }, { "E", "Noise", 3 }, { "F", "Space", 2 }, { "G", "Output", 2 } };
    static float groupX (int group);   // left edge
    static float cellWidth();

    void timerCallback() override { tick(); }
    void layout();
    void setParam (const char* id, float plain);
    void takeOverResonance (bool lowpass);   // auto off, the knob where auto had it
    void showDriven();                       // lock what the preset drives, at its values
    void setOpen (bool presetOn);            // unfold or fold the build band; the window follows
    int sheetHeight() const { return kHeight - (open ? 0 : kFold); }
    float fold() const { return open ? 0.f : (float) kFold; }   // how far everything under the band moves up
    juce::String defaultHelp() const;        // the footer when nothing is hovered
    juce::String lockedHelp() const;

    juce::Rectangle<int> footerArea() const { return { 24, 638 - (int) fold(), 832, 22 }; }

    BuildAppProcessor& proc;
    NightLookAndFeel lnf;
    Sheet sheet { *this };

    LevelMeter inMeter { "In" }, outMeter { "Out" };
    BuildTimeline timeline { proc };
    SpectrumView spectrum { proc };
    Dial build, lowCut, lowReso, highCut, highReso, pitch, noise, noisePitch, noiseLowCut, reverb, stereo, level, mix;
    TypeSelector preset, lowCutType, highCutType;
    Switch lowAuto, highAuto;

    juce::String help, rateText;
    bool helpIsDefault = true, open = true;
};
