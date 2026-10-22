// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source. See THIRD_PARTY_LICENSES.md (C-006).
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace klay::model
{

// Current version of the plugin state / preset JSON (spec §10: versioned JSON with a schemaVersion field).
// 2: layer modes Length / Pitch / Tape with one value each (spec v0.10, FR-106); version 1 files are migrated on load.
inline constexpr int kSchemaVersion = 2;

inline constexpr int kDefaultLayerCount = 6; // FR-100
inline constexpr int kMaxLayerCount = 12;    // FR-100

// Fader floor: values at or below this are −∞ dB (silence). JSON has no infinity, so −∞ is stored as this value.
inline constexpr float kMinusInfinityDb = -144.0f;

enum class Role
{
    kick,
    rumble,
    drive,
    crunch,
    transient,
    texture,
    any
};

// FR-106: Length changes length only, Pitch changes pitch only (both Signalsmith-based), Tape is varispeed (repitch).
enum class LayerMode
{
    length,
    pitch,
    tape
};

// Bus a layer feeds (FR-107, glossary "Aux chain / routing"): all or nothing.
enum class Routing
{
    main,
    aux1,
    aux2
};

// FR-108 kick length as a note value (4/4 assumed for "1 bar").
enum class KickLength
{
    sixteenth,
    eighth,
    quarter,
    half,
    bar
};

// Signalsmith preset used for Stretch-mode layers; switchable for LT-1 (task 1.6).
enum class StretchPreset
{
    standard,
    cheaper
};

// One envelope point (FR-280–283). Time is absolute in ms from the kick start, so it is kept when the kick length
// changes (FR-283). `tension` shapes the segment from this point to the next one (Q10: power curve, −1…+1).
struct EnvelopePoint
{
    double timeMs = 0.0;
    float gain = 1.0f; // 0…1 (0–100 %)
    float tension = 0.0f;

    bool operator==(const EnvelopePoint&) const = default;
};

// Volume envelope; no points means flat at 100 % (FR-280 default).
struct Envelope
{
    std::vector<EnvelopePoint> points;

    bool operator==(const Envelope&) const = default;
};

struct Layer
{
    std::string name;
    Role role = Role::any;
    std::string samplePath; // empty: the layer holds no part

    float faderDb = 0.0f;   // FR-101: −∞ (kMinusInfinityDb) … 0 dB, default 0 dB
    double offsetMs = 0.0;  // FR-102: delay of the part on the kick timeline, ≥ 0
    double startTrimMs = 0.0; // FR-102: cut from the start of the sample
    double endTrimMs = 0.0;   // FR-102 (Q9): cut from the end of the sample
    // FR-106 (spec v0.10): one layer knob, three modes; each mode keeps its own value, only the active one applies.
    LayerMode mode = LayerMode::length;
    double length = 1.0;      // Length mode, FR-105: 0.25…8 × length, pitch kept
    int pitchSemitones = 0;   // Pitch mode, FR-104: ±12 st …
    int pitchCents = 0;       // … and ±100 ct, length kept
    double tapeSpeed = 1.0;   // Tape mode: playback speed 0.25…8× (2× = half the length, one octave up)

    bool mute = false;
    bool solo = false;
    bool lock = false;
    Routing routing = Routing::main;
    bool pitchFollow = false; // FR-122: off by default on every layer
    Envelope envelope;

    bool operator==(const Layer&) const = default;
};

struct Settings
{
    KickLength kickLength = KickLength::quarter; // FR-108 default 1/4
    double manualBpm = 155.0;                    // FR-002 default
    bool manualBpmOverride = false;              // FR-002: use manualBpm even when the host sends a tempo
    bool autogain = true;                        // FR-109 (Q7) default On
    double autogainTargetDb = -0.3;              // FR-109 target peak
    int keyLayer = 0;                            // FR-124: index, layer 1 by default
    int envelopeGrid = 4;                        // FR-282: 4, 8 or 16
    StretchPreset stretchPreset = StretchPreset::standard;
    // FR-146 roll template: the layers (0-based indices) that Forge re-rolls; default layers 1–3 (Kick, Rumble, Drive).
    std::vector<int> rollTemplate{ 0, 1, 2 };

    bool operator==(const Settings&) const = default;
};

// Full kick: the stack plus settings. FX chains join in M4.
struct Kick
{
    std::vector<Layer> layers;
    Settings settings;

    bool operator==(const Kick&) const = default;
};

// FR-114: 1 Kick, 2 Rumble, 3 Drive, 4 Crunch, 5 Transient, 6 Texture, 7–12 "Layer N"; roles follow the name.
Layer makeDefaultLayer(int index);

// FR-100: the default stack has kDefaultLayerCount empty layers.
Kick makeDefaultKick();

// FR-110: Shift+click on any Mute unmutes all layers when at least one is muted; a plain click toggles this layer.
void clickMute(Kick& kick, size_t index, bool shift);

// FR-111: Solo is exclusive, a new Solo replaces the previous ones; Shift+click adds (or removes) this layer's Solo.
// A plain click on the only soloed layer clears it.
void clickSolo(Kick& kick, size_t index, bool shift);

// FR-101, FR-143: Lock protects a layer from rolls and from the bin (FR-112). Out-of-range indices are ignored.
void toggleLock(Kick& kick, size_t index);

// FR-112: the bin empties a layer: its sample is removed (the card shows the empty state), every other setting
// stays. Disabled while the layer is locked. Returns whether the layer changed.
bool canBin(const Kick& kick, size_t index) noexcept;
bool binLayer(Kick& kick, size_t index);

// FR-114: renames a layer. Leading and trailing whitespace is dropped and control characters become spaces; an empty
// name restores the layer's default name.
void renameLayer(Kick& kick, size_t index, std::string_view name);

// FR-146: whether Forge re-rolls layer `index`.
bool inRollTemplate(const Settings& settings, size_t index) noexcept;

// Clamps every value to its spec range (FR-100–106, FR-108, FR-124, FR-282) and sorts envelope points by time.
void sanitise(Kick& kick);

const char* toString(Role role);
const char* toString(LayerMode mode);
const char* toString(Routing routing);
const char* toString(KickLength length);
const char* toString(StretchPreset preset);

} // namespace klay::model
