#include "PluginProcessor.h"

#include "Format.h"
#include "PluginEditor.h"
#include "fx/Characters.h"
#include "fx/Mapping.h"
#include "fx/Reverb.h"

namespace
{
    namespace map = ba::map;

    const juce::String kOff ("Off");
    const juce::Identifier kIntensitiesOf ("intensitiesOf");   // in the saved state

    bool saysOff (const juce::String& t) { return t.trim().equalsIgnoreCase ("off"); }

    // The order of rawParams.
    enum Raw { lowCut, highCut, pitch, noise, noisePitch, reverb, stereo, level, mix, lowCutType, highCutType,
               lowCutReso, highCutReso, lowCutAuto, highCutAuto, noiseLowCut, preset, build,
               lowCutIntensity, lowResoIntensity, highCutIntensity, highResoIntensity, pitchIntensity, noiseIntensity,
               noisePitchIntensity, noiseLowCutIntensity, reverbIntensity, stereoIntensity, levelIntensity };
    constexpr const char* kRawIds[] = { ids::lowCut, ids::highCut, ids::pitch, ids::noise, ids::noisePitch,
                                        ids::reverb, ids::stereo, ids::level, ids::mix, ids::lowCutType, ids::highCutType,
                                        ids::lowCutReso, ids::highCutReso, ids::lowCutAuto, ids::highCutAuto, ids::noiseLowCut,
                                        ids::preset, ids::build,
                                        ids::lowCutIntensity, ids::lowResoIntensity, ids::highCutIntensity, ids::highResoIntensity,
                                        ids::pitchIntensity, ids::noiseIntensity, ids::noisePitchIntensity, ids::noiseLowCutIntensity,
                                        ids::reverbIntensity, ids::stereoIntensity, ids::levelIntensity };
    static_assert (std::size (kRawIds) == Raw::levelIntensity + 1);

    juce::StringArray typeNames (bool lowpass)
    {
        juce::StringArray names;
        for (const auto& c : ba::characters (lowpass)) names.add (c.name);
        return names;
    }

    juce::StringArray presetNames()
    {
        juce::StringArray names (kOff);
        for (const auto& p : ba::kPresets) names.add (p.name);
        for (int i = 1; i <= UserPresets::kSlots; ++i) names.add ("User " + juce::String (i));
        return names;
    }
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

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::lowCutType, 2 }, "Low Cut Type", typeNames (false), 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::highCutType, 2 }, "High Cut Type", typeNames (true), 0));
    for (const auto& [id, name] : { std::pair { ids::lowCutReso, "Low Cut Resonance" }, { ids::highCutReso, "High Cut Resonance" } })
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { id, 2 }, name, unit (0.f, 100.f), 30.f,
            Attr().withLabel ("%")
                  .withStringFromValueFunction ([] (float v, int) { return fmt::fixed (v, 0) + " %"; })
                  .withValueFromStringFunction ([] (const String& t) { return (float) juce::jlimit (0.0, 100.0, fmt::parse (t)); })));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::lowCutAuto, 2 }, "Low Cut Auto Resonance", true));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ids::highCutAuto, 2 }, "High Cut Auto Resonance", true));

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
        ParameterID { ids::noiseLowCut, 2 }, "Noise Low Cut", unit (0.f, 1.f), map::lowCutNorm (map::kNoiseLowCutDefault),
        Attr().withStringFromValueFunction ([] (float v, int) { return map::lowCutOff (v) ? kOff : fmt::hertz (map::lowCutHz (v)); })
              .withValueFromStringFunction ([] (const String& t) { return saysOff (t) ? 0.f : map::lowCutNorm (fmt::parse (t)); })));

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

    // Version 5: the user slots after the factory presets.
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::preset, 5 }, "Preset", presetNames(), 0));
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ids::build, 4 }, "Build", unit (0.f, 100.f), 0.f,
        Attr().withLabel ("%")
              .withStringFromValueFunction ([] (float v, int) { return fmt::fixed (v, 0) + " %"; })
              .withValueFromStringFunction ([] (const String& t) { return (float) juce::jlimit (0.0, 100.0, fmt::parse (t)); })));

    // One intensity per timeline lane (appended: the order is the save format).
    for (const auto& [id, name] : { std::pair { ids::lowCutIntensity, "Low Cut Intensity" }, { ids::lowResoIntensity, "Low Cut Resonance Intensity" },
                                    { ids::highCutIntensity, "High Cut Intensity" }, { ids::highResoIntensity, "High Cut Resonance Intensity" },
                                    { ids::pitchIntensity, "Pitch Intensity" }, { ids::noiseIntensity, "Noise Intensity" },
                                    { ids::noisePitchIntensity, "Noise Pitch Intensity" }, { ids::noiseLowCutIntensity, "Noise Low Cut Intensity" },
                                    { ids::reverbIntensity, "Reverb Intensity" }, { ids::stereoIntensity, "Stereo Intensity" },
                                    { ids::levelIntensity, "Level Intensity" } })
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { id, 5 }, name, unit (0.f, 100.f), 100.f,
            Attr().withLabel ("%")
                  .withStringFromValueFunction ([] (float v, int) { return fmt::fixed (v, 0) + " %"; })
                  .withValueFromStringFunction ([] (const String& t) { return (float) juce::jlimit (0.0, 100.0, fmt::parse (t)); })));

    return layout;
}

ba::Settings BuildAppProcessor::settings() const noexcept
{
    const auto s = ownSettings();
    if (const auto* p = activePreset()) return ba::applyPreset (*p, buildAmount(), s, intensity());
    return s;
}

