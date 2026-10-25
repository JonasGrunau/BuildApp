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

void NightLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (col::raised);
    g.setColour (col::hairline.brighter (0.3f));
    g.drawRect (0, 0, w, h, 1);
}

void NightLookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                                          bool isHighlighted, bool isTicked, bool, const juce::String& text, const juce::String&,
                                          const juce::Drawable*, const juce::Colour*)
{
    const auto r = area.toFloat();
    if (isSeparator)
    {
        g.setColour (col::hairline);
        g.fillRect (r.reduced (10.f, 0.f).withHeight (1.f).withY (r.getCentreY()));
        return;
    }
    if (isHighlighted && isActive)
    {
        g.setColour (col::hairline);
        g.fillRect (r.reduced (1.f, 0.f));
    }
    if (isTicked)
    {
        g.setColour (col::ink);
        g.fillRect (juce::Rectangle<float> (5.f, 5.f).withCentre ({ r.getX() + 13.f, r.getCentreY() }));
    }
    g.setColour (! isActive ? col::inkFaint : isTicked || isHighlighted ? col::ink : col::inkSoft);
    g.setFont (getPopupMenuFont());
    g.drawText (text, r.withTrimmedLeft (26.f).withTrimmedRight (10.f), juce::Justification::centredLeft, true);
}

void NightLookAndFeel::drawPopupMenuSectionHeader (juce::Graphics& g, const juce::Rectangle<int>& area, const juce::String& sectionName)
{
    caption (g, sectionName, area.toFloat().withTrimmedLeft (10.f).withTrimmedTop (5.f), juce::Justification::centredLeft, col::inkFaint, 9.f);
}

juce::Font NightLookAndFeel::getPopupMenuFont() { return mono (11.f); }

// ---- Dial ------------------------------------------------------------------------

Dial::Dial (juce::RangedAudioParameter& p, juce::String n, juce::String h)
    : param (p), name (std::move (n)), help (std::move (h)),
      attachment (p, [this] (float v) { value = v; repaint(); })
{
    attachment.sendInitialUpdate();
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    setRepaintsOnMouseActivity (false);
    setOpaque (true);
}

// Repaint only when it shows: a tick or a different word.
bool Dial::showsDifferent (std::optional<float> a, std::optional<float> b) const
{
    if (a.has_value() != b.has_value()) return true;
    if (! a) return false;
    const float na = param.convertTo0to1 (*a), nb = param.convertTo0to1 (*b);
    return std::abs (na - nb) > 0.002f || param.getText (na, 0) != param.getText (nb, 0);
}

void Dial::updateCursor()
{
    setMouseCursor (untouchable() ? juce::MouseCursor::NormalCursor : juce::MouseCursor::UpDownResizeCursor);
}

void Dial::setAutoValue (std::optional<float> plain)
{
    if (plain == autoValue) return;
    const bool visible = showsDifferent (plain, autoValue);
    autoValue = plain;
    updateCursor();
    if (visible) repaint();
}

void Dial::setLocked (std::optional<float> plain)
{
    if (plain == lockedValue) return;
    const bool visible = showsDifferent (plain, lockedValue);
    const bool locking = plain.has_value() && ! lockedValue.has_value();
    lockedValue = plain;
    updateCursor();
    if (locking)
    {
        // Taken over mid-gesture: let go of it.
        if (dragging)
        {
            dragging = false;
            attachment.endGesture();
        }
        entry.reset();
    }
    if (visible) repaint();
}

juce::Rectangle<float> Dial::discArea() const
{
    const auto b = getLocalBounds().toFloat();
    const float d = std::min ({ maxDisc, b.getWidth() - 8.f, b.getHeight() - 36.f });
    return juce::Rectangle<float> (d, d).withCentre ({ b.getCentreX(), b.getY() + d * 0.5f });
}

