// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source, uses JUCE (AGPLv3). See THIRD_PARTY_LICENSES.md (C-006).
#include "adapters/JuceResampler.h"
#include "adapters/SampleLoader.h"
#include "tests/core/TestSignals.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>

using namespace klay;

namespace
{

struct TempDir
{
    juce::File dir = juce::File::createTempFile("klay-tests");
    TempDir() { dir.createDirectory(); }
    ~TempDir() { dir.deleteRecursively(); }
};

// Writes a 100 Hz sine (left) / 200 Hz sine (right) test file with the given format.
juce::File writeTestFile(const juce::File& dir, juce::AudioFormat& format, const juce::String& name, double sampleRate,
                         int channels, int numSamples, int bits)
{
    const auto file = dir.getChildFile(name);
    juce::AudioBuffer<float> buffer(channels, numSamples);
    for (int i = 0; i < numSamples; ++i)
    {
        buffer.setSample(0, i, 0.5f * std::sin(2.0f * juce::MathConstants<float>::pi * 100.0f * i / (float) sampleRate));
        if (channels > 1)
            buffer.setSample(1, i, 0.25f * std::sin(2.0f * juce::MathConstants<float>::pi * 200.0f * i / (float) sampleRate));
    }
    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
    auto writer = format.createWriterFor(stream, juce::AudioFormatWriterOptions{}
                                                     .withSampleRate(sampleRate)
                                                     .withNumChannels(channels)
                                                     .withBitsPerSample(bits));
    REQUIRE(writer != nullptr);
    REQUIRE(writer->writeFromAudioSampleBuffer(buffer, 0, numSamples));
    return file;
}

} // namespace

// FR-161, FR-162 backend (task 1.2).
TEST_CASE("Sample loader reads WAV, AIFF and FLAC as stereo float", "[adapters][loader][FR-161]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    TempDir temp;
    juce::WavAudioFormat wav;
    juce::AiffAudioFormat aiff;
    juce::FlacAudioFormat flac;

    struct Case { juce::AudioFormat* format; const char* name; int bits; };
    const Case cases[] = { { &wav, "s.wav", 24 }, { &aiff, "s.aiff", 16 }, { &flac, "s.flac", 24 }, { &wav, "f.wav", 32 } };
    for (const auto& c : cases)
    {
        CAPTURE(c.name);
        const auto file = writeTestFile(temp.dir, *c.format, c.name, 48000.0, 2, 4800, c.bits);
        const auto result = adapters::loadSample(file.getFullPathName().toStdString(), 48000.0);
        REQUIRE(result.error.empty());
        REQUIRE(result.sample != nullptr);
        CHECK(result.sample->sampleRate == 48000.0);
        REQUIRE(result.sample->audio.size() == 4800);
        CHECK(std::abs(result.sample->audio.left[120] - 0.5f * std::sin(2.0 * juce::MathConstants<double>::pi * 100.0 * 120 / 48000.0)) < 1e-3);
        CHECK(test::zeroCrossingHz(result.sample->audio.right.data(), 0, 4800, 48000.0) == Catch::Approx(200.0).epsilon(0.001));
    }

    // Mono is copied to both channels.
    const auto mono = writeTestFile(temp.dir, wav, "mono.wav", 48000.0, 1, 1000, 24);
    const auto result = adapters::loadSample(mono.getFullPathName().toStdString(), 48000.0);
    REQUIRE(result.sample != nullptr);
    CHECK(result.sample->audio.left == result.sample->audio.right);
}

