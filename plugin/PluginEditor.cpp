// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source, uses JUCE (AGPLv3). See THIRD_PARTY_LICENSES.md (C-006).
#include "plugin/PluginEditor.h"
#include "plugin/KickFiles.h"
#include "plugin/PluginProcessor.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <optional>

namespace klay
{

namespace
{
    constexpr int kTopHeight = 128;
    constexpr int kRowHeight = 60;
    constexpr int kWidth = 1400;

    void setupSlider(juce::Slider& s, double lo, double hi, double step, const juce::String& suffix)
    {
        s.setSliderStyle(juce::Slider::LinearHorizontal);
        s.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 58, 20);
        s.setRange(lo, hi, step);
        s.setTextValueSuffix(suffix);
    }

    // Pitch mode keeps semitones and cents (FR-104: ±12 st, ±100 ct); the one knob runs in cents.
    int totalCents(const model::Layer& l)
    {
        return l.pitchSemitones * 100 + l.pitchCents;
    }

    void setTotalCents(model::Layer& l, int cents)
    {
        l.pitchSemitones = juce::jlimit(-12, 12, static_cast<int>(std::lround(cents / 100.0)));
        l.pitchCents = juce::jlimit(-100, 100, cents - l.pitchSemitones * 100);
    }

    juce::String signedText(double v, int decimals, const juce::String& unit)
    {
        return (v > 0.0 ? "+" : "") + juce::String(v, decimals) + unit;
    }

    // UI-007 readout: what the active mode does to length and pitch.
    juce::String readout(const model::Layer& l)
    {
        switch (l.mode)
        {
            case model::LayerMode::pitch:
                return "Pitch " + signedText(l.pitchSemitones, 0, " st ") + signedText(l.pitchCents, 0, " ct") + " | length kept";
            case model::LayerMode::tape:
                return "Speed " + juce::String(l.tapeSpeed, 2) + "x | " + signedText(12.0 * std::log2(l.tapeSpeed), 1, " st")
                       + ", length " + juce::String(1.0 / l.tapeSpeed, 2) + "x";
            case model::LayerMode::length:
                break;
        }
        return "Length " + juce::String(l.length, 2) + "x | pitch kept";
    }

    juce::String utf8(const std::string& s)
    {
        return juce::String::fromUTF8(s.c_str());
    }

    // Roles in the order of model::Role (FR-140).
    const juce::StringArray& roleNames()
    {
        static const juce::StringArray names{ "Kick", "Rumble", "Drive", "Crunch", "Transient", "Texture", "Any" };
        return names;
    }

    // Slider drags are one undo step each (FR-239): begin on mouse down, end on mouse up.
    void gestureOnDrag(juce::Slider& s, KlayProcessor& p, const std::string& label)
    {
        s.onDragStart = [&p, label] { p.beginGesture(label); };
        s.onDragEnd = [&p] { p.endGesture(); };
    }
} // namespace

// One row of per-layer controls (FR-101–106, FR-110–114, FR-122, FR-140, FR-142, routing). FR-106 / UI-007: one
// layer knob with a Length · Pitch · Tape switch below it; its range, travel and readout follow the mode.
struct KlayEditor::LayerRow
{
    juce::Label name, sample, readout;
    juce::ComboBox role;
    juce::TextButton load{ "Load" }, reforge{ juce::CharPointer_UTF8("\xe2\x86\xbb") }, bin{ "Bin" };
    juce::Slider fader, knob, offset;
    juce::TextButton modes[3]{ juce::TextButton("Length"), juce::TextButton("Pitch"), juce::TextButton("Tape") };
    juce::ComboBox routing;
    juce::ToggleButton lock{ "L" }, mute{ "M" }, solo{ "S" }, follow{ "P" };
    int knobMode = -1;
    int top = 0; // y of the row, for selection by click (FR-113)

    std::vector<juce::Component*> all()
    {
        return { &name, &role, &load, &reforge, &bin, &sample, &fader, &knob, &modes[0], &modes[1], &modes[2], &readout,
                 &offset, &lock, &mute, &solo, &follow, &routing };
    }

