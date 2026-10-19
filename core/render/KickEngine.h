// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source. See THIRD_PARTY_LICENSES.md (C-006).
#pragma once

#include "core/render/KickPlayer.h"
#include "core/render/RenderEngine.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

namespace klay::render
{

// Render engine plus the Key Track transposition cache (FR-121, FR-122, FR-125, NFR-003, NFR-014, NFR-015; spec §10
// option B). After each render the pitch-following buses are pitch-shifted with Signalsmith (length kept) on the
// worker thread: root ±1 … ±12 semitones nearest first, then any note the audio thread asked for because it was
// played before it was ready (it played through the real-time Repitch fallback meanwhile). Entries are capped in
// bytes; when a requested note does not fit, the least recently played notes are evicted. The audio thread reads
// entries through hazard slots and never allocates or frees (NFR-001).
class KickEngine final : public RenderEngine, public TranspositionSource
{
public:
    static constexpr int kNumNotes = 128;

    struct CacheConfig
    {
        size_t capBytes = size_t{ 128 } << 20; // NFR-014: 128 MB per instance
        int prebuildRange = 12;                // root ± this many semitones are built ahead of play
        model::StretchPreset preset = model::StretchPreset::standard;
    };

    explicit KickEngine(Config config) : KickEngine(std::move(config), CacheConfig{}) {}
    KickEngine(Config config, CacheConfig cacheConfig);
    ~KickEngine() override;

    // ---- audio thread (TranspositionSource) ----
    const TransposedBuses* acquire(int slot, std::uint64_t generation, int note) noexcept override;
    void release(int slot) noexcept override { cacheHazards.clear(slot); }

    // ---- any thread: statistics for tests and the AC-1/AC-2 harness ----
    size_t cacheBytes() const noexcept { return bytes.load(std::memory_order_acquire); }
    int cachedNotes() const noexcept { return count.load(std::memory_order_acquire); }
    bool isCached(int note) const noexcept;
    // True once root ±prebuildRange is built (or nothing needs building) for the current render.
    bool prebuildFinished() const noexcept { return prebuildDone.load(std::memory_order_acquire); }
    bool waitForCached(int note, std::chrono::milliseconds timeout) const;
    bool waitForPrebuild(std::chrono::milliseconds timeout) const;

protected:
    void onPublished(const std::shared_ptr<const RenderedKick>& kick) override;
    bool idleWork(const std::shared_ptr<const RenderedKick>& kick) override;

private:
    bool build(const RenderedKick& kick, int note, bool mayEvict);
    void unpublish(int note);
    void freeRetired();

    CacheConfig cacheConfig;

    // Shared with the audio thread.
    std::array<std::atomic<const TransposedBuses*>, kNumNotes> published{};
    std::array<std::atomic<bool>, kNumNotes> requested{};
    std::array<std::atomic<std::uint64_t>, kNumNotes> lastPlayed{};
    std::atomic<std::uint64_t> playClock{ 0 };
    rt::HazardSlots<TransposedBuses, 2> cacheHazards; // one slot per player voice
    std::atomic<std::uint64_t> generation{ 0 };
    std::atomic<size_t> bytes{ 0 };
    std::atomic<int> count{ 0 };
    std::atomic<bool> prebuildDone{ true };

    // Worker thread only.
    std::array<std::shared_ptr<const TransposedBuses>, kNumNotes> owned;
    std::vector<std::shared_ptr<const TransposedBuses>> retired;
    std::vector<int> prebuildOrder;
    size_t prebuildNext = 0;
    int root = -1;
    bool hasFollow = false;
};

} // namespace klay::render
