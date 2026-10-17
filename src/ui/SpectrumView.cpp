#include "SpectrumView.h"

#include "fx/CutFilters.h"
#include "fx/Mapping.h"
#include "fx/NoiseSource.h"
#include "plugin/PluginProcessor.h"

using namespace theme;
namespace map = ba::map;

namespace
{
    constexpr double kLow = 20.0, kHigh = 20000.0;
    constexpr double kSpecTop = -12.0, kSpecBottom = -96.0;   // analyser dB (tilted)
    constexpr double kFilterTopShare = 0.18, kFilterRange = 48.0;   // 0 dB 18 % down, -48 dB at the bottom
    constexpr float kGrabRange = 10.f;
} // namespace

SpectrumView::SpectrumView (BuildAppProcessor& p) : proc (p)
{
    setOpaque (true);
    spectrum.prepare (p.getRate());
}

// ---- Geometry --------------------------------------------------------------------

float SpectrumView::xFor (double hz) const
{
    const auto p = plot();
    return p.getX() + p.getWidth() * (float) (std::log (hz / kLow) / std::log (kHigh / kLow));
}

double SpectrumView::hzFor (float x) const
{
    const auto p = plot();
    return kLow * std::pow (kHigh / kLow, juce::jlimit (0.0, 1.0, (double) ((x - p.getX()) / p.getWidth())));
}

float SpectrumView::ySpectrum (double db) const
{
    const auto p = plot();
    const double t = (db - kSpecTop) / (kSpecBottom - kSpecTop);
    return p.getY() + p.getHeight() * (float) juce::jlimit (-0.02, 1.02, t);
}

float SpectrumView::yFilter (double db) const
{
    const auto p = plot();
    const double t = kFilterTopShare + (1.0 - kFilterTopShare) * (-db / kFilterRange);
    return p.getY() + p.getHeight() * (float) juce::jlimit (0.0, 1.02, t);
}

float SpectrumView::handleX (Grab g) const
{
    const auto s = proc.settings();
    const auto p = plot();
    if (g == Grab::low) return map::lowCutOff (s.lowCut) ? p.getX() + 5.f : xFor (map::lowCutHz (s.lowCut));
    return map::highCutOff (s.highCut) ? p.getRight() - 5.f : xFor (map::highCutHz (s.highCut));
}

bool SpectrumView::locked (Grab g) const
{
    const auto* preset = proc.activePreset();
    return preset != nullptr && ba::drives (*preset, g == Grab::low ? ba::Control::lowCut : ba::Control::highCut);
}

SpectrumView::Grab SpectrumView::grabAt (juce::Point<float> pt) const
{
    if (! plot().expanded (0.f, 4.f).contains (pt)) return Grab::none;
    const float far = 1e6f;
    const float dl = locked (Grab::low) ? far : std::abs (pt.x - handleX (Grab::low));
    const float dh = locked (Grab::high) ? far : std::abs (pt.x - handleX (Grab::high));
    if (std::min (dl, dh) > kGrabRange) return Grab::none;
    return dl <= dh ? Grab::low : Grab::high;
}

juce::RangedAudioParameter& SpectrumView::paramFor (Grab g) const { return proc.param (g == Grab::low ? ids::lowCut : ids::highCut); }

// ---- Updates ---------------------------------------------------------------------

void SpectrumView::refresh()
{
    bool changed = false;
    if (std::abs (spectrum.sampleRate() - proc.getRate()) > 0.5)
    {
        spectrum.prepare (proc.getRate());
        changed = true;
    }

    const auto count = proc.outputTap().count();
    if (count != lastCount)
    {
        lastCount = count;
        quietTicks = 0;
        changed = spectrum.update (proc.inputTap(), proc.outputTap()) || changed;
    }
    else if (++quietTicks > 4)   // transport stopped: let it sink away
        changed = spectrum.fall (2.5f) || changed;

    const auto s = proc.settings();
    const std::array<float, 13> knobs { s.lowCut, s.highCut, s.noise, s.noisePitch, (float) s.lowCutType, (float) s.highCutType,
                                        s.lowCutReso, s.highCutReso, (float) s.lowCutAuto, (float) s.highCutAuto, s.noiseLowCut,
                                        (float) locked (Grab::low), (float) locked (Grab::high) };
    if (knobs != shownKnobs)
    {
        shownKnobs = knobs;
        changed = true;
    }
    if (changed) repaint();
}

