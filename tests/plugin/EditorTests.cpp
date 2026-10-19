// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source, uses JUCE (AGPLv3). See THIRD_PARTY_LICENSES.md (C-006).
#include "plugin/KickFiles.h"
#include "plugin/PluginEditor.h"
#include "plugin/PluginProcessor.h"

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
