#include "BuildTimeline.h"

#include "plugin/PluginProcessor.h"

using namespace theme;

BuildTimeline::BuildTimeline (BuildAppProcessor& p) : proc (p)
{
    setOpaque (true);
}

std::vector<BuildTimeline::Row> BuildTimeline::rows (const ba::Preset& p) const
{
    // The knob's own text for a lane value: through its parameter, so it
    // reads exactly like the knob.
    const auto words = [this] (const char* id, float scale = 1.f) {
        return [this, id, scale] (float v) {
            auto& prm = proc.param (id);
            return prm.getText (prm.convertTo0to1 (v * scale), 0);
        };
    };
    const auto travel = [] (float v) { return v; };
    std::vector<Row> out;
    const auto add = [&] (const char* label, const ba::Lane& lane, std::function<float (float)> height,
                          std::function<juce::String (float)> text, bool bipolar = false) {
        if (lane.driven()) out.push_back ({ label, lane, std::move (height), std::move (text), bipolar });
    };
    add ("Low cut", p.lowCut, travel, words (ids::lowCut));
    add ("Low reso", p.lowReso, travel, words (ids::lowCutReso, 100.f));
    add ("High cut", p.highCut, [] (float v) { return 1.f - v; }, words (ids::highCut));
    add ("High reso", p.highReso, travel, words (ids::highCutReso, 100.f));
    add ("Pitch", p.pitch, [] (float v) { return v / (float) ba::map::kMaxPitch; }, words (ids::pitch));
    add ("Noise", p.noise, travel, words (ids::noise));
    add ("Noise pitch", p.noisePitch, travel, words (ids::noisePitch));
    add ("Noise cut", p.noiseLowCut, travel, words (ids::noiseLowCut));
    add ("Reverb", p.reverb, travel, words (ids::reverb));
    add ("Stereo", p.stereo, travel, words (ids::stereo));
    add ("Gain", p.gain, [] (float db) { return 0.5f + db / 12.f; }, [] (float db) { return fmt::signedNumber (db, 1) + " dB"; }, true);
    return out;
}

juce::Rectangle<float> BuildTimeline::plot() const
{
    return getLocalBounds().toFloat().withTrimmedLeft (kLabelW).withTrimmedRight (kValueW + 10.f).withTrimmedBottom (kAxis);
}

float BuildTimeline::xFor (float build) const
{
    const auto p = plot();
    return p.getX() + p.getWidth() * juce::jlimit (0.f, 1.f, build);
}

void BuildTimeline::refresh()
{
    const auto* preset = proc.activePreset();
    const float build = proc.buildAmount();
    if (preset == shownPreset && std::abs (build - shownBuild) < 1e-6f) return;
    if (preset != shownPreset)
        setMouseCursor (preset != nullptr ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
    shownPreset = preset;
    shownBuild = build;
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
    constexpr int kSteps = 240;
    for (size_t i = 0; i < list.size(); ++i)
    {
        const auto& row = list[i];
        const auto lane = juce::Rectangle<float> (p.getX(), p.getY() + laneH * (float) i, p.getWidth(), laneH);
        const float top = lane.getY() + 3.f, span = lane.getHeight() - 5.f;

        // Each lane spans its own range (gain: one that includes 0 dB), so
        // when it moves reads at any size; how far is in the value on the right.
        float lo = row.bipolar ? row.height (0.f) : 1e9f, hi = row.bipolar ? lo : -1e9f;
        for (int k = 0; k <= kSteps; ++k)
        {
            const float h = row.height (row.lane ((float) k / kSteps));
            lo = std::min (lo, h);
            hi = std::max (hi, h);
        }
        if (hi - lo < 1e-4f) { lo -= 0.5f; hi += 0.5f; }   // held still: a line through the middle
        const auto yFor = [&] (float v) { return top + span * (1.f - (row.height (v) - lo) / (hi - lo)); };

        // Rest line: the bottom of the lane, or 0 dB for gain.
        g.setColour (col::hairline);
        g.fillRect (lane.getX(), row.bipolar ? yFor (0.f) : lane.getBottom() - 1.f, lane.getWidth(), 1.f);

        // The whole path faint; lit in the signal colour (and filled down to
        // rest) from where it leaves rest up to the playhead: at rest it
        // steps back like a knob at rest does.
        const float startH = row.height (row.lane (0.f));
        const auto moved = [&] (float b) { return std::abs (row.height (row.lane (b)) - startH) > 1e-5f; };
        float leaves = 2.f;
        for (int k = 1; k <= kSteps && leaves > 1.f; ++k)
            if (moved ((float) k / kSteps)) leaves = (float) (k - 1) / kSteps;

        juce::Path whole, built, fill;
        const float restY = row.bipolar ? yFor (0.f) : top + span;
        for (int k = 0; k <= kSteps; ++k)
        {
            const float b = (float) k / kSteps, x = xFor (b), y = yFor (row.lane (b));
            if (k == 0) whole.startNewSubPath (x, y);
            else whole.lineTo (x, y);
        }
        if (build > leaves)
        {
            const int from = (int) std::floor (leaves * kSteps);
            const auto at = [&] (float b) { return juce::Point<float> (xFor (b), yFor (row.lane (b))); };
            const auto first = at ((float) from / kSteps), last = at (build);
            built.startNewSubPath (first);
            fill.startNewSubPath (first.x, restY);
            fill.lineTo (first);
            for (int k = from + 1; k <= kSteps && (float) k / kSteps < build; ++k)
            {
                built.lineTo (at ((float) k / kSteps));
                fill.lineTo (at ((float) k / kSteps));
            }
            built.lineTo (last);
            fill.lineTo (last);
            fill.lineTo (last.x, restY);
            fill.closeSubPath();
            g.setColour (col::accent.withAlpha (0.12f));
            g.fillPath (fill);
        }
        g.setColour (col::inkFaint);
        g.strokePath (whole, juce::PathStrokeType (1.2f));
        g.setColour (col::accent);
        g.strokePath (built, juce::PathStrokeType (1.6f));

        // Name on the left, value at BUILD on the right.
        caption (g, row.label, { 0.f, lane.getY(), kLabelW - 8.f, laneH }, juce::Justification::centredLeft, col::inkSoft, 9.f);
        g.setColour (moved (build) ? col::ink : col::inkFaint);
        g.setFont (mono (10.f));
        g.drawText (row.text (row.lane (build)), juce::Rectangle<float> ((float) getWidth() - kValueW, lane.getY(), kValueW, laneH),
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
