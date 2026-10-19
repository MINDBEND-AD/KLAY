// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source, uses JUCE (AGPLv3). See THIRD_PARTY_LICENSES.md (C-006).
#include "plugin/PluginProcessor.h"
#include "tests/rt/RtHarness.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>

using namespace klay;

namespace
{

struct TempKick
{
    juce::File dir = juce::File::createTempFile("klay-play");
    juce::File wav;

    TempKick()
    {
        dir.createDirectory();
        wav = dir.getChildFile("kick.wav");
        juce::AudioBuffer<float> b(1, 24000);
        for (int i = 0; i < b.getNumSamples(); ++i)
            b.setSample(0, i, 0.8f * std::exp(-i / 6000.0f) * std::sin(2.0f * juce::MathConstants<float>::pi * 55.0f * i / 48000.0f));
        juce::WavAudioFormat format;
        std::unique_ptr<juce::OutputStream> stream = wav.createOutputStream();
        auto writer = format.createWriterFor(stream, juce::AudioFormatWriterOptions{}.withSampleRate(48000.0).withNumChannels(1).withBitsPerSample(24));
        writer->writeFromAudioSampleBuffer(b, 0, b.getNumSamples());
    }
    ~TempKick() { dir.deleteRecursively(); }

    model::Kick kick() const
    {
        auto k = model::makeDefaultKick();
        k.layers[0].samplePath = wav.getFullPathName().toStdString();
        k.layers[0].pitchFollow = true;
        k.layers[1].samplePath = wav.getFullPathName().toStdString();
        k.layers[1].offsetMs = 3.0;
        k.layers[1].faderDb = -6.0f;
        return k;
    }
};

void prepared(KlayProcessor& p, int block = 512)
{
    p.setPlayConfigDetails(0, 2, 48000.0, block);
    p.prepareToPlay(48000.0, block);
}

void waitFor(KlayProcessor& p, std::uint64_t generation)
{
    REQUIRE(p.getEngine().waitForGeneration(generation, std::chrono::seconds(20)));
}

std::vector<float> playNote(KlayProcessor& p, int note, int velocity, int blocks, int at = 0)
{
    std::vector<float> out;
    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;
    for (int b = 0; b < blocks; ++b)
    {
        midi.clear();
        if (b == 0)
            midi.addEvent(juce::MidiMessage::noteOn(1, note, (juce::uint8) velocity), at);
        p.processBlock(buffer, midi);
        for (int i = 0; i < 512; ++i)
            out.push_back(buffer.getSample(0, i));
    }
    return out;
}

struct FakePlayHead final : juce::AudioPlayHead
{
    std::optional<double> bpm;
    bool playing = false;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        if (bpm)
            info.setBpm(*bpm);
        info.setIsPlaying(playing);
        return info;
    }
};

} // namespace

// FR-001: velocity is ignored; a note-on starts the kick at its sample position.
TEST_CASE("Processor plays the kick on MIDI; velocity 1 and 127 are identical", "[plugin][play][FR-001]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    TempKick temp;
    KlayProcessor p;
    prepared(p);
    waitFor(p, p.setKick(temp.kick()));

    const auto loud = playNote(p, 33, 127, 20, 100);
    p.releaseResources();
    prepared(p);
    const auto soft = playNote(p, 33, 1, 20, 100);
    REQUIRE(loud == soft);

    for (int i = 0; i < 100; ++i)
        REQUIRE(loud[static_cast<size_t>(i)] == 0.0f); // nothing before the note-on offset
    float peak = 0.0f;
    for (float v : loud)
        peak = std::max(peak, std::abs(v));
    CHECK(peak > 0.5f); // autogain normalises to −0.3 dBFS
    CHECK(peak < 1.0f);
}

