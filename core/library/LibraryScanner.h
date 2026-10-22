// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source. See THIRD_PARTY_LICENSES.md (C-006).
#pragma once

#include "core/library/SampleIndex.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>

namespace klay::library
{

// Keeps the sample index of one library folder up to date in the background (FR-165, NFR-007, NFR-008). On start it
// loads the disk cache and publishes it at once (rolls work from it), then re-scans the folder, analyses only new
// or changed files, publishes the result and writes the cache back. Never touches audio (no audio is kept: samples
// are loaded lazily by the render engine, NFR-007).
class LibraryScanner
{
public:
    enum class Status
    {
        idle,     // no folder set
        scanning, // the cached index (if any) is published; the folder is being checked
        ready     // the published index matches the folder
    };

    struct Config
    {
        std::filesystem::path root;
        std::filesystem::path cacheFile; // empty: no disk cache
        Analyzer analyzer;
        unsigned threads = 0; // 0: one per hardware thread
    };

    LibraryScanner() = default;
    ~LibraryScanner();

    LibraryScanner(const LibraryScanner&) = delete;
    LibraryScanner& operator=(const LibraryScanner&) = delete;

    // Starts (or restarts, for a new folder) the background scan. Message thread.
    void start(Config config);
    void stop();

    // The newest published index (never null; empty before the first publish). Any non-audio thread.
    std::shared_ptr<const SampleIndex> snapshot() const;
    // Increments with every publish, so callers can tell when to rebuild their pools.
    std::uint64_t version() const noexcept { return publishVersion.load(std::memory_order_acquire); }
    Status status() const noexcept { return state.load(std::memory_order_acquire); }
    BuildStats lastStats() const;
    bool loadedFromCache() const noexcept { return fromCache.load(std::memory_order_acquire); }

    bool waitUntilReady(std::chrono::milliseconds timeout) const;

private:
    void run(Config config);
    void publish(SampleIndex index);

    mutable std::mutex mutex;
    mutable std::condition_variable cv;
    std::shared_ptr<const SampleIndex> current = std::make_shared<SampleIndex>();
    BuildStats stats;
    std::atomic<std::uint64_t> publishVersion{ 0 };
    std::atomic<Status> state{ Status::idle };
    std::atomic<bool> fromCache{ false };
    std::atomic<bool> cancel{ false };
    std::thread worker;
};

} // namespace klay::library
