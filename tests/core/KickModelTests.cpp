// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source. See THIRD_PARTY_LICENSES.md (C-006).
#include "core/model/KickJson.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

using namespace klay::model;

// FR-100, FR-101, FR-104–106, FR-114, FR-122, FR-108, FR-109, FR-124, FR-282, FR-002 defaults.
TEST_CASE("Default kick matches the spec defaults", "[core][model][FR-100][FR-114]")
{
    const auto kick = makeDefaultKick();
    REQUIRE(kick.layers.size() == 6);

    const char* names[] = { "Kick", "Rumble", "Drive", "Crunch", "Transient", "Texture" };
    const Role roles[] = { Role::kick, Role::rumble, Role::drive, Role::crunch, Role::transient, Role::texture };
    for (int i = 0; i < 6; ++i)
    {
        const auto& layer = kick.layers[static_cast<size_t>(i)];
        CHECK(layer.name == names[i]);
        CHECK(layer.role == roles[i]);
        CHECK(layer.samplePath.empty());
        CHECK(layer.faderDb == 0.0f);
        CHECK(layer.offsetMs == 0.0);
        CHECK(layer.startTrimMs == 0.0);
        CHECK(layer.endTrimMs == 0.0);
        CHECK(layer.pitchSemitones == 0);
        CHECK(layer.pitchCents == 0);
        CHECK(layer.stretch == 1.0);
        CHECK(layer.mode == LayerMode::stretch);
        CHECK_FALSE(layer.mute);
        CHECK_FALSE(layer.solo);
        CHECK_FALSE(layer.lock);
        CHECK(layer.routing == Routing::main);
        CHECK_FALSE(layer.pitchFollow); // FR-122: off on every layer, incl. the key layer
        CHECK(layer.envelope.points.empty()); // FR-280: flat at 100 %
    }

    CHECK(makeDefaultLayer(6).name == "Layer 7");
    CHECK(makeDefaultLayer(11).name == "Layer 12");
    CHECK(makeDefaultLayer(11).role == Role::any);

    const auto& s = kick.settings;
    CHECK(s.kickLength == KickLength::quarter);
    CHECK(s.manualBpm == 155.0);
    CHECK_FALSE(s.manualBpmOverride);
    CHECK(s.autogain);
    CHECK(s.autogainTargetDb == -0.3);
    CHECK(s.keyLayer == 0);
    CHECK(s.envelopeGrid == 4);
    CHECK(s.stretchPreset == StretchPreset::standard);
}

TEST_CASE("Kick round-trips through JSON", "[core][model][FR-003][NFR-012]")
{
    auto kick = makeDefaultKick();
    kick.layers.push_back(makeDefaultLayer(6));
    auto& l = kick.layers[2];
    l.name = "Dröhn";
    l.samplePath = "C:/samples/Crunch/räsp.wav";
    l.faderDb = -6.5f;
    l.offsetMs = 12.25;
    l.startTrimMs = 3.0;
    l.endTrimMs = 40.0;
    l.pitchSemitones = -7;
    l.pitchCents = 33;
    l.stretch = 1.75;
    l.mode = LayerMode::repitch;
    l.mute = true;
    l.solo = true;
    l.lock = true;
    l.routing = Routing::aux2;
    l.pitchFollow = true;
    l.envelope.points = { { 0.0, 1.0f, 0.0f }, { 50.0, 0.5f, -0.4f }, { 120.0, 0.0f, 0.7f } };
    kick.layers[0].faderDb = kMinusInfinityDb;

    kick.settings.kickLength = KickLength::bar;
    kick.settings.manualBpm = 142.5;
    kick.settings.manualBpmOverride = true;
    kick.settings.autogain = false;
    kick.settings.keyLayer = 3;
    kick.settings.envelopeGrid = 16;
    kick.settings.stretchPreset = StretchPreset::cheaper;

    const auto json = toJson(kick);
    CHECK(json.find("\"schemaVersion\": 1") != std::string::npos);
    const auto parsed = kickFromJson(json);
    REQUIRE(parsed.has_value());
    CHECK(*parsed == kick);
    CHECK(toJson(*parsed) == json); // deterministic bytes
}

