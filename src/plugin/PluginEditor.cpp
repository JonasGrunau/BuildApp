#include "PluginEditor.h"

#include "fx/CutFilters.h"

using namespace theme;

namespace
{
    const juce::String kDash (juce::CharPointer_UTF8 (" \xe2\x80\x94 "));
    const juce::String kDefaultHelp (juce::CharPointer_UTF8 (
        "At rest \xe2\x80\x94 low cut left, high cut right, everything else at zero \xe2\x80\x94 the signal passes untouched, bit for bit."));

    constexpr float kLeft = 24.f, kRight = 856.f, kGroupGap = 20.f;
    constexpr float kRuleY = 348.f, kTagY = 360.f, kDialY = 382.f, kDialH = 112.f;
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
      lowCut (p.param (ids::lowCut), "Low cut", "24 dB/oct highpass with a +3 dB corner, 20 Hz to 8 kHz. All the way left: off."),
      highCut (p.param (ids::highCut), "High cut", "24 dB/oct lowpass with a +3 dB corner, 20 kHz down to 100 Hz. All the way right: off."),
      pitch (p.param (ids::pitch), "Pitch", "Shifts up to +12 semitones, continuously; splices land in phase with the material."),
      noise (p.param (ids::noise), "Volume", "White noise on top of the signal, after the cuts, wide stereo. All the way left: off."),
      noisePitch (p.param (ids::noisePitch), "Pitch", "Where the noise band sits, 80 Hz to 15 kHz; its resonant top edge gives it a pitch."),
      reverb (p.param (ids::reverb), "Reverb", "More send, a longer tail (0.8 to 12 s), less dry, until the loop washes out. Back to zero cuts the tail."),
      stereo (p.param (ids::stereo), "Stereo", "100 % is the full image; turning it up narrows it towards mono."),
      level (p.param (ids::level), "Level", juce::String (juce::CharPointer_UTF8 ("Output gain, \xe2\x88\x92" "36 to +12 dB."))),
      mix (p.param (ids::mix), "Dry/Wet", "The untouched input against the whole effect.")
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (sheet);
    for (juce::Component* c : std::initializer_list<juce::Component*> { &inMeter, &outMeter, &spectrum, &lowCut, &highCut, &pitch, &noise,
                                                                       &noisePitch, &reverb, &stereo, &level, &mix })
        sheet.addAndMakeVisible (c);

    highCut.setRing (Dial::Ring::build, true);
    noisePitch.setRing (Dial::Ring::position);
    level.setRing (Dial::Ring::bipolar);
    mix.setRing (Dial::Ring::plain);
    for (auto* d : { &lowCut, &highCut, &pitch, &noise, &noisePitch, &reverb, &stereo, &level, &mix })
        d->onHover = [this, d] (Dial*, bool on) { showHelp (on ? d->getHelp() : juce::String()); };
    spectrum.onHover = [this] (bool on) {
        showHelp (on ? juce::String (juce::CharPointer_UTF8 ("Drag a marker to move that cut \xc2\xb7 drag it past the edge or double-click it to switch it off.")) : juce::String());
    };
    help = kDefaultHelp;

    setResizable (true, true);
    setResizeLimits (kWidth * 3 / 4, kHeight * 3 / 4, kWidth * 2, kHeight * 2);
    getConstrainer()->setFixedAspectRatio ((double) kWidth / kHeight);
    setSize (kWidth, kHeight);
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
    sheet.setBounds (0, 0, kWidth, kHeight);
    sheet.setTransform (juce::AffineTransform::scale ((float) getWidth() / kWidth));
}

void BuildAppEditor::layout()
{
    inMeter.setBounds (560, 24, 136, 26);
    outMeter.setBounds (720, 24, 136, 26);
    spectrum.setBounds (24, 106, 832, 230);

    Dial* dials[] = { &lowCut, &highCut, &pitch, &noise, &noisePitch, &reverb, &stereo, &level, &mix };
    int d = 0;
    const float cw = cellWidth();
    for (int gi = 0; gi < (int) std::size (kGroups); ++gi)
        for (int c = 0; c < kGroups[gi].cells; ++c)
            dials[d++]->setBounds (juce::Rectangle<float> (groupX (gi) + (float) c * cw, kDialY, cw, kDialH).toNearestInt());
}

void BuildAppEditor::showHelp (const juce::String& line)
{
    const auto next = line.isEmpty() ? kDefaultHelp : line;
    if (next == help) return;
    help = next;
    sheet.repaint (footerArea());
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

    // Section A and its legend.
    sectionTag (g, "A", "Spectrum", { kLeft, 86.f });
    {
        float lx = kRight;
        const auto item = [&] (const juce::String& name, const std::function<void (juce::Rectangle<float>)>& swatch) {
            g.setFont (label (9.f));
            const float w = textWidth (g.getCurrentFont(), name.toUpperCase());
            lx -= w;
            caption (g, name, { lx, 86.f, w + 2.f, 13.f }, juce::Justification::centredLeft, col::inkSoft, 9.f);
            lx -= 22.f;
            swatch ({ lx, 92.f, 16.f, 1.f });
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
    g.fillRect (kLeft, kRuleY, kRight - kLeft, 1.f);
    for (int gi = 0; gi < (int) std::size (kGroups); ++gi)
    {
        sectionTag (g, kGroups[gi].letter, kGroups[gi].name, { groupX (gi), kTagY });
        if (gi > 0)
        {
            g.setColour (col::hairline);
            g.fillRect (groupX (gi) - kGroupGap * 0.5f, kTagY, 1.f, kDialY + kDialH - kTagY - 4.f);
        }
    }

    // Footer.
    g.setColour (col::hairline);
    g.fillRect (kLeft, 508.f, kRight - kLeft, 1.f);
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

    const double fs = proc.getRate();
    const auto r = fmt::fixed (fs / 1000.0, std::abs (fs - std::round (fs / 1000.0) * 1000.0) > 1.0 ? 1 : 0) + " kHz" + theme::dot
                 + "CUTS " + juce::String (ba::CutFilters::oversamplingFor (fs)) + juce::String (juce::CharPointer_UTF8 ("\xc3\x97")) + " OVERSAMPLED";
    if (r != rateText)
    {
        rateText = r;
        sheet.repaint (footerArea());
    }
}
