// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source, uses JUCE (AGPLv3). See THIRD_PARTY_LICENSES.md (C-006).
#include "tools/render-cli/Listening.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <cstdio>

namespace klay::cli
{

bool writeWav(const juce::File& file, const render::StereoBuffer& audio, double sampleRate, int bits)
{
    file.getParentDirectory().createDirectory();
    file.deleteFile();
    juce::AudioBuffer<float> buffer(2, static_cast<int>(audio.size()));
    for (int ch = 0; ch < 2; ++ch)
        buffer.copyFrom(ch, 0, audio.channel(ch), static_cast<int>(audio.size()));

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
    if (stream == nullptr)
        return false;
    auto options = juce::AudioFormatWriterOptions{}.withSampleRate(sampleRate).withNumChannels(2).withBitsPerSample(bits);
    if (bits == 32)
        options = options.withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
    auto writer = wav.createWriterFor(stream, options);
    return writer != nullptr && writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
}

int listening(const juce::StringArray&)
{
    std::fprintf(stderr, "klay-render: listening material is not implemented yet (task 1.17)\n");
    return 2;
}

} // namespace klay::cli
