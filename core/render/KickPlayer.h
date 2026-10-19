// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source. See THIRD_PARTY_LICENSES.md (C-006).
#pragma once

#include "core/render/RenderEngine.h"

#include <array>
#include <memory>
#include <vector>

namespace klay::render
{

// Transposed pitch-following buses for one note, from the transposition cache (FR-121, FR-125). Built off the audio
// thread; the player only reads them.
struct TransposedBuses
{
    std::uint64_t generation = 0; // the RenderedKick it was built from
    int semitones = 0;
    std::array<StereoBuffer, kNumBuses> follow;
};

// Where the player finds cached transpositions (task 1.12). Every call is made on the audio thread and must be
// real-time safe.
class TranspositionSource
{
public:
    virtual ~TranspositionSource() = default;
    // Protects and returns the cached buses for `note` of `generation` in the player's slot, or nullptr (then the
    // player falls back to real-time Repitch). `slot` is 0 or 1.
    virtual const TransposedBuses* acquire(int slot, std::uint64_t generation, int note) noexcept = 0;
    virtual void release(int slot) noexcept = 0;
};

// Monophonic real-time kick player (FR-001, FR-115, FR-121, FR-125, FR-236). Plays the published render: fixed
// layers as rendered, pitch-following layers transposed by (note − detected root), from the cache when ready, else
// by real-time Repitch with preallocated resamplers. A new note cuts the ringing kick with a 5 ms fade; velocity is
// ignored. Everything after prepare() is real-time safe (NFR-001).
class KickPlayer
{
public:
    // Buses are summed into `left`/`right` per bus so M4 can run the aux chains; M1 output = sum of all three.
    struct BusOutputs
    {
        std::array<float*, kNumBuses> left{};
        std::array<float*, kNumBuses> right{};
    };

    void prepare(double sampleRate, int maxBlockSize, const ResamplerFactory& factory);

    // Starts a kick at the next rendered sample; `note` < 0 plays untransposed (audition, FR-116).
    void noteOn(RenderEngine& engine, TranspositionSource* cache, int note) noexcept;
    // FR-236: fades the ringing kick out over 5 ms (transport stop).
    void stop() noexcept;
    // Adds numSamples of output to the bus buffers.
    void render(const BusOutputs& out, int numSamples) noexcept;
    // Releases everything the player holds (call before the engine goes away).
    void reset(RenderEngine& engine, TranspositionSource* cache) noexcept;

    bool isPlaying() const noexcept { return voices[0].active || voices[1].active; }
    // Transposition of the newest voice in semitones, and whether it came from the cache (tests, AC-4/AC-5).
    int lastTranspose() const noexcept { return lastSemitones; }
    bool lastFromCache() const noexcept { return lastCached; }

private:
    struct Voice
    {
        int slot = 0; // hazard slot in the engine and the cache
        bool active = false;
        const RenderedKick* kick = nullptr;
        const TransposedBuses* cached = nullptr;
        int semitones = 0;
        double ratio = 1.0;
        size_t position = 0;     // output samples played
        size_t followInput = 0;  // fallback: input samples consumed
        bool followDone = false;
        float gain = 1.0f;
        float gainStep = 0.0f; // > 0 while fading out
        std::array<std::unique_ptr<Resampler>, kNumBuses * 2> resamplers;
    };

    void renderVoice(Voice& v, const BusOutputs& out, int offset, int numSamples) noexcept;
    void finish(Voice& v) noexcept;

    std::array<Voice, 2> voices;
    int newest = 0;
    RenderEngine* engine = nullptr;
    TranspositionSource* source = nullptr;
    std::vector<float> scratch;
    float fadeStep = 1.0f;
    int lastSemitones = 0;
    bool lastCached = false;
};

} // namespace klay::render
