#pragma once

#include "Controls.h"

class BuildAppProcessor;

// The PRESET menu, the same in the main editor and the preset editor: Off,
// the factory presets in their sections, then USER (every preset in your
// library, and the user slots in use that aren't in it, `•` while they hold
// unsaved changes), New preset and Open presets folder. The < > keys and the
// wheel walk Off, the factory presets, then the library. Picking a library
// preset loads it into a user slot (the one it's in already, else the slot
// that's on if its work is saved, else a free one) and turns PRESET to it.
namespace presetmenu
{
    std::vector<TypeSelector::Choice> choices();   // PRESET's: Off, the factory presets, the user slots
    // `say` puts a line in the footer (a full slot, a file it can't read);
    // `onNew` runs after New preset made one.
    void attach (TypeSelector&, BuildAppProcessor&, std::function<void (const juce::String&)> say, std::function<void()> onNew = {});
    bool load (BuildAppProcessor&, const juce::File&, const std::function<void (const juce::String&)>& say);
    int newPreset (BuildAppProcessor&);   // an empty user preset, turned on; its slot, -1 when none is free
    // `base`, or `base 2`, `base 3`... whichever no library preset or slot has.
    juce::String freeName (const BuildAppProcessor&, const juce::String& base);
} // namespace presetmenu
