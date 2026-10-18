#pragma once

#include "fx/PresetBank.h"

#include <juce_data_structures/juce_data_structures.h>

#include <array>
#include <vector>

// The user presets: PRESET's last kSlots choices ("User 1".."User 16" to a
// host). A slot holds a preset of your own in this plugin instance (a
// factory preset you edited, one picked from your library, a new one) with
// its name, its help line and the file it was last saved to or loaded from.
// The project keeps every slot in use, so it plays the same wherever it
// opens, file or no file. The library is a folder of .bapreset files that
// every instance lists (~/Documents/BuildApp/Presets).
//
// Threads: the slots belong to the message thread (setStateInformation
// runs there, as replaceState wants), the audio thread plays them through a
// ba::PresetBank, and getStateInformation may come from anywhere: writing a
// slot and saving the state share a lock.
class UserPresets
{
public:
    static constexpr int kSlots = ba::PresetBank::kSlots;

    struct Slot
    {
        bool used = false;
        juce::String name, help;
        juce::File file;              // saved to or loaded from; none: never saved
        int copiedFrom = 0;           // the factory preset (1-based) it began as, 0: none
        ba::Preset path {};           // its cut types and lanes (the words are above)
        juce::int64 savedHash = 0;    // its help and path as last saved or loaded (0: never)
        bool unsaved = false;         // set() works it out: they differ from that
        uint32_t id = 0;              // which preset this is (set() numbers a new one), so undo knows it again
    };
    // The help line and the path, for telling whether they changed since a save.
    static juce::int64 hashOf (const Slot&);

    // ---- Message thread -------------------------------------------------------
    const Slot& operator[] (int slot) const { return slots[(size_t) slot]; }
    void set (int slot, const Slot&);   // plays at once; works out `unsaved`, and an id for a new preset
    void release (int slot);
    void touch (int slot);              // just picked: the last to be reused
    int holding (const juce::File&) const;   // the slot with that file in it, -1
    // Where to load a preset when `current` is on: `current` itself if it
    // holds saved work (browsing replaces it), else an empty slot, else the
    // longest unpicked one with nothing unsaved; -1 when none is free.
    int slotFor (int current) const;
    // A slot for a new preset (an edited copy, New preset): an empty one,
    // else the longest unpicked saved one that isn't `keep`; -1: none.
    int freeSlot (int keep = -1) const;
    uint32_t generation() const noexcept { return gen; }   // moves with every change

    // ---- The project ------------------------------------------------------------
    void saveInto (juce::ValueTree& state) const;   // any thread
    void loadFrom (const juce::ValueTree& state);   // message thread; no UserPresets child: all empty
    // How many factory presets the project knew (PRESET's user slots start after them).
    static int factoryCountIn (const juce::ValueTree& state, int otherwise);

    // ---- Audio thread -------------------------------------------------------------
    const ba::Preset* forAudio (int slot) noexcept { return bank.forAudio (slot); }

    // ---- The library ----------------------------------------------------------------
    static juce::File folder();   // BUILDAPP_PRESET_FOLDER, if set, moves it (tests)
    struct Entry
    {
        juce::String name;
        juce::File file;
    };
    static std::vector<Entry> library();   // by name
    static bool read (const juce::File&, Slot& out);   // name, help, path, file; false: not a preset
    static bool write (const juce::File&, const Slot&);
    static juce::File fileFor (const juce::String& name);   // its file in the folder, the name made safe

private:
    void publish (int slot);

    ba::PresetBank bank;
    std::array<Slot, kSlots> slots;
    std::array<uint32_t, kSlots> picked {};
    uint32_t gen = 0, clock = 0, ids = 0;
    mutable juce::CriticalSection lock;
};
