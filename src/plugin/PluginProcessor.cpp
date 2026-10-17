#include "PluginProcessor.h"

#include "Format.h"
#include "PluginEditor.h"
#include "fx/Mapping.h"
#include "fx/Reverb.h"

namespace
{
    namespace map = ba::map;

    const juce::String kOff ("Off");

    bool saysOff (const juce::String& t) { return t.trim().equalsIgnoreCase ("off"); }

    // The order of rawParams.
    enum Raw { lowCut, highCut, pitch, noise, noisePitch, reverb, stereo, level, mix };
    constexpr const char* kRawIds[] = { ids::lowCut, ids::highCut, ids::pitch, ids::noise, ids::noisePitch,
                                        ids::reverb, ids::stereo, ids::level, ids::mix };
} // namespace

BuildAppProcessor::BuildAppProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "BuildApp", createLayout())
{
    for (size_t i = 0; i < rawParams.size(); ++i)
        rawParams[i] = apvts.getRawParameterValue (kRawIds[i]);
}

juce::AudioProcessorValueTreeState::ParameterLayout BuildAppProcessor::createLayout()
{
    using namespace juce;
    using Attr = AudioParameterFloatAttributes;
    AudioProcessorValueTreeState::ParameterLayout layout;
    const auto unit = [] (float lo, float hi) { return NormalisableRange<float> (lo, hi); };

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::lowCut, 1 }, "Low Cut", unit (0.f, 1.f), 0.f,
        Attr().withStringFromValueFunction ([] (float v, int) { return map::lowCutOff (v) ? kOff : fmt::hertz (map::lowCutHz (v)); })
              .withValueFromStringFunction ([] (const String& t) { return saysOff (t) ? 0.f : map::lowCutNorm (fmt::parse (t)); })));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::highCut, 1 }, "High Cut", unit (0.f, 1.f), 1.f,
        Attr().withStringFromValueFunction ([] (float v, int) { return map::highCutOff (v) ? kOff : fmt::hertz (map::highCutHz (v)); })
              .withValueFromStringFunction ([] (const String& t) { return saysOff (t) ? 1.f : map::highCutNorm (fmt::parse (t)); })));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::pitch, 1 }, "Pitch", unit (0.f, (float) map::kMaxPitch), 0.f,
        Attr().withLabel ("st")
              .withStringFromValueFunction ([] (float v, int) { return fmt::signedNumber (v, 1) + " st"; })
              .withValueFromStringFunction ([] (const String& t) { return (float) fmt::parse (t); })));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::noise, 1 }, "Noise", unit (0.f, 1.f), 0.f,
        Attr().withStringFromValueFunction ([] (float v, int) { return v <= map::kEnd ? kOff : fmt::signedNumber (map::noiseDb (v), 1) + " dB"; })
              .withValueFromStringFunction ([] (const String& t) { return saysOff (t) ? 0.f : map::noiseNormFromDb (fmt::parse (t)); })));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::noisePitch, 1 }, "Noise Pitch", unit (0.f, 1.f), 0.5f,
        Attr().withStringFromValueFunction ([] (float v, int) { return fmt::hertz (map::noiseHz (v)); })
              .withValueFromStringFunction ([] (const String& t) { return map::noiseHzNorm (fmt::parse (t)); })));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::reverb, 1 }, "Reverb", unit (0.f, 1.f), 0.f,
        Attr().withStringFromValueFunction ([] (float v, int) { return v <= map::kEnd ? kOff : fmt::fixed (v * 100.0, 0) + " %"; })
              .withValueFromStringFunction ([] (const String& t) { return saysOff (t) ? 0.f : (float) juce::jlimit (0.0, 1.0, fmt::parse (t) / 100.0); })));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::stereo, 1 }, "Stereo", unit (0.f, 1.f), 0.f,
        Attr().withStringFromValueFunction ([] (float v, int) {
                  const double w = map::width (v);
                  return w <= 0.0005 ? String ("Mono") : fmt::fixed (w * 100.0, 0) + " %";
              })
              .withValueFromStringFunction ([] (const String& t) {
                  return t.trim().equalsIgnoreCase ("mono") ? 1.f : (float) juce::jlimit (0.0, 1.0, 1.0 - fmt::parse (t) / 100.0);
              })));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::level, 1 }, "Level", unit ((float) map::kLevelMin, (float) map::kLevelMax), 0.f,
        Attr().withLabel ("dB")
              .withStringFromValueFunction ([] (float v, int) { return fmt::signedNumber (v, 1) + " dB"; })
              .withValueFromStringFunction ([] (const String& t) { return (float) fmt::parse (t); })));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::mix, 1 }, "Dry/Wet", unit (0.f, 100.f), 100.f,
        Attr().withLabel ("%")
              .withStringFromValueFunction ([] (float v, int) { return fmt::fixed (v, 0) + " %"; })
              .withValueFromStringFunction ([] (const String& t) { return (float) fmt::parse (t); })));

    return layout;
}

