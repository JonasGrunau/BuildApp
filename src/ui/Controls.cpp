#include "Controls.h"

using namespace theme;

// ---- NightLookAndFeel ------------------------------------------------------------

NightLookAndFeel::NightLookAndFeel()
{
    setColour (juce::TextEditor::backgroundColourId, col::well);
    setColour (juce::TextEditor::textColourId, col::ink);
    setColour (juce::TextEditor::outlineColourId, col::inkSoft);
    setColour (juce::TextEditor::focusedOutlineColourId, col::accent);
    setColour (juce::TextEditor::highlightColourId, col::accentDim);
    setColour (juce::TextEditor::highlightedTextColourId, col::ink);
    setColour (juce::CaretComponent::caretColourId, col::accent);
}

void NightLookAndFeel::drawCornerResizer (juce::Graphics& g, int w, int h, bool over, bool dragging)
{
    g.setColour (over || dragging ? col::ink : col::inkFaint);
    for (float i = 0.25f; i < 1.f; i += 0.25f)
        g.drawLine ((float) w * i, (float) h, (float) w, (float) h * i, 1.f);
}

// ---- Dial ------------------------------------------------------------------------

Dial::Dial (juce::RangedAudioParameter& p, juce::String n, juce::String h)
    : param (p), name (std::move (n)), help (std::move (h)),
      attachment (p, [this] (float v) { value = v; repaint(); if (onValueChange) onValueChange(); })
{
    attachment.sendInitialUpdate();
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    setRepaintsOnMouseActivity (false);
    setOpaque (true);
}

juce::Rectangle<float> Dial::discArea() const
{
    const auto b = getLocalBounds().toFloat();
    const float d = std::min ({ 66.f, b.getWidth() - 8.f, b.getHeight() - 36.f });
    return juce::Rectangle<float> (d, d).withCentre ({ b.getCentreX(), b.getY() + d * 0.5f });
}

void Dial::resized()
{
    if (entry != nullptr)
        entry->setBounds (getLocalBounds().removeFromBottom (18).reduced (6, 0));
}

void Dial::paint (juce::Graphics& g)
{
    g.fillAll (col::sheet);
    const auto area = discArea();
    const auto c = area.getCentre();
    const float R = area.getWidth() * 0.5f;
    const float norm = param.convertTo0to1 (value);
    const bool off = valueText() == "Off";
    const bool hot = isMouseOverOrDragging();
    const auto angle = [] (float t) { return juce::MathConstants<float>::pi * (-0.75f + 1.5f * t); };
    const auto at = [&] (float a, float r) { return juce::Point<float> (c.x + r * std::sin (a), c.y - r * std::cos (a)); };

    // Tick ring: the lit span depends on the kind of knob. Two paths, two strokes.
    constexpr int kTicks = 40;
    const float origin = ring == Ring::bipolar ? param.getDefaultValue() : restAtEnd ? 1.f : 0.f;
    const float lo = ring == Ring::position ? norm - 0.5f / kTicks : std::min (origin, norm);
    const float hi = ring == Ring::position ? norm + 0.5f / kTicks : std::max (origin, norm);
    const bool rest = std::abs (norm - origin) < 1e-4f && ring != Ring::position;
    juce::Path lit, unlit;
    for (int i = 0; i <= kTicks; ++i)
    {
        const float t = (float) i / kTicks;
        const bool major = i % 10 == 0;
        const bool on = ! off && ! rest && t >= lo - 1e-4f && t <= hi + 1e-4f;
        const float a = angle (t);
        auto& p = on ? lit : unlit;
        p.startNewSubPath (at (a, R * (major ? 0.76f : 0.82f)));
        p.lineTo (at (a, R * 0.96f));
    }
    const auto litColour = ring == Ring::build ? col::accent : ring == Ring::plain ? col::inkSoft : col::ink;
    g.setColour (col::hairline.brighter (0.25f));
    g.strokePath (unlit, juce::PathStrokeType (1.2f));
    g.setColour (litColour);
    g.strokePath (lit, juce::PathStrokeType (ring == Ring::position ? 2.f : 1.6f));

    // Disc and pointer.
    const float rd = R * 0.62f;
    const auto disc = juce::Rectangle<float> (2.f * rd, 2.f * rd).withCentre (c);
    g.setColour (col::raised);
    g.fillEllipse (disc);
    g.setColour (hot ? col::inkSoft : col::hairline.brighter (0.4f));
    g.drawEllipse (disc, 1.2f);
    const float a = angle (norm);
    g.setColour (off ? col::inkSoft : col::ink);
    juce::Path pointer;
    pointer.startNewSubPath (at (a, rd * 0.2f));
    pointer.lineTo (at (a, rd * 0.86f));
    g.strokePath (pointer, juce::PathStrokeType (2.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Name and value.
    auto txt = getLocalBounds().toFloat().withTop (area.getBottom() + 4.f);
    caption (g, name, txt.removeFromTop (13.f), juce::Justification::centredTop, hot ? col::ink : col::inkSoft, 9.5f);
    if (entry == nullptr)
    {
        // A build knob at rest isn't doing anything: its value steps back.
        g.setColour (off || (rest && ring == Ring::build) ? col::inkFaint : col::ink);
        g.setFont (mono (11.f));
        g.drawText (off ? juce::String ("OFF") : valueText(), txt.removeFromTop (16.f), juce::Justification::centredTop, false);
    }
}

void Dial::nudge (float delta, bool asGesture)
{
    const float norm = juce::jlimit (0.f, 1.f, param.convertTo0to1 (value) + delta);
    const float plain = param.convertFrom0to1 (norm);
    if (asGesture) attachment.setValueAsPartOfGesture (plain);
    else attachment.setValueAsCompleteGesture (plain);
}

void Dial::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isAltDown() || e.mods.isCommandDown())
    {
        attachment.setValueAsCompleteGesture (param.convertFrom0to1 (param.getDefaultValue()));
        return;
    }
    dragging = true;
    lastY = (float) e.position.y;
    attachment.beginGesture();
    repaint();
}

