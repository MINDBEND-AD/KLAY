// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source. See THIRD_PARTY_LICENSES.md (C-006).
#include "core/render/StackRenderer.h"
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

void LayerCache::trim()
{
    std::lock_guard lock(mutex);
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

    const double semitones = layer.pitchSemitones + layer.pitchCents / 100.0;
    if (layer.mode == model::LayerMode::stretch)
        return stretch(trimmed, sr, layer.stretch, semitones, preset);

    auto lengthChanged = stretch(trimmed, sr, layer.stretch, 0.0, preset);
    if (semitones == 0.0)
        return lengthChanged;
    return repitch(lengthChanged, semitones, context.resamplers);
}

namespace
{
    std::string partKey(const model::Layer& layer, double sampleRate, model::StretchPreset preset)
    {
        std::ostringstream key;
        key.precision(17);
        key << layer.samplePath << '|' << sampleRate << '|' << layer.startTrimMs << '|' << layer.endTrimMs << '|'
            << layer.pitchSemitones << '|' << layer.pitchCents << '|' << layer.stretch << '|' << model::toString(layer.mode)
            << '|' << model::toString(preset);
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
    std::vector<std::shared_ptr<const SampleData>> samples(kick.layers.size());
    for (size_t i = 0; i < kick.layers.size(); ++i)
    {
        const auto& path = kick.layers[i].samplePath;
        if (path.empty())
            continue;
        samples[i] = context.samples ? context.samples(path) : nullptr;
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