TEST_CASE("Kick JSON stores minus infinity as the fader floor", "[core][model][FR-101]")
{
    auto kick = makeDefaultKick();
    kick.layers[0].faderDb = -std::numeric_limits<float>::infinity();
    const auto parsed = kickFromJson(toJson(kick));
    REQUIRE(parsed.has_value());
    CHECK(parsed->layers[0].faderDb == kMinusInfinityDb);
}

TEST_CASE("Kick JSON ignores unknown fields", "[core][model][NFR-012]")
{
    const auto parsed = kickFromJson(R"({
        "schemaVersion": 7,
        "futureTopLevel": { "x": [1, 2, 3] },
        "settings": { "manualBpm": 140.0, "newSetting": "yes" },
        "layers": [ { "name": "A", "sparkle": 11, "envelope": { "points": [], "shape": "new" } } ]
    })");
    REQUIRE(parsed.has_value());
    CHECK(parsed->settings.manualBpm == 140.0);
    REQUIRE(parsed->layers.size() == 1);
    CHECK(parsed->layers[0].name == "A");
}

TEST_CASE("Kick JSON defaults missing and mistyped fields", "[core][model][NFR-012]")
{
    const auto parsed = kickFromJson(R"({
        "schemaVersion": 1,
        "settings": { "kickLength": "1/7", "manualBpm": "fast", "envelopeGrid": 5 },
        "layers": [ {}, { "faderDb": "loud", "mode": "granular", "routing": 3 }, 42 ]
    })");
    REQUIRE(parsed.has_value());
    CHECK(parsed->settings == Settings{});
    REQUIRE(parsed->layers.size() == 3);
    CHECK(parsed->layers[0] == makeDefaultLayer(0));
    CHECK(parsed->layers[1] == makeDefaultLayer(1));
    CHECK(parsed->layers[2] == makeDefaultLayer(2));

    // No layers array at all: the default 6-layer stack.
    const auto noLayers = kickFromJson(R"({"schemaVersion":1})");
    REQUIRE(noLayers.has_value());
    CHECK(*noLayers == makeDefaultKick());
}

TEST_CASE("Kick JSON clamps values to their ranges", "[core][model][FR-100][FR-104][FR-105]")
{
    std::string layers;
    for (int i = 0; i < 14; ++i)
        layers += std::string(i ? "," : "") + R"({"faderDb": 6, "pitchSemitones": 30, "pitchCents": -400, "stretch": 20,
            "offsetMs": -5, "envelope": {"points": [{"timeMs": 90, "gain": 3}, {"timeMs": 10, "gain": -1, "tension": 4}]}})";
    const auto parsed = kickFromJson(R"({"schemaVersion":1,"settings":{"keyLayer":40},"layers":[)" + layers + "]}");
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->layers.size() == 12); // FR-100: at most 12
    const auto& l = parsed->layers[0];
    CHECK(l.faderDb == 0.0f);
    CHECK(l.pitchSemitones == 12);
    CHECK(l.pitchCents == -100);
    CHECK(l.stretch == 8.0);
    CHECK(l.offsetMs == 0.0);
    REQUIRE(l.envelope.points.size() == 2);
    CHECK(l.envelope.points[0] == EnvelopePoint{ 10.0, 0.0f, 1.0f }); // sorted by time
    CHECK(l.envelope.points[1].gain == 1.0f);
    CHECK(parsed->settings.keyLayer == 0);
}

TEST_CASE("Kick JSON rejects malformed input", "[core][model][FR-003]")
{
    CHECK_FALSE(kickFromJson("").has_value());
    CHECK_FALSE(kickFromJson("not json").has_value());
    CHECK_FALSE(kickFromJson("[1,2]").has_value());
    CHECK_FALSE(kickFromJson("{}").has_value());
    CHECK_FALSE(kickFromJson(R"({"schemaVersion":"one"})").has_value());
    CHECK_FALSE(kickFromJson(R"({"schemaVersion":0})").has_value());
    CHECK_FALSE(kickFromJson(R"({"schemaVersion":1,)").has_value());
}
