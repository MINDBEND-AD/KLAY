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
    constexpr std::array modes{ LayerMode::stretch, LayerMode::repitch };
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
            { "pitchSemitones", layer.pitchSemitones },
            { "pitchCents", layer.pitchCents },
            { "stretch", layer.stretch },
            { "mode", toString(layer.mode) },
            { "mute", layer.mute },
            { "solo", layer.solo },
            { "lock", layer.lock },
            { "routing", toString(layer.routing) },
            { "pitchFollow", layer.pitchFollow },
            { "envelope", { { "points", points } } },
        };
    }

    Layer layerFromJson(const json& j, int index)
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
        layer.stretch = read(j, "stretch", layer.stretch);
        layer.mode = readEnum(j, "mode", layer.mode, modes);
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

std::optional<Kick> kickFromJson(std::string_view text)
{
    const json root = json::parse(text, nullptr, false);
    if (root.is_discarded() || ! root.is_object())
        return std::nullopt;
    if (read<long long>(root, "schemaVersion", 0) <= 0)
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
            kick.layers.push_back(layerFromJson(lj, index++));
    }

    sanitise(kick);
    return kick;
}

} // namespace klay::model
