// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source. See THIRD_PARTY_LICENSES.md (C-006).
#include "core/render/KickPlayer.h"
#include "core/render/TimeBase.h"

#include <algorithm>
#include <cmath>

namespace klay::render
{

void KickPlayer::prepare(double sampleRate, int maxBlockSize, const ResamplerFactory& factory)
{
    fadeStep = 1.0f / static_cast<float>(std::max<size_t>(1, msToSamples(kNoteCutFadeMs, sampleRate)));
    scratch.assign(static_cast<size_t>(std::max(maxBlockSize, 64)) + 16, 0.0f);
    for (int i = 0; i < 2; ++i)
    {
        voices[static_cast<size_t>(i)].slot = i;
        for (auto& r : voices[static_cast<size_t>(i)].resamplers)
            r = factory ? factory() : nullptr;
    }
}

void KickPlayer::finish(Voice& v) noexcept
{
    if (engine)
        engine->release(v.slot);
    if (source)
        source->release(v.slot);
    v.active = false;
    v.kick = nullptr;
    v.cached = nullptr;
}

void KickPlayer::reset(RenderEngine& e, TranspositionSource* cache) noexcept
{
    engine = &e;
    source = cache;
    for (auto& v : voices)
        finish(v);
}

void KickPlayer::noteOn(RenderEngine& e, TranspositionSource* cache, int note) noexcept
{
    engine = &e;
    source = cache;

    // FR-115: the ringing kick fades out over 5 ms; a voice still fading from an earlier cut is dropped.
    auto& old = voices[static_cast<size_t>(newest)];
    auto& fresh = voices[static_cast<size_t>(1 - newest)];
    if (fresh.active)
        finish(fresh);
    if (old.active)
        old.gainStep = fadeStep;

    const RenderedKick* kick = e.acquire(fresh.slot);
    if (! kick || kick->length == 0)
    {
        e.release(fresh.slot);
        return;
    }

    // FR-121 (Q6): transpose relative to the key layer's root; untransposed without a detected key or for audition.
    int semitones = 0;
    if (note >= 0 && kick->key.detected)
        semitones = note - kick->key.midiNote;

    fresh.kick = kick;
    fresh.semitones = semitones;
    fresh.ratio = std::exp2(semitones / 12.0);
    fresh.position = 0;
    fresh.followInput = 0;
    fresh.followDone = false;
    fresh.gain = 1.0f; // FR-001: velocity ignored, full level
    fresh.gainStep = 0.0f;
    fresh.cached = (semitones != 0 && cache) ? cache->acquire(fresh.slot, kick->generation, note) : nullptr;
    fresh.active = true;

    // Real-time Repitch fallback: prime each resampler with its latency at ratio 1, so the first output sample is
    // the first input sample (no added latency, FR-125, NFR-003).
    if (semitones != 0 && ! fresh.cached)
        for (size_t b = 0; b < kNumBuses; ++b)
            for (int ch = 0; ch < 2; ++ch)
                if (auto& r = fresh.resamplers[b * 2 + static_cast<size_t>(ch)])
                {
                    r->reset();
                    const int latency = std::min<int>(r->latencySamples(), static_cast<int>(kick->length));
                    if (latency > 0)
                        fresh.followInput = static_cast<size_t>(
                            r->process(1.0, kick->buses[b].follow.channel(ch), scratch.data(), latency));
                }

    newest = 1 - newest;
    lastSemitones = semitones;
    lastCached = fresh.cached != nullptr;
}

void KickPlayer::stop() noexcept
{
    for (auto& v : voices)
        if (v.active && v.gainStep == 0.0f)
            v.gainStep = fadeStep;
}

void KickPlayer::renderVoice(Voice& v, const BusOutputs& out, int offset, int numSamples) noexcept
{
    const auto& kick = *v.kick;
    const size_t remaining = kick.length > v.position ? kick.length - v.position : 0;
    const size_t fixedCount = std::min<size_t>(remaining, static_cast<size_t>(numSamples));

    // Gain ramp shared by all buses of this voice for this block.
    const float startGain = v.gain;
    const float step = v.gainStep;
    const auto gainAt = [&](size_t i) { return std::max(0.0f, startGain - step * static_cast<float>(i + 1)); };

    for (size_t b = 0; b < kNumBuses; ++b)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            float* dst = (ch == 0 ? out.left[b] : out.right[b]) + offset;
            const float* fixedSrc = kick.buses[b].fixed.channel(ch) + v.position;
            for (size_t i = 0; i < fixedCount; ++i)
                dst[i] += fixedSrc[i] * gainAt(i);

            if (v.semitones == 0 || v.cached)
            {
                const auto& followBuffer = v.cached ? v.cached->follow[b] : kick.buses[b].follow;
                const float* src = followBuffer.channel(ch) + v.position;
                const size_t count = std::min(fixedCount, followBuffer.size() > v.position ? followBuffer.size() - v.position : 0);
                for (size_t i = 0; i < count; ++i)
                    dst[i] += src[i] * gainAt(i);
            }
        }
    }

    // Fallback Repitch of the follow buses: pitch and length change together, so it may run past the kick length
    // (shorter or longer than the fixed layers); it stops when its input is used up.
    size_t followCount = 0;
    if (v.semitones != 0 && ! v.cached && ! v.followDone && v.resamplers[0])
    {
        const size_t guard = static_cast<size_t>(v.resamplers[0]->latencySamples()) + 4;
        const size_t available = kick.length > v.followInput + guard ? kick.length - v.followInput - guard : 0;
        followCount = std::min<size_t>(static_cast<size_t>(numSamples), static_cast<size_t>(static_cast<double>(available) / v.ratio));
        int consumed = 0;
        for (size_t b = 0; b < kNumBuses; ++b)
            for (int ch = 0; ch < 2; ++ch)
            {
                auto& r = v.resamplers[b * 2 + static_cast<size_t>(ch)];
                consumed = r->process(v.ratio, kick.buses[b].follow.channel(ch) + v.followInput, scratch.data(),
                                      static_cast<int>(followCount));
                float* dst = (ch == 0 ? out.left[b] : out.right[b]) + offset;
                for (size_t i = 0; i < followCount; ++i)
                    dst[i] += scratch[i] * gainAt(i);
            }
        v.followInput += static_cast<size_t>(consumed);
        if (followCount < static_cast<size_t>(numSamples))
            v.followDone = true;
    }

    const size_t played = std::max(fixedCount, followCount);
    v.position += static_cast<size_t>(numSamples);
    v.gain = gainAt(static_cast<size_t>(numSamples) - 1);

    const bool followRunning = v.semitones != 0 && ! v.cached && ! v.followDone;
    if (v.gain <= 0.0f || (played < static_cast<size_t>(numSamples) && ! followRunning))
        finish(v);
}

void KickPlayer::render(const BusOutputs& out, int numSamples) noexcept
{
    if (numSamples <= 0)
        return;
    // The scratch buffer holds one block; longer calls are split.
    const int maxChunk = static_cast<int>(scratch.size()) - 16;
    for (int offset = 0; offset < numSamples; offset += maxChunk)
    {
        const int n = std::min(maxChunk, numSamples - offset);
        for (auto& v : voices)
            if (v.active)
                renderVoice(v, out, offset, n);
    }
}

} // namespace klay::render