// ---- Painting --------------------------------------------------------------------

void SpectrumView::paint (juce::Graphics& g)
{
    paintOffscreen (g, getLocalBounds(), buffer, [this] (juce::Graphics& off) { draw (off); });
}

void SpectrumView::draw (juce::Graphics& g)
{
    const auto p = plot();
    const auto s = proc.settings();
    const double fs = proc.getRate();
    g.fillAll (col::sheet);
    g.setColour (col::well);
    g.fillRect (p);

    // Grid: every 1-2-5 decade step, decades stronger; the filter scale every 12 dB.
    {
        juce::Path minor, major;
        for (double decade = 10.0; decade <= 10000.0; decade *= 10.0)
            for (int m = 1; m <= 9; ++m)
            {
                const double f = decade * m;
                if (f < kLow || f > kHigh) continue;
                const float x = std::round (xFor (f)) + 0.5f;
                auto& path = m == 1 ? major : minor;
                path.startNewSubPath (x, p.getY());
                path.lineTo (x, p.getBottom());
            }
        for (double db = -12.0; db >= -kFilterRange + 1.0; db -= 12.0)
        {
            const float y = std::round (yFilter (db)) + 0.5f;
            minor.startNewSubPath (p.getX(), y);
            minor.lineTo (p.getRight(), y);
        }
        g.setColour (col::grid);
        g.strokePath (minor, juce::PathStrokeType (1.f));
        g.setColour (col::hairline);
        g.strokePath (major, juce::PathStrokeType (1.f));
    }

    const auto& in = spectrum.input();
    const auto& out = spectrum.output();
    const auto trace = [&] (const std::vector<float>& db) {
        juce::Path path;
        for (int i = 0; i < ba::Spectrum::kPoints; ++i)
        {
            const juce::Point<float> pt (xFor (ba::Spectrum::pointHz (i)), ySpectrum (db[(size_t) i]));
            if (i == 0) path.startNewSubPath (pt);
            else path.lineTo (pt);
        }
        return path;
    };

    {
        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (p.toNearestInt());

        // Output, filled; input as a faint line over it, so what changed shows.
        auto outPath = trace (out);
        auto fill = outPath;
        fill.lineTo (p.getRight(), p.getBottom() + 2.f);
        fill.lineTo (p.getX(), p.getBottom() + 2.f);
        fill.closeSubPath();
        g.setColour (col::ink.withAlpha (0.09f));
        g.fillPath (fill);
        g.setColour (col::ink.withAlpha (0.62f));
        g.strokePath (outPath, juce::PathStrokeType (1.2f));
        g.setColour (col::inkFaint);
        g.strokePath (trace (in), juce::PathStrokeType (1.f));

        // Noise band: dotted, its top edge peaking where the noise pitch is.
        if (s.noise > map::kEnd)
        {
            const double top = map::noiseHz (s.noisePitch);
            juce::Path band;
            for (int i = 0; i <= 160; ++i)
            {
                const double f = kLow * std::pow (kHigh / kLow, i / 160.0);
                const double db = map::noiseDb (s.noise) - 24.0 + 20.0 * std::log10 (ba::NoiseSource::shape (f, s.noisePitch, s.noiseLowCut, fs) + 1e-9);
                const juce::Point<float> pt (xFor (f), ySpectrum (db));
                if (i == 0) band.startNewSubPath (pt);
                else band.lineTo (pt);
            }
            juce::Path dotted;
            const float dashes[] = { 1.5f, 3.f };
            juce::PathStrokeType (1.4f).createDashedStroke (dotted, band, dashes, 2);
            g.setColour (col::ink.withAlpha (0.8f));
            g.fillPath (dotted);
            const float px = xFor (top), py = ySpectrum (map::noiseDb (s.noise) - 24.0);
            caption (g, "Noise", { px - 40.f, py - 16.f, 80.f, 12.f }, juce::Justification::centred, col::ink, 9.f);
        }

        // The cuts: response on its own scale, what it takes away hatched.
        const bool lowOn = ! map::lowCutOff (s.lowCut), highOn = ! map::highCutOff (s.highCut);
        const float y0 = yFilter (0.0);
        juce::Path curve, cut;
        cut.startNewSubPath (p.getX(), y0);
        constexpr int kSteps = 220;
        std::array<juce::Point<float>, kSteps + 1> pts;
        for (int i = 0; i <= kSteps; ++i)
        {
            const double f = kLow * std::pow (kHigh / kLow, (double) i / kSteps);
            double db = 20.0 * std::log10 (ba::CutFilters::magnitude (f, s.lowCutKnobs(), s.highCutKnobs(), fs) + 1e-12);
            if (db > 4.0) db = 4.0 + 6.0 * std::tanh ((db - 4.0) / 6.0);   // tall resonance eases into the headroom
            const juce::Point<float> pt (xFor (f), std::min (yFilter (db), p.getBottom() + 2.f));
            pts[(size_t) i] = pt;
            if (i == 0) curve.startNewSubPath (pt);
            else curve.lineTo (pt);
            cut.lineTo (pt.x, std::max (pt.y, y0));
        }
        // The curve's height at x, from the points above.
        const auto curveY = [&] (float x) {
            const float t = juce::jlimit (0.f, (float) kSteps, (x - p.getX()) / p.getWidth() * (float) kSteps);
            const int i = std::min ((int) t, kSteps - 1);
            return pts[(size_t) i].y + (t - (float) i) * (pts[(size_t) i + 1].y - pts[(size_t) i].y);
        };
        cut.lineTo (p.getRight(), y0);
        cut.closeSubPath();
        if (lowOn || highOn)
        {
            juce::Graphics::ScopedSaveState inner (g);
            g.reduceClipRegion (cut);
            hatch (g, p, col::accent.withAlpha (0.13f), 5.f, 1.f);
        }
        g.setColour (lowOn || highOn ? col::accent : col::accentDim);
        g.strokePath (curve, juce::PathStrokeType (lowOn || highOn ? 1.8f : 1.2f));

        // Markers on the 0 dB line, with their names and frequencies.
        const auto marker = [&] (Grab which, bool on, const juce::String& name, double hz) {
            const float x = handleX (which);
            // A cut the preset drives has no handle: it can't be dragged.
            if (! locked (which))
            {
                const bool hot = hover == which || grab == which;
                const auto box = juce::Rectangle<float> (hot ? 9.f : 7.f, hot ? 9.f : 7.f).withCentre ({ x, y0 });
                g.setColour (col::well);
                g.fillRect (box.expanded (2.f));
                g.setColour (on ? col::accent : col::inkFaint);
                if (on) g.fillRect (box);
                else g.drawRect (box, 1.f);
            }

            const juce::String value = on ? fmt::hertz (hz) : juce::String ("OFF");
            g.setFont (label (9.f));
            const float wName = textWidth (g.getCurrentFont(), name.toUpperCase());
            g.setFont (mono (10.f));
            const float wVal = textWidth (g.getCurrentFont(), value);
            const float w = wName + 6.f + wVal;
            // Over the region the filter removes (low cut: left, high cut: right),
            // where the curve has dropped away; flipped when the edge is too close.
            bool left = which == Grab::low;
            if (left && x - 10.f - w < p.getX() + 6.f) left = false;
            if (! left && which == Grab::high && x + 10.f + w > p.getRight() - 6.f) left = true;
            float tx = left ? x - 10.f - w : x + 10.f;
            const float ty = y0 - 21.f;
            // A resonant curve can climb through that spot: slide out until the text is clear of it.
            const auto clear = [&] (float from) {
                for (float cx = from - 2.f; cx <= from + w + 2.f; cx += 2.f)
                    if (curveY (cx) < ty + 15.f) return false;
                return true;
            };
            for (int step = 0; step < 80 && ! clear (tx); ++step)
                tx += left ? -3.f : 3.f;
            tx = juce::jlimit (p.getX() + 4.f, p.getRight() - 4.f - w, tx);
            caption (g, name, { tx, ty, wName + 1.f, 12.f }, juce::Justification::centredLeft, on ? col::accent : col::inkFaint, 9.f);
            g.setColour (on ? col::ink : col::inkFaint);
            g.setFont (mono (10.f));
            g.drawText (value, juce::Rectangle<float> (tx + wName + 6.f, ty, wVal + 2.f, 12.f), juce::Justification::centredLeft, false);
        };
        marker (Grab::low, lowOn, "Low cut", map::lowCutHz (s.lowCut));
        marker (Grab::high, highOn, "High cut", map::highCutHz (s.highCut));

        // Filter scale, at the right edge, on the well so the hatching doesn't eat it.
        g.setFont (mono (9.f));
        for (double db : { 0.0, -24.0 })
        {
            const auto t = db == 0.0 ? juce::String ("0 dB") : fmt::signedNumber (db, 0) + " dB";
            const float w = textWidth (g.getCurrentFont(), t) + 6.f;
            const juce::Rectangle<float> r (p.getRight() - w - 4.f, yFilter (db) + 3.f, w, 12.f);
            g.setColour (col::well);
            g.fillRect (r);
            g.setColour (col::inkFaint);
            g.drawText (t, r, juce::Justification::centred, false);
        }
    }

    g.setColour (col::hairline);
    g.drawRect (p, 1.f);

    // Frequency axis.
    g.setFont (mono (9.f));
    g.setColour (col::inkFaint);
    const std::pair<double, const char*> ticks[] = { { 20, "20" }, { 50, "50" }, { 100, "100" }, { 200, "200" }, { 500, "500" },
                                                     { 1000, "1k" }, { 2000, "2k" }, { 5000, "5k" }, { 10000, "10k" }, { 20000, "20k Hz" } };
    for (const auto& [f, t] : ticks)
    {
        const float x = xFor (f);
        const auto j = f == 20.0 ? juce::Justification::centredLeft : f == 20000.0 ? juce::Justification::centredRight : juce::Justification::centred;
        const auto r = f == 20.0 ? juce::Rectangle<float> (x, p.getBottom() + 4.f, 60.f, 12.f)
                     : f == 20000.0 ? juce::Rectangle<float> (x - 60.f, p.getBottom() + 4.f, 60.f, 12.f)
                                    : juce::Rectangle<float> (x - 30.f, p.getBottom() + 4.f, 60.f, 12.f);
        g.drawText (t, r, j, false);
    }
}

