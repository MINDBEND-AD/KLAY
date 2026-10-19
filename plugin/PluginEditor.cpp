// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source, uses JUCE (AGPLv3). See THIRD_PARTY_LICENSES.md (C-006).
#include "plugin/PluginEditor.h"
#include "plugin/KickFiles.h"
#include "plugin/PluginProcessor.h"

#include <cmath>

namespace klay
{

namespace
{
    constexpr int kTopHeight = 92;
    constexpr int kRowHeight = 34;
    constexpr int kWidth = 1180;

    void setupSlider(juce::Slider& s, double lo, double hi, double step, const juce::String& suffix)
    {
        s.setSliderStyle(juce::Slider::LinearHorizontal);
        s.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 58, 20);
        s.setRange(lo, hi, step);
        s.setTextValueSuffix(suffix);
    }
} // namespace

// One row of per-layer controls (FR-101–106, FR-110, FR-111, FR-122, routing).
struct KlayEditor::LayerRow
{
    juce::Label name, sample;
    juce::TextButton load{ "Load" };
    juce::Slider fader, semitones, cents, stretch, offset;
    juce::ComboBox mode, routing;
    juce::ToggleButton mute{ "M" }, solo{ "S" }, follow{ "P" };

    std::vector<juce::Component*> all()
    {
        return { &name, &load, &sample, &fader, &semitones, &cents, &stretch, &mode, &offset, &mute, &solo, &follow, &routing };
    }
};

KlayEditor::KlayEditor(KlayProcessor& p) : AudioProcessorEditor(p), processor(p)
{
    for (auto* b : { &loadButton, &saveButton, &auditionButton })
        addAndMakeVisible(*b);
    loadButton.onClick = [this] { loadKick(); };
    saveButton.onClick = [this] { saveKick(); };
    auditionButton.onClick = [this] { processor.triggerAudition(); }; // FR-116 API

    setupSlider(bpmSlider, 20.0, 300.0, 0.01, " BPM");
    bpmSlider.onValueChange = [this] { edit([this](model::Kick& k) { k.settings.manualBpm = bpmSlider.getValue(); }); };
    bpmOverride.onClick = [this] { edit([this](model::Kick& k) { k.settings.manualBpmOverride = bpmOverride.getToggleState(); }); };
    autogain.onClick = [this] { edit([this](model::Kick& k) { k.settings.autogain = autogain.getToggleState(); }); };
    kickLength.addItemList({ "1/16", "1/8", "1/4", "1/2", "1 bar" }, 1);
    kickLength.onChange = [this] {
        edit([this](model::Kick& k) { k.settings.kickLength = static_cast<model::KickLength>(kickLength.getSelectedItemIndex()); });
    };
    for (juce::Component* c : { (juce::Component*) &bpmSlider, (juce::Component*) &bpmOverride, (juce::Component*) &autogain,
                                (juce::Component*) &kickLength, (juce::Component*) &keyLabel, (juce::Component*) &statusLabel })
        addAndMakeVisible(c);
    keyLabel.setFont(juce::FontOptions(18.0f, juce::Font::bold));

    for (size_t i = 0; i < processor.getKick().layers.size(); ++i)
    {
        auto row = std::make_unique<LayerRow>();
        auto& r = *row;
        setupSlider(r.fader, -60.0, 0.0, 0.1, " dB");
        setupSlider(r.semitones, -12.0, 12.0, 1.0, " st");
        setupSlider(r.cents, -100.0, 100.0, 1.0, " ct");
        setupSlider(r.stretch, 0.25, 8.0, 0.001, "x");
        r.stretch.setSkewFactorFromMidPoint(1.0); // log-like travel around 1× (decision 8)
        setupSlider(r.offset, 0.0, 200.0, 0.01, " ms");
        r.mode.addItemList({ "Stretch", "Repitch" }, 1);
        r.routing.addItemList({ "MAIN", "A1", "A2" }, 1);
        r.sample.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.7f));

        auto layerEdit = [this, i](std::function<void(model::Layer&)> f) {
            edit([i, f](model::Kick& k) { if (i < k.layers.size()) f(k.layers[i]); });
        };
        // The bottom of the fader range is −∞ (FR-101).
        r.fader.onValueChange = [&r, layerEdit] {
            const float v = static_cast<float>(r.fader.getValue());
            layerEdit([v](model::Layer& l) { l.faderDb = v <= -60.0f ? model::kMinusInfinityDb : v; });
        };
        r.semitones.onValueChange = [&r, layerEdit] { layerEdit([&r](model::Layer& l) { l.pitchSemitones = (int) r.semitones.getValue(); }); };
        r.cents.onValueChange = [&r, layerEdit] { layerEdit([&r](model::Layer& l) { l.pitchCents = (int) r.cents.getValue(); }); };
        r.stretch.onValueChange = [&r, layerEdit] { layerEdit([&r](model::Layer& l) { l.stretch = r.stretch.getValue(); }); };
        r.offset.onValueChange = [&r, layerEdit] { layerEdit([&r](model::Layer& l) { l.offsetMs = r.offset.getValue(); }); };
        r.mode.onChange = [&r, layerEdit] {
            layerEdit([&r](model::Layer& l) { l.mode = r.mode.getSelectedItemIndex() == 1 ? model::LayerMode::repitch : model::LayerMode::stretch; });
        };
        r.routing.onChange = [&r, layerEdit] {
            layerEdit([&r](model::Layer& l) { l.routing = static_cast<model::Routing>(juce::jmax(0, r.routing.getSelectedItemIndex())); });
        };
        r.mute.onClick = [this, i] { edit([this, i](model::Kick& k) { model::clickMute(k, i, juce::ModifierKeys::currentModifiers.isShiftDown()); }); };
        r.solo.onClick = [this, i] { edit([this, i](model::Kick& k) { model::clickSolo(k, i, juce::ModifierKeys::currentModifiers.isShiftDown()); }); };
        r.follow.onClick = [&r, layerEdit] { layerEdit([&r](model::Layer& l) { l.pitchFollow = r.follow.getToggleState(); }); };
        r.load.onClick = [this, i] {
            chooser = std::make_unique<juce::FileChooser>("Load a part", juce::File(), "*.wav;*.aif;*.aiff;*.flac");
            chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                 [this, i](const juce::FileChooser& fc) {
                                     const auto file = fc.getResult();
                                     if (file.existsAsFile())
                                         edit([&file, i](model::Kick& k) { if (i < k.layers.size()) k.layers[i].samplePath = file.getFullPathName().toStdString(); });
                                 });
        };

        for (auto* c : r.all())
            addAndMakeVisible(c);
        rows.push_back(std::move(row));
    }

    setSize(kWidth, kTopHeight + kRowHeight * static_cast<int>(rows.size()) + 12);
    refreshFromKick();
    startTimerHz(10);
}

