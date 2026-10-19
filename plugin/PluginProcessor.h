// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source, uses JUCE (AGPLv3). See THIRD_PARTY_LICENSES.md (C-006).
#pragma once

#include "core/model/StateHeader.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace klay
{

// M0: empty instrument. MIDI in, stereo out, renders silence (C-001, FR-005). The voice, buffer swap and
// parameters arrive in M1 (spec §10, plugin/).
class KlayProcessor final : public juce::AudioProcessor
{
public:
    KlayProcessor();
    ~KlayProcessor() override = default;

    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    // The VST3 validator requires a non-empty name for every program (NFR-011).
    const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int, const juce::String&) override {}

    // FR-003 stub: the state is the versioned JSON from core/model.
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    const model::StateHeader& getStateHeader() const noexcept { return stateHeader; }

private:
    model::StateHeader stateHeader;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KlayProcessor)
};

} // namespace klay
