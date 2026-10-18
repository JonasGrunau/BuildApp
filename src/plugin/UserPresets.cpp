#include "UserPresets.h"

#include "fx/PresetText.h"

namespace
{
    const juce::Identifier kUserPresets ("UserPresets"), kSlot ("Slot"), kFactoryCount ("factoryCount"), kIndex ("index"),
        kName ("name"), kHelp ("help"), kFile ("file"), kCopiedFrom ("copiedFrom"), kSaved ("saved"), kPath ("path");
    const juce::String kExtension (".bapreset"), kFileTag ("BuildAppPreset");

    // The words travel next to the path, not in it.
    ba::Preset pathOnly (ba::Preset p)
    {
        p.name = p.section = p.help = nullptr;
        return p;
    }
} // namespace

void UserPresets::publish (int slot)
{
    const auto& s = slots[(size_t) slot];
    if (s.used) bank.publish (slot, s.path);
    else bank.clear (slot);
    ++gen;
}

juce::int64 UserPresets::hashOf (const Slot& s)
{
    return juce::String (ba::toText (s.path)).hashCode64() * 31 + s.help.hashCode64();
}

void UserPresets::set (int slot, const Slot& s)
{
    if (slot < 0 || slot >= kSlots) return;
    {
        const juce::ScopedLock sl (lock);
        auto& to = slots[(size_t) slot];
        to = s;
        to.used = true;
        to.path = pathOnly (s.path);
        to.unsaved = hashOf (to) != to.savedHash;
        if (to.id == 0) to.id = ++ids;
    }
    publish (slot);
}

void UserPresets::release (int slot)
{
    if (slot < 0 || slot >= kSlots) return;
    {
        const juce::ScopedLock sl (lock);
        slots[(size_t) slot] = Slot {};
    }
    picked[(size_t) slot] = 0;
    publish (slot);
}

void UserPresets::touch (int slot)
{
    if (slot >= 0 && slot < kSlots) picked[(size_t) slot] = ++clock;
}

int UserPresets::holding (const juce::File& f) const
{
    if (f == juce::File()) return -1;
    for (int i = 0; i < kSlots; ++i)
        if (slots[(size_t) i].used && slots[(size_t) i].file == f) return i;
    return -1;
}

int UserPresets::freeSlot (int keep) const
{
    for (int i = 0; i < kSlots; ++i)
        if (! slots[(size_t) i].used) return i;
    int best = -1;
    for (int i = 0; i < kSlots; ++i)
        if (i != keep && ! slots[(size_t) i].unsaved && (best < 0 || picked[(size_t) i] < picked[(size_t) best])) best = i;
    return best;
}

int UserPresets::slotFor (int current) const
{
    if (current >= 0 && current < kSlots && slots[(size_t) current].used && ! slots[(size_t) current].unsaved) return current;
    return freeSlot (current);
}

// ---- The project --------------------------------------------------------------

void UserPresets::saveInto (juce::ValueTree& state) const
{
    juce::ValueTree tree (kUserPresets);
    tree.setProperty (kFactoryCount, (int) std::size (ba::kPresets), nullptr);
    {
        const juce::ScopedLock sl (lock);
        for (int i = 0; i < kSlots; ++i)
        {
            const auto& s = slots[(size_t) i];
            if (! s.used) continue;
            juce::ValueTree t (kSlot);
            t.setProperty (kIndex, i, nullptr);
            t.setProperty (kName, s.name, nullptr);
            t.setProperty (kHelp, s.help, nullptr);
            t.setProperty (kFile, s.file.getFullPathName(), nullptr);
            t.setProperty (kCopiedFrom, s.copiedFrom, nullptr);
            t.setProperty (kSaved, juce::String (s.savedHash), nullptr);
            t.setProperty (kPath, juce::String (ba::toText (s.path)), nullptr);
            tree.appendChild (t, nullptr);
        }
    }
    state.removeChild (state.getChildWithName (kUserPresets), nullptr);
    state.appendChild (tree, nullptr);
}

void UserPresets::loadFrom (const juce::ValueTree& state)
{
    std::array<bool, kSlots> seen {};
    for (const auto& t : state.getChildWithName (kUserPresets))
    {
        const int i = t.getProperty (kIndex, -1);
        if (! t.hasType (kSlot) || i < 0 || i >= kSlots) continue;
        Slot s;
        s.name = t.getProperty (kName).toString();
        s.help = t.getProperty (kHelp).toString();
        if (const auto path = t.getProperty (kFile).toString(); juce::File::isAbsolutePath (path)) s.file = juce::File (path);
        s.copiedFrom = juce::jlimit (0, (int) std::size (ba::kPresets), (int) t.getProperty (kCopiedFrom, 0));
        s.savedHash = t.getProperty (kSaved).toString().getLargeIntValue();
        ba::fromText (t.getProperty (kPath).toString().toStdString(), s.path);
        set (i, s);
        seen[(size_t) i] = true;
    }
    for (int i = 0; i < kSlots; ++i)
        if (! seen[(size_t) i] && slots[(size_t) i].used) release (i);
}

int UserPresets::factoryCountIn (const juce::ValueTree& state, int otherwise)
{
    const auto t = state.getChildWithName (kUserPresets);
    return t.isValid() ? (int) t.getProperty (kFactoryCount, otherwise) : otherwise;
}

// ---- The library ----------------------------------------------------------------

juce::File UserPresets::folder()
{
    // BUILDAPP_PRESET_FOLDER: somewhere else (the snapshot tool's self test).
    if (const auto other = juce::SystemStats::getEnvironmentVariable ("BUILDAPP_PRESET_FOLDER", {}); juce::File::isAbsolutePath (other))
        return juce::File (other);
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("BuildApp").getChildFile ("Presets");
}

std::vector<UserPresets::Entry> UserPresets::library()
{
    std::vector<Entry> out;
    for (const auto& f : folder().findChildFiles (juce::File::findFiles, false, "*" + kExtension))
    {
        Slot s;
        if (read (f, s)) out.push_back ({ s.name, f });
    }
    std::sort (out.begin(), out.end(), [] (const Entry& a, const Entry& b) { return a.name.compareNatural (b.name) < 0; });
    return out;
}

bool UserPresets::read (const juce::File& f, Slot& out)
{
    const auto xml = juce::parseXML (f);
    if (xml == nullptr || ! xml->hasTagName (kFileTag)) return false;
    Slot s;
    s.name = xml->getStringAttribute (kName.toString(), f.getFileNameWithoutExtension());
    s.help = xml->getStringAttribute (kHelp.toString());
    const auto* path = xml->getChildByName (kPath.toString());
    if (path == nullptr || ! ba::fromText (path->getAllSubText().toStdString(), s.path)) return false;
    s.file = f;
    s.used = true;
    s.savedHash = hashOf (s);
    out = s;
    return true;
}

bool UserPresets::write (const juce::File& f, const Slot& s)
{
    juce::XmlElement xml (kFileTag);
    xml.setAttribute ("version", 1);
    xml.setAttribute (kName.toString(), s.name);
    xml.setAttribute (kHelp.toString(), s.help);
    xml.createNewChildElement (kPath.toString())->addTextElement (juce::String (ba::toText (s.path)));
    if (! f.getParentDirectory().createDirectory()) return false;
    return xml.writeTo (f);
}

juce::File UserPresets::fileFor (const juce::String& name)
{
    auto safe = juce::File::createLegalFileName (name.trim());
    if (safe.isEmpty()) safe = "Preset";
    return folder().getChildFile (safe + kExtension);
}