KlayEditor::~KlayEditor()
{
    stopTimer();
}

void KlayEditor::edit(const std::function<void(model::Kick&)>& change)
{
    if (updating)
        return;
    auto kick = processor.getKick();
    change(kick);
    processor.setKick(std::move(kick));
    refreshFromKick(); // shows clamped values and mute/solo side effects
}

void KlayEditor::refreshFromKick()
{
    const juce::ScopedValueSetter<bool> guard(updating, true);
    const auto& kick = processor.getKick();
    const auto& s = kick.settings;
    shown = kick;
    bpmSlider.setValue(s.manualBpm, juce::dontSendNotification);
    bpmOverride.setToggleState(s.manualBpmOverride, juce::dontSendNotification);
    autogain.setToggleState(s.autogain, juce::dontSendNotification);
    kickLength.setSelectedItemIndex(static_cast<int>(s.kickLength), juce::dontSendNotification);

    for (size_t i = 0; i < rows.size(); ++i)
    {
        auto& r = *rows[i];
        const bool exists = i < kick.layers.size();
        for (auto* c : r.all())
            c->setEnabled(exists);
        if (! exists)
            continue;
        const auto& l = kick.layers[i];
        r.name.setText(juce::String(i + 1) + " " + juce::String::fromUTF8(l.name.c_str()), juce::dontSendNotification);
        r.sample.setText(l.samplePath.empty() ? "(empty)" : juce::File(juce::String::fromUTF8(l.samplePath.c_str())).getFileName(),
                         juce::dontSendNotification);
        r.fader.setValue(l.faderDb <= model::kMinusInfinityDb ? -60.0 : l.faderDb, juce::dontSendNotification);
        r.semitones.setValue(l.pitchSemitones, juce::dontSendNotification);
        r.cents.setValue(l.pitchCents, juce::dontSendNotification);
        r.stretch.setValue(l.stretch, juce::dontSendNotification);
        r.offset.setValue(l.offsetMs, juce::dontSendNotification);
        r.mode.setSelectedItemIndex(l.mode == model::LayerMode::repitch ? 1 : 0, juce::dontSendNotification);
        r.routing.setSelectedItemIndex(static_cast<int>(l.routing), juce::dontSendNotification);
        r.mute.setToggleState(l.mute, juce::dontSendNotification);
        r.solo.setToggleState(l.solo, juce::dontSendNotification);
        r.follow.setToggleState(l.pitchFollow, juce::dontSendNotification);
        // FR-106 (2026-10-19): Pitch is greyed out in Repitch mode; the Stretch knob is the varispeed.
        const bool pitchActive = l.mode == model::LayerMode::stretch;
        r.semitones.setEnabled(pitchActive);
        r.cents.setEnabled(pitchActive);
    }
}

