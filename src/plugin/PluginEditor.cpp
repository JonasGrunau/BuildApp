#include "PluginEditor.h"

#include "fx/CutFilters.h"

#include <vector>

using namespace theme;

namespace
{
    const juce::String kDash (juce::CharPointer_UTF8 (" \xe2\x80\x94 "));
    const juce::String kDefaultHelp (juce::CharPointer_UTF8 (
        "At rest \xe2\x80\x94 low cut left, high cut right, everything else at zero \xe2\x80\x94 the signal passes untouched, bit for bit."));

    constexpr float kLeft = 24.f, kRight = 856.f, kGroupGap = 20.f;
    // Each section: a rule, its tag (13 high) kGap clear of it, its content
    // kGap clear of the tag, and kGap from the content to the next rule.
    constexpr float kGap = 12.f, kTagH = 13.f, kRuleH = 1.f;
    // A (under the 1.5 px masthead rule at 74): seven lanes of 22 px (room
    // for their intensity knobs) over the BUILD axis.
    constexpr float kBuildTagY = 87.f, kBuildY = kBuildTagY + kTagH + kGap, kBuildH = 7.f * 22.f + BuildTimeline::kAxis;
    constexpr float kBuildRuleY = kBuildY + kBuildH + kGap;                                              // 294
    constexpr float kBuildDisc = 120.f;                                                                  // the BUILD knob
    constexpr float kSpecTagY = kBuildRuleY + kRuleH + kGap, kSpecY = kSpecTagY + kTagH + kGap, kSpecH = 194.f;   // B
    constexpr float kRuleY = kSpecY + kSpecH + kGap, kTagY = kRuleY + kRuleH + kGap;                    // C..G
    constexpr float kDialY = kTagY + kTagH + kGap, kDialH = 108.f;
    constexpr float kFootRuleY = kDialY + kDialH + 14.f;
    static_assert (kBuildRuleY - (kBuildTagY + kTagH + kGap) == (float) BuildAppEditor::kFold, "folded, the rule sits kGap under the tag");
    static_assert (kFootRuleY == (float) BuildAppEditor::kHeight - 32.f, "the footer line sits 4 px over the footer");

    std::vector<TypeSelector::Choice> typeChoices (bool lowpass)
    {
        std::vector<TypeSelector::Choice> out;
        for (const auto& c : ba::characters (lowpass))
            out.push_back ({ c.name, c.help, c.build ? "Buildup" : "Breakdown" });
        return out;
    }

    std::vector<TypeSelector::Choice> presetChoices()
    {
        std::vector<TypeSelector::Choice> out { { "Off", "No preset: every knob is yours, and BUILD does nothing.", {} } };
        for (const auto& p : ba::kPresets)
            out.push_back ({ p.name, p.help, p.section });
        return out;
    }
} // namespace

float BuildAppEditor::cellWidth()
{
    int cells = 0;
    for (const auto& g : kGroups) cells += g.cells;
    return (kRight - kLeft - kGroupGap * (float) (std::size (kGroups) - 1)) / (float) cells;
}

float BuildAppEditor::groupX (int group)
{
    float x = kLeft;
    for (int i = 0; i < group; ++i) x += (float) kGroups[i].cells * cellWidth() + kGroupGap;
    return x;
}

