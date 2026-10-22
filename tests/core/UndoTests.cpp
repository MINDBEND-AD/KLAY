// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source. See THIRD_PARTY_LICENSES.md (C-006).
//
// Task 2.5: undo history (FR-145, FR-239). The processor's edit/gesture API on top of it is tested in
// tests/plugin/UndoPluginTests.cpp.
#include "core/model/UndoHistory.h"

#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <vector>

using namespace klay;

namespace
{

// 60 edits of every kind the model has: layer values, mode switches, envelopes, routing, switches, names, samples,
// settings (FR-145 "all edits").
std::vector<std::function<void(model::Kick&)>> mixedEdits()
{
    std::vector<std::function<void(model::Kick&)>> edits;
    for (int i = 0; i < 60; ++i)
    {
        const auto layer = static_cast<size_t>(i % 6);
        switch (i % 12)
        {
            case 0: edits.push_back([=](model::Kick& k) { k.layers[layer].faderDb = -0.5f * static_cast<float>(i + 1); }); break;
            case 1: edits.push_back([=](model::Kick& k) { k.layers[layer].mode = k.layers[layer].mode == model::LayerMode::tape ? model::LayerMode::pitch : model::LayerMode::tape; }); break;
            case 2: edits.push_back([=](model::Kick& k) { k.layers[layer].tapeSpeed = 1.0 + 0.01 * i; }); break;
            case 3: edits.push_back([=](model::Kick& k) { k.layers[layer].envelope.points.push_back({ 1.0 * i, 0.5f, 0.1f }); }); break;
            case 4: edits.push_back([=](model::Kick& k) { k.layers[layer].routing = static_cast<model::Routing>((static_cast<int>(k.layers[layer].routing) + 1) % 3); }); break;
            case 5: edits.push_back([=](model::Kick& k) { model::clickSolo(k, layer, false); }); break;
            case 6: edits.push_back([=](model::Kick& k) { model::renameLayer(k, layer, "name " + std::to_string(i)); }); break;
            case 7: edits.push_back([=](model::Kick& k) { k.layers[layer].samplePath = "s" + std::to_string(i) + ".wav"; }); break;
            case 8: edits.push_back([=](model::Kick& k) { k.settings.manualBpm = 120.0 + i; }); break;
            case 9: edits.push_back([=](model::Kick& k) { k.settings.rollTemplate = { i / 12 }; }); break;
            case 10: edits.push_back([=](model::Kick& k) { model::toggleLock(k, layer); }); break;
            default: edits.push_back([=](model::Kick& k) { k.layers[layer].offsetMs = 0.1 * i; }); break;
        }
    }
    return edits;
}

} // namespace

TEST_CASE("Undo history: 60 edits undone and redone give identical states", "[core][model][undo][FR-145]")
{
    model::UndoHistory history;
    std::vector<model::Kick> states{ model::makeDefaultKick() };
    for (const auto& change : mixedEdits())
    {
        auto next = states.back();
        change(next);
        REQUIRE_FALSE(next == states.back()); // every edit is a real step
        history.record(states.back(), next, "edit");
        states.push_back(next);
    }
    REQUIRE(history.undoCount() == 60);

    for (size_t i = states.size() - 1; i > 0; --i)
    {
        const auto restored = history.undo();
        REQUIRE(restored);
        CHECK(*restored == states[i - 1]);
    }
    CHECK_FALSE(history.canUndo());
    CHECK_FALSE(history.undo());
    CHECK(history.redoCount() == 60);

    for (size_t i = 1; i < states.size(); ++i)
    {
        const auto restored = history.redo();
        REQUIRE(restored);
        CHECK(*restored == states[i]);
    }
    CHECK_FALSE(history.canRedo());
}

TEST_CASE("Undo history: at least 50 steps; oldest dropped past the capacity; no-op edits not recorded",
          "[core][model][undo][FR-145]")
{
    STATIC_REQUIRE(model::UndoHistory::kDefaultCapacity >= 50);
    model::UndoHistory history;
    auto kick = model::makeDefaultKick();
    const auto first = kick;
    for (int i = 0; i < 150; ++i)
    {
        auto next = kick;
        next.settings.manualBpm = 100.0 + i;
        history.record(kick, next, "bpm");
        kick = next;
    }
    CHECK(history.undoCount() == model::UndoHistory::kDefaultCapacity);
    std::optional<model::Kick> last;
    while (history.canUndo())
        last = history.undo();
    REQUIRE(last);
    CHECK(last->settings.manualBpm == 100.0 + 150 - model::UndoHistory::kDefaultCapacity - 1);
    CHECK_FALSE(*last == first); // the oldest 50 steps are gone

    model::UndoHistory h2;
    h2.record(first, first, "nothing");
    CHECK_FALSE(h2.canUndo());
}

TEST_CASE("Undo history: a new edit after an undo clears the redo steps; labels", "[core][model][undo][FR-145]")
{
    model::UndoHistory history;
    auto a = model::makeDefaultKick();
    auto b = a;
    b.layers[0].mute = true;
    auto c = b;
    c.layers[1].mute = true;
    history.record(a, b, "Mute 1");
    history.record(b, c, "Mute 2");
    CHECK(history.undoLabel() == "Mute 2");
    REQUIRE(history.undo());
    CHECK(history.redoLabel() == "Mute 2");
    auto d = b;
    d.layers[2].solo = true;
    history.record(b, d, "Solo 3");
    CHECK_FALSE(history.canRedo());
    CHECK(history.undoCount() == 2);
    history.clear();
    CHECK_FALSE(history.canUndo());
}
