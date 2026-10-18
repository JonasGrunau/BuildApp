#include "PresetBank.h"

namespace ba
{
// Three buffers: the writer fills `back`, then swaps it with `middle` and
// marks it fresh; the reader, when it finds `middle` fresh, swaps it with
// `front`. Each side only ever touches its own buffer, so a read never sees
// half a write. `last` is the writer's copy of what it published.
struct PresetBank::Slot
{
    struct Entry
    {
        Preset preset {};
        bool empty = true;
    };
    static constexpr int kFresh = 4;

    std::array<Entry, 3> buffers {};
    std::atomic<int> middle { 0 };
    int back = 1, front = 2;
    Entry last {};
};

PresetBank::PresetBank() = default;
PresetBank::~PresetBank() = default;

PresetBank::Slot& PresetBank::slotFor (int slot)
{
    auto& s = owned[(size_t) slot];
    if (s == nullptr)
    {
        s = std::make_unique<Slot>();
        slots[(size_t) slot].store (s.get(), std::memory_order_release);
    }
    return *s;
}

void PresetBank::publish (int slot, const Preset& p)
{
    if (slot < 0 || slot >= kSlots) return;
    const std::lock_guard<std::mutex> lock (writer);
    auto& s = slotFor (slot);
    s.buffers[(size_t) s.back] = { p, false };
    s.back = s.middle.exchange (s.back | Slot::kFresh, std::memory_order_acq_rel) & 3;
    s.last = { p, false };
    gen.fetch_add (1, std::memory_order_release);
}

void PresetBank::clear (int slot)
{
    if (slot < 0 || slot >= kSlots) return;
    const std::lock_guard<std::mutex> lock (writer);
    if (owned[(size_t) slot] == nullptr) return;   // never held anything
    auto& s = *owned[(size_t) slot];
    s.buffers[(size_t) s.back].empty = true;
    s.back = s.middle.exchange (s.back | Slot::kFresh, std::memory_order_acq_rel) & 3;
    s.last.empty = true;
    gen.fetch_add (1, std::memory_order_release);
}

bool PresetBank::latest (int slot, Preset& out) const
{
    if (slot < 0 || slot >= kSlots) return false;
    const std::lock_guard<std::mutex> lock (writer);
    const auto& s = owned[(size_t) slot];
    if (s == nullptr || s->last.empty) return false;
    out = s->last.preset;
    return true;
}

const Preset* PresetBank::forAudio (int slot) noexcept
{
    if (slot < 0 || slot >= kSlots) return nullptr;
    Slot* s = slots[(size_t) slot].load (std::memory_order_acquire);
    if (s == nullptr) return nullptr;
    if (s->middle.load (std::memory_order_relaxed) & Slot::kFresh)
        s->front = s->middle.exchange (s->front, std::memory_order_acq_rel) & 3;
    const auto& e = s->buffers[(size_t) s->front];
    return e.empty ? nullptr : &e.preset;
}
} // namespace ba
