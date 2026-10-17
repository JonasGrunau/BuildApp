#pragma once

#include "CutFilters.h"
#include "NoiseSource.h"
#include "PitchShifter.h"
#include "Reverb.h"

#include <vector>

namespace ba
{

// The knobs, as the chain reads them (see Mapping.h for what they mean).
struct Settings
{
    float lowCut = 0.f;       // 0 = off
    float highCut = 1.f;      // 1 = off
    float pitch = 0.f;        // semitones, 0..12
    float noise = 0.f;        // noise volume, 0 = off
    float noisePitch = 0.5f;
    float reverb = 0.f;
    float stereo = 0.f;       // 0 = full width, 1 = mono
    float levelDb = 0.f;
    float mix = 1.f;          // dry/wet, 0..1
};

// The whole effect, stereo, in place:
//
//   in ─┬─ pitch ─ low cut ─ high cut ─ + noise ─ reverb ─ stereo ─┐ wet
//       └──────────────────────────────────────────────────────────┤ dry
//                                                  dry/wet ─ level ─ out
//
// With every knob at its default the signal passes bit for bit: each stage
// at rest is skipped, not run at unity.
class Buildup
{
public:
    Buildup() { set (Settings {}); }

    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    void set (const Settings& s) noexcept;
    void process (float* left, float* right, int numSamples) noexcept;

    double getSampleRate() const noexcept { return fs; }

private:
    void processChunk (float* left, float* right, int n) noexcept;

    double fs = 48000.0;
    PitchShifter pitch;
    CutFilters filters;
    NoiseSource noise;
    Reverb reverb;
    dsp::Smoother width, mix, gain;
    std::vector<float> dryL, dryR;
};

} // namespace ba
