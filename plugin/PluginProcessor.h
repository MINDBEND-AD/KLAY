// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source, uses JUCE (AGPLv3). See THIRD_PARTY_LICENSES.md (C-006).
#pragma once

#include "core/model/Kick.h"
#include "core/render/KickEngine.h"
#include "core/render/KickPlayer.h"
#include "core/render/TempoDebouncer.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace klay
{

// Parts loaded for the render engine, at the session rate (FR-161, plan decision 18). Called on the engine's worker
// thread only (file I/O never runs on the audio thread, NFR-001).
class SampleCache
{
public:
    std::shared_ptr<const render::SampleData> get(const std::string& path, double sampleRate);

private:
    std::mutex mutex;
    std::map<std::pair<std::string, double>, std::shared_ptr<const render::SampleData>> loaded;
};

// M1 instrument (spec §10 plugin/): MIDI → monophonic kick player over the background render cache. Edits change
// the model on the message thread and request a re-render; the audio thread only reads published renders.
class KlayProcessor final : public juce::AudioProcessor, private juce::Timer
{
public:
    KlayProcessor();
    ~KlayProcessor() override;

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

    // FR-003: the full kick (stack + settings + sample paths) as versioned JSON.
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // ---- message thread ----
    const model::Kick& getKick() const noexcept { return kick; }
    // Replaces the kick and re-renders; returns the render generation.
    std::uint64_t setKick(model::Kick newKick);
    // FR-116 audition API: plays the kick untransposed at the start of the next block.
    void triggerAudition() noexcept { auditionRequested.store(true, std::memory_order_release); }
    // FR-002: checks the host tempo seen by the audio thread and re-renders once a change has settled. Runs from a
    // timer; public for tests with a simulated playhead.
    void pollHostTempo(double nowMs);
    // FR-003: parts referenced by the kick that could not be loaded (from the newest render).
    std::vector<std::string> getMissingSamples() const;
    render::KickEngine& getEngine() noexcept { return *engine; }
    double getEffectiveBpm() const;

private:
    void timerCallback() override;
    std::uint64_t requestRender();

    model::Kick kick = model::makeDefaultKick();
    SampleCache sampleCache;
    std::unique_ptr<render::KickEngine> engine;
    render::TempoDebouncer tempo;
    double currentSampleRate = 48000.0;

    // Audio thread.
    render::KickPlayer player;
    std::vector<float> busScratch; // 6 channels × maxBlock, preallocated in prepareToPlay
    int scratchBlock = 0;
    bool wasPlaying = false;

    std::atomic<double> hostBpm{ -1.0 }; // −1: the host sends no tempo
    std::atomic<bool> auditionRequested{ false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KlayProcessor)
};

} // namespace klay
