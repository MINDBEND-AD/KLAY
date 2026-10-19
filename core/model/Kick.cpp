// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source. See THIRD_PARTY_LICENSES.md (C-006).
#include "core/model/Kick.h"

#include <algorithm>
#include <cmath>

namespace klay::model
{

Layer makeDefaultLayer(int index)
{
    static constexpr const char* names[] = { "Kick", "Rumble", "Drive", "Crunch", "Transient", "Texture" };
    static constexpr Role roles[] = { Role::kick, Role::rumble, Role::drive, Role::crunch, Role::transient, Role::texture };

    Layer layer;
    if (index >= 0 && index < 6)
    {
        layer.name = names[index];
        layer.role = roles[index];
    }
    else
    {
        layer.name = "Layer " + std::to_string(index + 1);
        layer.role = Role::any;
    }
    return layer;
}

Kick makeDefaultKick()
{
    Kick kick;
    for (int i = 0; i < kDefaultLayerCount; ++i)
        kick.layers.push_back(makeDefaultLayer(i));
    return kick;
}

namespace
{
    template <typename T>
    T clampFinite(T value, T lo, T hi, T fallback)
    {
        if (! std::isfinite(static_cast<double>(value)))
            return fallback;
        return std::clamp(value, lo, hi);
    }
} // namespace

void sanitise(Kick& kick)
{
    if (kick.layers.size() > static_cast<size_t>(kMaxLayerCount))
        kick.layers.resize(kMaxLayerCount);
    if (kick.layers.empty())
        kick.layers.push_back(makeDefaultLayer(0));

    for (auto& layer : kick.layers)
    {
        // −∞ may arrive as -inf from code or as any value at or below the floor.
        if (std::isnan(layer.faderDb))
            layer.faderDb = 0.0f;
        layer.faderDb = std::clamp(layer.faderDb, kMinusInfinityDb, 0.0f);
        layer.offsetMs = clampFinite(layer.offsetMs, 0.0, 60'000.0, 0.0);
        layer.startTrimMs = clampFinite(layer.startTrimMs, 0.0, 60'000.0, 0.0);
        layer.endTrimMs = clampFinite(layer.endTrimMs, 0.0, 60'000.0, 0.0);
        layer.pitchSemitones = std::clamp(layer.pitchSemitones, -12, 12);
        layer.pitchCents = std::clamp(layer.pitchCents, -100, 100);
        layer.stretch = clampFinite(layer.stretch, 0.25, 8.0, 1.0);

        auto& points = layer.envelope.points;
        for (auto& p : points)
        {
            p.timeMs = clampFinite(p.timeMs, 0.0, 60'000.0, 0.0);
            p.gain = clampFinite(p.gain, 0.0f, 1.0f, 1.0f);
            p.tension = clampFinite(p.tension, -1.0f, 1.0f, 0.0f);
        }
        std::stable_sort(points.begin(), points.end(),
                         [](const EnvelopePoint& a, const EnvelopePoint& b) { return a.timeMs < b.timeMs; });
    }

    auto& s = kick.settings;
    s.manualBpm = clampFinite(s.manualBpm, 20.0, 999.0, 155.0);
    s.autogainTargetDb = clampFinite(s.autogainTargetDb, -60.0, 0.0, -0.3);
    if (s.keyLayer < 0 || s.keyLayer >= static_cast<int>(kick.layers.size()))
        s.keyLayer = 0;
    if (s.envelopeGrid != 4 && s.envelopeGrid != 8 && s.envelopeGrid != 16)
        s.envelopeGrid = 4;
}

const char* toString(Role role)
{
    switch (role)
    {
        case Role::kick: return "kick";
        case Role::rumble: return "rumble";
        case Role::drive: return "drive";
        case Role::crunch: return "crunch";
        case Role::transient: return "transient";
        case Role::texture: return "texture";
        case Role::any: break;
    }
    return "any";
}

const char* toString(LayerMode mode)
{
    return mode == LayerMode::repitch ? "repitch" : "stretch";
}

const char* toString(Routing routing)
{
    switch (routing)
    {
        case Routing::aux1: return "aux1";
        case Routing::aux2: return "aux2";
        case Routing::main: break;
    }
    return "main";
}

const char* toString(KickLength length)
{
    switch (length)
    {
        case KickLength::sixteenth: return "1/16";
        case KickLength::eighth: return "1/8";
        case KickLength::half: return "1/2";
        case KickLength::bar: return "1";
        case KickLength::quarter: break;
    }
    return "1/4";
}

const char* toString(StretchPreset preset)
{
    return preset == StretchPreset::cheaper ? "cheaper" : "default";
}

} // namespace klay::model
