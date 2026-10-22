// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source. See THIRD_PARTY_LICENSES.md (C-006).
#include "core/render/StackRenderer.h"
#include "core/library/KeyDetector.h"
#include "core/render/Envelope.h"
#include "core/render/Stretcher.h"
#include "core/render/TimeBase.h"

#include <algorithm>
#include <cmath>
#include <future>
#include <sstream>

namespace klay::render
{

std::shared_ptr<const StereoBuffer> LayerCache::find(const std::string& key)
{
    std::lock_guard lock(mutex);
    const auto it = entries.find(key);
    if (it == entries.end())
        return nullptr;
    it->second.second = true;
    return it->second.first;
}

void LayerCache::store(const std::string& key, std::shared_ptr<const StereoBuffer> audio)
{
    std::lock_guard lock(mutex);
    entries[key] = { std::move(audio), true };
}

std::optional<KeyInfo> LayerCache::findKey(const std::string& key)
{
    std::lock_guard lock(mutex);
    const auto it = keys.find(key);
    if (it == keys.end())
        return std::nullopt;
    it->second.second = true;
    return it->second.first;
}

void LayerCache::storeKey(const std::string& key, KeyInfo info)
{
    std::lock_guard lock(mutex);
    keys[key] = { std::move(info), true };
}

void LayerCache::trim()
{
    std::lock_guard lock(mutex);
    for (auto it = keys.begin(); it != keys.end();)
    {
        if (! it->second.second)
            it = keys.erase(it);
        else
            (it++)->second.second = false;
    }
    for (auto it = entries.begin(); it != entries.end();)
    {
        if (! it->second.second)
            it = entries.erase(it);
        else
            (it++)->second.second = false;
    }
}

size_t LayerCache::size()
{
    std::lock_guard lock(mutex);
    return entries.size();
}

bool isAudible(const model::Kick& kick, size_t index) noexcept
{
    const auto& layer = kick.layers[index];
    if (layer.mute)
        return false; // Mute beats Solo (FR-111)
    const bool anySolo = std::any_of(kick.layers.begin(), kick.layers.end(), [](const model::Layer& l) { return l.solo; });
    return ! anySolo || layer.solo;
}

StereoBuffer processPart(const model::Layer& layer, const StereoBuffer& sample, const RenderContext& context,
                         model::StretchPreset preset)
{
    const double sr = context.sampleRate;
    const size_t start = std::min(sample.size(), msToSamples(layer.startTrimMs, sr));
    const size_t endCut = std::min(sample.size() - start, msToSamples(layer.endTrimMs, sr));
    StereoBuffer trimmed(sample.size() - start - endCut);
    for (int ch = 0; ch < 2; ++ch)
        std::copy_n(sample.channel(ch) + start, trimmed.size(), trimmed.channel(ch));

    // FR-106 (spec v0.10): only the active mode is applied; the other modes' values are kept but ignored.
    switch (layer.mode)
    {
        case model::LayerMode::pitch:
        {
            const double semitones = activeSemitones(layer);
            return semitones == 0.0 ? trimmed : stretch(trimmed, sr, 1.0, semitones, preset, context.resamplers); // FR-104, exact pitch
        }
        case model::LayerMode::tape:
            // Speed s: length / s, pitch +12·log2(s) semitones (2× = half the length, one octave up).
            return layer.tapeSpeed == 1.0 ? trimmed : repitch(trimmed, 12.0 * std::log2(layer.tapeSpeed), context.resamplers);
        case model::LayerMode::length:
            break;
    }
    return layer.length == 1.0 ? trimmed : stretch(trimmed, sr, layer.length, 0.0, preset); // FR-105
}

double activeSemitones(const model::Layer& layer) noexcept
{
    switch (layer.mode)
    {
        case model::LayerMode::pitch: return layer.pitchSemitones + layer.pitchCents / 100.0;
        case model::LayerMode::tape: return 12.0 * std::log2(layer.tapeSpeed);
        case model::LayerMode::length: break;
    }
    return 0.0;
}

namespace
{
    std::string partKey(const model::Layer& layer, double sampleRate, model::StretchPreset preset)
    {
        std::ostringstream key;
        key.precision(17);
        // Only the active mode's value affects the part, so switching back to a mode finds its cached part again.
        key << layer.samplePath << '|' << sampleRate << '|' << layer.startTrimMs << '|' << layer.endTrimMs << '|'
            << model::toString(layer.mode) << '|';
        switch (layer.mode)
        {
            case model::LayerMode::pitch: key << layer.pitchSemitones << '|' << layer.pitchCents; break;
            case model::LayerMode::tape: key << layer.tapeSpeed; break;
            case model::LayerMode::length: key << layer.length; break;
        }
        key << '|' << model::toString(preset);
        return key.str();
    }

