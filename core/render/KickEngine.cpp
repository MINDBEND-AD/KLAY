// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source. See THIRD_PARTY_LICENSES.md (C-006).
#include "core/render/KickEngine.h"
#include "core/render/Stretcher.h"

#include <algorithm>
#include <future>
#include <thread>

namespace klay::render
{

namespace
{
    size_t entryBytes(const TransposedBuses& entry)
    {
        size_t total = 0;
        for (const auto& bus : entry.follow)
            total += bus.size() * 2 * sizeof(float);
        return total;
    }

    bool isSilent(const StereoBuffer& b)
    {
        for (int ch = 0; ch < 2; ++ch)
            for (size_t i = 0; i < b.size(); ++i)
                if (b.channel(ch)[i] != 0.0f)
                    return false;
        return true;
    }
} // namespace

KickEngine::KickEngine(Config c, CacheConfig cc) : RenderEngine(std::move(c)), cacheConfig(cc)
{
}

KickEngine::~KickEngine()
{
    stopWorker(); // the worker calls our hooks, so it must stop before our members go away
    for (auto& p : published)
        p.store(nullptr);
}

const TransposedBuses* KickEngine::acquire(int slot, std::uint64_t gen, int note) noexcept
{
    if (note < 0 || note >= kNumNotes)
        return nullptr;
    const auto n = static_cast<size_t>(note);
    const TransposedBuses* entry = cacheHazards.protect(slot, published[n]);
    if (entry != nullptr && entry->generation == gen)
    {
        lastPlayed[n].store(playClock.fetch_add(1, std::memory_order_relaxed) + 1, std::memory_order_relaxed);
        return entry;
    }
    cacheHazards.clear(slot);
    requested[n].store(true, std::memory_order_relaxed); // "others on first play" (spec §10)
    return nullptr;
}

bool KickEngine::isCached(int note) const noexcept
{
    if (note < 0 || note >= kNumNotes)
        return false;
    const auto* entry = published[static_cast<size_t>(note)].load(std::memory_order_acquire);
    return entry != nullptr && entry->generation == generation.load(std::memory_order_acquire);
}

bool KickEngine::waitForCached(int note, std::chrono::milliseconds timeout) const
{
    const auto end = std::chrono::steady_clock::now() + timeout;
    while (! isCached(note))
    {
        if (std::chrono::steady_clock::now() > end)
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

bool KickEngine::waitForPrebuild(std::chrono::milliseconds timeout) const
{
    const auto end = std::chrono::steady_clock::now() + timeout;
    while (! prebuildFinished() || renderPending())
    {
        if (std::chrono::steady_clock::now() > end)
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

void KickEngine::unpublish(int note)
{
    const auto n = static_cast<size_t>(note);
    published[n].store(nullptr, std::memory_order_seq_cst);
    if (owned[n])
    {
        bytes.fetch_sub(entryBytes(*owned[n]), std::memory_order_acq_rel);
        count.fetch_sub(1, std::memory_order_acq_rel);
        retired.push_back(std::move(owned[n]));
        owned[n].reset();
    }
}

void KickEngine::freeRetired()
{
    retired.erase(std::remove_if(retired.begin(), retired.end(),
                                 [this](const std::shared_ptr<const TransposedBuses>& e) { return ! cacheHazards.isProtected(e.get()); }),
                  retired.end());
}

void KickEngine::onPublished(const std::shared_ptr<const RenderedKick>& kick)
{
    // A new render invalidates every entry; entries a voice still plays stay alive until it lets go.
    for (int note = 0; note < kNumNotes; ++note)
    {
        unpublish(note);
        requested[static_cast<size_t>(note)].store(false, std::memory_order_relaxed);
    }
    freeRetired();

    generation.store(kick->generation, std::memory_order_release);
    root = kick->key.detected ? kick->key.midiNote : -1; // Q6: no key → follow layers play untransposed
    hasFollow = false;
    for (const auto& bus : kick->buses)
        hasFollow = hasFollow || ! isSilent(bus.follow);

    prebuildOrder.clear();
    prebuildNext = 0;
    if (root >= 0 && hasFollow)
        for (int d = 1; d <= cacheConfig.prebuildRange; ++d) // nearest first (spec §10)
            for (int note : { root + d, root - d })
                if (note >= 0 && note < kNumNotes)
                    prebuildOrder.push_back(note);
    prebuildDone.store(prebuildOrder.empty(), std::memory_order_release);
}

bool KickEngine::build(const RenderedKick& kick, int note, bool mayEvict)
{
    const auto n = static_cast<size_t>(note);
    if (owned[n] || note == root)
        return false;

    auto entry = std::make_shared<TransposedBuses>();
    entry->generation = kick.generation;
    entry->semitones = note - root;
    size_t needed = 0;
    for (size_t b = 0; b < kNumBuses; ++b)
        needed += isSilent(kick.buses[b].follow) ? 0 : kick.length * 2 * sizeof(float);

    // NFR-014 cap: prebuilt notes only fill free space; a played note evicts the least recently played ones.
    while (bytes.load() + needed > cacheConfig.capBytes)
    {
        if (! mayEvict)
            return false;
        int victim = -1;
        for (int other = 0; other < kNumNotes; ++other)
            if (owned[static_cast<size_t>(other)]
                && (victim < 0 || lastPlayed[static_cast<size_t>(other)].load() < lastPlayed[static_cast<size_t>(victim)].load()))
                victim = other;
        if (victim < 0)
            return false; // a single note is larger than the cap
        unpublish(victim);
    }

    if (renderPending())
        return false; // a newer edit is waiting: this render's cache is obsolete

    // The buses of one note are independent: shift them in parallel (AC-2: root ±2 with 3 follow buses took 130 ms
    // one after another, target 100 ms). Results do not depend on scheduling (NFR-009).
    std::array<std::future<StereoBuffer>, kNumBuses> jobs;
    for (size_t b = 0; b < kNumBuses; ++b)
        if (! isSilent(kick.buses[b].follow))
            jobs[b] = std::async(std::launch::async, [this, &kick, b, semitones = entry->semitones] {
                return stretch(kick.buses[b].follow, kick.sampleRate, 1.0, semitones, cacheConfig.preset, config.resamplers);
            });
    for (size_t b = 0; b < kNumBuses; ++b)
        if (jobs[b].valid())
            entry->follow[b] = jobs[b].get();
    if (renderPending())
        return false;

    bytes.fetch_add(entryBytes(*entry), std::memory_order_acq_rel);
    count.fetch_add(1, std::memory_order_acq_rel);
    owned[n] = std::move(entry);
    published[n].store(owned[n].get(), std::memory_order_seq_cst);
    return true;
}

bool KickEngine::idleWork(const std::shared_ptr<const RenderedKick>& kick)
{
    freeRetired();
    if (root < 0 || ! hasFollow)
        return false;

    // Notes that were played before they were ready come first.
    for (int note = 0; note < kNumNotes; ++note)
        if (requested[static_cast<size_t>(note)].exchange(false, std::memory_order_relaxed))
            if (build(*kick, note, true))
                return true;

    while (prebuildNext < prebuildOrder.size())
    {
        const int note = prebuildOrder[prebuildNext++];
        const bool built = build(*kick, note, false);
        if (prebuildNext == prebuildOrder.size())
            prebuildDone.store(true, std::memory_order_release);
        if (built)
            return true;
    }
    prebuildDone.store(true, std::memory_order_release);
    return false;
}

} // namespace klay::render
