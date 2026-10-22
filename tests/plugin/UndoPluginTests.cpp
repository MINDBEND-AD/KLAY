// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source, uses JUCE (AGPLv3). See THIRD_PARTY_LICENSES.md (C-006).
//
// Task 2.5 in the processor: edits, gestures and rolls as undo steps; undo re-renders (FR-145, FR-239, FR-106).
#include "plugin/PluginProcessor.h"
#include "tests/plugin/LibraryFixture.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>

using namespace klay;
using namespace std::chrono_literals;

namespace
{

ProcessorOptions noLibrary()
{
    return ProcessorOptions{ std::filesystem::path(), std::filesystem::temp_directory_path() };
}

} // namespace

TEST_CASE("Processor: 60 edits undone and redone give identical states and renders follow", "[plugin][undo][FR-145]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    KlayProcessor processor(noLibrary());
    std::vector<model::Kick> states{ processor.getKick() };
    for (int i = 0; i < 60; ++i)
    {
        const auto layer = static_cast<size_t>(i % 6);
        CHECK(processor.edit("edit", [&](model::Kick& k) {
            if (i % 4 == 0)
                k.layers[layer].faderDb = -1.0f - static_cast<float>(i);
            else if (i % 4 == 1) // FR-106: a mode switch is one step
                k.layers[layer].mode = k.layers[layer].mode == model::LayerMode::tape ? model::LayerMode::pitch : model::LayerMode::tape;
            else if (i % 4 == 2)
                k.layers[layer].offsetMs = 0.1 * i;
            else
                k.settings.manualBpm = 100.0 + i;
        }));
        states.push_back(processor.getKick());
    }
    CHECK(processor.getUndoHistory().undoCount() == 60);

    while (processor.undo())
        ;
    CHECK(processor.getKick() == states.front());
    const auto generation = processor.getEngine().requestRender(processor.getKick(), 48000.0, 155.0);
    CHECK(processor.getEngine().waitForGeneration(generation, 10s));

    while (processor.redo())
        ;
    CHECK(processor.getKick() == states.back());
}

TEST_CASE("Processor: one gesture is one undo step", "[plugin][undo][FR-239]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    KlayProcessor processor(noLibrary());
    const auto start = processor.getKick();

    processor.beginGesture("Fader");
    for (int i = 1; i <= 25; ++i)
        processor.edit("Fader", [i](model::Kick& k) { k.layers[0].faderDb = -0.4f * static_cast<float>(i); });
    CHECK(processor.getKick().layers[0].faderDb == -10.0f); // applied live during the gesture
    CHECK(processor.getUndoHistory().undoCount() == 0);
    processor.endGesture();
    CHECK(processor.getUndoHistory().undoCount() == 1);
    CHECK(processor.getUndoHistory().undoLabel() == "Fader");

    REQUIRE(processor.undo());
    CHECK(processor.getKick() == start);
    REQUIRE(processor.redo());
    CHECK(processor.getKick().layers[0].faderDb == -10.0f);

    // A gesture that ends where it started records nothing; an edit that changes nothing is no step.
    processor.beginGesture("Fader");
    processor.edit("Fader", [](model::Kick& k) { k.layers[0].faderDb = -3.0f; });
    processor.edit("Fader", [](model::Kick& k) { k.layers[0].faderDb = -10.0f; });
    processor.endGesture();
    CHECK_FALSE(processor.edit("Mute", [](model::Kick&) {}));
    CHECK(processor.getUndoHistory().undoCount() == 1);

    // Nested begin/end (e.g. a knob inside a drag) still makes one step; an unmatched end is ignored.
    processor.beginGesture("Outer");
    processor.beginGesture("Inner");
    processor.edit("x", [](model::Kick& k) { k.layers[1].offsetMs = 3.0; });
    processor.endGesture();
    CHECK(processor.getUndoHistory().undoCount() == 1);
    processor.endGesture();
    processor.endGesture();
    CHECK(processor.getUndoHistory().undoCount() == 2);
    CHECK(processor.getUndoHistory().undoLabel() == "Outer");
}

TEST_CASE("Processor: Forge, Reforge, bin and settings edits are undoable; a state restore clears the history",
          "[plugin][undo][FR-145][FR-112]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    test::SyntheticLibrary lib(2);
    KlayProcessor processor(ProcessorOptions{ lib.root(), lib.dataDir() });
    REQUIRE(processor.getLibrary().waitUntilReady(30s));

    const auto empty = processor.getKick();
    processor.forge();
    const auto forged = processor.getKick();
    CHECK_FALSE(forged == empty);
    processor.reforge(0);
    processor.edit("Bin", [](model::Kick& k) { model::binLayer(k, 1); });
    CHECK(processor.getKick().layers[1].samplePath.empty());
    processor.edit("Autogain", [](model::Kick& k) { k.settings.autogain = false; });
    CHECK(processor.getUndoHistory().undoCount() == 4);

    REQUIRE(processor.undo()); // autogain
    CHECK(processor.getKick().settings.autogain);
    REQUIRE(processor.undo()); // bin
    CHECK_FALSE(processor.getKick().layers[1].samplePath.empty());
    REQUIRE(processor.undo()); // reforge
    CHECK(processor.getKick() == forged);
    REQUIRE(processor.undo()); // forge
    CHECK(processor.getKick() == empty);

    juce::MemoryBlock state;
    processor.getStateInformation(state);
    processor.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    CHECK_FALSE(processor.getUndoHistory().canUndo());
    CHECK_FALSE(processor.getUndoHistory().canRedo());
}
