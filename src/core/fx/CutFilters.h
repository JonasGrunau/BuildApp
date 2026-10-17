#pragma once

#include "CharacterFilter.h"
#include "dsp/Halfband.h"
#include "dsp/Smoother.h"

#include <array>

namespace ba
{

// One cut's controls.
struct CutKnobs
{
    float position = 0.f;    // the cut knob (Mapping.h): low cut 0 = off, high cut 1 = off
    int type = 0;            // kLowCutCharacters / kHighCutCharacters
    float reso = 0.3f;       // resonance (Volcano's peak, 0..1) when not automatic
    bool autoReso = true;    // resonance follows the cutoff, the way the type wants (autoReso)
};

// Low cut and high cut, each a Volcano 3 style filter (CharacterFilter.h)
// with a character picked from a list of buildup and breakdown sounds
// (Characters.h): style, slope, peak and drive in one choice.
//
// Live they run at the host rate, as Volcano does. High quality, meant for
// offline renders, runs both oversampled through polyphase IIR halfbands
// (4x at 44.1/48 kHz, 2x at 88.2/96 kHz), so the clamps, the cubic damping
// and the clipper alias far less; it applies from the next block.
//
// The cutoff glides per sample in log frequency (20 ms), so automation steps
// and fast knob moves sweep evenly across the range; the resonance glides
// after it (30 ms), and in auto mode follows the gliding cutoff, so it moves
// with the sweep sample by sample. Changing a cut's type
// crossfades from the old filter to the new one (30 ms), the new one
// starting from the old one's state. A filter at its "off" end stop fades
// out and stops running; with both off the stage is not processed at all
// and the signal passes bit for bit, at either quality.
class CutFilters
{
public:
    CutFilters() { setTargets ({ 0.f }, { 1.f }); }

    void prepare (double sampleRate);
    void reset();

    void setTargets (const CutKnobs& low, const CutKnobs& high) noexcept;
    void setHighQuality (bool on) noexcept { wantHigh = on; }
    void process (float* left, float* right, int numSamples) noexcept;

    bool isBypassed() const noexcept { return low.isOff() && high.isOff() && pathFade.isOff(); }
    int oversampling() const noexcept { return factor; }
    static int oversamplingFor (double fs) noexcept { return fs < 50000.0 ? 4 : fs < 100000.0 ? 2 : 1; }

    // The resonance a cut runs at for these knobs (once the glides settle).
    static double resonance (bool lowpass, const CutKnobs&) noexcept;

    // Magnitude of the stage at `hz` for these knobs, as it runs live at
    // `fs` on a signal below clipping.
    static double magnitude (double hz, const CutKnobs& low, const CutKnobs& high, double fs) noexcept;

private:
    // One cut: its filter, the one it is crossfading away from after a type
    // change, its cutoff and resonance glides and its on/off fade, all at
    // the internal rate.
    class Cut
    {
    public:
        explicit Cut (bool isLowpass) : lowpass (isLowpass), now (isLowpass), before (isLowpass) {}

        void prepare (double rate);
        void setRate (double rate) noexcept;   // keeps the states
        void reset() noexcept;
        void setTarget (bool on, double log2Hz, const CutKnobs&) noexcept;
        void process (double* v) noexcept;   // one sample, both channels, in place
        bool isOff() const noexcept { return fade.isOff(); }

    private:
        double resonanceAt (double hz) noexcept;   // the resonance target at this (gliding) cutoff

        bool lowpass;
        CharacterFilter now, before;
        dsp::Smoother log2Hz, peak;
        dsp::Fade fade, change;
        int type = 0;
        double manual = 0.3, autoAt = -1.0, autoValue = 0.0;
        bool autoReso = true, cold = true;
    };

    void setFactor (int f) noexcept;
    void resetPath() noexcept;
    void settlePath() noexcept;   // runs the halfbands over the recent input
    void run (const double* in, double* out, bool filter) noexcept;   // one host sample

    double fs = 48000.0;
    int factor = 1;
    bool wantHigh = false;
    Cut low { false }, high { true };

    // The oversampled path delays by a few samples, so it fades in and out
    // against the untouched signal rather than switching.
    dsp::Fade pathFade;
    // Per channel: fs <-> 2 fs (steep), 2 fs <-> 4 fs (light, all the audio is far below its band edge).
    std::array<dsp::Halfband, 2> up1, up2, down2, down1;

    static constexpr int kHistory = 512;   // the latest input, to settle the halfbands on waking
    std::array<std::array<float, kHistory>, 2> history {};
    int historyPos = 0;
};

} // namespace ba
