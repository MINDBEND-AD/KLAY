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
