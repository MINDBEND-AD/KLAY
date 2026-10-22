// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source. See THIRD_PARTY_LICENSES.md (C-006).
//
// Task 2.4: layer operations (FR-110–114): lock, mute, solo, bin, rename. Selection (FR-113) is processor state and
// is tested in tests/plugin/ProcessorTests.cpp.
#include "core/model/Kick.h"
#include "core/render/StackRenderer.h"

#include <catch2/catch_test_macros.hpp>

using namespace klay;

namespace
{

std::vector<bool> audible(const model::Kick& kick)
{
    std::vector<bool> out;
    for (size_t i = 0; i < kick.layers.size(); ++i)
        out.push_back(render::isAudible(kick, i));
    return out;
}

} // namespace

TEST_CASE("Mute: several layers at once; Shift+click on any Mute unmutes all", "[core][model][FR-110]")
{
    auto kick = model::makeDefaultKick();
    model::clickMute(kick, 0, false);
    model::clickMute(kick, 3, false);
    CHECK(kick.layers[0].mute);
    CHECK(kick.layers[3].mute);
    CHECK(audible(kick) == std::vector<bool>{ false, true, true, false, true, true });

    model::clickMute(kick, 5, true); // Shift on an unmuted layer's Mute: unmutes all
    for (const auto& l : kick.layers)
        CHECK_FALSE(l.mute);

    model::clickMute(kick, 2, true); // Shift with nothing muted: toggles as a plain click
    CHECK(kick.layers[2].mute);
    model::clickMute(kick, 2, false);
    CHECK_FALSE(kick.layers[2].mute);
    model::clickMute(kick, 40, false); // out of range: ignored
}

TEST_CASE("Solo: given layer 2 soloed, Solo on layer 3 leaves only layer 3; Shift+click hears both",
          "[core][model][FR-111][gherkin]")
{
    auto kick = model::makeDefaultKick();
    model::clickSolo(kick, 1, false);
    model::clickSolo(kick, 2, false);
    CHECK(audible(kick) == std::vector<bool>{ false, false, true, false, false, false });

    auto shifted = model::makeDefaultKick();
    model::clickSolo(shifted, 1, false);
    model::clickSolo(shifted, 2, true);
    CHECK(audible(shifted) == std::vector<bool>{ false, true, true, false, false, false });

    // Mute beats Solo.
    model::clickMute(shifted, 2, false);
    CHECK(audible(shifted) == std::vector<bool>{ false, true, false, false, false, false });
}

TEST_CASE("Lock and bin: the bin resets the whole layer and is disabled while it is locked", "[core][model][FR-112][FR-101]")
{
    auto kick = model::makeDefaultKick();
    auto& layer = kick.layers[2];
    layer.samplePath = "x.wav";
    layer.name = "Grit";
    layer.role = model::Role::any;
    layer.faderDb = -6.0f;
    layer.offsetMs = 4.0;
    layer.startTrimMs = 3.0;
    layer.endTrimMs = 5.0;
    layer.mode = model::LayerMode::tape;
    layer.length = 2.0;
    layer.pitchSemitones = 4;
    layer.pitchCents = -20;
    layer.tapeSpeed = 1.5;
    layer.routing = model::Routing::aux1;
    layer.mute = true;
    layer.solo = true;
    layer.pitchFollow = true;
    layer.envelope.points = { { 0.0, 1.0f, 0.0f }, { 20.0, 0.5f, 0.0f } };
    kick.layers[0].solo = true;

    model::toggleLock(kick, 2);
    CHECK(kick.layers[2].lock);
    CHECK_FALSE(model::canBin(kick, 2));
    const auto lockedBefore = kick;
    CHECK_FALSE(model::binLayer(kick, 2));
    CHECK(kick == lockedBefore);

    model::toggleLock(kick, 2);
    CHECK_FALSE(kick.layers[2].lock);
    CHECK(model::canBin(kick, 2));
    const auto others = kick;
    CHECK(model::binLayer(kick, 2));
    // PO decision 2026-10-22: sample, fader, knobs, mode, offset, trims, envelope, routing, name, role, Mute, Solo,
    // Lock and pitch-follow all back to the defaults of layer 3.
    CHECK(kick.layers[2] == model::makeDefaultLayer(2));
    for (const size_t i : { 0u, 1u, 3u, 4u, 5u })
        CHECK(kick.layers[i] == others.layers[i]); // other layers untouched (layer 1 keeps its Solo)
    CHECK_FALSE(model::binLayer(kick, 2)); // already default: nothing to do

    kick.layers[4].faderDb = -12.0f; // no sample, but changed settings: the bin still resets them
    CHECK(model::binLayer(kick, 4));
    CHECK(kick.layers[4] == model::makeDefaultLayer(4));

    CHECK_FALSE(model::canBin(kick, 99));
    model::toggleLock(kick, 99); // ignored
}

TEST_CASE("Rename: any layer, cleaned; an empty name restores the default", "[core][model][FR-114]")
{
    auto kick = model::makeDefaultKick();
    model::renameLayer(kick, 0, "  Big Body  ");
    CHECK(kick.layers[0].name == "Big Body");
    model::renameLayer(kick, 4, "Click\tTop\n");
    CHECK(kick.layers[4].name == "Click Top");
    model::renameLayer(kick, 4, "   ");
    CHECK(kick.layers[4].name == "Transient");
    model::renameLayer(kick, 1, reinterpret_cast<const char*>(u8"Grollen ü"));
    CHECK(kick.layers[1].name == reinterpret_cast<const char*>(u8"Grollen ü"));
    // The role does not follow a rename (FR-140: layers 1–6 default to the role of their default name).
    CHECK(kick.layers[0].role == model::Role::kick);
    model::renameLayer(kick, 50, "x"); // ignored
}