BuildAppEditor::BuildAppEditor (BuildAppProcessor& p)
    : AudioProcessorEditor (p), proc (p),
      build (p.param (ids::build), "Build",
             "The buildup in one knob: it drives everything the preset took over, from rest (0 %) to the top of the build. Back to 0 is the drop."),
      lowCut (p.param (ids::lowCut), "Low cut", "Highpass, 20 Hz to 8 kHz, in the low cut type picked above the spectrum. All the way left: off."),
      lowReso (p.param (ids::lowCutReso), "Reso", "Low cut resonance. AUTO: it moves with the cutoff the way the low cut type wants; switch AUTO off to set it yourself."),
      highCut (p.param (ids::highCut), "High cut", "Lowpass, 20 kHz down to 100 Hz, in the high cut type picked above the spectrum. All the way right: off."),
      highReso (p.param (ids::highCutReso), "Reso", "High cut resonance. AUTO: it moves with the cutoff the way the high cut type wants; switch AUTO off to set it yourself."),
      pitch (p.param (ids::pitch), "Pitch", "Shifts up to +12 semitones, continuously; splices land in phase with the material."),
      noise (p.param (ids::noise), "Volume", "White noise on top of the signal, after the cuts, wide stereo. All the way left: off."),
      noisePitch (p.param (ids::noisePitch), "Pitch", "Where the noise band sits, 80 Hz to 15 kHz; its resonant top edge gives it a pitch."),
      noiseLowCut (p.param (ids::noiseLowCut), "Low cut", "Highpass on the noise only, 24 dB/oct, 20 Hz to 8 kHz; 200 Hz keeps it off the kick and the bass. All the way left: off."),
      reverb (p.param (ids::reverb), "Reverb", "More send, a longer tail (0.8 to 12 s), less dry, until the loop washes out. Back to zero cuts the tail."),
      stereo (p.param (ids::stereo), "Stereo", "100 % is the full image; turning it up narrows it towards mono."),
      level (p.param (ids::level), "Level", juce::String (juce::CharPointer_UTF8 ("Output gain, \xe2\x88\x92" "36 to +12 dB."))),
      mix (p.param (ids::mix), "Dry/Wet", "The untouched input against the whole effect."),
      preset (p.param (ids::preset), "Preset", presetChoices()),
      lowCutType (p.param (ids::lowCutType), "Low cut", typeChoices (false)),
      highCutType (p.param (ids::highCutType), "High cut", typeChoices (true)),
      lowAuto (p.param (ids::lowCutAuto), "Auto", "Auto resonance for the low cut: on, it rises as the cut climbs, shaped by its type; off, the Reso knob sets it."),
      highAuto (p.param (ids::highCutAuto), "Auto", "Auto resonance for the high cut: on, it follows the sweep, shaped by its type; off, the Reso knob sets it.")
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (sheet);
    for (juce::Component* c : std::initializer_list<juce::Component*> { &inMeter, &outMeter, &timeline, &build, &preset, &spectrum,
                                                                       &lowCut, &lowReso, &highCut, &highReso,
                                                                       &pitch, &noise, &noisePitch, &noiseLowCut, &reverb, &stereo, &level, &mix,
                                                                       &lowCutType, &highCutType, &lowAuto, &highAuto })
        sheet.addAndMakeVisible (c);

    highCut.setRing (Dial::Ring::build, true);
    noisePitch.setRing (Dial::Ring::position);
    level.setRing (Dial::Ring::bipolar);
    mix.setRing (Dial::Ring::plain);
    build.setMaxDisc (kBuildDisc);
    build.onHover = [this] (Dial* d, bool on) { showHelp (on ? d->getHelp() : juce::String()); };
    for (auto* d : { &lowCut, &lowReso, &highCut, &highReso, &pitch, &noise, &noisePitch, &noiseLowCut, &reverb, &stereo, &level, &mix })
        d->onHover = [this, d] (Dial*, bool on) { showHelp (! on ? juce::String() : d->isLocked() ? lockedHelp() : d->getHelp()); };
    for (auto* a : { &lowAuto, &highAuto })
        a->onHover = [this] (Switch* sw, bool on) { showHelp (! on ? juce::String() : sw->isDriven() ? lockedHelp() : sw->getHelp()); };
    // Taking the resonance over from auto starts where auto had it: no jump.
    lowAuto.beforeToggle = [this] (bool on) { if (! on) takeOverResonance (false); };
    highAuto.beforeToggle = [this] (bool on) { if (! on) takeOverResonance (true); };
    for (auto* t : { &lowCutType, &highCutType })
    {
        t->setSteppers (true);
        t->onHover = [this] (TypeSelector* sel, bool on) { showHelp (! on ? juce::String() : sel->isDriven() ? lockedHelp() : sel->getHelp()); };
    }
    preset.setSteppers (true);
    preset.onHover = [this] (TypeSelector* sel, bool on) { showHelp (on ? sel->getHelp() : juce::String()); };
    // One field width for the three menus, room for the longest name in any of
    // them; the preset's field starts where the low cut's does, right under it.
    const float field = std::max ({ preset.widestChoice(), lowCutType.widestChoice(), highCutType.widestChoice() });
    const float fieldX = std::max (preset.choiceX(), lowCutType.choiceX());
    preset.setField (fieldX, field);
    lowCutType.setField (fieldX, field);
    highCutType.setField (0.f, field);
    timeline.onHelp = [this] (const juce::String& line) { showHelp (line); };
    spectrum.onHover = [this] (bool on) {
        showHelp (on ? juce::String (juce::CharPointer_UTF8 ("Drag a marker to move that cut \xc2\xb7 drag it past the edge or double-click it to switch it off.")) : juce::String());
    };
    help = defaultHelp();

    setResizable (true, true);
    setResizeLimits (kWidth * 3 / 4, (kHeight - kFold) * 3 / 4, kWidth * 2, kHeight * 2);
    open = proc.activePreset() != nullptr;
    getConstrainer()->setFixedAspectRatio ((double) kWidth / sheetHeight());
    setSize (kWidth, sheetHeight());
    layout();
    tick();
    startTimerHz (30);
}

