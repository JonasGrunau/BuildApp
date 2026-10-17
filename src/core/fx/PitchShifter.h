#pragma once

#include "dsp/Fft.h"
#include "dsp/Smoother.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace ba
{

// Pitch up by 0..12 semitones in real time with a phase vocoder that keeps
// the spectral peaks locked (Laroche & Dolson's peak-based shifting):
//
//   - every peak and the bins around it (down to the valleys either side)
//     move up together by the nearest whole number of bins, and their phase
//     is rotated by an amount that grows each frame with the peak's true
//     frequency (from the phase advance), so each partial comes out at
//     exactly ratio x its frequency and keeps its shape;
//   - a frame where the high frequencies jump (a hit) starts the rotations
//     over from the analysed phases, so drums keep their attack;
//   - it analyses the analytic signal (a 90-degree allpass pair), so a low
//     partial's mirror image at the negative frequency does not leak into
//     its own bins: the sub-bass shifts as cleanly as the rest. The pair's
//     delay (milliseconds in the bass) is turned back out per bin, so a
//     kick's low end stays with its click.
//
// Low delay: the analysis window is long (4096 points at 44.1/48 kHz, for
// the resolution the bass needs) but asymmetric, with a short tail, and the
// synthesis window covers only that tail (Mauler & Martin). So the shifted
// signal runs 16 ms late, not a whole window. High quality (offline
// renders) keeps that delay, so a bounce lines up with what was heard, and
// buys its quality with an analysis window twice as long.
//
// At 0 semitones the stage passes the signal untouched, with no delay. It
// fades to the shifted signal (and back) over 25 ms, waking up with the
// window already run over the recent input, so there is no cold start.
class PitchShifter
{
public:
    void prepare (double sampleRate);
    void reset();

    void setSemitones (float st) noexcept;
    void setHighQuality (bool on) noexcept { wantHigh = on; }
    void process (float* left, float* right, int numSamples) noexcept;

    bool isBypassed() const noexcept { return engage.isOff(); }
    int delay() const noexcept { return lag; }   // how late the shifted signal runs, in samples

private:
    struct Shape
    {
        int size = 0, hop = 0;
        std::vector<float> analysis, synthesis;   // synthesis is zero but for the last 2 tail samples
        std::vector<dsp::cplx> undo;   // per bin: takes the allpass pair's delay back out
        std::vector<double> lift;      // per frequency (in bins): the level that costs, given back
    };

    void design (Shape&, int size, int hop) const;
    void use (const Shape&) noexcept;
    void frame (int64_t end, bool synthesise) noexcept;   // the window that ends at input sample `end`
    void shift (int channel, bool hit) noexcept;   // spec -> out for one channel, at `ratio`
    void wake() noexcept;

    double fs = 48000.0;
    int tail = 384, lag = 768;
    Shape live, high;
    const Shape* shape = &live;
    bool wantHigh = false;
    std::unique_ptr<dsp::Fft> liveFft, highFft;
    const dsp::Fft* fft = nullptr;

    // A 90-degree pair per channel: two chains of four allpasses in z^-2
    // (Niemitalo's coefficients), the first a sample later.
    struct Quadrature
    {
        std::array<std::array<double, 4>, 2> x1 {}, x2 {}, y1 {}, y2 {};
        double delayed = 0.0;
        void reset() noexcept { *this = {}; }
        void process (double in, float& re, float& im) noexcept;
    };
    std::array<Quadrature, 2> quadrature;

    // Input history (as it came in, and its analytic pair) and the
    // overlap-add accumulator, indexed by absolute input sample.
    std::array<std::vector<float>, 2> input, real, imag, acc;
    int inMask = 0, accMask = 0;
    int64_t written = 0;

    // Per channel and bin: last frame's spectrum and the rotation of the peak
    // region the bin belonged to.
    std::array<std::vector<dsp::cplx>, 2> spec, out, lastSpec;
    std::array<std::vector<double>, 2> rotation, nextRotation;
    std::vector<dsp::cplx> work;
    std::vector<double> mag;
    std::vector<int> peaks;
    double ratio = 1.0, lastHf = 0.0;
    int sinceHit = 0;

    dsp::Smoother semis;
    dsp::Fade engage;
    bool running = false;
};

} // namespace ba
