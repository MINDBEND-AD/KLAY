// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source, uses JUCE (AGPLv3). See THIRD_PARTY_LICENSES.md (C-006).
#include "plugin/PluginProcessor.h"
#include "plugin/PluginEditor.h"

#include "adapters/JuceResampler.h"
#include "adapters/SampleLoader.h"
#include "core/model/KickJson.h"
#include "core/render/TimeBase.h"
#include "core/rt/RtGuard.h"

namespace klay
{

std::shared_ptr<const render::SampleData> SampleCache::get(const std::string& path, double sampleRate)
{
    std::lock_guard lock(mutex);
    const auto key = std::make_pair(path, sampleRate);
    if (const auto it = loaded.find(key); it != loaded.end())
        return it->second;
    auto result = adapters::loadSample(path, sampleRate);
    if (result.sample) // failures are not cached, so a file that appears later is picked up
        loaded[key] = result.sample;
    return result.sample;
}

KlayProcessor::KlayProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    engine = std::make_unique<render::KickEngine>(render::RenderEngine::Config{
        [this](const std::string& path) { return sampleCache.get(path, currentSampleRate); },
        adapters::lagrangeResamplerFactory(), true });
    requestRender();
    startTimerHz(20);
}

KlayProcessor::~KlayProcessor()
{
    stopTimer();
    player.reset(*engine, engine.get());
    engine.reset();
}

void KlayProcessor::prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock)
{
    // FR-005: 44.1–192 kHz, 16–4096 samples. Everything the audio thread needs is allocated here (NFR-001).
    player.reset(*engine, engine.get());
    scratchBlock = juce::jmax(16, maximumExpectedSamplesPerBlock);
    busScratch.assign(static_cast<size_t>(scratchBlock) * 6, 0.0f);
    player.prepare(sampleRate, scratchBlock, adapters::lagrangeResamplerFactory());
    wasPlaying = false;
    if (sampleRate != currentSampleRate)
    {
        currentSampleRate = sampleRate; // parts are reloaded at the new rate (plan decision 18)
        requestRender();
    }
}

void KlayProcessor::releaseResources()
{
    player.reset(*engine, engine.get());
}

bool KlayProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.inputBuses.isEmpty() && layouts.outputBuses.size() == 1
           && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void KlayProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    // Real-time path (NFR-001, FR-234): no allocation, locks, I/O or logging. RT_GUARD lets the test harness
    // catch violations; it costs one thread-local increment in the shipped plugin.
    RT_GUARD;
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    const int numSamples = buffer.getNumSamples();

    // FR-002: host tempo for the debounced re-render; FR-236: fade on transport stop.
    if (auto* head = getPlayHead())
        if (const auto position = head->getPosition())
        {
            const auto bpm = position->getBpm();
            hostBpm.store(bpm ? *bpm : -1.0, std::memory_order_relaxed);
            const bool playing = position->getIsPlaying();
            if (wasPlaying && ! playing)
                player.stop();
            wasPlaying = playing;
        }

    if (auditionRequested.exchange(false, std::memory_order_acq_rel))
        player.noteOn(*engine, engine.get(), -1); // FR-116: untransposed

    if (scratchBlock <= 0 || buffer.getNumChannels() < 2)
    {
        midi.clear();
        return;
    }

    // Renders [from, to) of the block into the bus scratch and adds the summed buses to the output (M1: no FX chains
    // yet, so Main + A1 + A2 are summed directly).
    auto renderRange = [&](int from, int to) {
        while (from < to)
        {
            const int n = juce::jmin(to - from, scratchBlock);
            std::fill(busScratch.begin(), busScratch.end(), 0.0f);
            render::KickPlayer::BusOutputs out;
            for (size_t b = 0; b < render::kNumBuses; ++b)
            {
                out.left[b] = busScratch.data() + (b * 2) * static_cast<size_t>(scratchBlock);
                out.right[b] = busScratch.data() + (b * 2 + 1) * static_cast<size_t>(scratchBlock);
            }
            player.render(out, n);
            for (int ch = 0; ch < 2; ++ch)
            {
                float* dst = buffer.getWritePointer(ch, from);
                for (size_t b = 0; b < render::kNumBuses; ++b)
                {
                    const float* src = ch == 0 ? out.left[b] : out.right[b];
                    for (int i = 0; i < n; ++i)
                        dst[i] += src[i];
                }
            }
            from += n;
        }
    };

    // FR-001: note-ons start the kick sample-accurately; velocity and note-offs are ignored.
    int position = 0;
    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();
        if (! message.isNoteOn())
            continue;
        const int at = juce::jlimit(0, numSamples, metadata.samplePosition);
        renderRange(position, at);
        position = at;
        player.noteOn(*engine, engine.get(), message.getNoteNumber());
    }
    renderRange(position, numSamples);
    midi.clear();
}

juce::AudioProcessorEditor* KlayProcessor::createEditor()
{
    return new KlayEditor(*this);
}

std::uint64_t KlayProcessor::requestRender()
{
    return engine->requestRender(kick, currentSampleRate, getEffectiveBpm());
}

double KlayProcessor::getEffectiveBpm() const
{
    return render::effectiveBpm(kick.settings, tempo.current());
}

std::uint64_t KlayProcessor::setKick(model::Kick newKick)
{
    model::sanitise(newKick);
    kick = std::move(newKick);
    return requestRender();
}

void KlayProcessor::pollHostTempo(double nowMs)
{
    const double bpm = hostBpm.load(std::memory_order_relaxed);
    const auto host = bpm > 0.0 ? std::optional<double>(bpm) : std::nullopt;
    if (tempo.update(host, nowMs) && ! kick.settings.manualBpmOverride)
        requestRender();
}

void KlayProcessor::timerCallback()
{
    pollHostTempo(juce::Time::getMillisecondCounterHiRes());
}

std::vector<std::string> KlayProcessor::getMissingSamples() const
{
    const auto latest = engine->latest();
    return latest ? latest->missingSamples : std::vector<std::string>{};
}

void KlayProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    const auto json = model::toJson(kick);
    destData.replaceAll(json.data(), json.size());
}

void KlayProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0)
        return;

    const std::string_view json(static_cast<const char*>(data), static_cast<size_t>(sizeInBytes));
    std::vector<std::string> warnings;
    if (auto parsed = model::kickFromJson(json, &warnings))
    {
        loadWarnings = std::move(warnings);
        setKick(std::move(*parsed));
    }
}

} // namespace klay

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new klay::KlayProcessor();
}