    // Range, travel and default of the knob for a mode (UI-019: log travel in Length and Tape).
    void configureKnob(model::LayerMode mode)
    {
        if (static_cast<int>(mode) == knobMode)
            return; // a range change may send an async value change; only do it when the mode changes
        knobMode = static_cast<int>(mode);
        if (mode == model::LayerMode::pitch)
        {
            knob.setNormalisableRange({ -1300.0, 1300.0, 1.0 });
            knob.setDoubleClickReturnValue(true, 0.0);
        }
        else
        {
            juce::NormalisableRange<double> range(0.25, 8.0, 0.001);
            range.setSkewForCentre(1.0);
            knob.setNormalisableRange(range);
            knob.setDoubleClickReturnValue(true, 1.0);
        }
    }
};

KlayEditor::KlayEditor(KlayProcessor& p) : AudioProcessorEditor(p), processor(p)
{
    setWantsKeyboardFocus(true); // Ctrl+Z / Ctrl+Shift+Z (FR-145), space = audition (FR-116)

    for (auto* b : { &loadButton, &saveButton, &auditionButton, &forgeButton, &undoButton, &redoButton, &libraryButton,
                     &seedForgeButton })
        addAndMakeVisible(*b);
    forgeButton.setComponentID("forge");
    undoButton.setComponentID("undo");
    redoButton.setComponentID("redo");
    loadButton.onClick = [this] { loadKick(); };
    saveButton.onClick = [this] { saveKick(); };
    auditionButton.onClick = [this] { processor.triggerAudition(); }; // FR-116 API
    forgeButton.onClick = [this] { forge(false); };                    // FR-141
    seedForgeButton.onClick = [this] { forge(true); };                 // FR-147: reproduce a roll from its seed
    undoButton.onClick = [this] { processor.undo(); refreshFromKick(); };
    redoButton.onClick = [this] { processor.redo(); refreshFromKick(); };
    libraryButton.onClick = [this] { chooseLibrary(); };
    seedField.setInputRestrictions(20, "0123456789");
    seedField.setTextToShowWhenEmpty("seed", juce::Colours::grey);
    addAndMakeVisible(seedField);

    // FR-146 roll template: which layers Forge re-rolls.
    addAndMakeVisible(templateLabel);
    for (size_t i = 0; i < processor.getKick().layers.size(); ++i)
    {
        auto button = std::make_unique<juce::ToggleButton>(juce::String((int) i + 1));
        button->setComponentID("template-" + juce::String((int) i));
        button->onClick = [this, i, b = button.get()] {
            const bool on = b->getToggleState();
            edit("Roll template", [i, on](model::Kick& k) {
                auto& t = k.settings.rollTemplate;
                t.erase(std::remove(t.begin(), t.end(), static_cast<int>(i)), t.end());
                if (on)
                    t.push_back(static_cast<int>(i));
            });
        };
        addAndMakeVisible(*button);
        templateButtons.push_back(std::move(button));
    }

    setupSlider(bpmSlider, 20.0, 300.0, 0.01, " BPM");
    gestureOnDrag(bpmSlider, processor, "BPM");
    bpmSlider.onValueChange = [this] { edit("BPM", [this](model::Kick& k) { k.settings.manualBpm = bpmSlider.getValue(); }); };
    bpmOverride.onClick = [this] { edit("Manual BPM", [this](model::Kick& k) { k.settings.manualBpmOverride = bpmOverride.getToggleState(); }); };
    autogain.onClick = [this] { edit("Autogain", [this](model::Kick& k) { k.settings.autogain = autogain.getToggleState(); }); };
    kickLength.addItemList({ "1/16", "1/8", "1/4", "1/2", "1 bar" }, 1);
    kickLength.onChange = [this] {
        edit("Kick length", [this](model::Kick& k) { k.settings.kickLength = static_cast<model::KickLength>(kickLength.getSelectedItemIndex()); });
    };
    for (juce::Component* c : { (juce::Component*) &bpmSlider, (juce::Component*) &bpmOverride, (juce::Component*) &autogain,
                                (juce::Component*) &kickLength, (juce::Component*) &keyLabel, (juce::Component*) &statusLabel,
                                (juce::Component*) &libraryLabel, (juce::Component*) &rollLabel })
        addAndMakeVisible(c);
    keyLabel.setFont(juce::FontOptions(18.0f, juce::Font::bold));
    for (auto* l : { &libraryLabel, &rollLabel })
        l->setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.75f));

    for (size_t i = 0; i < processor.getKick().layers.size(); ++i)
    {
        auto row = std::make_unique<LayerRow>();
        auto& r = *row;
        const auto id = juce::String((int) i);
        setupSlider(r.fader, -60.0, 0.0, 0.1, " dB");
        r.knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        r.knob.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        r.knob.setComponentID("knob-" + id);
        for (int m = 0; m < 3; ++m)
        {
            r.modes[m].setRadioGroupId(1000 + static_cast<int>(i));
            r.modes[m].setClickingTogglesState(true);
            r.modes[m].setComponentID("mode-" + id + "-" + model::toString(static_cast<model::LayerMode>(m)));
        }
        setupSlider(r.offset, 0.0, 200.0, 0.01, " ms");
        r.routing.addItemList({ "MAIN", "A1", "A2" }, 1);
        r.role.addItemList(roleNames(), 1);
        r.sample.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.7f));
        r.name.setEditable(false, true); // FR-114: double-click renames
        r.name.setComponentID("name-" + id);
        r.role.setComponentID("role-" + id);
        r.lock.setComponentID("lock-" + id);
        r.reforge.setComponentID("reforge-" + id);
        r.bin.setComponentID("bin-" + id);
        r.mute.setComponentID("mute-" + id);
        r.solo.setComponentID("solo-" + id);
        gestureOnDrag(r.fader, processor, "Fader");
        gestureOnDrag(r.knob, processor, "Layer knob");
        gestureOnDrag(r.offset, processor, "Offset");

        auto layerEdit = [this, i](const std::string& label, std::function<void(model::Layer&)> f) {
            edit(label, [i, f](model::Kick& k) { if (i < k.layers.size()) f(k.layers[i]); });
        };
        // The bottom of the fader range is −∞ (FR-101).
        r.fader.onValueChange = [&r, layerEdit] {
            const float v = static_cast<float>(r.fader.getValue());
            layerEdit("Fader", [v](model::Layer& l) { l.faderDb = v <= -60.0f ? model::kMinusInfinityDb : v; });
        };
        // The knob edits only the active mode's value; the other modes keep theirs (FR-106).
        r.knob.onValueChange = [&r, layerEdit] {
            const double v = r.knob.getValue();
            layerEdit("Layer knob", [v](model::Layer& l) {
                switch (l.mode)
                {
                    case model::LayerMode::pitch: setTotalCents(l, static_cast<int>(std::lround(v))); break;
                    case model::LayerMode::tape: l.tapeSpeed = v; break;
                    case model::LayerMode::length: l.length = v; break;
                }
            });
        };
        for (int m = 0; m < 3; ++m)
            r.modes[m].onClick = [&r, layerEdit, m] {
                if (r.modes[m].getToggleState()) // FR-106: one undo step
                    layerEdit("Mode", [m](model::Layer& l) { l.mode = static_cast<model::LayerMode>(m); });
            };
        r.offset.onValueChange = [&r, layerEdit] { layerEdit("Offset", [&r](model::Layer& l) { l.offsetMs = r.offset.getValue(); }); };
        r.routing.onChange = [&r, layerEdit] {
            layerEdit("Routing", [&r](model::Layer& l) { l.routing = static_cast<model::Routing>(juce::jmax(0, r.routing.getSelectedItemIndex())); });
        };
        r.role.onChange = [&r, layerEdit] {
            layerEdit("Role", [&r](model::Layer& l) { l.role = static_cast<model::Role>(juce::jmax(0, r.role.getSelectedItemIndex())); });
        };
        r.name.onTextChange = [this, &r, i] {
            edit("Rename", [i, t = r.name.getText().toStdString()](model::Kick& k) { model::renameLayer(k, i, t); });
        };
        r.lock.onClick = [this, i] { edit("Lock", [i](model::Kick& k) { model::toggleLock(k, i); }); };
        r.mute.onClick = [this, i] { edit("Mute", [i](model::Kick& k) { model::clickMute(k, i, juce::ModifierKeys::currentModifiers.isShiftDown()); }); };
        r.solo.onClick = [this, i] { edit("Solo", [i](model::Kick& k) { model::clickSolo(k, i, juce::ModifierKeys::currentModifiers.isShiftDown()); }); };
        r.follow.onClick = [&r, layerEdit] { layerEdit("Pitch-follow", [&r](model::Layer& l) { l.pitchFollow = r.follow.getToggleState(); }); };
        r.bin.onClick = [this, i] { edit("Bin", [i](model::Kick& k) { model::binLayer(k, i); }); }; // FR-112
        r.reforge.onClick = [this, i] {                                                             // FR-142
            processor.selectLayer(i);
            reportRoll("Reforge", processor.reforge(i));
            refreshFromKick();
        };
        r.load.onClick = [this, i] {
            chooser = std::make_unique<juce::FileChooser>("Load a sample", juce::File(), "*.wav;*.aif;*.aiff;*.flac");
            chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                 [this, i](const juce::FileChooser& fc) {
                                     const auto file = fc.getResult();
                                     if (file.existsAsFile())
                                         edit("Load sample", [&file, i](model::Kick& k) {
                                             if (i < k.layers.size())
                                                 k.layers[i].samplePath = file.getFullPathName().toStdString();
                                         });
                                 });
        };

        for (auto* c : r.all())
        {
            addAndMakeVisible(c);
            c->addMouseListener(this, false); // a click anywhere in the row selects the layer (FR-113)
        }
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