TEST_CASE("Sample loader converts to the session rate", "[adapters][loader][FR-161]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    TempDir temp;
    juce::WavAudioFormat wav;
    const double fileRate = GENERATE(44100.0, 48000.0, 96000.0);
    const double sessionRate = GENERATE(44100.0, 48000.0, 192000.0);
    CAPTURE(fileRate, sessionRate);

    const int n = static_cast<int>(fileRate / 2); // 0.5 s
    const auto file = writeTestFile(temp.dir, wav, "rate.wav", fileRate, 2, n, 24);
    const auto result = adapters::loadSampleAsync(file.getFullPathName().toStdString(), sessionRate).get();
    REQUIRE(result.sample != nullptr);
    CHECK(result.sample->sampleRate == sessionRate);
    CHECK(result.sample->audio.size() == static_cast<size_t>(std::llround(sessionRate / 2)));

    // Same pitch and level after conversion (measured over 40 whole cycles).
    const auto& left = result.sample->audio.left;
    const size_t len = left.size();
    CHECK(test::zeroCrossingHz(left.data(), len / 10, len * 9 / 10, sessionRate) == Catch::Approx(100.0).epsilon(0.0005));
    CHECK(test::rms(left.data(), len / 10, len * 9 / 10) == Catch::Approx(0.5 / std::sqrt(2.0)).epsilon(0.02));
}

TEST_CASE("Sample loader reports corrupt and missing files without crashing", "[adapters][loader][FR-162]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    TempDir temp;

    const auto garbage = temp.dir.getChildFile("corrupt.wav");
    garbage.replaceWithText("RIFF\x01\x02 this is not audio at all");
    auto result = adapters::loadSample(garbage.getFullPathName().toStdString(), 48000.0);
    CHECK(result.sample == nullptr);
    CHECK_FALSE(result.error.empty());

    // A valid header with the data chunk cut off.
    juce::WavAudioFormat wav;
    const auto good = writeTestFile(temp.dir, wav, "trunc.wav", 48000.0, 2, 48000, 24);
    juce::MemoryBlock bytes;
    good.loadFileAsData(bytes);
    const auto truncated = temp.dir.getChildFile("truncated.wav");
    truncated.replaceWithData(bytes.getData(), 60);
    result = adapters::loadSample(truncated.getFullPathName().toStdString(), 48000.0);
    CHECK(result.sample == nullptr);
    CHECK_FALSE(result.error.empty());

    result = adapters::loadSample(temp.dir.getChildFile("missing.wav").getFullPathName().toStdString(), 48000.0);
    CHECK(result.sample == nullptr);
    CHECK(result.error.find("not found") != std::string::npos);
}

// FR-106 Repitch (task 1.5): 1 kHz at +12 st gives 2 kHz at half the length.
TEST_CASE("Repitch through the Lagrange resampler doubles the frequency at +12 st", "[adapters][repitch][FR-106]")
{
    const double sr = 48000.0;
    const size_t n = 48000;
    const auto input = test::sine(1000.0, sr, n);
    const auto output = render::repitch(input, 12.0, adapters::lagrangeResamplerFactory());
    REQUIRE(output.size() == n / 2);
    CHECK(test::fftPeakHz(output.left.data(), output.size(), sr, 100.0, 20000.0) == Catch::Approx(2000.0).margin(1.0));
    CHECK(test::fftPeakHz(output.right.data(), output.size(), sr, 100.0, 20000.0) == Catch::Approx(2000.0).margin(1.0));

    const auto down = render::repitch(input, -7.0, adapters::lagrangeResamplerFactory());
    CHECK(down.size() == static_cast<size_t>(std::llround(n * std::exp2(7.0 / 12.0))));
    CHECK(test::fftPeakHz(down.left.data(), down.size(), sr, 100.0, 20000.0) == Catch::Approx(1000.0 * std::exp2(-7.0 / 12.0)).margin(1.0));
}

TEST_CASE("Repitch keeps time alignment: no added latency", "[adapters][repitch][FR-106][NFR-003]")
{
    const double semitones = GENERATE(0.0, 12.0, -12.0);
    CAPTURE(semitones);
    render::StereoBuffer impulse(4000);
    impulse.left[1000] = impulse.right[1000] = 1.0f;

    const auto output = render::repitch(impulse, semitones, adapters::lagrangeResamplerFactory());
    size_t peak = 0;
    for (size_t i = 0; i < output.size(); ++i)
        if (std::abs(output.left[i]) > std::abs(output.left[peak]))
            peak = i;
    CHECK(peak == static_cast<size_t>(std::llround(1000.0 / std::exp2(semitones / 12.0))));
}