juce::Rectangle<float> Dial::valueArea() const
{
    // 4 px gap, the 13 px name line, then the 16 px value line.
    return getLocalBounds().toFloat().withTop (discArea().getBottom() + 17.f).withHeight (16.f);
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
    const float norm = param.convertTo0to1 (shown());
    const bool off = valueText() == "Off";
    const bool hot = isMouseOverOrDragging() && ! untouchable();
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
    const auto litColour = disabled() ? col::inkFaint : ring == Ring::build ? col::accent : ring == Ring::plain ? col::inkSoft : col::ink;
    g.setColour (col::hairline.brighter (disabled() ? 0.f : 0.25f));
    g.strokePath (unlit, juce::PathStrokeType (1.2f));
    g.setColour (litColour);
    g.strokePath (lit, juce::PathStrokeType (ring == Ring::position ? 2.f : 1.6f));

    // Disc and pointer.
    const float rd = R * 0.62f;
    const auto disc = juce::Rectangle<float> (2.f * rd, 2.f * rd).withCentre (c);
    // Flat (sheet, hairline rim) when the mouse can't turn it; hatched, as
    // on a drawing, when a preset holds it.
    g.setColour (untouchable() ? col::sheet : col::raised);
    g.fillEllipse (disc);
    if (isLocked())
    {
        juce::Graphics::ScopedSaveState clip (g);
        juce::Path round;
        round.addEllipse (disc);
        g.reduceClipRegion (round);
        hatch (g, disc, col::hairline.brighter (0.15f), 3.5f, 1.f);
    }
    g.setColour (hot ? col::inkSoft : col::hairline.brighter (untouchable() ? 0.f : 0.4f));
    g.drawEllipse (disc, 1.2f);
    const float a = angle (norm);
    g.setColour (disabled() ? col::inkFaint : off ? col::inkSoft : col::ink);
    juce::Path pointer;
    pointer.startNewSubPath (at (a, rd * 0.2f));
    pointer.lineTo (at (a, rd * 0.86f));
    g.strokePath (pointer, juce::PathStrokeType (2.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Name and value.
    auto txt = getLocalBounds().toFloat().withTop (area.getBottom() + 4.f);
    caption (g, name, txt.removeFromTop (13.f), juce::Justification::centredTop, disabled() ? col::inkFaint : hot ? col::ink : col::inkSoft, 9.5f);
    if (entry == nullptr)
    {
        // A build knob at rest isn't doing anything: its value steps back.
        g.setColour (disabled() || off || (rest && ring == Ring::build) ? col::inkFaint : col::ink);
        g.setFont (mono (11.f));
        g.drawText (off ? juce::String ("OFF") : autoValue ? "AUTO " + valueText() : valueText(), valueArea(),
                    juce::Justification::centredTop, false);
    }
}

void Dial::nudge (float delta, bool asGesture)
{
    const float norm = juce::jlimit (0.f, 1.f, param.convertTo0to1 (value) + delta);
    const float plain = param.convertFrom0to1 (norm);
    if (asGesture) attachment.setValueAsPartOfGesture (plain);
    else attachment.setValueAsCompleteGesture (plain);
}

void Dial::reset() { attachment.setValueAsCompleteGesture (param.convertFrom0to1 (param.getDefaultValue())); }

void Dial::mouseDown (const juce::MouseEvent& e)
{
    if (untouchable()) return;
    if (e.mods.isAltDown() || e.mods.isCommandDown())
    {
        reset();
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
    if (untouchable())
    {
        Component::mouseWheelMove (e, w);   // the editor behind may scroll
        return;
    }
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

void Dial::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (untouchable()) return;
    // The second click opened a drag gesture: close it before writing the value.
    if (dragging)
    {
        dragging = false;
        attachment.endGesture();
    }

    // Only the number types a value; anywhere else on the dial resets it.
    const float w = std::max (24.f, textWidth (mono (11.f), valueText()) + 8.f);
    if (valueArea().withSizeKeepingCentre (w, valueArea().getHeight()).contains (e.position))
        openEntry();
    else
        reset();
}

void Dial::openEntry()
{
    // Already typing: replacing the editor would close the new one as the old one loses focus.
    if (entry != nullptr) return;
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

// ---- SmallDial ---------------------------------------------------------------------

SmallDial::SmallDial (juce::RangedAudioParameter& p, juce::String n)
    : param (p), name (std::move (n)),
      attachment (p, [this] (float v) {
          value = v;
          repaint();
          if (isMouseOverOrDragging() && onHover) onHover (this, true);   // the footer follows the value
      })
{
    attachment.sendInitialUpdate();
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    setRepaintsOnMouseActivity (false);
    setOpaque (true);
}

juce::String SmallDial::getHelp() const
{
    return name + " " + param.getText (param.convertTo0to1 (value), 0) + theme::dot + help;
}

void SmallDial::paint (juce::Graphics& g)
{
    g.fillAll (col::sheet);
    const auto b = getLocalBounds().toFloat();
    const auto c = b.getCentre();
    const float R = std::min (b.getWidth(), b.getHeight()) * 0.5f - 1.f;
    const float norm = param.convertTo0to1 (value);
    const bool hot = isMouseOverOrDragging();
    const auto angle = [] (float t) { return juce::MathConstants<float>::pi * (-0.75f + 1.5f * t); };
    const auto at = [&] (float a, float r) { return juce::Point<float> (c.x + r * std::sin (a), c.y - r * std::cos (a)); };

    // Ring: the whole travel dark, lit from the left to the value.
    const auto arc = [&] (float from, float to) {
        juce::Path p;
        p.addCentredArc (c.x, c.y, R - 0.8f, R - 0.8f, 0.f, angle (from), angle (to), true);
        return p;
    };
    g.setColour (col::hairline.brighter (0.25f));
    g.strokePath (arc (0.f, 1.f), juce::PathStrokeType (1.2f));
    if (norm > 1e-4f)
    {
        g.setColour (hot ? col::ink : col::inkSoft);
        g.strokePath (arc (0.f, norm), juce::PathStrokeType (1.6f));
    }

    // Disc and pointer.
    const float rd = R * 0.6f;
    const auto disc = juce::Rectangle<float> (2.f * rd, 2.f * rd).withCentre (c);
    g.setColour (col::raised);
    g.fillEllipse (disc);
    g.setColour (hot ? col::inkSoft : col::hairline.brighter (0.4f));
    g.drawEllipse (disc, 1.f);
    g.setColour (col::ink);
    juce::Path pointer;
    pointer.startNewSubPath (at (angle (norm), rd * 0.15f));
    pointer.lineTo (at (angle (norm), rd * 0.95f));
    g.strokePath (pointer, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void SmallDial::nudge (float delta, bool asGesture)
{
    const float plain = param.convertFrom0to1 (juce::jlimit (0.f, 1.f, param.convertTo0to1 (value) + delta));
    if (asGesture) attachment.setValueAsPartOfGesture (plain);
    else attachment.setValueAsCompleteGesture (plain);
}

void SmallDial::reset() { attachment.setValueAsCompleteGesture (param.convertFrom0to1 (param.getDefaultValue())); }

void SmallDial::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isAltDown() || e.mods.isCommandDown())
    {
        reset();
        return;
    }
    dragging = true;
    lastY = e.position.y;
    attachment.beginGesture();
    repaint();
}

void SmallDial::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging) return;
    const float dy = lastY - e.position.y;
    lastY = e.position.y;
    nudge (dy / 160.f * (e.mods.isShiftDown() ? 0.08f : 1.f), true);
}

void SmallDial::mouseUp (const juce::MouseEvent&)
{
    if (! dragging) return;
    dragging = false;
    attachment.endGesture();
    repaint();
}

void SmallDial::mouseDoubleClick (const juce::MouseEvent&)
{
    if (dragging)
    {
        dragging = false;
        attachment.endGesture();
    }
    reset();
}

void SmallDial::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const float d = (std::abs (w.deltaY) > std::abs (w.deltaX) ? w.deltaY : -w.deltaX) * (w.isReversed ? -1.f : 1.f);
    nudge (d * 0.06f * (e.mods.isShiftDown() ? 0.1f : 1.f), false);
}

void SmallDial::mouseEnter (const juce::MouseEvent&)
{
    repaint();
    if (onHover) onHover (this, true);
}

void SmallDial::mouseExit (const juce::MouseEvent&)
{
    repaint();
    if (onHover) onHover (this, false);
}

// ---- TypeSelector ------------------------------------------------------------------

TypeSelector::TypeSelector (juce::RangedAudioParameter& p, juce::String n, std::vector<Choice> c) : TypeSelector (std::move (n), std::move (c))
{
    attachment = std::make_unique<juce::ParameterAttachment> (p, [this] (float v) { value = v; repaint(); });
    attachment->sendInitialUpdate();
}

TypeSelector::TypeSelector (juce::String n, std::vector<Choice> c) : name (std::move (n)), choices (std::move (c))
{
    for (const auto& ch : choices) widest = std::max (widest, textWidth (mono (10.5f), ch.name));
    field = widest;
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setOpaque (true);
}

void TypeSelector::setIndex (int choice)
{
    if (attachment != nullptr || (float) choice == value) return;
    value = (float) choice;
    repaint();
}

void TypeSelector::setDriven (std::optional<int> choice)
{
    if (choice == driven) return;
    driven = choice;
    setMouseCursor (driven ? juce::MouseCursor::NormalCursor : juce::MouseCursor::PointingHandCursor);
    repaint();
}

juce::String TypeSelector::getHelp() const
{
    const int i = index();
    const auto line = helpOf ? helpOf (i) : choices[(size_t) i].help;
    return line.isEmpty() ? textOf (i) : textOf (i) + theme::dot + line;
}

void TypeSelector::choose (int i)
{
    i = juce::jlimit (0, (int) choices.size() - 1, i);
    if (i != index())
    {
        if (beforeChoose) beforeChoose (i);
        if (attachment != nullptr) attachment->setValueAsCompleteGesture ((float) i);
        else
        {
            value = (float) i;
            repaint();
            if (onChoose) onChoose (i);
        }
    }
    if (isMouseOver() && onHover) onHover (this, true);   // the help line follows the choice
}

void TypeSelector::stepBy (int direction)
{
    if (step) step (direction);
    else
    {
        const int n = (int) choices.size();
        choose ((index() + (direction > 0 ? 1 : n - 1)) % n);
    }
    if (isMouseOver() && onHover) onHover (this, true);
}

float TypeSelector::choiceX() const
{
    return std::max (textWidth (label (9.f), name.toUpperCase()) + 2.f + 7.f, fieldX);
}

// The step keys sit 8 px after the field's chevron (where it would be while
// a preset holds the choice), as tall as a section tag box and centred on
// the line: one place for every choice.
juce::Rectangle<float> TypeSelector::stepKey (int direction) const
{
    const float x = choiceX() + field + 12.f + 8.f + (direction > 0 ? 16.f : 0.f);
    return juce::Rectangle<float> (13.f, 13.f).withCentre ({ x + 6.5f, (float) getHeight() * 0.5f });
}

int TypeSelector::idealWidth() const
{
    return (int) std::ceil (choiceX() + field + 14.f + (steppers ? 8.f + 16.f + 13.f + 4.f : 0.f));
}

TypeSelector::Part TypeSelector::partAt (juce::Point<float> p) const
{
    if (! getLocalBounds().toFloat().contains (p)) return Part::none;
    if (steppers)
    {
        // Each key takes the full height and half the gap between them.
        const float split = (stepKey (-1).getRight() + stepKey (1).getX()) * 0.5f;
        if (p.x > stepKey (1).getRight() + 4.f) return Part::none;   // the room kept for longer choices
        if (p.x >= split) return Part::next;
        if (p.x >= stepKey (-1).getX() - 4.f) return Part::prev;
    }
    return Part::menu;
}

void TypeSelector::setHover (Part p)
{
    if (p == hover) return;
    hover = p;
    if (! isDriven()) setMouseCursor (p == Part::none ? juce::MouseCursor::NormalCursor : juce::MouseCursor::PointingHandCursor);
    repaint();
}

void TypeSelector::paint (juce::Graphics& g)
{
    g.fillAll (col::sheet);
    const bool hot = (isMouseOver() || open) && ! isDriven();
    const bool menuHot = hot && (open || hover == Part::menu);
    auto r = getLocalBounds().toFloat();

    g.setFont (label (9.f));
    const float wName = textWidth (g.getCurrentFont(), name.toUpperCase());
    caption (g, name, r.removeFromLeft (wName + 2.f), juce::Justification::centredLeft, hot ? col::ink : col::inkSoft, 9.f);
    r.setLeft (choiceX());
    r.setRight (choiceX() + field + 14.f);   // the field and its chevron; the keys follow

    // Step keys: outlined, ink on hover, pressed one pixel down; dimmed while a preset holds the choice.
    if (steppers)
    {
        for (const int dir : { -1, 1 })
        {
            const auto part = dir < 0 ? Part::prev : Part::next;
            const bool over = ! isDriven() && hover == part;
            const auto key = stepKey (dir).reduced (0.5f).translated (0.f, over && pressed == part ? 1.f : 0.f);
            g.setColour (isDriven() ? col::hairline : over ? col::ink : col::inkSoft);
            g.drawRoundedRectangle (key, kRadius, 1.f);
            const float cx = key.getCentreX() - (float) dir * 0.5f, cy = key.getCentreY();
            juce::Path arrow;
            arrow.startNewSubPath (cx - 1.5f * (float) dir, cy - 3.f);
            arrow.lineTo (cx + 1.5f * (float) dir, cy);
            arrow.lineTo (cx - 1.5f * (float) dir, cy + 3.f);
            g.setColour (isDriven() ? col::inkFaint : over ? col::ink : col::inkSoft);
            g.strokePath (arrow, juce::PathStrokeType (1.2f));
        }
    }

    // Chevron at the right end of the field; none when a preset holds it.
    const auto text = textOf (index());
    g.setFont (mono (10.5f));
    const float wText = std::min (textWidth (g.getCurrentFont(), text), field);
    g.setColour (col::ink);
    g.drawText (text, r.withWidth (wText + 2.f), juce::Justification::centredLeft, true);
    if (isDriven()) return;
    const float cx = r.getX() + field + 9.f, cy = r.getCentreY();
    juce::Path chevron;
    chevron.startNewSubPath (cx - 3.f, cy - 1.5f);
    chevron.lineTo (cx, cy + 1.5f);
    chevron.lineTo (cx + 3.f, cy - 1.5f);
    g.setColour (menuHot ? col::ink : col::inkFaint);
    g.strokePath (chevron, juce::PathStrokeType (1.2f));
    if (menuHot)
    {
        g.setColour (col::inkFaint);
        g.fillRect (r.getX(), r.getBottom() - 1.f, field + 12.f, 1.f);
    }
}

void TypeSelector::mouseDown (const juce::MouseEvent& e)
{
    if (isDriven() || partAt (e.position) == Part::none) return;
    if (const auto part = partAt (e.position); part == Part::prev || part == Part::next)
    {
        pressed = part;
        stepBy (part == Part::next ? 1 : -1);
        repaint();
        return;
    }
    if (showMenu)
    {
        showMenu (*this);
        return;
    }
    juce::PopupMenu menu;
    juce::String section;
    for (size_t i = 0; i < choices.size(); ++i)
    {
        if (choices[i].section != section)
        {
            section = choices[i].section;
            menu.addSectionHeader (section);
        }
        menu.addItem ((int) i + 1, choices[i].name, true, (int) i == index());
    }
    open = true;
    repaint();
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMinimumWidth (getWidth()).withStandardItemHeight (22)
                                                   .withItemThatMustBeVisible (index() + 1),
                        [safe = juce::Component::SafePointer<TypeSelector> (this)] (int picked) {
                            if (safe == nullptr) return;
                            safe->open = false;
                            if (picked > 0) safe->choose (picked - 1);
                            safe->repaint();
                        });
}

