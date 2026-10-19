// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source. See THIRD_PARTY_LICENSES.md (C-006).
#pragma once

#include "core/render/StackRenderer.h"
#include "core/rt/HazardSlots.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace klay::render
{

// Background renderer with a lock-free hand-over to the audio thread (FR-107, NFR-004, NFR-013, spec §10 threading
// model). The message thread posts edits; a worker thread renders the latest one (older pending edits are skipped),
// publishes it through an atomic pointer with a generation counter, and frees replaced renders itself once no audio
// slot protects them, so the audio thread never allocates or frees (NFR-001).
class RenderEngine
{
public:
    static constexpr int kAudioSlots = 4; // voice slots the audio thread may hold at once

    struct Config
    {
        SampleProvider samples;
        ResamplerFactory resamplers;
        bool parallelLayers = true;
    };

    explicit RenderEngine(Config config);
    virtual ~RenderEngine();

    RenderEngine(const RenderEngine&) = delete;
    RenderEngine& operator=(const RenderEngine&) = delete;

    // ---- message thread ----
    // Queues a render of `kick` at the given rate and effective BPM; returns its generation.
    std::uint64_t requestRender(const model::Kick& kick, double sampleRate, double bpm);

    // The newest published render (shared ownership, for UI and tests; never call on the audio thread).
    std::shared_ptr<const RenderedKick> latest() const;

    // Blocks until a render with generation ≥ `generation` is published (tests, offline tools).
    bool waitForGeneration(std::uint64_t generation, std::chrono::milliseconds timeout) const;

    // ---- audio thread (one reader) ----
    // Protects the current render in `slot` and returns it (nullptr before the first render).
    const RenderedKick* acquire(int slot) noexcept { return hazards.protect(slot, published); }
    void hold(int slot, const RenderedKick* kick) noexcept { hazards.hold(slot, kick); }
    void release(int slot) noexcept { hazards.clear(slot); }
    std::uint64_t publishedGeneration() const noexcept { return publishedGen.load(std::memory_order_acquire); }

protected:
    // Worker thread hooks for subclasses (task 1.12 builds the transposition cache here).
    virtual void onPublished(const std::shared_ptr<const RenderedKick>&) {}
    // Called whenever the worker is idle; return true if it did work (the loop then polls again at once).
    virtual bool idleWork(const std::shared_ptr<const RenderedKick>&) { return false; }
    // Lets idleWork stop early when a new edit is waiting.
    bool renderPending() const noexcept { return pendingFlag.load(std::memory_order_acquire); }
    void wake();
    void stopWorker(); // subclasses call this first in their destructor

    Config config;

private:
    struct Job
    {
        model::Kick kick;
        double sampleRate = 48000.0;
        double bpm = 155.0;
        std::uint64_t generation = 0;
    };

    void run();
    void freeRetired();

    mutable std::mutex mutex;
    mutable std::condition_variable published_cv;
    std::condition_variable jobCv;
    std::optional<Job> pending;
    std::atomic<bool> pendingFlag{ false };
    std::uint64_t nextGeneration = 0;
    bool stopping = false;

    std::shared_ptr<const RenderedKick> current;              // guarded by mutex
    std::vector<std::shared_ptr<const RenderedKick>> retired; // worker thread only
    std::atomic<const RenderedKick*> published{ nullptr };
    std::atomic<std::uint64_t> publishedGen{ 0 };
    rt::HazardSlots<RenderedKick, kAudioSlots> hazards;
    LayerCache layerCache;

    std::thread worker;
};

} // namespace klay::render