    std::shared_ptr<const StereoBuffer> processedPart(const model::Layer& layer, const SampleData& sample,
                                                      const RenderContext& context, model::StretchPreset preset)
    {
        const auto key = context.layerCache ? partKey(layer, context.sampleRate, preset) : std::string{};
        if (context.layerCache)
            if (auto cached = context.layerCache->find(key))
                return cached;
        auto part = std::make_shared<const StereoBuffer>(processPart(layer, sample.audio, context, preset));
        if (context.layerCache)
            context.layerCache->store(key, part);
        return part;
    }

    float dbToGain(float db) noexcept
    {
        return db <= model::kMinusInfinityDb ? 0.0f : std::pow(10.0f, db / 20.0f);
    }
} // namespace

RenderedKick renderKick(const model::Kick& kick, const RenderContext& context)
{
    RenderedKick result;
    result.sampleRate = context.sampleRate;
    result.bpm = context.bpm;
    result.length = kickLengthSamples(kick.settings.kickLength, context.bpm, context.sampleRate);
    for (auto& bus : result.buses)
    {
        bus.follow = StereoBuffer(result.length);
        bus.fixed = StereoBuffer(result.length);
    }

    // Load (and report) every referenced part, also of silent layers, so missing files are always listed.
    // Each distinct file is loaded once, all of them in parallel when allowed: after a Forge the new samples are read
    // and resampled together (NFR-005). Results are assigned in layer order, so the render does not depend on timing.
    std::vector<std::string> paths;
    for (const auto& layer : kick.layers)
        if (! layer.samplePath.empty() && std::find(paths.begin(), paths.end(), layer.samplePath) == paths.end())
            paths.push_back(layer.samplePath);
    std::vector<std::shared_ptr<const SampleData>> loadedFiles(paths.size());
    if (context.samples)
    {
        if (context.parallel && paths.size() > 1)
        {
            std::vector<std::future<std::shared_ptr<const SampleData>>> loads;
            for (const auto& path : paths)
                loads.push_back(std::async(std::launch::async, context.samples, path));
            for (size_t p = 0; p < paths.size(); ++p)
                loadedFiles[p] = loads[p].get();
        }
        else
            for (size_t p = 0; p < paths.size(); ++p)
                loadedFiles[p] = context.samples(paths[p]);
    }
    std::vector<std::shared_ptr<const SampleData>> samples(kick.layers.size());
    for (size_t i = 0; i < kick.layers.size(); ++i)
    {
        const auto& path = kick.layers[i].samplePath;
        if (path.empty())
            continue;
        samples[i] = loadedFiles[static_cast<size_t>(std::find(paths.begin(), paths.end(), path) - paths.begin())];
        if (! samples[i] && std::find(result.missingSamples.begin(), result.missingSamples.end(), path) == result.missingSamples.end())
            result.missingSamples.push_back(path);
    }

    // Trim + stretch/repitch per audible layer, in parallel when allowed. Each part is independent and summed in
    // layer order below, so the result does not depend on scheduling (NFR-009).
    std::vector<std::shared_ptr<const StereoBuffer>> parts(kick.layers.size());
    std::vector<std::future<std::shared_ptr<const StereoBuffer>>> jobs(kick.layers.size());
    for (size_t i = 0; i < kick.layers.size(); ++i)
    {
        const auto& layer = kick.layers[i];
        if (! samples[i] || ! isAudible(kick, i) || layer.faderDb <= model::kMinusInfinityDb)
            continue;
        if (context.parallel)
            jobs[i] = std::async(std::launch::async, processedPart, std::cref(layer), std::cref(*samples[i]),
                                 std::cref(context), kick.settings.stretchPreset);
        else
            parts[i] = processedPart(layer, *samples[i], context, kick.settings.stretchPreset);
    }
    for (size_t i = 0; i < jobs.size(); ++i)
        if (jobs[i].valid())
            parts[i] = jobs[i].get();

    // FR-120, FR-124: key of the key layer's processed part (before offset, envelope and fader), even when muted.
    if (const auto k = static_cast<size_t>(kick.settings.keyLayer); k < kick.layers.size() && samples[k])
    {
        const auto cacheKey = partKey(kick.layers[k], context.sampleRate, kick.settings.stretchPreset);
        if (auto known = context.layerCache ? context.layerCache->findKey(cacheKey) : std::nullopt)
        {
            result.key = *known;
        }
        else
        {
            const auto part = parts[k] ? parts[k] : processedPart(kick.layers[k], *samples[k], context, kick.settings.stretchPreset);
            std::vector<float> mono(part->size());
            for (size_t s = 0; s < mono.size(); ++s)
                mono[s] = 0.5f * (part->left[s] + part->right[s]);
            result.key = library::detectKey(mono.data(), mono.size(), context.sampleRate);
            if (context.layerCache)
                context.layerCache->storeKey(cacheKey, result.key);
        }
    }

    std::vector<float> envelope(result.length);
    for (size_t i = 0; i < kick.layers.size(); ++i)
    {
        if (! parts[i])
            continue;
        const auto& layer = kick.layers[i];
        const auto& part = *parts[i];
        const size_t offset = msToSamples(layer.offsetMs, context.sampleRate); // FR-102 sample-accurate
        if (offset >= result.length)
            continue;
        const size_t count = std::min(part.size(), result.length - offset);
        renderEnvelope(layer.envelope, context.sampleRate, offset, envelope.data(), count); // kick timeline (FR-283)
        const float fader = dbToGain(layer.faderDb);

        auto& bus = result.buses[static_cast<size_t>(layer.routing)];
        auto& target = layer.pitchFollow ? bus.follow : bus.fixed;
        for (int ch = 0; ch < 2; ++ch)
        {
            const float* src = part.channel(ch);
            float* dst = target.channel(ch) + offset;
            for (size_t s = 0; s < count; ++s)
                dst[s] += src[s] * envelope[s] * fader;
        }
    }

    // FR-108 end fade on every bus, then FR-109 autogain from the summed buses.
    for (auto& bus : result.buses)
        for (auto* buffer : { &bus.follow, &bus.fixed })
            for (int ch = 0; ch < 2; ++ch)
                applyEndFade(buffer->channel(ch), result.length, context.sampleRate);

    if (kick.settings.autogain)
    {
        float peak = 0.0f;
        for (int ch = 0; ch < 2; ++ch)
            for (size_t s = 0; s < result.length; ++s)
            {
                float sum = 0.0f;
                for (const auto& bus : result.buses)
                    sum += bus.follow.channel(ch)[s] + bus.fixed.channel(ch)[s];
                peak = std::max(peak, std::abs(sum));
            }
        if (peak > 0.0f)
        {
            result.autogain = static_cast<float>(std::pow(10.0, kick.settings.autogainTargetDb / 20.0)) / peak;
            for (auto& bus : result.buses)
                for (auto* buffer : { &bus.follow, &bus.fixed })
                    for (int ch = 0; ch < 2; ++ch)
                        for (size_t s = 0; s < result.length; ++s)
                            buffer->channel(ch)[s] *= result.autogain;
        }
    }
    return result;
}

} // namespace klay::render