void TypeSelector::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (isDriven())
    {
        Component::mouseWheelMove (e, w);
        return;
    }
    wheel += (std::abs (w.deltaY) > std::abs (w.deltaX) ? w.deltaY : -w.deltaX) * (w.isReversed ? -1.f : 1.f);
    if (std::abs (wheel) < 0.12f) return;
    const int dir = wheel < 0.f ? 1 : -1;
    wheel = 0.f;
    if (step) stepBy (dir);
    else choose (index() + dir);
}

void TypeSelector::mouseUp (const juce::MouseEvent&)
{
    if (pressed == Part::none) return;
    pressed = Part::none;
    repaint();
}

void TypeSelector::mouseMove (const juce::MouseEvent& e)
{
    setHover (partAt (e.position));
}

void TypeSelector::mouseDrag (const juce::MouseEvent& e)
{
    setHover (partAt (e.position));   // a pressed key lifts when the mouse leaves it
}

void TypeSelector::mouseEnter (const juce::MouseEvent& e)
{
    hover = partAt (e.position);
    repaint();
    if (onHover) onHover (this, true);
}

void TypeSelector::mouseExit (const juce::MouseEvent&)
{
    hover = Part::none;
    repaint();
    if (onHover) onHover (this, false);
}

// ---- Switch ------------------------------------------------------------------------

