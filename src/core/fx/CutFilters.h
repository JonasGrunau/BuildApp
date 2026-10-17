#pragma once

#include "dsp/Halfband.h"
#include "dsp/Smoother.h"
#include "dsp/Svf.h"

#include <array>

namespace ba
{

// Low cut and high cut, 24 dB/oct each: two trapezoidal SVFs in series, the
// second one resonant, so the corner carries a +3 dB bump that makes a sweep
// sing without whistling. They run oversampled (polyphase IIR halfbands):
// 4x at 44.1/48 kHz, 2x at 88.2/96 kHz, so the response matches the analog
// filter within 0.25 dB right up to 18 kHz instead of being squeezed against
// Nyquist.
//
// The cutoff glides per sample in log frequency (20 ms), so automation steps
// and fast knob moves sweep evenly across the range. A filter at its "off"
// end stop crossfades out and stops running; with both off the stage is not
// processed at all and the signal passes bit for bit.
class CutFilters
{
public:
    static constexpr double kQ1 = 0.60, kQ2 = 2.1;

    CutFilters() { setTargets (0.f, 1.f); }

    void prepare (double sampleRate);
    void reset();

    // Knob positions (see Mapping.h): low cut 0 = off, high cut 1 = off.
    void setTargets (float lowCut, float highCut) noexcept;
    void process (float* left, float* right, int numSamples) noexcept;

    bool isBypassed() const noexcept { return lowFade.isOff() && highFade.isOff() && pathFade.isOff(); }
    int oversampling() const noexcept { return factor; }
    static int oversamplingFor (double fs) noexcept { return fs < 50000.0 ? 4 : fs < 100000.0 ? 2 : 1; }

    // Magnitude of the stage at `hz` for these knob positions, as it runs at `fs`.
    static double magnitude (double hz, float lowCut, float highCut, double fs) noexcept;

private:
    void step (double* v) noexcept;   // one sample at the internal rate, both channels
    void run (const double* in, double* out, bool filter) noexcept;
    static void coefficients (double cutoffHz, double fs, dsp::SvfCoeffs& c1, dsp::SvfCoeffs& c2) noexcept;

    double fs = 48000.0, rate = 192000.0;
    int factor = 4;

    // Per channel: fs <-> 2 fs (steep), 2 fs <-> 4 fs (light, all the audio is far below its band edge).
    std::array<dsp::Halfband, 2> up1, up2, down2, down1;
    std::array<std::array<dsp::SvfState, 2>, 2> lowState, highState;   // [channel][stage]
    dsp::SvfCoeffs low1, low2, high1, high2;
    dsp::Smoother lowLog, highLog;      // cutoffs as log2 (Hz)
    double lowShown = -1.0, highShown = -1.0;   // the log2 cutoffs the coefficients were made for
    dsp::Fade lowFade, highFade, pathFade;
    bool highCold = true;

    static constexpr int kHistory = 512;   // input kept while resting, to settle the halfbands on waking
    std::array<std::array<float, kHistory>, 2> history {};
    int historyPos = 0;
};

} // namespace ba
