#include "BuildTimeline.h"

#include "plugin/PluginProcessor.h"

using namespace theme;

namespace
{
    const juce::String kBandHelp (juce::CharPointer_UTF8 (
        "Where the preset takes each control as BUILD rises, the built part lit \xc2\xb7 drag to scrub BUILD, double-click to drop it to 0."));
}

BuildTimeline::BuildTimeline (BuildAppProcessor& p) : proc (p)
{
    setOpaque (true);
    // In Lane order.
    const std::pair<const char*, const char*> knobParams[kLanes] = {
        { ids::lowCutIntensity, "Low cut intensity" },   { ids::lowResoIntensity, "Low reso intensity" },
        { ids::highCutIntensity, "High cut intensity" }, { ids::highResoIntensity, "High reso intensity" },
        { ids::pitchIntensity, "Pitch intensity" },      { ids::noiseIntensity, "Noise intensity" },
        { ids::noisePitchIntensity, "Noise pitch intensity" }, { ids::noiseLowCutIntensity, "Noise cut intensity" },
        { ids::reverbIntensity, "Reverb intensity" },    { ids::stereoIntensity, "Stereo intensity" },
        { ids::levelIntensity, "Level intensity" },
    };
    for (size_t i = 0; i < kLanes; ++i)
    {
        knobs[i] = std::make_unique<SmallDial> (proc.param (knobParams[i].first), knobParams[i].second);
        knobs[i]->onHover = [this] (SmallDial* d, bool on) { if (onHelp) onHelp (on ? d->getHelp() : juce::String()); };
        addChildComponent (*knobs[i]);
    }
}

void BuildTimeline::mouseEnter (const juce::MouseEvent&) { if (onHelp) onHelp (kBandHelp); }
void BuildTimeline::mouseExit (const juce::MouseEvent&) { if (onHelp) onHelp ({}); }

// What turning a lane all the way down leaves, for the knob's help line.
juce::String BuildTimeline::knobHelp (Lane lane, const ba::Preset& p) const
{
    const auto byDefault = [this] (const char* id) { auto& prm = proc.param (id); return prm.getText (prm.getDefaultValue(), 0); };
    juce::String zero;
    switch (lane)
    {
        case lowCut:      zero = "the low cut stays off"; break;
        case highCut:     zero = "the high cut stays off"; break;
        case lowReso:
        case highReso:    zero = "auto resonance"; break;
        case pitch:       zero = "no shift"; break;
        case noise:       zero = "no noise"; break;
        case noisePitch:  zero = "the band sits at " + byDefault (ids::noisePitch); break;
        case noiseLowCut: zero = "the noise cut stays at " + byDefault (ids::noiseLowCut); break;
        case reverb:      zero = p.reverb.driven() ? "no reverb, no tail" : "no tail, just your reverb knob"; break;
        case stereo:      zero = "the full image"; break;
        case level:       zero = "no makeup gain, no fade"; break;
        case kLanes:      break;
    }
    return "How much of this lane you hear; 0 %: " + zero + ".";
}

