#pragma once

#include "Tap.h"
#include "dsp/Fft.h"

#include <memory>
#include <vector>

namespace ba
{

// Live spectra of the input and the output for the display (message thread):
// one FFT of the latest samples of both taps per update, read out at
// log-spaced points from 20 Hz to 20 kHz (bins averaged where they're dense,
// interpolated where they're sparse), tilted +4.5 dB/oct around 1 kHz so a
// mix reads roughly flat, with fast-rise / slow-fall ballistics.
class Spectrum
{
public:
    static constexpr int kPoints = 256;
    static constexpr double kLowHz = 20.0, kHighHz = 20000.0, kFloorDb = -120.0;

    void prepare (double sampleRate);
    bool update (const Tap& input, const Tap& output);   // false until there is enough audio
    bool fall (float db);   // no new audio: sink towards the floor; false once all is there
    void clear();

    const std::vector<float>& input() const noexcept { return inDb; }
    const std::vector<float>& output() const noexcept { return outDb; }
    double sampleRate() const noexcept { return fs; }

    static double pointHz (int i) noexcept;

private:
    void readOut (const dsp::cplx* bins, std::vector<float>& shown) const;

    double fs = 0.0;
    int size = 0;
    std::unique_ptr<dsp::Fft> fft;
    std::vector<float> window, a, b, inDb, outDb;
    std::vector<dsp::cplx> fa, fb, scratch;
    double scale = 1.0;
};

} // namespace ba
