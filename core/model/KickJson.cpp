// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source. See THIRD_PARTY_LICENSES.md (C-006).
#include "core/model/KickJson.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cmath>

namespace klay::model
{

using nlohmann::json;

namespace
{
    // Reads `key` if present and of the right type; otherwise keeps `fallback` (NFR-012: missing fields defaulted).
    template <typename T>
    T read(const json& object, const char* key, T fallback)
    {
        const auto it = object.find(key);
        if (it == object.end())
            return fallback;
        if constexpr (std::is_same_v<T, bool>)
            return it->is_boolean() ? it->template get<bool>() : fallback;
        else if constexpr (std::is_integral_v<T>)
            return it->is_number_integer() ? static_cast<T>(it->template get<long long>()) : fallback;
        else if constexpr (std::is_floating_point_v<T>)
            return it->is_number() ? static_cast<T>(it->template get<double>()) : fallback;
        else
            return it->is_string() ? it->template get<std::string>() : fallback;
    }

    template <typename Enum, size_t N>
    Enum readEnum(const json& object, const char* key, Enum fallback, const std::array<Enum, N>& values)
    {
        const auto text = read<std::string>(object, key, {});
        for (auto value : values)
            if (text == toString(value))
                return value;
        return fallback;
    }

    constexpr std::array roles{ Role::kick, Role::rumble, Role::drive, Role::crunch, Role::transient, Role::texture, Role::any };
    constexpr std::array modes{ LayerMode::length, LayerMode::pitch, LayerMode::tape };
    constexpr std::array routings{ Routing::main, Routing::aux1, Routing::aux2 };
    constexpr std::array lengths{ KickLength::sixteenth, KickLength::eighth, KickLength::quarter, KickLength::half, KickLength::bar };
    constexpr std::array presets{ StretchPreset::standard, StretchPreset::cheaper };

    json layerToJson(const Layer& layer)
    {
        json points = json::array();
        for (const auto& p : layer.envelope.points)
            points.push_back({ { "timeMs", p.timeMs }, { "gain", p.gain }, { "tension", p.tension } });

        const float fader = std::isfinite(layer.faderDb) ? layer.faderDb : kMinusInfinityDb;
        return {
            { "name", layer.name },
            { "role", toString(layer.role) },
            { "sample", layer.samplePath },
            { "faderDb", std::max(fader, kMinusInfinityDb) },
            { "offsetMs", layer.offsetMs },
            { "startTrimMs", layer.startTrimMs },
            { "endTrimMs", layer.endTrimMs },
            { "mode", toString(layer.mode) },
            { "length", layer.length },
            { "pitchSemitones", layer.pitchSemitones },
            { "pitchCents", layer.pitchCents },
            { "tapeSpeed", layer.tapeSpeed },
            { "mute", layer.mute },
            { "solo", layer.solo },
            { "lock", layer.lock },
            { "routing", toString(layer.routing) },
            { "pitchFollow", layer.pitchFollow },
            { "envelope", { { "points", points } } },
        };
    }

    // Schema 1 (spec v0.4): Stretch mode applied a length factor ("stretch") and a pitch together; Repitch mode used
    // "stretch" as a length factor for varispeed. Migration (plan Q-B): keep both values in their v2 modes; Length is
    // active if its factor is not 1×, else Pitch if the pitch is not 0, else Length; Repitch becomes Tape with
    // speed = 1 / stretch.
    void migrateV1Layer(const json& j, Layer& layer, int index, std::vector<std::string>* warnings)
    {
        const double stretch = read(j, "stretch", 1.0);
        const bool hasPitch = layer.pitchSemitones != 0 || layer.pitchCents != 0;
        if (read<std::string>(j, "mode", "stretch") == "repitch")
        {
            layer.mode = LayerMode::tape;
            layer.tapeSpeed = stretch > 0.0 ? 1.0 / stretch : 1.0;
            return;
        }
        layer.length = stretch;
        layer.mode = stretch != 1.0 ? LayerMode::length : (hasPitch ? LayerMode::pitch : LayerMode::length);
        if (stretch != 1.0 && hasPitch && warnings != nullptr)
            warnings->push_back("Layer " + std::to_string(index + 1) + " (" + layer.name
                                + "): this kick was saved with a length and a pitch change on one layer; only the length "
                                  "change (Length mode) is applied now, the pitch value is kept in Pitch mode");
    }