BuildAppEditor::~BuildAppEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void BuildAppEditor::resized()
{
    sheet.setBounds (0, 0, kWidth, sheetHeight());
    sheet.setTransform (juce::AffineTransform::scale ((float) getWidth() / kWidth));
}

void BuildAppEditor::setOpen (bool presetOn)
{
    if (presetOn == open) return;
    open = presetOn;
    const float scale = (float) getWidth() / kWidth;
    getConstrainer()->setFixedAspectRatio ((double) kWidth / sheetHeight());
    layout();
    setSize (getWidth(), juce::roundToInt ((float) sheetHeight() * scale));
    sheet.repaint();
}

void BuildAppEditor::layout()
{
    inMeter.setBounds (560, 24, 136, 26);
    outMeter.setBounds (720, 24, 136, 26);
    preset.setBounds (150, (int) kBuildTagY - 2, preset.idealWidth(), 17);
    timeline.setBounds (24, (int) kBuildY, 680, (int) kBuildH);
    // Its value on the timeline's axis line (the disc, 4 px, the name's 13 px, then the value).
    const float buildTop = kBuildY + kBuildH - BuildTimeline::kAxis + 4.f - (kBuildDisc + 17.f);
    build.setBounds (728, (int) buildTop, 128, (int) kBuildDisc + 36);
    timeline.setVisible (open);
    build.setVisible (open);

    // Everything under the build band moves up when it folds.
    const float dy = fold();
    spectrum.setBounds (24, (int) (kSpecY - dy), 832, (int) kSpecH);
    lowCutType.setBounds (150, (int) (kSpecTagY - dy) - 2, lowCutType.idealWidth(), 17);
    highCutType.setBounds (384, (int) (kSpecTagY - dy) - 2, highCutType.idealWidth(), 17);

    Dial* dials[] = { &lowCut, &lowReso, &highCut, &highReso, &pitch, &noise, &noisePitch, &noiseLowCut, &reverb, &stereo, &level, &mix };
    int d = 0;
    const float cw = cellWidth();
    for (int gi = 0; gi < (int) std::size (kGroups); ++gi)
        for (int c = 0; c < kGroups[gi].cells; ++c)
            dials[d++]->setBounds (juce::Rectangle<float> (groupX (gi) + (float) c * cw, kDialY - dy, cw, kDialH).toNearestInt());
    // The auto switches under their resonance knobs.
    lowAuto.setBounds (lowReso.getBounds().withY ((int) (kDialY - dy + kDialH) - 16).withHeight (16));
    highAuto.setBounds (highReso.getBounds().withY ((int) (kDialY - dy + kDialH) - 16).withHeight (16));
}

void BuildAppEditor::setParam (const char* id, float plain)
{
    auto& prm = proc.param (id);
    prm.beginChangeGesture();
    prm.setValueNotifyingHost (prm.convertTo0to1 (plain));
    prm.endChangeGesture();
}

