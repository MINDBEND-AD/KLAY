// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source, uses JUCE (AGPLv3). See THIRD_PARTY_LICENSES.md (C-006).
#pragma once

// A synthetic sample library on disk (FR-160 layout: Role/sub-folder/files) for plugin tests and benchmarks, plus a
// user-data folder for the index cache. Automated tests never use the factory library (plan decision 5).

#include "core/library/SampleIndex.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>

namespace klay::test
{

struct SyntheticLibrary
{
    juce::File dir = juce::File::createTempFile("klay-lib");
    juce::File data = juce::File::createTempFile("klay-data");

    // `perRole` files in each of the six role folders, spread over two sub-folders. Each file is a kick-like
    // decaying sine (`baseHz` + a few Hz per file, so the roots differ) of `seconds` at `rate`.
    explicit SyntheticLibrary(int perRole, double seconds = 0.3, double rate = 44100.0, double baseHz = 45.0)
    {
        dir.createDirectory();
        data.createDirectory();
        const char* roles[] = { "Kick", "Rumble", "Drive", "Crunch", "Transient", "Texture" };
        juce::WavAudioFormat wav;
        const int n = static_cast<int>(seconds * rate);
        juce::AudioBuffer<float> buffer(1, n);
        int fileNo = 0;
        for (const char* role : roles)
            for (int i = 0; i < perRole; ++i, ++fileNo)
            {
                const double hz = baseHz + (fileNo % 24);
                double phase = 0.0;
                for (int s = 0; s < n; ++s)
                {
                    const double t = s / rate;
                    phase += 2.0 * juce::MathConstants<double>::pi * (hz + 120.0 * std::exp(-t * 40.0)) / rate;
                    buffer.setSample(0, s, static_cast<float>(0.7 * std::exp(-t * 6.0) * std::sin(phase)));
                }
                const auto file = dir.getChildFile(role).getChildFile(i % 2 ? "b" : "a")
                                      .getChildFile(juce::String(role) + "_" + juce::String(i) + ".wav");
                file.getParentDirectory().createDirectory();
                std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
                auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions{}
                                                              .withSampleRate(rate)
                                                              .withNumChannels(1)
                                                              .withBitsPerSample(16));
                REQUIRE(writer != nullptr);
                REQUIRE(writer->writeFromAudioSampleBuffer(buffer, 0, n));
            }
    }

    ~SyntheticLibrary()
    {
        dir.deleteRecursively();
        data.deleteRecursively();
    }

    std::filesystem::path root() const { return library::fromUtf8(dir.getFullPathName().toStdString()); }
    std::filesystem::path dataDir() const { return library::fromUtf8(data.getFullPathName().toStdString()); }
};

} // namespace klay::test
