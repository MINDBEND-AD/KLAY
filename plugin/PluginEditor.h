// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source, uses JUCE (AGPLv3). See THIRD_PARTY_LICENSES.md (C-006).
#pragma once

#include "core/model/Kick.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>
#include <memory>
#include <vector>

namespace klay
{

class KlayProcessor;

// Temporary dev editor for M1–M2 (plan decision 14, task 1.15): load/save kick JSON, basic per-layer controls,
// audition, key display. Replaced by the real UI in M3 (task 3.8).
class KlayEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit KlayEditor(KlayProcessor& processor);
    ~KlayEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Rebuilds every control from the processor's kick (after a load or a state restore).
    void refreshFromKick();

private:
    struct LayerRow;

    void timerCallback() override;
    void edit(const std::function<void(model::Kick&)>& change);
    void loadKick();
    void saveKick();

    KlayProcessor& processor;

    juce::TextButton loadButton{ "Load kick..." }, saveButton{ "Save kick..." }, auditionButton{ "Audition" };
    juce::Slider bpmSlider;
    juce::ToggleButton bpmOverride{ "Manual BPM" }, autogain{ "Autogain" };
    juce::ComboBox kickLength;
    juce::Label keyLabel, statusLabel;
    std::vector<std::unique_ptr<LayerRow>> rows;
    std::unique_ptr<juce::FileChooser> chooser;
    model::Kick shown; // what the controls show; a host state restore is picked up by the timer
    bool updating = false;
};

} // namespace klay
