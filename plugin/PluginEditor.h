// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source, uses JUCE (AGPLv3). See THIRD_PARTY_LICENSES.md (C-006).
#pragma once

#include "core/model/Kick.h"
#include "core/random/Roll.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace klay
{

class KlayProcessor;

// Temporary dev editor for M1–M2 (plan decision 14, tasks 1.15 and 2.7): load/save kick JSON, per-layer controls,
// audition, key display; in M2 also Forge, Reforge per layer, lock, bin, rename, select, role, roll template, library
// folder, roll seed and undo/redo (buttons, Ctrl+Z / Ctrl+Shift+Z). Every edit goes through KlayProcessor::edit; a
// slider drag is one undo step (FR-239). Replaced by the real UI in M3 (task 3.8).
class KlayEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit KlayEditor(KlayProcessor& processor);
    ~KlayEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    bool keyPressed(const juce::KeyPress& key) override;

    // Rebuilds every control from the processor's kick (after a load or a state restore).
    void refreshFromKick();

private:
    struct LayerRow;

    void timerCallback() override;
    void edit(const std::string& label, const std::function<void(model::Kick&)>& change);
    void loadKick();
    void saveKick();
    void chooseLibrary();
    void forge(bool withSeed);
    void reportRoll(const char* what, const random::RollResult& result);
    void updateStatus();

    KlayProcessor& processor;

    juce::TextButton loadButton{ "Load kick..." }, saveButton{ "Save kick..." }, auditionButton{ "Audition" };
    juce::TextButton forgeButton{ "Forge" }, undoButton{ "Undo" }, redoButton{ "Redo" }, libraryButton{ "Library..." };
    juce::TextButton seedForgeButton{ "Forge seed" };
    juce::TextEditor seedField;
    juce::Label templateLabel{ {}, "Roll template:" };
    std::vector<std::unique_ptr<juce::ToggleButton>> templateButtons;
    juce::Slider bpmSlider;
    juce::ToggleButton bpmOverride{ "Manual BPM" }, autogain{ "Autogain" };
    juce::ComboBox kickLength;
    juce::Label keyLabel, statusLabel, libraryLabel, rollLabel;
    std::vector<std::unique_ptr<LayerRow>> rows;
    std::unique_ptr<juce::FileChooser> chooser;
    model::Kick shown; // what the controls show; a host state restore is picked up by the timer
    size_t shownSelection = 0;
    bool updating = false;
};

} // namespace klay