// ---- Mouse -----------------------------------------------------------------------

void SpectrumView::setFromX (Grab which, float x)
{
    const auto p = plot();
    auto& prm = paramFor (which);
    float v;
    if (which == Grab::low) v = x < p.getX() + 1.f ? 0.f : map::lowCutNorm (hzFor (x));
    else v = x > p.getRight() - 1.f ? 1.f : map::highCutNorm (hzFor (x));
    prm.setValueNotifyingHost (v);   // the cut parameters are 0..1 already
}

void SpectrumView::mouseMove (const juce::MouseEvent& e)
{
    const auto h = grabAt (e.position);
    if (h != hover)
    {
        hover = h;
        setMouseCursor (h == Grab::none ? juce::MouseCursor::NormalCursor : juce::MouseCursor::LeftRightResizeCursor);
        repaint();
    }
}

void SpectrumView::mouseExit (const juce::MouseEvent&)
{
    if (hover != Grab::none)
    {
        hover = Grab::none;
        repaint();
    }
    if (onHover) onHover (false);
}

void SpectrumView::mouseDown (const juce::MouseEvent& e)
{
    grab = grabAt (e.position);
    if (grab == Grab::none) return;
    paramFor (grab).beginChangeGesture();
    repaint();
}

void SpectrumView::mouseDrag (const juce::MouseEvent& e)
{
    if (grab != Grab::none) setFromX (grab, e.position.x);
}

void SpectrumView::mouseUp (const juce::MouseEvent&)
{
    if (grab == Grab::none) return;
    paramFor (grab).endChangeGesture();
    grab = Grab::none;
    repaint();
}

void SpectrumView::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto which = grabAt (e.position);
    if (which == Grab::none) return;
    auto& prm = paramFor (which);
    prm.beginChangeGesture();
    prm.setValueNotifyingHost (which == Grab::low ? 0.f : 1.f);
    prm.endChangeGesture();
}
