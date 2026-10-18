#pragma once

#include "analysis/Tap.h"
#include "fx/Buildup.h"
#include "fx/Presets.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace ids
{
    inline constexpr auto lowCut = "lowCut";
    inline constexpr auto highCut = "highCut";
    inline constexpr auto lowCutType = "lowCutType";
    inline constexpr auto highCutType = "highCutType";
    inline constexpr auto lowCutReso = "lowCutReso";
    inline constexpr auto highCutReso = "highCutReso";
    inline constexpr auto lowCutAuto = "lowCutAuto";
    inline constexpr auto highCutAuto = "highCutAuto";
    inline constexpr auto pitch = "pitch";
    inline constexpr auto noise = "noise";
    inline constexpr auto noisePitch = "noisePitch";
    inline constexpr auto noiseLowCut = "noiseLowCut";
    inline constexpr auto reverb = "reverb";
    inline constexpr auto stereo = "stereo";
    inline constexpr auto level = "level";
    inline constexpr auto mix = "mix";
    inline constexpr auto preset = "preset";
    inline constexpr auto build = "build";
    // How much of each timeline lane lands (ba::Intensity), 0..100 %.
    inline constexpr auto lowCutIntensity = "lowCutIntensity";
    inline constexpr auto lowResoIntensity = "lowResoIntensity";
    inline constexpr auto highCutIntensity = "highCutIntensity";
    inline constexpr auto highResoIntensity = "highResoIntensity";
    inline constexpr auto pitchIntensity = "pitchIntensity";
    inline constexpr auto noiseIntensity = "noiseIntensity";
    inline constexpr auto noisePitchIntensity = "noisePitchIntensity";
    inline constexpr auto noiseLowCutIntensity = "noiseLowCutIntensity";
    inline constexpr auto reverbIntensity = "reverbIntensity";
    inline constexpr auto stereoIntensity = "stereoIntensity";
    inline constexpr auto levelIntensity = "levelIntensity";
}

// The knobs of a buildup chain (ba::Buildup), plus a type, a resonance and an
// auto-resonance switch for each cut. The parameters are what the knobs are
// (0..1 for the cuts, noise and reverb; semitones, dB and percent where those
// are the natural unit); their texts say what they do, in Hz, dB and %. The
// cut types are choices from ba::kLowCutCharacters and ba::kHighCutCharacters.
// PRESET picks one of ba::kPresets (or Off); BUILD then drives the controls
// that preset takes over, and the knobs' own values wait underneath until
// the preset is switched off. One INTENSITY per timeline lane says how much
// of the preset's path for that control lands (ba::Intensity); picking
// another preset in the editor sets them back to 100 %. Offline renders run
// the cuts and the pitch in high quality. A host reset (transport stop or
// jump) clears the reverb tail and the pitch history. Input and output are
// tapped (mono) for the editor's spectrum.
class BuildAppProcessor : public juce::AudioProcessor
{
public:
    BuildAppProcessor();

    // ---- AudioProcessor -------------------------------------------------------
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override { chain.reset(); }   // the host stopped or jumped: tails and history go
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 12.0; }   // the longest reverb

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    juce::RangedAudioParameter& param (const char* id) const { return *apvts.getParameter (id); }

    // The knobs as the chain reads them, the preset's applied (any thread),
    // and as they stand, without it.
    ba::Settings settings() const noexcept;
    ba::Settings ownSettings() const noexcept;
    // The preset BUILD drives, or nullptr when PRESET is Off; BUILD, 0..1;
    // how much of each of its lanes lands.
    const ba::Preset* activePreset() const noexcept;
    float buildAmount() const noexcept;
    ba::Intensity intensity() const noexcept;
    // The user is picking PRESET i (0 = Off) in the editor (message thread):
    // a preset other than the one the intensities were set on starts with
    // every lane at 100 %. Off keeps them, so switching it off and back finds
    // them as they were. Host automation of PRESET leaves them alone: they
    // have automation of their own.
    void presetPicked (int i);

    // ---- For the editor -------------------------------------------------------
    const ba::Tap& inputTap() const noexcept { return inTap; }
    const ba::Tap& outputTap() const noexcept { return outTap; }
    double getRate() const noexcept { return currentRate.load (std::memory_order_relaxed); }
    // Peak level since the last call, linear.
    float takePeak (bool output) noexcept { return (output ? outPeak : inPeak).exchange (0.f, std::memory_order_relaxed); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    float raw (int i) const noexcept { return rawParams[(size_t) i]->load (std::memory_order_relaxed); }

    std::array<std::atomic<float>*, 29> rawParams {};
    ba::Buildup chain;
    ba::Tap inTap, outTap;
    juce::AudioBuffer<float> scratch;   // [0] right channel for a mono input, [1] mono sum for the taps
    std::atomic<double> currentRate { 48000.0 };
    std::atomic<float> inPeak { 0.f }, outPeak { 0.f };
    std::atomic<int> intensitiesOf { 0 };   // the preset (1-based) the intensities were set on, 0: none yet; saved with the state

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BuildAppProcessor)
};