    Layer layerFromJson(const json& j, int index, int schemaVersion, std::vector<std::string>* warnings)
    {
        Layer layer = makeDefaultLayer(index);
        if (! j.is_object())
            return layer;

        layer.name = read(j, "name", layer.name);
        layer.role = readEnum(j, "role", layer.role, roles);
        layer.samplePath = read(j, "sample", layer.samplePath);
        layer.faderDb = read(j, "faderDb", layer.faderDb);
        layer.offsetMs = read(j, "offsetMs", layer.offsetMs);
        layer.startTrimMs = read(j, "startTrimMs", layer.startTrimMs);
        layer.endTrimMs = read(j, "endTrimMs", layer.endTrimMs);
        layer.pitchSemitones = read(j, "pitchSemitones", layer.pitchSemitones);
        layer.pitchCents = read(j, "pitchCents", layer.pitchCents);
        if (schemaVersion < 2)
        {
            migrateV1Layer(j, layer, index, warnings);
        }
        else
        {
            layer.mode = readEnum(j, "mode", layer.mode, modes);
            layer.length = read(j, "length", layer.length);
            layer.tapeSpeed = read(j, "tapeSpeed", layer.tapeSpeed);
        }
        layer.mute = read(j, "mute", layer.mute);
        layer.solo = read(j, "solo", layer.solo);
        layer.lock = read(j, "lock", layer.lock);
        layer.routing = readEnum(j, "routing", layer.routing, routings);
        layer.pitchFollow = read(j, "pitchFollow", layer.pitchFollow);

        if (const auto env = j.find("envelope"); env != j.end() && env->is_object())
            if (const auto pts = env->find("points"); pts != env->end() && pts->is_array())
                for (const auto& pj : *pts)
                    if (pj.is_object())
                    {
                        EnvelopePoint p;
                        p.timeMs = read(pj, "timeMs", p.timeMs);
                        p.gain = read(pj, "gain", p.gain);
                        p.tension = read(pj, "tension", p.tension);
                        layer.envelope.points.push_back(p);
                    }
        return layer;
    }
} // namespace

std::string toJson(const Kick& kick)
{
    json layers = json::array();
    for (const auto& layer : kick.layers)
        layers.push_back(layerToJson(layer));

    const auto& s = kick.settings;
    const json root = {
        { "schemaVersion", kSchemaVersion },
        { "settings",
          {
              { "kickLength", toString(s.kickLength) },
              { "manualBpm", s.manualBpm },
              { "manualBpmOverride", s.manualBpmOverride },
              { "autogain", s.autogain },
              { "autogainTargetDb", s.autogainTargetDb },
              { "keyLayer", s.keyLayer },
              { "envelopeGrid", s.envelopeGrid },
              { "stretchPreset", toString(s.stretchPreset) },
          } },
        { "layers", layers },
    };
    return root.dump(2);
}

std::optional<Kick> kickFromJson(std::string_view text, std::vector<std::string>* warnings)
{
    const json root = json::parse(text, nullptr, false);
    if (root.is_discarded() || ! root.is_object())
        return std::nullopt;
    const auto schemaVersion = read<long long>(root, "schemaVersion", 0);
    if (schemaVersion <= 0)
        return std::nullopt;

    Kick kick = makeDefaultKick();
    if (const auto it = root.find("settings"); it != root.end() && it->is_object())
    {
        auto& s = kick.settings;
        const auto& j = *it;
        s.kickLength = readEnum(j, "kickLength", s.kickLength, lengths);
        s.manualBpm = read(j, "manualBpm", s.manualBpm);
        s.manualBpmOverride = read(j, "manualBpmOverride", s.manualBpmOverride);
        s.autogain = read(j, "autogain", s.autogain);
        s.autogainTargetDb = read(j, "autogainTargetDb", s.autogainTargetDb);
        s.keyLayer = read(j, "keyLayer", s.keyLayer);
        s.envelopeGrid = read(j, "envelopeGrid", s.envelopeGrid);
        s.stretchPreset = readEnum(j, "stretchPreset", s.stretchPreset, presets);
    }

    if (const auto it = root.find("layers"); it != root.end() && it->is_array())
    {
        kick.layers.clear();
        int index = 0;
        for (const auto& lj : *it)
            kick.layers.push_back(layerFromJson(lj, index++, static_cast<int>(schemaVersion), warnings));
    }

    sanitise(kick);
    return kick;
}

} // namespace klay::model