// FR-003 Gherkin: "Given any state, when the project is saved and reopened, then a render is bit-identical."
TEST_CASE("Save and reopen gives a bit-identical render", "[plugin][state][FR-003][NFR-009]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    TempKick temp;
    KlayProcessor a;
    prepared(a);
    auto kick = temp.kick();
    kick.layers[1].length = 1.4;
    kick.layers[2].samplePath = kick.layers[1].samplePath;
    kick.layers[2].mode = klay::model::LayerMode::pitch;
    kick.layers[2].pitchSemitones = -2;
    kick.layers[0].envelope.points = { { 0.0, 1.0f, 0.3f }, { 200.0, 0.2f, 0.0f } };
    waitFor(a, a.setKick(kick));

    juce::MemoryBlock state;
    a.getStateInformation(state);
    KlayProcessor b;
    prepared(b);
    b.setStateInformation(state.getData(), (int) state.getSize());
    REQUIRE(b.getKick() == a.getKick());
    waitFor(b, b.getEngine().requestRender(b.getKick(), 48000.0, b.getEffectiveBpm()));

    const auto ra = a.getEngine().latest();
    const auto rb = b.getEngine().latest();
    REQUIRE(ra->length == rb->length);
    for (size_t bus = 0; bus < 3; ++bus)
    {
        CHECK(ra->buses[bus].follow == rb->buses[bus].follow);
        CHECK(ra->buses[bus].fixed == rb->buses[bus].fixed);
    }
    CHECK(playNote(a, 40, 100, 30) == playNote(b, 40, 100, 30));
}

TEST_CASE("Missing samples are reported, not crashing", "[plugin][state][FR-003]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    KlayProcessor p;
    prepared(p);
    auto kick = model::makeDefaultKick();
    kick.layers[2].samplePath = "C:/does/not/exist.wav";
    waitFor(p, p.setKick(kick));
    CHECK(p.getMissingSamples() == std::vector<std::string>{ "C:/does/not/exist.wav" });
    const auto out = playNote(p, 36, 100, 4);
    for (float v : out)
        REQUIRE(v == 0.0f);
}

// FR-002 (task 1.10) with a simulated playhead; FR-236 transport stop fade.
TEST_CASE("Host tempo re-renders after it settles; manual override wins", "[plugin][tempo][FR-002][FR-236]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    TempKick temp;
    KlayProcessor p;
    FakePlayHead head;
    p.setPlayHead(&head);
    prepared(p);
    waitFor(p, p.setKick(temp.kick()));
    CHECK(p.getEngine().latest()->bpm == 155.0); // no host tempo yet: manual default

    head.bpm = 120.0;
    playNote(p, 33, 100, 1);
    p.pollHostTempo(1000.0);
    p.pollHostTempo(1200.0);
    CHECK(p.getEffectiveBpm() == 120.0);
    REQUIRE(p.getEngine().waitForGeneration(p.getEngine().publishedGeneration() + 1, std::chrono::seconds(20)));
    CHECK(p.getEngine().latest()->bpm == 120.0);
    CHECK(p.getEngine().latest()->length == 24000); // 1/4 at 120 BPM

    auto kick = p.getKick();
    kick.settings.manualBpmOverride = true;
    kick.settings.manualBpm = 150.0;
    waitFor(p, p.setKick(kick));
    CHECK(p.getEngine().latest()->bpm == 150.0);

    // Transport stop fades the ringing kick out within 5 ms.
    head.playing = true;
    playNote(p, 33, 100, 1);
    head.playing = false;
    const auto after = playNote(p, 60, 0, 1); // velocity 0 counts as note-off: only the stop is seen
    for (size_t i = 240; i < after.size(); ++i)
        REQUIRE(after[i] == 0.0f);
    p.setPlayHead(nullptr);
}

TEST_CASE("Audition plays the kick untransposed", "[plugin][play][FR-116]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    TempKick temp;
    KlayProcessor p;
    prepared(p);
    waitFor(p, p.setKick(temp.kick()));
    p.triggerAudition();
    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;
    p.processBlock(buffer, midi);
    CHECK(buffer.getMagnitude(0, 512) > 0.1f);
}

TEST_CASE("Processor playback is real-time safe", "[plugin][rt][NFR-001]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    TempKick temp;
    KlayProcessor p;
    prepared(p, 128);
    waitFor(p, p.setKick(temp.kick()));

    juce::AudioBuffer<float> buffer(2, 128);
    juce::MidiBuffer midi;
    midi.ensureSize(256);
    klay::rt::test::ScopedViolationRecorder recorder;
    for (int block = 0; block < 64; ++block)
    {
        midi.clear();
        if (block % 8 == 0)
            midi.addEvent(juce::MidiMessage::noteOn(1, 24 + block, (juce::uint8) 100), block % 128);
        if (block == 20)
            p.triggerAudition();
        RT_GUARD;
        p.processBlock(buffer, midi);
    }
    const auto counts = recorder.counts();
    CAPTURE(counts.allocations, counts.deallocations, counts.locks, counts.io, counts.logging, counts.sleeps);
    CHECK(counts.total() == 0);
}