void KlayEditor::edit(const std::string& label, const std::function<void(model::Kick&)>& change)
{
    if (updating)
        return;
    processor.edit(label, change);
    refreshFromKick(); // shows clamped values and mute/solo side effects
}

void KlayEditor::forge(bool withSeed)
{
    std::optional<std::uint64_t> seed;
    if (withSeed)
    {
        const auto text = seedField.getText().trim();
        if (text.isEmpty())
            return;
        seed = static_cast<std::uint64_t>(std::strtoull(text.toRawUTF8(), nullptr, 10));
    }
    reportRoll("Forge", processor.forge(seed));
    seedField.setText(juce::String(processor.getLastRollSeed()), juce::dontSendNotification);
    refreshFromKick();
}

void KlayEditor::reportRoll(const char* what, const random::RollResult& result)
{
    auto list = [](const std::vector<size_t>& layers) {
        juce::StringArray s;
        for (const auto l : layers)
            s.add(juce::String((int) l + 1));
        return s.joinIntoString(",");
    };
    juce::String text = juce::String(what) + " seed " + juce::String(result.seed) + ": ";
    text += result.changed.empty() ? juce::String("nothing changed") : "new sample on layer " + list(result.changed);
    if (! result.locked.empty())
        text += "; locked " + list(result.locked);
    // PO decision 2026-10-22: a layer whose role has no samples stays as it is, with this message.
    for (const auto l : result.emptyPool)
        text += "; Layer " + juce::String((int) l + 1) + ": No sample found for this layer";
    rollLabel.setText(text, juce::dontSendNotification);
}

