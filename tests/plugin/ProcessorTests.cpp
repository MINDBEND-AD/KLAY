// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source, uses JUCE (AGPLv3). See THIRD_PARTY_LICENSES.md (C-006).
#include "plugin/PluginProcessor.h"
#include "tests/rt/RtHarness.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <memory>

namespace
{

void fillWithNoise(juce::AudioBuffer<float>& buffer, juce::Random& random)
{
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            buffer.setSample(ch, i, random.nextFloat() * 2.0f - 1.0f);
}

bool isSilent(const juce::AudioBuffer<float>& buffer)
{
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            if (buffer.getSample(ch, i) != 0.0f)
                return false;
    return true;
}

} // namespace

TEST_CASE("Processor reports the plugin identity", "[plugin]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    klay::KlayProcessor processor;
    CHECK(processor.getName() == "Klaymore");
    CHECK(juce::String(JucePlugin_Manufacturer) == "MINDBEND AD");
    CHECK(processor.acceptsMidi());
    CHECK_FALSE(processor.producesMidi());
    CHECK(processor.getTotalNumInputChannels() == 0);
    CHECK(processor.getTotalNumOutputChannels() == 2);
    REQUIRE(processor.getNumPrograms() == 1);
    CHECK(processor.getProgramName(0).isNotEmpty()); // VST3 validator "Scan Programs" (NFR-011)
}

TEST_CASE("Processor only accepts a stereo output without inputs", "[plugin]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    klay::KlayProcessor processor;

    juce::AudioProcessor::BusesLayout stereo;
    stereo.outputBuses.add(juce::AudioChannelSet::stereo());
    CHECK(processor.checkBusesLayoutSupported(stereo));

    juce::AudioProcessor::BusesLayout mono;
    mono.outputBuses.add(juce::AudioChannelSet::mono());
    CHECK_FALSE(processor.checkBusesLayoutSupported(mono));

    juce::AudioProcessor::BusesLayout withInput = stereo;
    withInput.inputBuses.add(juce::AudioChannelSet::stereo());
    CHECK_FALSE(processor.checkBusesLayoutSupported(withInput));
}

// C-001, FR-005: 44.1–192 kHz and 16–4096 samples per block, no crash, silent output.
TEST_CASE("Processor outputs silence at every sample rate and block size", "[plugin][FR-005]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    const double sampleRate = GENERATE(44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0);
    const int blockSize = GENERATE(16, 17, 32, 64, 128, 256, 441, 512, 1024, 2048, 4096);
    CAPTURE(sampleRate, blockSize);

    klay::KlayProcessor processor;
    processor.setPlayConfigDetails(0, 2, sampleRate, blockSize);
    processor.prepareToPlay(sampleRate, blockSize);

    juce::Random random(1234);
    juce::AudioBuffer<float> buffer(2, blockSize);
    juce::MidiBuffer midi;

    for (int block = 0; block < 8; ++block)
    {
        // Hosts may deliver shorter blocks than announced; alternate full and partial blocks.
        const int numSamples = (block % 2 == 0) ? blockSize : juce::jmax(1, blockSize / 3);
        buffer.setSize(2, numSamples, false, false, true);
        fillWithNoise(buffer, random);

        midi.clear();
        midi.addEvent(juce::MidiMessage::noteOn(1, 36, (juce::uint8) 127), 0);
        midi.addEvent(juce::MidiMessage::noteOff(1, 36), numSamples - 1);

        processor.processBlock(buffer, midi);

        REQUIRE(isSilent(buffer));
        CHECK(midi.isEmpty());
    }

    processor.releaseResources();
}

// FR-003 (stub): the state carries schemaVersion and survives a save/restore round-trip.
TEST_CASE("Processor state round-trips the schema version", "[plugin][FR-003]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    klay::KlayProcessor source;
    juce::MemoryBlock saved;
    source.getStateInformation(saved);
    CHECK(saved.toString().contains("\"schemaVersion\":1"));

    klay::KlayProcessor restored;
    restored.setStateInformation(saved.getData(), (int) saved.getSize());
    CHECK(restored.getStateHeader() == source.getStateHeader());

    juce::MemoryBlock savedAgain;
    restored.getStateInformation(savedAgain);
    CHECK(savedAgain == saved);
}

TEST_CASE("Processor ignores corrupt state", "[plugin][FR-003]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    klay::KlayProcessor processor;

    const char garbage[] = "\x01\x02 not json";
    processor.setStateInformation(garbage, (int) sizeof(garbage));
    processor.setStateInformation(nullptr, 0);
    CHECK(processor.getStateHeader() == klay::model::StateHeader{});
}

TEST_CASE("Editor opens and closes", "[plugin]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    klay::KlayProcessor processor;

    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditorAndMakeActive());
    REQUIRE(editor != nullptr);
    CHECK(editor->getWidth() > 0);
    CHECK(editor->getHeight() > 0);

    // Paint once into an image to exercise the name + version drawing.
    juce::Image image(juce::Image::ARGB, editor->getWidth(), editor->getHeight(), true);
    juce::Graphics g(image);
    editor->paintEntireComponent(g, false);

    processor.editorBeingDeleted(editor.get());
    editor.reset();
}

// NFR-001, FR-234: the audio-thread entry points neither allocate nor lock. The test wraps each call in its own
// RT_GUARD, so it still fails if the guard inside processBlock is removed.
TEST_CASE("Processor processBlock is real-time safe", "[plugin][rt][NFR-001]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    const double sampleRate = GENERATE(44100.0, 192000.0);
    const int blockSize = GENERATE(16, 4096);
    const bool bypassed = GENERATE(false, true);
    CAPTURE(sampleRate, blockSize, bypassed);

    klay::KlayProcessor processor;
    processor.setPlayConfigDetails(0, 2, sampleRate, blockSize);
    processor.prepareToPlay(sampleRate, blockSize);

    juce::AudioBuffer<float> buffer(2, blockSize);
    juce::MidiBuffer midi;
    midi.ensureSize(256);

    klay::rt::test::ScopedViolationRecorder recorder;
    for (int block = 0; block < 16; ++block)
    {
        midi.clear();
        midi.addEvent(juce::MidiMessage::noteOn(1, 36, (juce::uint8) 100), 0);
        RT_GUARD;
        if (bypassed)
            processor.processBlockBypassed(buffer, midi);
        else
            processor.processBlock(buffer, midi);
    }

    const auto counts = recorder.counts();
    CAPTURE(counts.allocations, counts.deallocations, counts.locks, counts.io, counts.logging, counts.sleeps);
    CHECK(counts.total() == 0);
}

// M0 renders in single precision only, so hosts never call the double-precision processBlock. When a later
// milestone adds it, replace this with an RT test of the double overload (NFR-001).
TEST_CASE("Processor does not support double precision", "[plugin][rt][NFR-001]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    klay::KlayProcessor processor;
    CHECK_FALSE(processor.supportsDoublePrecisionProcessing());
}

// The harness also sees JUCE's own allocations and locks, which go through std::malloc and the OS, not operator new.
TEST_CASE("RT harness catches JUCE allocations and locks", "[plugin][rt][NFR-001]")
{
    juce::AudioBuffer<float> buffer(2, 16);
    juce::CriticalSection section;

    klay::rt::test::ScopedViolationRecorder recorder;
    {
        RT_GUARD;
        buffer.setSize(2, 8192);
    }
    CHECK(recorder.counts().allocations >= 1);

    {
        RT_GUARD;
        const juce::ScopedLock lock(section);
    }
    CHECK(recorder.counts().locks >= 1);
}