void KlayEditor::timerCallback()
{
    if (! (processor.getKick() == shown))
        refreshFromKick();

    // FR-123 key display (note name + Hz, "—" without a key, "?" when uncertain); missing parts (FR-003).
    const auto latest = processor.getEngine().latest();
    if (latest)
    {
        const auto& key = latest->key;
        keyLabel.setText(key.detected ? juce::String("Key ") + key.name + (key.uncertain ? " ?" : "") + "  "
                                            + juce::String(key.hz, 1) + " Hz"
                                      : juce::String(juce::CharPointer_UTF8("Key \xe2\x80\x94")),
                         juce::dontSendNotification);
        juce::String status = juce::String(latest->bpm, 2) + " BPM, " + juce::String((int) latest->length) + " samples, cache "
                              + juce::String(processor.getEngine().cachedNotes()) + " notes";
        if (! latest->missingSamples.empty())
            status += "  |  missing: " + juce::String::fromUTF8(latest->missingSamples.front().c_str())
                      + (latest->missingSamples.size() > 1 ? " (+" + juce::String((int) latest->missingSamples.size() - 1) + ")" : "");
        statusLabel.setText(status, juce::dontSendNotification);
    }
}

void KlayEditor::loadKick()
{
    chooser = std::make_unique<juce::FileChooser>("Load kick JSON", juce::File(), "*.json");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                         [this](const juce::FileChooser& fc) {
                             if (auto kick = loadKickFile(fc.getResult()))
                             {
                                 processor.setKick(std::move(*kick));
                                 refreshFromKick();
                             }
                             else if (fc.getResult() != juce::File())
                                 statusLabel.setText("Not a valid kick JSON: " + fc.getResult().getFileName(), juce::dontSendNotification);
                         });
}

void KlayEditor::saveKick()
{
    chooser = std::make_unique<juce::FileChooser>("Save kick JSON", juce::File(), "*.json");
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                             | juce::FileBrowserComponent::warnAboutOverwriting,
                         [this](const juce::FileChooser& fc) {
                             if (fc.getResult() != juce::File())
                                 saveKickFile(fc.getResult().withFileExtension("json"), processor.getKick());
                         });
}

void KlayEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff16171a));
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(20.0f, juce::Font::bold));
    g.drawText(juce::String(JucePlugin_Name) + "  dev editor  v" + JucePlugin_VersionString, 12, 6, 400, 24,
               juce::Justification::centredLeft);
    g.setColour(juce::Colours::white.withAlpha(0.5f));
    g.setFont(juce::FontOptions(12.0f));
    g.drawText("layer | part | fader | pitch st | ct | stretch | mode | offset | M S P | bus", 12, kTopHeight - 18, kWidth - 24, 16,
               juce::Justification::centredLeft);
}

void KlayEditor::resized()
{
    auto top = getLocalBounds().reduced(12, 0).withTrimmedTop(34).withHeight(28);
    loadButton.setBounds(top.removeFromLeft(100));
    saveButton.setBounds(top.removeFromLeft(100).withTrimmedLeft(6));
    auditionButton.setBounds(top.removeFromLeft(100).withTrimmedLeft(6));
    bpmSlider.setBounds(top.removeFromLeft(230).withTrimmedLeft(12));
    bpmOverride.setBounds(top.removeFromLeft(110));
    kickLength.setBounds(top.removeFromLeft(90));
    autogain.setBounds(top.removeFromLeft(100).withTrimmedLeft(10));
    keyLabel.setBounds(top.removeFromLeft(220).withTrimmedLeft(10));
    statusLabel.setBounds(getLocalBounds().reduced(12, 0).withTrimmedTop(6).withHeight(24).withTrimmedLeft(420));

    int y = kTopHeight;
    for (auto& row : rows)
    {
        auto r = juce::Rectangle<int>(12, y, kWidth - 24, kRowHeight - 4);
        row->name.setBounds(r.removeFromLeft(90));
        row->load.setBounds(r.removeFromLeft(48));
        row->sample.setBounds(r.removeFromLeft(130));
        row->fader.setBounds(r.removeFromLeft(150));
        row->semitones.setBounds(r.removeFromLeft(130));
        row->cents.setBounds(r.removeFromLeft(130));
        row->stretch.setBounds(r.removeFromLeft(140));
        row->mode.setBounds(r.removeFromLeft(86).reduced(2, 2));
        row->offset.setBounds(r.removeFromLeft(140));
        row->mute.setBounds(r.removeFromLeft(42));
        row->solo.setBounds(r.removeFromLeft(42));
        row->follow.setBounds(r.removeFromLeft(42));
        row->routing.setBounds(r.reduced(2, 2));
        y += kRowHeight;
    }
}

} // namespace klay
