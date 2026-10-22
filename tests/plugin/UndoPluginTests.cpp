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

// PO check 2026-10-22: an accidental Bin click is undone completely: the whole layer (sample, fader, knob values,
// trims, offset, routing, switches, name, role and the volume envelope that replaces fades, FR-103) and the render.
TEST_CASE("Processor: Undo after Bin restores the layer exactly, envelope included", "[plugin][undo][FR-112][FR-145]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    test::SyntheticLibrary lib(2);
    KlayProcessor processor(ProcessorOptions{ lib.root(), lib.dataDir() });
    REQUIRE(processor.getLibrary().waitUntilReady(30s));
    processor.forge();

    processor.edit("Set up layer 2", [](model::Kick& k) {
        auto& l = k.layers[1];
        l.name = "Sub tail";
        l.role = model::Role::any;
        l.faderDb = -4.5f;
        l.offsetMs = 3.25;
        l.startTrimMs = 2.0;
        l.endTrimMs = 40.0;
        l.mode = model::LayerMode::pitch;
        l.length = 1.7;
        l.pitchSemitones = -3;
        l.pitchCents = 25;
        l.tapeSpeed = 0.8;
        l.routing = model::Routing::aux1;
        l.mute = false;
        l.solo = true;
        l.pitchFollow = true;
        l.envelope.points = { { 0.0, 0.0f, 0.4f }, { 5.0, 1.0f, -0.6f }, { 120.0, 0.35f, 0.2f }, { 300.0, 0.0f, 0.0f } };
    });
    const auto before = processor.getKick();
    REQUIRE(processor.getEngine().waitForGeneration(processor.getRequestedGeneration(), 10s));
    const auto renderBefore = processor.getEngine().latest();

    // Bin through the dev editor's button, as a user click.
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditorAndMakeActive());
    REQUIRE(editor != nullptr);
    auto* bin = dynamic_cast<juce::Button*>(editor->findChildWithID("bin-1"));
    REQUIRE(bin != nullptr);
    REQUIRE(bin->isEnabled());
    bin->onClick();
    CHECK(processor.getKick().layers[1] == model::makeDefaultLayer(1));
    CHECK(processor.getUndoHistory().undoLabel() == "Bin");

    auto* undo = dynamic_cast<juce::Button*>(editor->findChildWithID("undo"));
    REQUIRE(undo != nullptr);
    undo->onClick();
    CHECK(processor.getKick().layers[1] == before.layers[1]); // every field, envelope points and tensions included
    CHECK(processor.getKick().layers[1].envelope == before.layers[1].envelope);
    CHECK(processor.getKick() == before); // nothing else changed either

    // The sound is back too: the render after Undo is bit-identical to the one before Bin (NFR-009).
    REQUIRE(processor.getEngine().waitForGeneration(processor.getRequestedGeneration(), 10s));
    const auto renderAfter = processor.getEngine().latest();
    REQUIRE(renderBefore != nullptr);
    REQUIRE(renderAfter != nullptr);
    CHECK(renderAfter->length == renderBefore->length);
    for (size_t b = 0; b < render::kNumBuses; ++b)
    {
        CHECK(renderAfter->buses[b].follow == renderBefore->buses[b].follow);
        CHECK(renderAfter->buses[b].fixed == renderBefore->buses[b].fixed);
    }
    CHECK(renderAfter->autogain == renderBefore->autogain);

    processor.editorBeingDeleted(editor.get());
    editor.reset();
}