Switch::Switch (juce::RangedAudioParameter& p, juce::String t, juce::String h)
    : param (p), text (std::move (t)), help (std::move (h)),
      attachment (p, [this] (float v) { value = v; repaint(); })
{
    attachment.sendInitialUpdate();
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setOpaque (true);
}

void Switch::setDriven (std::optional<bool> on)
{
    if (on == driven) return;
    driven = on;
    setMouseCursor (driven ? juce::MouseCursor::NormalCursor : juce::MouseCursor::PointingHandCursor);
    repaint();
}

void Switch::paint (juce::Graphics& g)
{
    g.fillAll (col::sheet);
    const bool hot = isMouseOver() && ! isDriven();
    g.setFont (label (9.f));
    const float w = textWidth (g.getCurrentFont(), text.toUpperCase());
    const auto b = getLocalBounds().toFloat();
    const float x = b.getCentreX() - (w + 13.f) * 0.5f;
    const auto box = juce::Rectangle<float> (7.f, 7.f).withCentre ({ x + 3.5f, b.getCentreY() });
    g.setColour (isOn() ? col::ink : hot ? col::inkSoft : col::inkFaint);
    if (isOn()) g.fillRect (box);
    else g.drawRect (box, 1.f);
    caption (g, text, { x + 13.f, b.getY(), w + 2.f, b.getHeight() }, juce::Justification::centredLeft,
             isOn() || hot ? col::ink : col::inkFaint, 9.f);
}

