// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source. See THIRD_PARTY_LICENSES.md (C-006).
#pragma once

#include <array>
#include <atomic>

namespace klay::rt
{

// Hazard pointers for one real-time reader (NFR-001, NFR-013). The audio thread protects the object it is about to
// use in one of its slots; the owning background thread only frees an object after it was unpublished and no slot
// holds it. Protect and clear are wait-free apart from the re-check loop, which only repeats while a publish races.
template <typename T, int NumSlots>
class HazardSlots
{
public:
    // Loads `source` and protects the result in `slot`. Returns nullptr if nothing is published.
    const T* protect(int slot, const std::atomic<const T*>& source) noexcept
    {
        auto& hazard = slots[static_cast<size_t>(slot)];
        const T* p = source.load(std::memory_order_seq_cst);
        for (;;)
        {
            hazard.store(p, std::memory_order_seq_cst);
            const T* again = source.load(std::memory_order_seq_cst);
            if (again == p)
                return p;
            p = again;
        }
    }

    // Protects an object the reader already holds safely (e.g. moving it from one slot to another).
    void hold(int slot, const T* p) noexcept { slots[static_cast<size_t>(slot)].store(p, std::memory_order_seq_cst); }

    void clear(int slot) noexcept { slots[static_cast<size_t>(slot)].store(nullptr, std::memory_order_seq_cst); }

    // Owner side: true while any slot protects p. Call only after p was unpublished.
    bool isProtected(const T* p) const noexcept
    {
        for (const auto& s : slots)
            if (s.load(std::memory_order_seq_cst) == p)
                return true;
        return false;
    }

private:
    std::array<std::atomic<const T*>, NumSlots> slots{};
};

} // namespace klay::rt
