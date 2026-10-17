#pragma once

#include "dsp/Smoother.h"
#include "dsp/Svf.h"

#include <array>
#include <vector>

namespace ba
{

// The buildup reverb, on one knob. Turning it up does what a producer does
// by hand before a drop: more send, a longer tail (0.8 s to 12 s) and less of
// the dry signal (down to -6 dB), until the loop dissolves into the wash. The
// send is cut below 160 Hz and above 9 kHz so the wash never muddies the kick
// and bass.
//
// Inside: four diffusion steps (8 channels each: delays, Hadamard mix,
// polarity flips) into an 8-line feedback delay network with a Householder
// matrix, slowly modulated delay lines (no metallic ringing) and a high
// shelf in the loop so the highs die away first, like air. Pulling the knob
// back to zero cuts the tail within ~30 ms: the classic dry drop.
class Reverb
{
public:
    static constexpr int kChannels = 8, kSteps = 4;

    void prepare (double sampleRate);
    void reset();

    void setAmount (float amount) noexcept { target = amount; amountSmooth.setTarget (amount); }
    void process (float* left, float* right, int numSamples) noexcept;   // in place: dry * x + wet * reverb (x)

    bool isBypassed() const noexcept { return amountSmooth.settled() && amountSmooth.get() == 0.0; }

    static double decaySeconds (double amount) noexcept;
    static double dryGain (double amount) noexcept;
    static double wetGain (double amount) noexcept;

private:
    struct Line
    {
        std::vector<float> data;
        int mask = 0, pos = 0;

        void allocate (int maxDelay);
        void clear();
        void write (float x) noexcept { data[(size_t) pos] = x; pos = (pos + 1) & mask; }
        float read (int delay) const noexcept { return data[(size_t) ((pos - delay) & mask)]; }
        float readFrac (double delay) const noexcept;
    };

    void updateDecay (double amount) noexcept;

    double fs = 48000.0;
    float target = 0.f;
    dsp::Smoother amountSmooth;
    double decayShown = -1.0;

    std::array<dsp::SvfState, 2> sendLow, sendHigh;
    dsp::SvfCoeffs sendLowC, sendHighC;
    std::array<Line, 2> preDelay;
    int preDelaySamples = 0;

    std::array<std::array<Line, kChannels>, kSteps> diffuser;
    std::array<std::array<int, kChannels>, kSteps> diffuserDelay {};
    std::array<std::array<float, kChannels>, kSteps> diffuserSign {};

    std::array<Line, kChannels> loop;
    std::array<double, kChannels> loopDelay {}, loopGain {}, damp {};
    std::array<double, kChannels> lfoPhase {}, lfoStep {}, lfoDepth {};
    double dampCoef = 0.5;
    bool cleared = true;
};

} // namespace ba
