#pragma once

#include "Presets.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>

// The user presets as the chain plays them: kSlots slots, each holding a
// preset (or nothing). Any thread but the audio thread publishes a new
// preset into a slot (the editor while you drag, a state load); the audio
// thread reads each slot's latest one with no locks and no copies: a triple
// buffer per slot, its buffers allocated the first time the slot is used.
namespace ba
{
    class PresetBank
    {
    public:
        static constexpr int kSlots = 16;

        PresetBank();
        ~PresetBank();
        PresetBank (const PresetBank&) = delete;
        PresetBank& operator= (const PresetBank&) = delete;

        // Not the audio thread; one writer at a time (a lock serialises them).
        void publish (int slot, const Preset&);
        void clear (int slot);   // the slot holds nothing
        // The latest published preset, copied (not the audio thread). False: empty.
        bool latest (int slot, Preset& out) const;
        // Bumped by every publish and clear: the UI redraws when it moves.
        uint32_t generation() const noexcept { return gen.load (std::memory_order_acquire); }

        // The audio thread (one reader at a time): the slot's latest preset,
        // nullptr when it holds nothing. Valid until the next call for that slot.
        const Preset* forAudio (int slot) noexcept;

    private:
        struct Slot;
        Slot& slotFor (int slot);   // the writer's, allocated on first use

        std::array<std::atomic<Slot*>, kSlots> slots {};
        std::array<std::unique_ptr<Slot>, kSlots> owned;
        mutable std::mutex writer;
        std::atomic<uint32_t> gen { 0 };
    };
} // namespace ba
