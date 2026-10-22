// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source, uses JUCE (AGPLv3). See THIRD_PARTY_LICENSES.md (C-006).
#include "plugin/KickFiles.h"
#include "plugin/PluginEditor.h"
#include "plugin/PluginProcessor.h"
#include "tests/plugin/LibraryFixture.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>

namespace
{
juce::File fixture(const char* name)
{
    return juce::File(KLAY_TEST_FIXTURES).getChildFile("kicks").getChildFile(name);
}
} // namespace

// Task 1.15: the dev editor loads kick JSON; relative part paths resolve against the JSON's folder.
TEST_CASE("Kick files load with relative sample paths resolved", "[plugin][editor]")
{
    const auto kick = klay::loadKickFile(fixture("stack.json"));
    REQUIRE(kick.has_value());
    const juce::File body(juce::String::fromUTF8(kick->layers[0].samplePath.c_str()));
    CHECK(juce::File::isAbsolutePath(body.getFullPathName()));
    CHECK(body.existsAsFile());
    CHECK_FALSE(klay::loadKickFile(fixture("does-not-exist.json")).has_value());

    const auto temp = juce::File::createTempFile(".json");
    REQUIRE(klay::saveKickFile(temp, *kick));
    const auto again = klay::loadKickFile(temp);
    temp.deleteFile();
    REQUIRE(again.has_value());
    CHECK(*again == *kick);
}

TEST_CASE("Dev editor shows a loaded kick and its key", "[plugin][editor]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    klay::KlayProcessor processor;
    processor.setPlayConfigDetails(0, 2, 48000.0, 512);
    processor.prepareToPlay(48000.0, 512);
    auto kick = klay::loadKickFile(fixture("stack.json"));
    REQUIRE(kick.has_value());
    REQUIRE(processor.getEngine().waitForGeneration(processor.setKick(*kick), std::chrono::seconds(20)));

    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditorAndMakeActive());
    REQUIRE(editor != nullptr);
    CHECK(editor->getWidth() >= 1000);
    juce::Image image(juce::Image::ARGB, editor->getWidth(), editor->getHeight(), true);
    juce::Graphics g(image);
    editor->paintEntireComponent(g, false);
    CHECK(processor.getKick() == *kick); // opening the editor does not edit the kick

    processor.editorBeingDeleted(editor.get());
    editor.reset();
}

// Task 1.22 (FR-106, UI-007): one knob per layer with a Length · Pitch · Tape switch; switching modes keeps every
// mode's value, the knob edits only the active one.
TEST_CASE("Dev editor layer knob follows the mode and keeps each mode's value", "[plugin][editor][FR-106]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    klay::KlayProcessor processor;
    auto kick = klay::model::makeDefaultKick();
    kick.layers[0].length = 1.5;
    kick.layers[0].pitchSemitones = 3;
    kick.layers[0].tapeSpeed = 0.5;
    processor.setKick(kick);

    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditorAndMakeActive());
    REQUIRE(editor != nullptr);
    auto* knob = dynamic_cast<juce::Slider*>(editor->findChildWithID("knob-0"));
    auto* tape = dynamic_cast<juce::Button*>(editor->findChildWithID("mode-0-tape"));
    auto* pitch = dynamic_cast<juce::Button*>(editor->findChildWithID("mode-0-pitch"));
    REQUIRE(knob != nullptr);
    REQUIRE(tape != nullptr);
    REQUIRE(pitch != nullptr);
    CHECK(knob->getValue() == 1.5); // Length mode by default

    tape->setToggleState(true, juce::dontSendNotification);
    tape->onClick();
    const auto& layer = processor.getKick().layers[0];
    CHECK(layer.mode == klay::model::LayerMode::tape);
    CHECK(knob->getValue() == 0.5);
    knob->setValue(2.0, juce::sendNotificationSync);
    CHECK(processor.getKick().layers[0].tapeSpeed == 2.0);
    CHECK(processor.getKick().layers[0].length == 1.5); // the other modes keep their values
    CHECK(processor.getKick().layers[0].pitchSemitones == 3);

    pitch->setToggleState(true, juce::dontSendNotification);
    pitch->onClick();
    CHECK(knob->getValue() == 300.0); // cents
    knob->setValue(-1150.0, juce::sendNotificationSync);
    CHECK(processor.getKick().layers[0].pitchSemitones == -12);
    CHECK(processor.getKick().layers[0].pitchCents == 50);

    juce::Image image(juce::Image::ARGB, editor->getWidth(), editor->getHeight(), true);
    juce::Graphics g(image);
    editor->paintEntireComponent(g, false);
    processor.editorBeingDeleted(editor.get());
    editor.reset();
}