// Every control shows what the chain runs with: auto resonance where auto
// has it, and what the preset drives locked at the preset's value.
void BuildAppEditor::showDriven()
{
    const auto s = proc.settings();
    const auto* p = proc.activePreset();
    const auto drives = [p, build = proc.buildAmount()] (ba::Control c) { return p != nullptr && ba::drives (*p, c, build); };
    const auto lock = [&] (Dial& d, ba::Control c, float plain) { d.setLocked (drives (c) ? std::optional<float> (plain) : std::nullopt); };
    using C = ba::Control;

    for (const bool lowpass : { false, true })
    {
        const auto k = lowpass ? s.highCutKnobs() : s.lowCutKnobs();
        const auto c = lowpass ? C::highCut : C::lowCut;
        auto& reso = lowpass ? highReso : lowReso;
        const auto autoValue = k.autoReso ? std::optional<float> ((float) (100.0 * ba::CutFilters::resonance (lowpass, k))) : std::nullopt;
        reso.setAutoValue (autoValue);
        lock (reso, c, autoValue.value_or (100.f * k.reso));
        lock (lowpass ? highCut : lowCut, c, k.position);
        (lowpass ? highCutType : lowCutType).setDriven (drives (c) ? std::optional<int> (k.type) : std::nullopt);
        (lowpass ? highAuto : lowAuto).setDriven (drives (c) ? std::optional<bool> (k.autoReso) : std::nullopt);
    }
    lock (pitch, C::pitch, s.pitch);
    lock (noise, C::noise, s.noise);
    lock (noisePitch, C::noisePitch, s.noisePitch);
    lock (noiseLowCut, C::noiseLowCut, s.noiseLowCut);
    lock (reverb, C::reverb, s.reverb);
    lock (stereo, C::stereo, s.stereo);
    setOpen (p != nullptr);

    if (helpIsDefault && help != defaultHelp())
    {
        help = defaultHelp();
        sheet.repaint (footerArea());
    }
}

void BuildAppEditor::takeOverResonance (bool lowpass)
{
    const auto s = proc.settings();
    if (! (lowpass ? s.highCutAuto : s.lowCutAuto)) return;
    const double reso = ba::CutFilters::resonance (lowpass, lowpass ? s.highCutKnobs() : s.lowCutKnobs());
    setParam (lowpass ? ids::highCutReso : ids::lowCutReso, (float) (100.0 * reso));
    setParam (lowpass ? ids::highCutAuto : ids::lowCutAuto, 0.f);
    (lowpass ? highReso : lowReso).setAutoValue (std::nullopt);
}

void BuildAppEditor::showHelp (const juce::String& line)
{
    helpIsDefault = line.isEmpty();
    const auto next = helpIsDefault ? defaultHelp() : line;
    if (next == help) return;
    help = next;
    sheet.repaint (footerArea());
}

juce::String BuildAppEditor::defaultHelp() const
{
    if (const auto* p = proc.activePreset()) return juce::String (p->name) + theme::dot + p->help;
    return kDefaultHelp;
}

juce::String BuildAppEditor::lockedHelp() const
{
    const auto* p = proc.activePreset();
    return "Driven by BUILD in " + juce::String (p != nullptr ? p->name : "the preset") + kDash + "pick Off in the preset menu to take it back.";
}

