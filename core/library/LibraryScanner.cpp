// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source. See THIRD_PARTY_LICENSES.md (C-006).
#include "core/library/LibraryScanner.h"

namespace klay::library
{

LibraryScanner::~LibraryScanner()
{
    stop();
}

void LibraryScanner::stop()
{
    cancel.store(true, std::memory_order_release);
    if (worker.joinable())
        worker.join();
    cancel.store(false, std::memory_order_release);
}

void LibraryScanner::start(Config config)
{
    stop();
    fromCache.store(false, std::memory_order_release);
    {
        std::lock_guard lock(mutex);
        current = std::make_shared<SampleIndex>();
        stats = {};
        publishVersion.fetch_add(1, std::memory_order_acq_rel);
        state.store(config.root.empty() ? Status::idle : Status::scanning, std::memory_order_release);
    }
    cv.notify_all();
    if (config.root.empty())
        return;
    worker = std::thread([this, config = std::move(config)] { run(config); });
}

void LibraryScanner::publish(SampleIndex index)
{
    {
        std::lock_guard lock(mutex);
        current = std::make_shared<const SampleIndex>(std::move(index));
    }
    publishVersion.fetch_add(1, std::memory_order_acq_rel);
}

void LibraryScanner::run(Config config)
{
    const auto rootUtf8 = toUtf8(config.root);
    std::optional<SampleIndex> cached;
    if (! config.cacheFile.empty())
        cached = loadIndex(config.cacheFile);
    if (cached && cached->root == rootUtf8)
    {
        fromCache.store(true, std::memory_order_release);
        publish(*cached); // NFR-008: usable before the folder check finishes
    }
    else
        cached.reset();

    BuildStats built;
    auto index = buildIndex(config.root, config.analyzer, cached ? &*cached : nullptr, &built, config.threads, &cancel);
    if (built.cancelled)
        return;

    const bool changed = ! cached || built.analysed > 0 || built.removed > 0 || built.failed > 0
                         || index.samples.size() != cached->samples.size();
    if (changed && ! config.cacheFile.empty())
        saveIndex(config.cacheFile, index);
    {
        std::lock_guard lock(mutex);
        stats = built;
        current = std::make_shared<const SampleIndex>(std::move(index));
        publishVersion.fetch_add(1, std::memory_order_acq_rel);
        state.store(Status::ready, std::memory_order_release); // under the mutex: waitUntilReady cannot miss it
    }
    cv.notify_all();
}

std::shared_ptr<const SampleIndex> LibraryScanner::snapshot() const
{
    std::lock_guard lock(mutex);
    return current;
}

BuildStats LibraryScanner::lastStats() const
{
    std::lock_guard lock(mutex);
    return stats;
}

bool LibraryScanner::waitUntilReady(std::chrono::milliseconds timeout) const
{
    std::unique_lock lock(mutex);
    return cv.wait_for(lock, timeout, [this] { return state.load(std::memory_order_acquire) != Status::scanning; });
}

} // namespace klay::library