void KlayEditor::refreshFromKick()
{
    const juce::ScopedValueSetter<bool> guard(updating, true);
    const auto& kick = processor.getKick();
    const auto& s = kick.settings;
    shown = kick;
    shownSelection = processor.getSelectedLayer();
    bpmSlider.setValue(s.manualBpm, juce::dontSendNotification);
    bpmOverride.setToggleState(s.manualBpmOverride, juce::dontSendNotification);
    autogain.setToggleState(s.autogain, juce::dontSendNotification);
    kickLength.setSelectedItemIndex(static_cast<int>(s.kickLength), juce::dontSendNotification);
    for (size_t i = 0; i < templateButtons.size(); ++i)
        templateButtons[i]->setToggleState(model::inRollTemplate(s, i), juce::dontSendNotification);
    undoButton.setEnabled(processor.getUndoHistory().canUndo());
    redoButton.setEnabled(processor.getUndoHistory().canRedo());
    undoButton.setTooltip(utf8(processor.getUndoHistory().undoLabel()));
    redoButton.setTooltip(utf8(processor.getUndoHistory().redoLabel()));

    for (size_t i = 0; i < rows.size(); ++i)
    {
        auto& r = *rows[i];
        const bool exists = i < kick.layers.size();
        for (auto* c : r.all())
            c->setEnabled(exists);
        if (! exists)
            continue;
        const auto& l = kick.layers[i];
        r.name.setText(utf8(l.name), juce::dontSendNotification);
        r.role.setSelectedItemIndex(static_cast<int>(l.role), juce::dontSendNotification);
        r.sample.setText(l.samplePath.empty() ? "(empty)" : juce::File(utf8(l.samplePath)).getFileName(), juce::dontSendNotification);
        r.sample.setTooltip(utf8(l.samplePath));
        r.fader.setValue(l.faderDb <= model::kMinusInfinityDb ? -60.0 : l.faderDb, juce::dontSendNotification);
        r.configureKnob(l.mode);
        r.knob.setValue(l.mode == model::LayerMode::pitch ? totalCents(l)
                        : l.mode == model::LayerMode::tape ? l.tapeSpeed
                                                           : l.length,
                        juce::dontSendNotification);
        for (int m = 0; m < 3; ++m)
            r.modes[m].setToggleState(static_cast<int>(l.mode) == m, juce::dontSendNotification);
        r.readout.setText(readout(l), juce::dontSendNotification);
        r.offset.setValue(l.offsetMs, juce::dontSendNotification);
        r.routing.setSelectedItemIndex(static_cast<int>(l.routing), juce::dontSendNotification);
        r.lock.setToggleState(l.lock, juce::dontSendNotification);
        r.mute.setToggleState(l.mute, juce::dontSendNotification);
        r.solo.setToggleState(l.solo, juce::dontSendNotification);
        r.follow.setToggleState(l.pitchFollow, juce::dontSendNotification);
        r.bin.setEnabled(model::canBin(kick, i) && ! (l == model::makeDefaultLayer(static_cast<int>(i)))); // FR-112: disabled when locked
        r.reforge.setEnabled(! l.lock);                                     // FR-143
    }
    updateStatus();
    repaint();
}

