// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source, uses JUCE (AGPLv3). See THIRD_PARTY_LICENSES.md (C-006).
#include "plugin/PluginProcessor.h"
#include "plugin/PluginEditor.h"

#include "core/model/KickJson.h"
#include "core/rt/RtGuard.h"

namespace klay
{

KlayProcessor::KlayProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
}

void KlayProcessor::prepareToPlay(double, int)
{
    // FR-005: 44.1–192 kHz, 16–4096 samples. Nothing to prepare yet; M1 preallocates here, never in
    // processBlock (NFR-001).
}

void KlayProcessor::releaseResources() {}

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
    midi.clear();
    buffer.clear();
}

juce::AudioProcessorEditor* KlayProcessor::createEditor()
{
    return new KlayEditor(*this);
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
    if (auto parsed = model::kickFromJson(json))
        kick = std::move(*parsed);
}

} // namespace klay

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new klay::KlayProcessor();
}