void Switch::mouseDown (const juce::MouseEvent&)
{
    if (isDriven()) return;
    const bool next = ! isOn();
    if (beforeToggle) beforeToggle (next);
    attachment.setValueAsCompleteGesture (next ? 1.f : 0.f);
}

void Switch::mouseEnter (const juce::MouseEvent&)
{
    repaint();
    if (onHover) onHover (this, true);
}

void Switch::mouseExit (const juce::MouseEvent&)
{
    repaint();
    if (onHover) onHover (this, false);
}

// ---- TextKey -----------------------------------------------------------------------

TextKey::TextKey (juce::String t, juce::String h) : text (std::move (t)), help (std::move (h))
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setOpaque (true);
}

void TextKey::setText (juce::String t)
{
    if (t == text) return;
    text = std::move (t);
    repaint();
}

void TextKey::setOn (bool o)
{
    if (o == on) return;
    on = o;
    repaint();
}

void TextKey::setUsable (bool yes)
{
    if (yes == usable) return;
    usable = yes;
    setMouseCursor (usable ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    repaint();
}

int TextKey::idealWidth() const { return (int) std::ceil (textWidth (label (9.f), text.toUpperCase()) + 18.f); }

void TextKey::paint (juce::Graphics& g)
{
    g.fillAll (col::sheet);
    const bool over = usable && hover, pressed = down && over;
    // Pressed, its top edge comes down a pixel and the word with it; the bottom
    // edge stays (moved down too, it would leave the key's bounds and vanish).
    const auto box = getLocalBounds().toFloat().reduced (0.5f), r = box.withTrimmedTop (pressed ? 1.f : 0.f);
    if (on && usable)
    {
        g.setColour (col::ink);
        g.fillRoundedRectangle (r, kRadius);
    }
    else
    {
        g.setColour (! usable ? col::hairline : over ? col::ink : col::inkSoft);
        g.drawRoundedRectangle (r, kRadius, 1.f);
    }
    caption (g, text, box.translated (0.f, pressed ? 1.f : 0.f), juce::Justification::centred,
             on && usable ? col::sheet : usable ? col::ink : col::inkFaint, 9.f);
}

void TextKey::mouseDown (const juce::MouseEvent&)
{
    if (! usable) return;
    down = true;
    repaint();
}

void TextKey::mouseDrag (const juce::MouseEvent& e)
{
    // It lifts while the mouse is off it.
    if (const bool in = getLocalBounds().contains (e.getPosition()); in != hover)
    {
        hover = in;
        repaint();
    }
}

void TextKey::mouseUp (const juce::MouseEvent& e)
{
    if (! down) return;
    down = false;
    repaint();
    if (usable && getLocalBounds().contains (e.getPosition()) && onClick) onClick();
}

void TextKey::mouseEnter (const juce::MouseEvent&)
{
    hover = true;
    repaint();
    if (onHover) onHover (this, true);
}

void TextKey::mouseExit (const juce::MouseEvent&)
{
    hover = false;
    repaint();
    if (onHover) onHover (this, false);
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
