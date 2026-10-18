#include "PresetMenu.h"

#include "plugin/PluginProcessor.h"

namespace presetmenu
{
namespace
{
    using P = BuildAppProcessor;
    const juce::String kUnsaved (juce::CharPointer_UTF8 (" \xe2\x80\xa2"));
    constexpr int kOffId = 1, kLibraryId = 1000, kSlotId = 2000, kNewId = 3000, kFolderId = 3001;

    juce::String full() { return "All " + juce::String (UserPresets::kSlots) + " user slots hold unsaved presets: save or revert one first."; }
} // namespace

std::vector<TypeSelector::Choice> choices()
{
    std::vector<TypeSelector::Choice> out { { "Off", "No preset: every knob is yours, and BUILD does nothing.", {} } };
    for (const auto& p : ba::kPresets) out.push_back ({ p.name, p.help, p.section });
    for (int i = 1; i <= UserPresets::kSlots; ++i) out.push_back ({ "User " + juce::String (i), {}, "User" });
    return out;
}

juce::String freeName (const BuildAppProcessor& proc, const juce::String& base)
{
    juce::StringArray taken;
    for (const auto& e : UserPresets::library()) taken.add (e.name);
    for (int s = 0; s < UserPresets::kSlots; ++s)
        if (proc.user[s].used) taken.add (proc.user[s].name);
    auto name = base;
    for (int n = 2; taken.contains (name, true); ++n) name = base + " " + juce::String (n);
    return name;
}

bool load (BuildAppProcessor& proc, const juce::File& file, const std::function<void (const juce::String&)>& say)
{
    const int now = proc.presetIndex();
    int s = proc.user.holding (file);
    bool replaced = false;
    if (s < 0)
    {
        s = proc.user.slotFor (P::slotOf (now));
        if (s < 0)
        {
            if (say) say (full());
            return false;
        }
        UserPresets::Slot slot;
        if (! UserPresets::read (file, slot))
        {
            if (say) say ("That isn't a BuildApp preset: " + file.getFileName());
            return false;
        }
        replaced = P::presetOf (s) == now;
        proc.user.set (s, slot);
    }
    if (replaced) proc.presetReplaced (now);   // browsing in place: another preset, full intensity
    proc.selectPreset (P::presetOf (s));
    return true;
}

int newPreset (BuildAppProcessor& proc)
{
    const int s = proc.user.freeSlot (P::slotOf (proc.presetIndex()));
    if (s < 0) return -1;
    UserPresets::Slot slot;
    slot.used = slot.unsaved = true;
    slot.name = freeName (proc, "New preset");
    proc.user.set (s, slot);
    proc.selectPreset (P::presetOf (s));
    return s;
}

void attach (TypeSelector& sel, BuildAppProcessor& proc, std::function<void (const juce::String&)> say, std::function<void()> onNew)
{
    sel.nameOf = [&proc] (int i) { return proc.presetName (i); };
    sel.helpOf = [&proc] (int i) { return proc.presetHelp (i); };

    sel.showMenu = [&proc, say, onNew] (TypeSelector& t) {
        const int now = proc.presetIndex(), nowSlot = P::slotOf (now);
        const auto library = UserPresets::library();
        juce::PopupMenu m;
        m.addItem (kOffId, "Off", true, now == 0);
        juce::String section;
        for (int i = 1; i <= P::kFactory; ++i)
        {
            const auto& p = ba::kPresets[(size_t) i - 1];
            if (section != p.section) m.addSectionHeader (section = p.section);
            m.addItem (kOffId + i, p.name, true, now == i);
        }
        m.addSectionHeader ("User");
        std::array<bool, UserPresets::kSlots> listed {};
        int shown = kOffId + now;
        for (size_t k = 0; k < library.size(); ++k)
        {
            const int holder = proc.user.holding (library[k].file);
            if (holder >= 0) listed[(size_t) holder] = true;
            const bool on = holder >= 0 && holder == nowSlot;
            if (on) shown = kLibraryId + (int) k;
            m.addItem (kLibraryId + (int) k, library[k].name + (holder >= 0 && proc.user[holder].unsaved ? kUnsaved : juce::String()), true, on);
        }
        bool any = ! library.empty();
        for (int s = 0; s < UserPresets::kSlots; ++s)
            if (proc.user[s].used && ! listed[(size_t) s])
            {
                any = true;
                if (s == nowSlot) shown = kSlotId + s;
                m.addItem (kSlotId + s, proc.user[s].name + (proc.user[s].unsaved ? kUnsaved : juce::String()), true, s == nowSlot);
            }
        if (! any) m.addItem (-1, "Nothing of yours yet: EDIT a preset, then SAVE AS", false, false);
        m.addSeparator();
        m.addItem (kNewId, "New preset");
        m.addItem (kFolderId, "Open presets folder");

        t.setMenuOpen (true);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&t).withMinimumWidth (t.getWidth()).withStandardItemHeight (22)
                             .withItemThatMustBeVisible (shown),
                         [&proc, say, onNew, library, safe = juce::Component::SafePointer<TypeSelector> (&t)] (int id) {
                             if (safe == nullptr) return;   // its editor is gone
                             safe->setMenuOpen (false);
                             if (id >= kOffId && id <= kOffId + P::kFactory) proc.selectPreset (id - kOffId);
                             else if (id >= kSlotId && id < kSlotId + UserPresets::kSlots) proc.selectPreset (P::presetOf (id - kSlotId));
                             else if (id >= kLibraryId && id < kLibraryId + (int) library.size()) load (proc, library[(size_t) (id - kLibraryId)].file, say);
                             else if (id == kNewId)
                             {
                                 if (newPreset (proc) < 0) { if (say) say (full()); }
                                 else if (onNew) onNew();
                             }
                             else if (id == kFolderId)
                             {
                                 UserPresets::folder().createDirectory();
                                 UserPresets::folder().startAsProcess();
                             }
                         });
    };

    // Off, the factory presets, then the library, round and round.
    sel.step = [&proc, say] (int direction) {
        const auto library = UserPresets::library();
        const int n = 1 + P::kFactory + (int) library.size(), now = proc.presetIndex();
        int at = now <= P::kFactory ? now : -1;
        if (const int s = P::slotOf (now); s >= 0)
            for (size_t k = 0; k < library.size(); ++k)
                if (proc.user[s].used && proc.user[s].file == library[k].file) at = 1 + P::kFactory + (int) k;
        const int next = at < 0 ? (direction > 0 ? 0 : n - 1) : (at + (direction > 0 ? 1 : n - 1)) % n;
        if (next <= P::kFactory) proc.selectPreset (next);
        else load (proc, library[(size_t) (next - 1 - P::kFactory)].file, say);
    };
}
} // namespace presetmenu