void Dial::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging) return;
    const float dy = lastY - e.position.y;
    lastY = e.position.y;
    nudge (dy / 220.f * (e.mods.isShiftDown() ? 0.08f : 1.f), true);
}

void Dial::mouseUp (const juce::MouseEvent&)
{
    if (! dragging) return;
    dragging = false;
    attachment.endGesture();
    repaint();
}

void Dial::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const float d = (std::abs (w.deltaY) > std::abs (w.deltaX) ? w.deltaY : -w.deltaX) * (w.isReversed ? -1.f : 1.f);
    nudge (d * 0.06f * (e.mods.isShiftDown() ? 0.1f : 1.f), false);
}

void Dial::mouseEnter (const juce::MouseEvent&)
{
    repaint();
    if (onHover) onHover (this, true);
}

void Dial::mouseExit (const juce::MouseEvent&)
{
    repaint();
    if (onHover) onHover (this, false);
}

void Dial::mouseDoubleClick (const juce::MouseEvent&)
{
    entry = std::make_unique<juce::TextEditor>();
    entry->setFont (mono (11.f));
    entry->setJustification (juce::Justification::centred);
    entry->setText (valueText().replace (" st", "").replace (" dB", "").replace (" %", ""), false);
    entry->setIndents (2, 1);
    addAndMakeVisible (*entry);
    resized();
    entry->selectAll();
    entry->grabKeyboardFocus();

    const auto close = [this] {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<Dial> (this)] {
            if (safe != nullptr) { safe->entry.reset(); safe->repaint(); }
        });
    };
    entry->onReturnKey = [this, close] {
        if (const auto t = entry->getText().trim(); t.isNotEmpty())
            attachment.setValueAsCompleteGesture (param.convertFrom0to1 (param.getValueForText (t)));
        close();
    };
    entry->onEscapeKey = close;
    entry->onFocusLost = close;
    repaint();
}

// ---- LevelMeter --------------------------------------------------------------------

void LevelMeter::push (float peak)
{
    const float db = peak > 1e-6f ? 20.f * std::log10 (peak) : -100.f;
    levelDb = std::max (db, levelDb - 1.5f);
    if (db >= holdDb) { holdDb = db; holdTicks = 45; }
    else if (--holdTicks <= 0) holdDb = std::max (-100.f, holdDb - 1.5f);

    // Below the scale nothing shows; otherwise a quarter dB is a pixel or less.
    const float lv = std::max (levelDb, -61.f), hv = std::max (holdDb, -61.f);
    if (std::abs (lv - shownLevel) > 0.25f || std::abs (hv - shownHold) > 0.25f)
    {
        shownLevel = lv;
        shownHold = hv;
        repaint();
    }
}

void LevelMeter::paint (juce::Graphics& g)
{
    g.fillAll (col::sheet);
    auto b = getLocalBounds().toFloat();
    auto top = b.removeFromTop (13.f);
    caption (g, name, top, juce::Justification::centredLeft, col::ink, 9.5f);
    g.setFont (mono (9.5f));
    g.setColour (col::inkSoft);
    g.drawText (levelDb <= -60.f ? juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94")) : fmt::signedNumber (levelDb, 0).replace ("+", ""),
                top, juce::Justification::centredRight, false);

    const auto bar = b.removeFromTop (6.f).translated (0.f, 2.f);
    const auto x = [&] (float db) { return bar.getX() + bar.getWidth() * juce::jlimit (0.f, 1.f, (db + 60.f) / 60.f); };
    g.setColour (col::well);
    g.fillRect (bar);
    g.setColour (col::inkSoft);
    g.fillRect (bar.withRight (x (levelDb)));
    if (holdDb > -60.f)
    {
        g.setColour (holdDb > -0.5f ? col::accent : col::ink);
        g.fillRect (juce::Rectangle<float> (x (holdDb) - 1.f, bar.getY() - 1.f, 2.f, bar.getHeight() + 2.f));
    }
    g.setColour (col::hairline);
    g.drawRect (bar, 0.8f);
    g.setColour (col::inkFaint);
    for (float db : { -48.f, -36.f, -24.f, -12.f, 0.f })
        g.fillRect (juce::Rectangle<float> (x (db) - 0.5f, bar.getBottom() + 1.5f, 1.f, 3.f));
}