// Task 2.7: Forge, Reforge per layer, lock, bin, rename, roll template and undo/redo in the dev editor.
TEST_CASE("Dev editor: Forge, Reforge, lock, bin, rename, template and undo/redo", "[plugin][editor][FR-141][FR-142][FR-145]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    klay::test::SyntheticLibrary lib(3);
    klay::KlayProcessor processor(klay::ProcessorOptions{ lib.root(), lib.dataDir() });
    REQUIRE(processor.getLibrary().waitUntilReady(std::chrono::seconds(30)));
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditorAndMakeActive());
    REQUIRE(editor != nullptr);
    auto button = [&](const juce::String& id) {
        auto* b = dynamic_cast<juce::Button*>(editor->findChildWithID(id));
        REQUIRE(b != nullptr);
        return b;
    };
    const auto empty = processor.getKick();

    // Forge fills the roll template (layers 1–3).
    button("forge")->onClick();
    const auto forged = processor.getKick();
    for (size_t i = 0; i < 3; ++i)
        CHECK_FALSE(forged.layers[i].samplePath.empty());
    CHECK(forged.layers[3].samplePath.empty());

    // Lock layer 1: Forge leaves it, its Reforge and bin are disabled.
    auto* lock = button("lock-0");
    lock->setToggleState(true, juce::dontSendNotification);
    lock->onClick();
    CHECK(processor.getKick().layers[0].lock);
    CHECK_FALSE(button("reforge-0")->isEnabled());
    CHECK_FALSE(button("bin-0")->isEnabled());
    button("forge")->onClick();
    CHECK(processor.getKick().layers[0].samplePath == forged.layers[0].samplePath);

    // Reforge layer 4 (empty, Crunch) fills only that layer and selects it.
    const auto beforeReforge = processor.getKick();
    button("reforge-3")->onClick();
    CHECK_FALSE(processor.getKick().layers[3].samplePath.empty());
    CHECK(processor.getSelectedLayer() == 3);
    for (size_t i = 0; i < beforeReforge.layers.size(); ++i)
        if (i != 3)
            CHECK(processor.getKick().layers[i] == beforeReforge.layers[i]);

    // Bin layer 4, rename layer 2, change the roll template.
    REQUIRE(button("bin-3")->isEnabled());
    button("bin-3")->onClick();
    CHECK(processor.getKick().layers[3].samplePath.empty());
    auto* name = dynamic_cast<juce::Label*>(editor->findChildWithID("name-1"));
    REQUIRE(name != nullptr);
    name->setText("Sub", juce::sendNotificationSync);
    CHECK(processor.getKick().layers[1].name == "Sub");
    auto* t4 = button("template-3");
    t4->setToggleState(true, juce::dontSendNotification);
    t4->onClick();
    CHECK(processor.getKick().settings.rollTemplate == std::vector<int>{ 0, 1, 2, 3 });

    // A fader drag is one undo step (FR-239).
    auto* knob = dynamic_cast<juce::Slider*>(editor->findChildWithID("knob-2"));
    REQUIRE(knob != nullptr);
    const auto steps = processor.getUndoHistory().undoCount();
    knob->onDragStart();
    for (double v : { 1.2, 1.5, 2.0, 2.5 })
        knob->setValue(v, juce::sendNotificationSync);
    knob->onDragEnd();
    CHECK(processor.getUndoHistory().undoCount() == steps + 1);
    CHECK(processor.getKick().layers[2].length == 2.5);

    // Ctrl+Z undoes the drag, the Undo button walks back to the empty kick; Ctrl+Shift+Z / Redo go forward.
    CHECK(editor->keyPressed(juce::KeyPress('Z', juce::ModifierKeys::commandModifier, 0)));
    CHECK(processor.getKick().layers[2].length == 1.0);
    while (button("undo")->isEnabled())
        button("undo")->onClick();
    CHECK(processor.getKick() == empty);
    button("redo")->onClick();
    CHECK(processor.getKick() == forged);
    CHECK(editor->keyPressed(juce::KeyPress('Z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0)));
    CHECK(processor.getKick().layers[0].lock);

    juce::Image image(juce::Image::ARGB, editor->getWidth(), editor->getHeight(), true);
    juce::Graphics g(image);
    editor->paintEntireComponent(g, false);
    processor.editorBeingDeleted(editor.get());
    editor.reset();
}
