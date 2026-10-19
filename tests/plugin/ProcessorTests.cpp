// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source, uses JUCE (AGPLv3). See THIRD_PARTY_LICENSES.md (C-006).
#include "plugin/PluginProcessor.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Processor reports the plugin identity", "[plugin]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    klay::KlayProcessor processor;
    CHECK(processor.getName() == "Klaymore");
    CHECK(processor.acceptsMidi());
    CHECK_FALSE(processor.producesMidi());
    CHECK(processor.getTotalNumInputChannels() == 0);
    CHECK(processor.getTotalNumOutputChannels() == 2);
}