std::vector<BuildTimeline::Row> BuildTimeline::rows (const ba::Preset& p) const
{
    // The knob's own text for a value: through its parameter, so it reads
    // exactly like the knob.
    const auto words = [this] (const char* id, float scale = 1.f) {
        return [this, id, scale] (float v) {
            auto& prm = proc.param (id);
            return prm.getText (prm.convertTo0to1 (v * scale), 0);
        };
    };
    const auto travel = [] (float v) { return v; };
    std::vector<Row> out;
    const auto add = [&] (bool shown, Lane lane, const char* label, std::function<float (const ba::Settings&)> value,
                          std::function<float (float)> height, std::function<juce::String (float)> text, bool bipolar = false) {
        if (shown) out.push_back ({ lane, label, std::move (value), std::move (height), std::move (text), bipolar });
    };
    add (p.lowCut.driven(), lowCut, "Low cut", [] (const ba::Settings& s) { return s.lowCut; }, travel, words (ids::lowCut));
    add (p.lowReso.driven(), lowReso, "Low reso", [] (const ba::Settings& s) { return s.lowCutReso; }, travel, words (ids::lowCutReso, 100.f));
    add (p.highCut.driven(), highCut, "High cut", [] (const ba::Settings& s) { return s.highCut; }, [] (float v) { return 1.f - v; }, words (ids::highCut));
    add (p.highReso.driven(), highReso, "High reso", [] (const ba::Settings& s) { return s.highCutReso; }, travel, words (ids::highCutReso, 100.f));
    add (p.pitch.driven(), pitch, "Pitch", [] (const ba::Settings& s) { return s.pitch; },
         [] (float v) { return v / (float) ba::map::kMaxPitch; }, words (ids::pitch));
    add (p.noise.driven(), noise, "Noise", [] (const ba::Settings& s) { return s.noise; }, travel, words (ids::noise));
    add (p.noisePitch.driven(), noisePitch, "Noise pitch", [] (const ba::Settings& s) { return s.noisePitch; }, travel, words (ids::noisePitch));
    add (p.noiseLowCut.driven(), noiseLowCut, "Noise cut", [] (const ba::Settings& s) { return s.noiseLowCut; }, travel, words (ids::noiseLowCut));
    add (true, reverb, "Reverb", [] (const ba::Settings& s) { return s.reverb; }, travel, words (ids::reverb));
    add (p.stereo.driven(), stereo, "Stereo", [] (const ba::Settings& s) { return s.stereo; }, travel, words (ids::stereo));

    // Level: the makeup gain and the fade into the tail together, in dB.
    add (true, level, "Level", [] (const ba::Settings& s) { return s.makeupDb + s.fadeDb; },
         [] (float db) { return 0.5f + db / 12.f; }, [] (float db) { return fmt::signedNumber (db, 1) + " dB"; }, true);
    return out;
}

juce::Rectangle<float> BuildTimeline::plot() const
{
    return getLocalBounds().toFloat().withTrimmedLeft (kLabelW).withTrimmedRight (kValueW + kKnobW + kGap).withTrimmedBottom (kAxis);
}

float BuildTimeline::xFor (float build) const
{
    const auto p = plot();
    return p.getX() + p.getWidth() * juce::jlimit (0.f, 1.f, build);
}

void BuildTimeline::placeKnobs()
{
    std::array<bool, kLanes> shown {};
    if (const auto* preset = proc.activePreset())
    {
        const auto list = rows (*preset);
        const auto p = plot();
        const float laneH = p.getHeight() / (float) std::max<size_t> (1, list.size());
        for (size_t i = 0; i < list.size(); ++i)
        {
            auto& knob = *knobs[(size_t) list[i].lane];
            const juce::Point<float> centre (p.getRight() + kGap + kKnobW * 0.5f, p.getY() + laneH * ((float) i + 0.5f));
            knob.setBounds (juce::Rectangle<float> (kKnobW, kKnobW).withCentre (centre).toNearestInt());
            knob.setHelp (knobHelp (list[i].lane, *preset));
            shown[(size_t) list[i].lane] = true;
        }
    }
    for (size_t i = 0; i < kLanes; ++i) knobs[i]->setVisible (shown[i]);
}