void KlayEditor::updateStatus()
{
    auto& lib = processor.getLibrary();
    const auto index = lib.snapshot();
    const auto root = processor.getLibraryRoot();
    juce::String text = root.empty() ? juce::String("Library: none (Library... to choose the factory folder)")
                                     : "Library: " + utf8(library::toUtf8(root)) + " | " + juce::String((int) index->samples.size())
                                           + " samples" + (lib.status() == library::LibraryScanner::Status::scanning ? " (scanning...)" : "");
    if (const auto failed = lib.lastStats().failed; failed > 0)
        text += " | " + juce::String(failed) + " unreadable";
    libraryLabel.setText(text, juce::dontSendNotification);
}

void KlayEditor::timerCallback()
{
    if (! (processor.getKick() == shown) || processor.getSelectedLayer() != shownSelection)
        refreshFromKick();
    updateStatus();

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
        if (const auto warnings = processor.getLoadWarnings(); ! warnings.empty())
            status += "  |  " + utf8(warnings.front());
        if (! latest->missingSamples.empty())
            status += "  |  missing: " + utf8(latest->missingSamples.front())
                      + (latest->missingSamples.size() > 1 ? " (+" + juce::String((int) latest->missingSamples.size() - 1) + ")" : "");
        statusLabel.setText(status, juce::dontSendNotification);
    }
}

void KlayEditor::mouseDown(const juce::MouseEvent& e)
{
    const int y = e.getEventRelativeTo(this).y;
    for (size_t i = 0; i < rows.size(); ++i)
        if (y >= rows[i]->top && y < rows[i]->top + kRowHeight)
        {
            processor.selectLayer(i); // FR-113
            shownSelection = processor.getSelectedLayer();
            repaint();
            return;
        }
}

bool KlayEditor::keyPressed(const juce::KeyPress& key)
{
    // FR-145: Ctrl+Z undo, Ctrl+Shift+Z redo; FR-116: space plays the kick.
    if (key.getKeyCode() == 'Z' && key.getModifiers().isCommandDown())
    {
        if (key.getModifiers().isShiftDown())
            processor.redo();
        else
            processor.undo();
        refreshFromKick();
        return true;
    }
    if (key == juce::KeyPress::spaceKey)
    {
        processor.triggerAudition();
        return true;
    }
    return false;
}

void KlayEditor::chooseLibrary()
{
    chooser = std::make_unique<juce::FileChooser>("Choose the sample library folder (Role / sub-folder / files)",
                                                  juce::File(utf8(library::toUtf8(processor.getLibraryRoot()))));
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                         [this](const juce::FileChooser& fc) {
                             const auto dir = fc.getResult();
                             if (dir.isDirectory())
                             {
                                 processor.setLibraryRoot(library::fromUtf8(dir.getFullPathName().toStdString()));
                                 updateStatus();
                             }
                         });
}