ba::Settings BuildAppProcessor::settings() const noexcept
{
    ba::Settings s;
    s.lowCut = raw (Raw::lowCut);
    s.highCut = raw (Raw::highCut);
    s.pitch = raw (Raw::pitch);
    s.noise = raw (Raw::noise);
    s.noisePitch = raw (Raw::noisePitch);
    s.reverb = raw (Raw::reverb);
    s.stereo = raw (Raw::stereo);
    s.levelDb = raw (Raw::level);
    s.mix = raw (Raw::mix) / 100.f;
    return s;
}

// ---- Audio --------------------------------------------------------------------

bool BuildAppProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    const auto ok = [] (const juce::AudioChannelSet& s) { return s == juce::AudioChannelSet::mono() || s == juce::AudioChannelSet::stereo(); };
    // Mono in / stereo out is fine (the noise and the reverb are stereo); stereo into mono is not.
    return ok (in) && ok (out) && in.size() <= out.size();
}

void BuildAppProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentRate.store (sampleRate, std::memory_order_relaxed);
    chain.set (settings());
    chain.prepare (sampleRate, std::max (1, samplesPerBlock));
    scratch.setSize (2, std::max (1, samplesPerBlock));
}

void BuildAppProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int ins = getTotalNumInputChannels(), outs = getTotalNumOutputChannels();
    if (n == 0 || ins == 0 || outs == 0) return;
    if (scratch.getNumSamples() < n) scratch.setSize (2, n, false, false, true);   // a host that lied about the block size

    float* left = buffer.getWritePointer (0);
    float* right = outs > 1 ? buffer.getWritePointer (1) : scratch.getWritePointer (0);
    if (ins == 1) juce::FloatVectorOperations::copy (right, left, n);

    const auto tap = [&] (ba::Tap& t, std::atomic<float>& peak) {
        float* m = scratch.getWritePointer (1);
        juce::FloatVectorOperations::copy (m, left, n);
        juce::FloatVectorOperations::add (m, right, n);
        juce::FloatVectorOperations::multiply (m, 0.5f, n);
        t.push (m, n);
        const auto r = juce::FloatVectorOperations::findMinAndMax (m, n);
        const float p = std::max (-r.getStart(), r.getEnd());
        if (p > peak.load (std::memory_order_relaxed)) peak.store (p, std::memory_order_relaxed);
    };

    tap (inTap, inPeak);
    chain.set (settings());
    chain.process (left, right, n);
    tap (outTap, outPeak);

    if (outs == 1) // mono out: the sum
    {
        juce::FloatVectorOperations::add (left, right, n);
        juce::FloatVectorOperations::multiply (left, 0.5f, n);
    }
    for (int c = 2; c < outs; ++c)
        buffer.clear (c, 0, n);
}

// ---- State --------------------------------------------------------------------

void BuildAppProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void BuildAppProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* BuildAppProcessor::createEditor() { return new BuildAppEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new BuildAppProcessor(); }
