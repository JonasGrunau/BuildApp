#pragma once

#include "CutFilters.h"
#include "Mapping.h"
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
    int lowCutType = 0;       // kLowCutCharacters
    int highCutType = 0;      // kHighCutCharacters
    float lowCutReso = 0.3f;  // resonance, 0..1, when not automatic
    float highCutReso = 0.3f;
    bool lowCutAuto = true;   // resonance follows the cutoff
    bool highCutAuto = true;
    float pitch = 0.f;        // semitones, 0..12
    float noise = 0.f;        // noise volume, 0 = off
    float noisePitch = 0.5f;
    float noiseLowCut = map::lowCutNorm (map::kNoiseLowCutDefault);   // 0 = off
    float reverb = 0.f;
    float stereo = 0.f;       // 0 = full width, 1 = mono
    float levelDb = 0.f;
    float mix = 1.f;          // dry/wet, 0..1
    float makeupDb = 0.f;     // gain on the wet signal; only presets set it (Presets.h)
    bool highQuality = false; // the cuts' high quality mode (offline renders)

    CutKnobs lowCutKnobs() const noexcept { return { lowCut, lowCutType, lowCutReso, lowCutAuto }; }
    CutKnobs highCutKnobs() const noexcept { return { highCut, highCutType, highCutReso, highCutAuto }; }
};

// The whole effect, stereo, in place:
//
//   in ─┬─ pitch ─ low cut ─ high cut ─ + noise ─ reverb ─ stereo ─ makeup ─┐ wet
//       └───────────────────────────────────────────────────────────────────┤ dry
//                                                           dry/wet ─ level ─ out
//
// With every knob at its default the signal passes bit for bit: each stage
// at rest is skipped, not run at unity.
class Buildup
{
public:
    Buildup() { set (Settings {}); }

    void prepare (double sampleRate, int maxBlockSize);
    void reset();   // forgets the signal (tails, history); the knobs stay where they are

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
    dsp::Smoother width, mix, gain, makeup;
    std::vector<float> dryL, dryR;
};

} // namespace ba
