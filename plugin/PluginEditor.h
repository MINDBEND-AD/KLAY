// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source, uses JUCE (AGPLv3). See THIRD_PARTY_LICENSES.md (C-006).
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace klay
{

class KlayProcessor;

// M0 placeholder editor: shows the plugin name and version. The real UI is built in M3 (ui/).
class KlayEditor final : public juce::AudioProcessorEditor
{
public:
    explicit KlayEditor(KlayProcessor& processor);

    void paint(juce::Graphics& g) override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KlayEditor)
};

} // namespace klay
