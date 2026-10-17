#pragma once

#include "analysis/Tap.h"
#include "fx/Buildup.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace ids
{
    inline constexpr auto lowCut = "lowCut";
    inline constexpr auto highCut = "highCut";
    inline constexpr auto pitch = "pitch";
    inline constexpr auto noise = "noise";
    inline constexpr auto noisePitch = "noisePitch";
    inline constexpr auto reverb = "reverb";
    inline constexpr auto stereo = "stereo";
    inline constexpr auto level = "level";
    inline constexpr auto mix = "mix";
}

// Nine knobs on a buildup chain (ba::Buildup). The parameters are what the
// knobs are (0..1 for the cuts, noise and reverb; semitones, dB and percent
// where those are the natural unit); their texts say what they do, in Hz,
// dB and %. Input and output are tapped (mono) for the editor's spectrum.
class BuildAppProcessor : public juce::AudioProcessor
{
public:
    BuildAppProcessor();

    // ---- AudioProcessor -------------------------------------------------------
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
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

    // The knobs as the chain reads them (any thread).
    ba::Settings settings() const noexcept;

    // ---- For the editor -------------------------------------------------------
    const ba::Tap& inputTap() const noexcept { return inTap; }
    const ba::Tap& outputTap() const noexcept { return outTap; }
    double getRate() const noexcept { return currentRate.load (std::memory_order_relaxed); }
    // Peak level since the last call, linear.
    float takePeak (bool output) noexcept { return (output ? outPeak : inPeak).exchange (0.f, std::memory_order_relaxed); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    float raw (int i) const noexcept { return rawParams[(size_t) i]->load (std::memory_order_relaxed); }

    std::array<std::atomic<float>*, 9> rawParams {};
    ba::Buildup chain;
    ba::Tap inTap, outTap;
    juce::AudioBuffer<float> scratch;   // [0] right channel for a mono input, [1] mono sum for the taps
    std::atomic<double> currentRate { 48000.0 };
    std::atomic<float> inPeak { 0.f }, outPeak { 0.f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BuildAppProcessor)
};