void KlayEditor::loadKick()
{
    chooser = std::make_unique<juce::FileChooser>("Load kick JSON", juce::File(), "*.json");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                         [this](const juce::FileChooser& fc) {
                             if (auto kick = loadKickFile(fc.getResult()))
                                 edit("Load kick", [&kick](model::Kick& k) { k = std::move(*kick); });
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
    // FR-113: the selected layer's row is highlighted.
    const auto selected = processor.getSelectedLayer();
    if (selected < rows.size())
    {
        g.setColour(juce::Colour(0xff3a2a1c));
        g.fillRoundedRectangle(juce::Rectangle<int>(6, rows[selected]->top, kWidth - 12, kRowHeight - 4).toFloat(), 4.0f);
    }
    g.setColour(juce::Colours::white.withAlpha(0.6f));
    g.setFont(juce::FontOptions(14.0f, juce::Font::bold));
    for (size_t i = 0; i < rows.size(); ++i)
        g.drawText(juce::String((int) i + 1), 12, rows[i]->top, 18, kRowHeight - 6, juce::Justification::centredLeft);
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(20.0f, juce::Font::bold));
    g.drawText(juce::String(JucePlugin_Name) + "  dev editor  v" + JucePlugin_VersionString, 12, 6, 400, 24,
               juce::Justification::centredLeft);
    g.setColour(juce::Colours::white.withAlpha(0.5f));
    g.setFont(juce::FontOptions(12.0f));
    g.drawText("layer (double-click renames) | role | load / reforge / bin | sample | fader | knob | Length / Pitch / Tape | what it does"
               " | offset | L M S P | bus      click a row to select it",
               12, kTopHeight - 18, kWidth - 24, 16, juce::Justification::centredLeft);
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
    libraryButton.setBounds(top.removeFromRight(90));
    statusLabel.setBounds(getLocalBounds().reduced(12, 0).withTrimmedTop(6).withHeight(24).withTrimmedLeft(420));

    auto second = getLocalBounds().reduced(12, 0).withTrimmedTop(68).withHeight(28);
    forgeButton.setBounds(second.removeFromLeft(100));
    undoButton.setBounds(second.removeFromLeft(70).withTrimmedLeft(6));
    redoButton.setBounds(second.removeFromLeft(70).withTrimmedLeft(6));
    templateLabel.setBounds(second.removeFromLeft(100).withTrimmedLeft(10));
    for (auto& b : templateButtons)
        b->setBounds(second.removeFromLeft(42));
    seedField.setBounds(second.removeFromLeft(170).withTrimmedLeft(10).reduced(0, 2));
    seedForgeButton.setBounds(second.removeFromLeft(96).withTrimmedLeft(6));
    rollLabel.setBounds(second.withTrimmedLeft(10));
    libraryLabel.setBounds(getLocalBounds().reduced(12, 0).withTrimmedTop(94).withHeight(18));

    int y = kTopHeight;
    for (auto& row : rows)
    {
        row->top = y;
        auto r = juce::Rectangle<int>(12, y, kWidth - 24, kRowHeight - 6);
        auto line = [](juce::Rectangle<int> b) { return b.withSizeKeepingCentre(b.getWidth(), 26); };
        r.removeFromLeft(18); // layer number, painted
        row->name.setBounds(line(r.removeFromLeft(100)));
        row->role.setBounds(line(r.removeFromLeft(96)).reduced(2, 0));
        row->load.setBounds(line(r.removeFromLeft(46)).reduced(1, 0));
        row->reforge.setBounds(line(r.removeFromLeft(34)).reduced(1, 0));
        row->bin.setBounds(line(r.removeFromLeft(40)).reduced(1, 0));
        row->sample.setBounds(line(r.removeFromLeft(130)));
        row->fader.setBounds(line(r.removeFromLeft(150)));
        row->knob.setBounds(r.removeFromLeft(56));
        auto modes = r.removeFromLeft(162).reduced(4, 0);
        for (auto& b : row->modes)
            b.setBounds(line(modes.removeFromLeft(modes.getWidth() / (3 - static_cast<int>(&b - row->modes))).reduced(1, 0)));
        row->readout.setBounds(line(r.removeFromLeft(200)));
        row->offset.setBounds(line(r.removeFromLeft(130)));
        row->lock.setBounds(line(r.removeFromLeft(40)));
        row->mute.setBounds(line(r.removeFromLeft(40)));
        row->solo.setBounds(line(r.removeFromLeft(40)));
        row->follow.setBounds(line(r.removeFromLeft(40)));
        row->routing.setBounds(line(r).reduced(2, 0));
        y += kRowHeight;
    }
}

} // namespace klay
