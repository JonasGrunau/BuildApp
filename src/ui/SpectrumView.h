#pragma once

#include "Theme.h"
#include "analysis/Spectrum.h"

#include <juce_audio_processors/juce_audio_processors.h>

class BuildAppProcessor;

// Section A. The live spectrum of the input (a faint line) and the output
// (filled), with the cut filters' exact response in the signal colour on its
// own scale (0 dB near the top) and what it removes hatched, and the noise
// band dotted where it sits. The two cut markers can be dragged sideways;
// dragging one past the edge of the plot, or double-clicking it, turns that
// filter off. Paints offscreen (software renderer, one blit).
class SpectrumView : public juce::Component
{
public:
    explicit SpectrumView (BuildAppProcessor& p);

    void refresh();   // once per UI tick: new audio, changed knobs
    std::function<void (bool)> onHover;

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { if (onHover) onHover (true); }
    void mouseExit (const juce::MouseEvent&) override;

    static constexpr float kAxis = 18.f;   // frequency labels below the plot

private:
    enum class Grab { none, low, high };

    void draw (juce::Graphics&);
    juce::Rectangle<float> plot() const { return getLocalBounds().toFloat().withTrimmedBottom (kAxis); }
    float xFor (double hz) const;
    double hzFor (float x) const;
    float ySpectrum (double db) const;   // analyser scale
    float yFilter (double db) const;     // filter scale
    float handleX (Grab g) const;
    Grab grabAt (juce::Point<float> p) const;
    juce::RangedAudioParameter& paramFor (Grab g) const;
    void setFromX (Grab g, float x);

    BuildAppProcessor& proc;
    ba::Spectrum spectrum;
    juce::Image buffer;
    int64_t lastCount = -1;
    int quietTicks = 0;
    std::array<float, 4> shownKnobs { -1.f, -1.f, -1.f, -1.f };
    Grab grab = Grab::none, hover = Grab::none;
};