ba::Settings BuildAppProcessor::audioSettings() noexcept
{
    const auto s = ownSettings();
    const int i = presetIndex();
    const ba::Preset* p = i >= 1 && i <= kFactory ? &ba::kPresets[(size_t) i - 1] : user.forAudio (slotOf (i));
    return p != nullptr ? ba::applyPreset (*p, buildAmount(), s, intensity()) : s;
}

ba::Intensity BuildAppProcessor::intensity() const noexcept
{
    const auto at = [this] (int i) { return juce::jlimit (0.f, 1.f, raw (i) / 100.f); };
    return { at (Raw::lowCutIntensity), at (Raw::lowResoIntensity), at (Raw::highCutIntensity), at (Raw::highResoIntensity),
             at (Raw::pitchIntensity), at (Raw::noiseIntensity), at (Raw::noisePitchIntensity), at (Raw::noiseLowCutIntensity),
             at (Raw::reverbIntensity), at (Raw::stereoIntensity), at (Raw::levelIntensity) };
}

void BuildAppProcessor::presetPicked (int i)
{
    // Whatever is on now (picked here, loaded or automated) is what they were set on.
    if (const int now = juce::roundToInt (raw (Raw::preset)); now != 0) intensitiesOf = now;
    if (i == 0 || i == intensitiesOf) return;
    presetReplaced (i);
}

void BuildAppProcessor::presetReplaced (int i)
{
    intensitiesOf = i;
    for (int r = Raw::lowCutIntensity; r <= Raw::levelIntensity; ++r)
    {
        if (raw (r) >= 100.f) continue;
        auto& p = param (kRawIds[r]);
        p.beginChangeGesture();
        p.setValueNotifyingHost (p.getDefaultValue());
        p.endChangeGesture();
    }
}

ba::Settings BuildAppProcessor::ownSettings() const noexcept
{
    ba::Settings s;
    s.lowCut = raw (Raw::lowCut);
    s.highCut = raw (Raw::highCut);
    s.lowCutType = juce::roundToInt (raw (Raw::lowCutType));
    s.highCutType = juce::roundToInt (raw (Raw::highCutType));
    s.lowCutReso = raw (Raw::lowCutReso) / 100.f;
    s.highCutReso = raw (Raw::highCutReso) / 100.f;
    s.lowCutAuto = raw (Raw::lowCutAuto) >= 0.5f;
    s.highCutAuto = raw (Raw::highCutAuto) >= 0.5f;
    s.pitch = raw (Raw::pitch);
    s.noise = raw (Raw::noise);
    s.noisePitch = raw (Raw::noisePitch);
    s.noiseLowCut = raw (Raw::noiseLowCut);
    s.reverb = raw (Raw::reverb);
    s.stereo = raw (Raw::stereo);
    s.levelDb = raw (Raw::level);
    s.mix = raw (Raw::mix) / 100.f;
    s.highQuality = isNonRealtime();
    return s;
}

int BuildAppProcessor::presetIndex() const noexcept { return juce::roundToInt (raw (Raw::preset)); }

const ba::Preset* BuildAppProcessor::activePreset() const noexcept
{
    const int i = presetIndex();
    if (i >= 1 && i <= kFactory) return &ba::kPresets[(size_t) i - 1];
    if (const int slot = slotOf (i); slot >= 0 && user[slot].used) return &user[slot].path;
    return nullptr;
}

juce::String BuildAppProcessor::presetName (int i) const
{
    if (i >= 1 && i <= kFactory) return ba::kPresets[(size_t) i - 1].name;
    if (const int slot = slotOf (i); slot >= 0) return user[slot].used ? user[slot].name : "User " + juce::String (slot + 1);
    return kOff;
}

juce::String BuildAppProcessor::presetHelp (int i) const
{
    if (i >= 1 && i <= kFactory) return ba::kPresets[(size_t) i - 1].help;
    if (const int slot = slotOf (i); slot >= 0)
        return user[slot].used ? user[slot].help : juce::String ("An empty user slot: it plays nothing until a preset of yours is in it.");
    return "No preset: every knob is yours, and BUILD does nothing.";
}

void BuildAppProcessor::selectPreset (int i, bool picked)
{
    if (picked) presetPicked (i);
    user.touch (slotOf (i));
    auto& p = param (ids::preset);
    p.beginChangeGesture();
    p.setValueNotifyingHost (p.convertTo0to1 ((float) i));
    p.endChangeGesture();
}

float BuildAppProcessor::buildAmount() const noexcept { return juce::jlimit (0.f, 1.f, raw (Raw::build) / 100.f); }

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
    chain.set (audioSettings());
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
    chain.set (audioSettings());
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
    auto state = apvts.copyState();
    state.setProperty (kIntensitiesOf, intensitiesOf.load(), nullptr);
    user.saveInto (state);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void BuildAppProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            intensitiesOf = xml->getIntAttribute (kIntensitiesOf, 0);
            user.loadFrom (state);
            apvts.replaceState (state);
            // A project saved with fewer factory presets: its user slots were
            // right after them, so they move up with the presets appended since.
            const int saved = UserPresets::factoryCountIn (state, kFactory);
            if (const int i = presetIndex(); saved != kFactory && i > saved)
            {
                auto& p = param (ids::preset);
                p.setValueNotifyingHost (p.convertTo0to1 ((float) (i - saved + kFactory)));
            }
            if (saved != kFactory && intensitiesOf > saved) intensitiesOf += kFactory - saved;
        }
}

juce::AudioProcessorEditor* BuildAppProcessor::createEditor() { return new BuildAppEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new BuildAppProcessor(); }