void BuildTimeline::refresh()
{
    const auto* preset = proc.activePreset();
    const float build = proc.buildAmount(), reverb = proc.ownSettings().reverb;   // a preset without a reverb lane tails from yours
    const auto intensity = proc.intensity();
    const bool sameIntensity = std::all_of (std::begin (kIntensityOf), std::end (kIntensityOf),
                                            [&] (auto f) { return std::abs (intensity.*f - shownIntensity.*f) < 1e-6f; });
    const auto gen = proc.user.generation();   // a user preset edited in place: same pointer, other lanes
    if (preset == shownPreset && gen == shownGen && std::abs (build - shownBuild) < 1e-6f && reverb == shownReverb && sameIntensity) return;
    if (preset != shownPreset || gen != shownGen)
    {
        shownGen = gen;
        setMouseCursor (preset != nullptr ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
        shownPreset = preset;
        placeKnobs();
    }
    shownBuild = build;
    shownReverb = reverb;
    shownIntensity = intensity;
    repaint();
}

void BuildTimeline::paint (juce::Graphics& g)
{
    g.fillAll (col::sheet);
    const auto* preset = proc.activePreset();
    const auto p = plot();
    if (preset == nullptr) return;   // the band is folded away

    const float build = proc.buildAmount(), playX = xFor (build);
    const auto list = rows (*preset);
    const float laneH = p.getHeight() / (float) std::max<size_t> (1, list.size());
    // What the chain runs with along BUILD, sampled once for every lane: as
    // the intensities have it, at full intensity, and at none (where each
    // lane rests).
    constexpr int kSteps = 240;
    const auto own = proc.ownSettings();
    const auto intensity = proc.intensity();
    const auto sample = [&] (const ba::Intensity& k) {
        std::vector<ba::Settings> out ((size_t) kSteps + 1);
        for (int i = 0; i <= kSteps; ++i) out[(size_t) i] = ba::applyPreset (*preset, (float) i / kSteps, own, k);
        return out;
    };
    const auto full = sample ({}), none = sample (ba::Intensity::none());
    const auto along = intensity.asDesigned() ? full : sample (intensity);
    const auto now = ba::applyPreset (*preset, build, own, intensity), restNow = ba::applyPreset (*preset, build, own, ba::Intensity::none());

    for (size_t i = 0; i < list.size(); ++i)
    {
        const auto& row = list[i];
        const auto valueAt = [&] (int k) { return row.value (along[(size_t) k]); };
        const auto lane = juce::Rectangle<float> (p.getX(), p.getY() + laneH * (float) i, p.getWidth(), laneH);
        const float top = lane.getY() + 3.f, span = lane.getHeight() - 5.f;

        // Each lane spans its own range, from where it rests to the preset's
        // full path, so when it moves reads at any size, and turning its
        // intensity down flattens it towards rest; how far is in the value
        // on the right.
        float lo = 1e9f, hi = -1e9f;
        for (const auto* path : { &full, &none, &along })
            for (const auto& s : *path)
            {
                const float h = row.height (row.value (s));
                lo = std::min (lo, h);
                hi = std::max (hi, h);
            }
        if (hi - lo < 1e-4f) { lo -= 0.5f; hi += 0.5f; }   // held still: a line through the middle
        const auto yFor = [&] (float v) { return top + span * (1.f - (row.height (v) - lo) / (hi - lo)); };
        const auto pathOf = [&] (const std::vector<ba::Settings>& path) {
            juce::Path out;
            for (int k = 0; k <= kSteps; ++k)
            {
                const float x = xFor ((float) k / kSteps), y = yFor (row.value (path[(size_t) k]));
                if (k == 0) out.startNewSubPath (x, y);
                else out.lineTo (x, y);
            }
            return out;
        };

        // Rest: where the lane goes at intensity 0. A level line (the bottom,
        // 0 dB, a default), or auto resonance following the cut.
        g.setColour (col::hairline);
        const float rest0 = row.value (none.front());
        if (std::all_of (none.begin(), none.end(), [&] (const ba::Settings& s) { return std::abs (row.value (s) - rest0) < 1e-6f; }))
            g.fillRect (lane.getX(), yFor (rest0) - 0.5f, lane.getWidth(), 1.f);
        else
            g.strokePath (pathOf (none), juce::PathStrokeType (1.f));

        // Turned down: the preset's full path stays behind it, dashed.
        if (intensity.*kIntensityOf[(size_t) row.lane] < 1.f)
        {
            juce::Path dashed;
            const float dashes[] = { 2.f, 3.f };
            juce::PathStrokeType (1.f).createDashedStroke (dashed, pathOf (full), dashes, 2);
            g.setColour (col::inkFaint.withAlpha (0.7f));
            g.fillPath (dashed);
        }

        // The whole path faint; lit in the signal colour (and filled down to
        // rest) from where it leaves its start up to the playhead: at rest it
        // steps back like a knob at rest does.
        const float startH = row.height (valueAt (0));
        const auto moved = [&] (float v) { return std::abs (row.height (v) - startH) > 1e-5f; };
        float leaves = 2.f;
        for (int k = 1; k <= kSteps && leaves > 1.f; ++k)
            if (moved (valueAt (k))) leaves = (float) (k - 1) / kSteps;

        juce::Path built, fill;
        if (build > leaves)
        {
            const int from = (int) std::floor (leaves * kSteps);
            const auto at = [&] (int k) { return juce::Point<float> (xFor ((float) k / kSteps), yFor (valueAt (k))); };
            const auto restAt = [&] (int k) { return juce::Point<float> (xFor ((float) k / kSteps), yFor (row.value (none[(size_t) k]))); };
            const auto last = juce::Point<float> (playX, yFor (row.value (now)));
            built.startNewSubPath (at (from));
            fill.startNewSubPath (at (from));
            int to = from;
            for (int k = from + 1; k <= kSteps && (float) k / kSteps < build; ++k)
            {
                built.lineTo (at (k));
                fill.lineTo (at (k));
                to = k;
            }
            built.lineTo (last);
            fill.lineTo (last);
            fill.lineTo (playX, yFor (row.value (restNow)));
            for (int k = to; k >= from; --k) fill.lineTo (restAt (k));
            fill.closeSubPath();
            g.setColour (col::accent.withAlpha (0.12f));
            g.fillPath (fill);
        }
        g.setColour (col::inkFaint);
        g.strokePath (pathOf (along), juce::PathStrokeType (1.2f));
        g.setColour (col::accent);
        g.strokePath (built, juce::PathStrokeType (1.6f));

        // Name on the left, value at BUILD on the right.
        caption (g, row.label, { 0.f, lane.getY(), kLabelW - 8.f, laneH }, juce::Justification::centredLeft, col::inkSoft, 9.f);
        g.setColour (moved (row.value (now)) ? col::ink : col::inkFaint);
        g.setFont (mono (10.f));
        g.drawText (row.text (row.value (now)), juce::Rectangle<float> ((float) getWidth() - kValueW, lane.getY(), kValueW, laneH),
                    juce::Justification::centredRight, false);
    }

    // Playhead and the BUILD axis.
    g.setColour (col::accent);
    g.fillRect (playX - 0.5f, p.getY(), 1.f, p.getHeight() + 4.f);
    g.setFont (mono (9.f));
    for (int q = 0; q <= 4; ++q)
    {
        const float x = xFor ((float) q / 4.f);
        g.setColour (col::inkFaint);
        g.fillRect (x - 0.5f, p.getBottom() + 1.f, 1.f, 3.f);
        const auto t = q == 4 ? juce::String ("100 %") : juce::String (q * 25);
        const auto j = q == 0 ? juce::Justification::centredLeft : q == 4 ? juce::Justification::centredRight : juce::Justification::centred;
        const auto r = q == 0 ? juce::Rectangle<float> (x, p.getBottom() + 4.f, 40.f, 12.f)
                     : q == 4 ? juce::Rectangle<float> (x - 40.f, p.getBottom() + 4.f, 40.f, 12.f)
                              : juce::Rectangle<float> (x - 20.f, p.getBottom() + 4.f, 40.f, 12.f);
        g.drawText (t, r, j, false);
    }
}

void BuildTimeline::scrub (float x)
{
    const auto p = plot();
    auto& prm = proc.param (ids::build);
    prm.setValueNotifyingHost (juce::jlimit (0.f, 1.f, (x - p.getX()) / p.getWidth()));   // 0..100 % is 0..1 normalised
}

void BuildTimeline::mouseDown (const juce::MouseEvent& e)
{
    if (proc.activePreset() == nullptr) return;
    dragging = true;
    proc.param (ids::build).beginChangeGesture();
    scrub (e.position.x);
}

void BuildTimeline::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging) scrub (e.position.x);
}

void BuildTimeline::mouseUp (const juce::MouseEvent&)
{
    if (! dragging) return;
    dragging = false;
    proc.param (ids::build).endChangeGesture();
}

void BuildTimeline::mouseDoubleClick (const juce::MouseEvent&)
{
    if (proc.activePreset() == nullptr) return;
    auto& prm = proc.param (ids::build);
    prm.beginChangeGesture();
    prm.setValueNotifyingHost (0.f);
    prm.endChangeGesture();
}
