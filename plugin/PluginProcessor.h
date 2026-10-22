// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source, uses JUCE (AGPLv3). See THIRD_PARTY_LICENSES.md (C-006).
#pragma once

#include "core/library/LibraryScanner.h"
#include "core/model/Kick.h"
#include "core/model/UndoHistory.h"
#include "core/random/Roll.h"
#include "core/render/KickEngine.h"
#include "core/render/KickPlayer.h"
#include "core/render/TempoDebouncer.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
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

// Where the processor finds the sample library and keeps its user data (plan decisions 4 and 5). Unset fields use
// the defaults: data in %APPDATA%/MINDBEND AD/Klaymore, library folder from the KLAY_LIBRARY_PATH environment
// variable, else from <data>/library-path.txt (written by setLibraryRoot), else none.
struct ProcessorOptions
{
    std::optional<std::filesystem::path> libraryRoot;
    std::optional<std::filesystem::path> dataDir;
};

// M1 instrument (spec §10 plugin/): MIDI → monophonic kick player over the background render cache. Edits change
// the model on the message thread and request a re-render; the audio thread only reads published renders.
class KlayProcessor final : public juce::AudioProcessor, private juce::Timer
{
public:
    KlayProcessor() : KlayProcessor(ProcessorOptions{}) {}
    explicit KlayProcessor(ProcessorOptions options);
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
    // Replaces the kick and re-renders without an undo step (host state restore, tests); returns the render
    // generation. Edits go through edit() instead.
    std::uint64_t setKick(model::Kick newKick);

    // FR-145, FR-239: every model and settings edit. `change` edits a copy of the kick; the result is sanitised,
    // applied and re-rendered, and recorded as one undo step, unless a gesture is open: between beginGesture() and
    // endGesture() (mouse down to mouse up) edits apply live and the whole gesture becomes one step. Returns whether
    // the kick changed.
    bool edit(const std::string& label, const std::function<void(model::Kick&)>& change);
    void beginGesture(const std::string& label);
    void endGesture();
    bool undo();
    bool redo();
    const model::UndoHistory& getUndoHistory() const noexcept { return history; }
    // FR-116 audition API: plays the kick untransposed at the start of the next block.
    void triggerAudition() noexcept { auditionRequested.store(true, std::memory_order_release); }
    // FR-002: checks the host tempo seen by the audio thread and re-renders once a change has settled. Runs from a
    // timer; public for tests with a simulated playhead.
    void pollHostTempo(double nowMs);
    // FR-003: parts referenced by the kick that could not be loaded (from the newest render).
    std::vector<std::string> getMissingSamples() const;
    // NFR-012: notes from loading an older state (e.g. a spec v0.4 layer that now applies only one mode).
    const std::vector<std::string>& getLoadWarnings() const noexcept { return loadWarnings; }
    render::KickEngine& getEngine() noexcept { return *engine; }
    double getEffectiveBpm() const;
    // Generation of the newest render this processor requested (e.g. by an edit), for tests and benchmarks.
    std::uint64_t getRequestedGeneration() const noexcept { return requestedGeneration; }

    // FR-160, FR-165: the sample library index, kept up to date in the background (task 2.1).
    library::LibraryScanner& getLibrary() noexcept { return library; }
    const std::filesystem::path& getLibraryRoot() const noexcept { return libraryRoot; }
    const std::filesystem::path& getDataDir() const noexcept { return dataDir; }
    // Points the library at a new folder and re-scans; `persist` stores it in <data>/library-path.txt.
    void setLibraryRoot(const std::filesystem::path& root, bool persist = true);
    static std::filesystem::path defaultDataDir();

    // FR-140: role pools of the newest published library index (rebuilt when the scanner publishes a new one).
    const random::RolePools& getRolePools();
    // FR-141 Forge over the roll template / FR-142 Reforge of one layer, then a re-render. Without a seed a fresh one
    // is drawn; the seed used is returned and kept (FR-147).
    random::RollResult forge(std::optional<std::uint64_t> seed = std::nullopt);
    random::RollResult reforge(size_t layer, std::optional<std::uint64_t> seed = std::nullopt);
    std::uint64_t getLastRollSeed() const noexcept { return lastRollSeed; }

    // FR-113: the selected layer, layer 1 by default. UI state: not part of the kick, its state or the undo history.
    size_t getSelectedLayer() const noexcept { return std::min(selectedLayer, kick.layers.size() - 1); }
    void selectLayer(size_t index) noexcept { selectedLayer = std::min(index, kick.layers.size() - 1); }

private:
    void timerCallback() override;
    std::uint64_t requestRender();
    void startLibrary();

    model::Kick kick = model::makeDefaultKick();
    std::vector<std::string> loadWarnings;
    std::filesystem::path dataDir, libraryRoot;
    library::LibraryScanner library;
    random::RolePools pools;
    std::uint64_t poolsVersion = ~std::uint64_t{ 0 };
    std::uint64_t seedState = 0, lastRollSeed = 0;
    size_t selectedLayer = 0;
    std::uint64_t requestedGeneration = 0;
    model::UndoHistory history;
    model::Kick gestureStart;
    std::string gestureLabel;
    int gestureDepth = 0;
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