void BuildAppEditor::Sheet::paint (juce::Graphics& g)
{
    auto& ed = editor;
    g.fillAll (col::sheet);
    cropMarks (g, getLocalBounds().toFloat().reduced (8.f), 7.f);

    // Wordmark.
    g.setFont (title (27.f));
    const juce::String a ("BUILD"), slash ("/"), b ("APP");
    float x = kLeft;
    for (auto [s, c] : { std::pair { a, col::ink }, { slash, col::accent }, { b, col::ink } })
    {
        g.setColour (c);
        const float w = textWidth (g.getCurrentFont(), s);
        g.drawText (s, juce::Rectangle<float> (x, 14.f, w + 2.f, 30.f), juce::Justification::centredLeft, false);
        x += w + (s == slash ? 1.f : 0.f);
    }
    g.setFont (mono (9.5f));
    g.setColour (col::inkSoft);
    g.drawText (juce::String (juce::CharPointer_UTF8 ("PSYTRANCE BUILDUP \xc2\xb7 BA-1 \xc2\xb7 v")) + JucePlugin_VersionString,
                juce::Rectangle<float> (kLeft, 46.f, 330.f, 14.f), juce::Justification::centredLeft, false);

    g.setColour (col::inkFaint);
    g.fillRect (kLeft, 74.f, kRight - kLeft, 1.5f);

    // Section A: build (folded to its tag and menu without a preset; all
    // below moves up by dy).
    const float dy = ed.fold();
    sectionTag (g, "A", "Build", { kLeft, kBuildTagY });
    g.setColour (col::hairline);
    g.fillRect (kLeft, kBuildRuleY - dy, kRight - kLeft, 1.f);

    // Section B and its legend.
    sectionTag (g, "B", "Spectrum", { kLeft, kSpecTagY - dy });
    {
        float lx = kRight;
        const auto item = [&] (const juce::String& name, const std::function<void (juce::Rectangle<float>)>& swatch) {
            g.setFont (label (9.f));
            const float w = textWidth (g.getCurrentFont(), name.toUpperCase());
            lx -= w;
            caption (g, name, { lx, kSpecTagY - dy, w + 2.f, 13.f }, juce::Justification::centredLeft, col::inkSoft, 9.f);
            lx -= 22.f;
            swatch ({ lx, kSpecTagY - dy + 6.f, 16.f, 1.f });
            lx -= 18.f;
        };
        item ("Cuts", [&] (auto r) { g.setColour (col::accent); g.fillRect (r.withHeight (2.f).translated (0.f, -0.5f)); });
        item ("Noise", [&] (auto r) { g.setColour (col::ink); for (float dx = 0.f; dx < 16.f; dx += 4.f) g.fillRect (r.getX() + dx, r.getY() - 0.5f, 1.5f, 1.5f); });
        item ("Input", [&] (auto r) { g.setColour (col::inkFaint); g.fillRect (r); });
        item ("Output", [&] (auto r) {
            g.setColour (col::ink.withAlpha (0.09f));
            g.fillRect (r.withTop (r.getY() - 3.f).withHeight (7.f));
            g.setColour (col::ink.withAlpha (0.62f));
            g.fillRect (r.withY (r.getY() - 3.f).withHeight (1.2f));
        });
    }

    // Knob groups.
    g.setColour (col::hairline);
    g.fillRect (kLeft, kRuleY - dy, kRight - kLeft, 1.f);
    for (int gi = 0; gi < (int) std::size (kGroups); ++gi)
    {
        sectionTag (g, kGroups[gi].letter, kGroups[gi].name, { groupX (gi), kTagY - dy });
        if (gi > 0)
        {
            g.setColour (col::hairline);
            g.fillRect (groupX (gi) - kGroupGap * 0.5f, kTagY - dy, 1.f, kDialY + kDialH - kTagY - 4.f);
        }
    }

    // Footer.
    g.setColour (col::hairline);
    g.fillRect (kLeft, kFootRuleY - dy, kRight - kLeft, 1.f);
    const auto foot = ed.footerArea().toFloat();
    g.setFont (mono (10.5f));
    g.setColour (col::ink);
    g.drawText (ed.help, foot.withTrimmedRight (190.f), juce::Justification::centredLeft, true);
    g.setFont (mono (9.5f));
    g.setColour (col::inkFaint);
    g.drawText (ed.rateText, foot, juce::Justification::centredRight, false);
}

void BuildAppEditor::tick()
{
    inMeter.push (proc.takePeak (false));
    outMeter.push (proc.takePeak (true));
    spectrum.refresh();
    timeline.refresh();
    showDriven();

    const double fs = proc.getRate();
    // Live the cuts run at the host rate; offline renders oversample them.
    const int hq = ba::CutFilters::oversamplingFor (fs);
    const auto r = fmt::fixed (fs / 1000.0, std::abs (fs - std::round (fs / 1000.0) * 1000.0) > 1.0 ? 1 : 0) + " kHz" + theme::dot
                 + (hq == 1 ? juce::String ("CUTS AT FULL RATE")
                            : "CUTS " + juce::String (hq) + juce::String (juce::CharPointer_UTF8 ("\xc3\x97"))
                                  + (proc.isNonRealtime() ? " (RENDERING)" : " ON EXPORT"));
    if (r != rateText)
    {
        rateText = r;
        sheet.repaint (footerArea());
    }
}
