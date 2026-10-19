// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source. See THIRD_PARTY_LICENSES.md (C-006).
#pragma once

#include "core/model/Kick.h"
#include "core/render/Resampler.h"
#include "core/render/StereoBuffer.h"

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace klay::render
{

inline constexpr int kNumBuses = 3; // Main-routed, Aux 1, Aux 2 (FR-107)

// One bus of the render cache, split for Key Track (FR-107, FR-125): pitch-following layers are transposed in real
// time, fixed layers never.
struct BusAudio
{
    StereoBuffer follow;
    StereoBuffer fixed;
};

// Result of the key detection on the key layer (FR-120, FR-123 data). Filled by the render engine (task 1.11).
struct KeyInfo
{
    bool detected = false; // false: no key, the display shows "—" and follow layers play untransposed (Q6)
    double hz = 0.0;       // YIN estimate
    double fftHz = 0.0;    // FFT cross-check
    bool uncertain = false;
    int midiNote = -1;     // nearest MIDI note, C4 = 60
    double centsOff = 0.0; // hz relative to midiNote
    std::string name;      // e.g. "F#1"
};

// The offline render of the whole stack (FR-107): what the audio thread plays.
struct RenderedKick
{
    double sampleRate = 0.0;
    double bpm = 0.0;
    size_t length = 0; // samples, the kick length (FR-108)
    std::array<BusAudio, kNumBuses> buses;
    float autogain = 1.0f; // FR-109 gain already applied to every bus
    KeyInfo key;
    std::vector<std::string> missingSamples; // FR-003: reported, the layer stays silent
    std::uint64_t generation = 0;
};

using SampleProvider = std::function<std::shared_ptr<const SampleData>(const std::string& path)>;

// Processed parts (trim + stretch/repitch) from earlier renders, keyed by everything that affects them, so an edit of
// fader, offset, envelope, mute/solo or routing does not re-run Signalsmith (NFR-004). Thread-safe.
class LayerCache
{
public:
    std::shared_ptr<const StereoBuffer> find(const std::string& key);
    void store(const std::string& key, std::shared_ptr<const StereoBuffer> audio);
    // Key detection results per processed part (FR-120), so edits that keep the key layer's part skip YIN.
    std::optional<KeyInfo> findKey(const std::string& key);
    void storeKey(const std::string& key, KeyInfo info);
    // Drops every entry not used since the last call.
    void trim();
    size_t size();

private:
    std::mutex mutex;
    std::map<std::string, std::pair<std::shared_ptr<const StereoBuffer>, bool>> entries; // audio, used since trim
    std::map<std::string, std::pair<KeyInfo, bool>> keys;
};

struct RenderContext
{
    double sampleRate = 48000.0;
    double bpm = 155.0; // effective BPM (FR-002), already resolved
    SampleProvider samples;
    ResamplerFactory resamplers;
    LayerCache* layerCache = nullptr; // optional
    bool parallel = true;             // process layers on worker threads
};

// FR-110, FR-111: a layer is heard unless muted; while any layer is soloed only soloed layers are heard; Mute beats
// Solo.
bool isAudible(const model::Kick& kick, size_t index) noexcept;

// Start/end trim then Stretch (Signalsmith, pitch kept) or Repitch (varispeed) of one part, before the offset
// (FR-102, FR-104–106). In Repitch mode the Pitch knob is ignored (greyed out in the UI) and the Stretch knob is
// the varispeed: length × s, pitch −12·log2(s) semitones.
StereoBuffer processPart(const model::Layer& layer, const StereoBuffer& sample, const RenderContext& context,
                         model::StretchPreset preset);

// FR-107 offline render: for each audible layer, processPart → offset → volume envelope → fader → its bus (Main / A1
// / A2 × follow / fixed); then the kick length cut with the 5 ms end fade (FR-108) and autogain (FR-109, Q7: one gain
// from the summed buses, applied to all). Deterministic (NFR-009).
RenderedKick renderKick(const model::Kick& kick, const RenderContext& context);

} // namespace klay::render
