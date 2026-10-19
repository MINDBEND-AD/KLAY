// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source, uses JUCE (AGPLv3). See THIRD_PARTY_LICENSES.md (C-006).
#include "plugin/PluginEditor.h"
#include "plugin/PluginProcessor.h"

namespace klay
{

KlayEditor::KlayEditor(KlayProcessor& processor)
    : AudioProcessorEditor(processor)
{
    setSize(480, 240);
}

void KlayEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff16171a));

    auto area = getLocalBounds().reduced(24);
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(32.0f, juce::Font::bold));
    g.drawText(JucePlugin_Name, area.removeFromTop(area.getHeight() / 2), juce::Justification::centredBottom);

    g.setColour(juce::Colours::white.withAlpha(0.6f));
    g.setFont(juce::FontOptions(16.0f));
    g.drawText(juce::String(JucePlugin_Manufacturer) + "  |  v" + JucePlugin_VersionString, area,
               juce::Justification::centredTop);
}

} // namespace klay
