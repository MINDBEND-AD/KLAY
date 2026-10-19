// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source. See THIRD_PARTY_LICENSES.md (C-006).
#include "core/render/RenderEngine.h"

#include <algorithm>

namespace klay::render
{

RenderEngine::RenderEngine(Config c) : config(std::move(c))
{
    worker = std::thread([this] { run(); });
}

RenderEngine::~RenderEngine()
{
    stopWorker();
}

void RenderEngine::stopWorker()
{
    {
        std::lock_guard lock(mutex);
        stopping = true;
    }
    jobCv.notify_all();
    if (worker.joinable())
        worker.join();
}

void RenderEngine::wake()
{
    jobCv.notify_all();
}

std::uint64_t RenderEngine::requestRender(const model::Kick& kick, double sampleRate, double bpm)
{
    std::uint64_t generation = 0;
    {
        std::lock_guard lock(mutex);
        generation = ++nextGeneration;
        pending = Job{ kick, sampleRate, bpm, generation };
        pendingFlag.store(true, std::memory_order_release);
    }
    jobCv.notify_all();
    return generation;
}

std::shared_ptr<const RenderedKick> RenderEngine::latest() const
{
    std::lock_guard lock(mutex);
    return current;
}

bool RenderEngine::waitForGeneration(std::uint64_t generation, std::chrono::milliseconds timeout) const
{
    std::unique_lock lock(mutex);
    return published_cv.wait_for(lock, timeout, [&] { return current && current->generation >= generation; });
}

void RenderEngine::freeRetired()
{
    // Unpublished renders are freed here, on the worker thread, once no audio slot holds them.
    retired.erase(std::remove_if(retired.begin(), retired.end(),
                                 [this](const std::shared_ptr<const RenderedKick>& k) { return ! hazards.isProtected(k.get()); }),
                  retired.end());
}

void RenderEngine::run()
{
    std::shared_ptr<const RenderedKick> mine; // the worker's reference to the current render
    for (;;)
    {
        std::optional<Job> job;
        {
            std::unique_lock lock(mutex);
            jobCv.wait_for(lock, std::chrono::milliseconds(5), [this] { return stopping || pending.has_value(); });
            if (stopping)
                break;
            if (pending)
            {
                job = std::move(pending);
                pending.reset();
                pendingFlag.store(false, std::memory_order_release);
            }
        }

        if (job)
        {
            RenderContext context;
            context.sampleRate = job->sampleRate;
            context.bpm = job->bpm;
            context.samples = config.samples;
            context.resamplers = config.resamplers;
            context.layerCache = &layerCache;
            context.parallel = config.parallelLayers;

            auto rendered = std::make_shared<RenderedKick>(renderKick(job->kick, context));
            rendered->generation = job->generation;
            layerCache.trim();
            onPublished(rendered);

            std::shared_ptr<const RenderedKick> published_ = rendered;
            if (mine)
                retired.push_back(mine);
            mine = published_;
            published.store(mine.get(), std::memory_order_seq_cst);
            publishedGen.store(mine->generation, std::memory_order_release);
            {
                std::lock_guard lock(mutex);
                current = mine;
            }
            published_cv.notify_all();
        }

        freeRetired();
        while (mine && ! renderPending() && idleWork(mine))
            freeRetired();
    }

    // Shutdown: the audio thread is gone (the owner stops it first), so everything can be freed.
    published.store(nullptr);
    retired.clear();
}

} // namespace klay::render
