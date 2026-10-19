// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source, uses JUCE (AGPLv3). See THIRD_PARTY_LICENSES.md (C-006).
#include "adapters/SampleLoader.h"
#include "adapters/JuceResampler.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>

namespace klay::adapters
{

namespace
{
    std::vector<float> resampleChannel(const float* in, size_t n, double ratio, size_t outLength)
    {
        JuceResampler<juce::WindowedSincInterpolator> resampler;
        const int latency = resampler.latencySamples();
        std::vector<float> padded(n + static_cast<size_t>(latency) + 64 + static_cast<size_t>(std::ceil(ratio)) * 8, 0.0f);
        std::copy_n(in, n, padded.begin());
        std::vector<float> out(outLength), prime(static_cast<size_t>(latency) + 1);
        const int consumed = latency > 0 ? resampler.process(1.0, padded.data(), prime.data(), latency) : 0;
        resampler.process(ratio, padded.data() + consumed, out.data(), static_cast<int>(outLength));
        return out;
    }
} // namespace

LoadResult loadSample(const std::string& utf8Path, double targetSampleRate)
{
    LoadResult result;
    const juce::File file(juce::String::fromUTF8(utf8Path.c_str()));
    if (! file.existsAsFile())
    {
        result.error = "file not found: " + utf8Path;
        return result;
    }

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr)
    {
        result.error = "unsupported or corrupt audio file: " + utf8Path;
        return result;
    }

    const double fileRate = reader->sampleRate;
    const auto length = reader->lengthInSamples;
    if (fileRate <= 0.0 || reader->numChannels == 0 || length <= 0)
    {
        result.error = "empty or invalid audio file: " + utf8Path;
        return result;
    }
    if (static_cast<double>(length) > kMaxSampleSeconds * fileRate)
    {
        result.error = "audio file longer than " + std::to_string(static_cast<int>(kMaxSampleSeconds)) + " s: " + utf8Path;
        return result;
    }

    const int n = static_cast<int>(length);
    juce::AudioBuffer<float> buffer(2, n);
    if (! reader->read(&buffer, 0, n, 0, true, reader->numChannels > 1))
    {
        result.error = "could not read audio data: " + utf8Path;
        return result;
    }
    if (reader->numChannels == 1)
        buffer.copyFrom(1, 0, buffer, 0, 0, n); // FR-161: mono to stereo

    auto data = std::make_shared<render::SampleData>();
    data->sampleRate = targetSampleRate;
    if (fileRate == targetSampleRate)
    {
        data->audio.left.assign(buffer.getReadPointer(0), buffer.getReadPointer(0) + n);
        data->audio.right.assign(buffer.getReadPointer(1), buffer.getReadPointer(1) + n);
    }
    else
    {
        const double ratio = fileRate / targetSampleRate; // input samples per output sample
        const auto outLength = static_cast<size_t>(std::llround(n / ratio));
        data->audio.left = resampleChannel(buffer.getReadPointer(0), static_cast<size_t>(n), ratio, outLength);
        data->audio.right = resampleChannel(buffer.getReadPointer(1), static_cast<size_t>(n), ratio, outLength);
    }
    result.sample = std::move(data);
    return result;
}

std::future<LoadResult> loadSampleAsync(std::string utf8Path, double targetSampleRate)
{
    return std::async(std::launch::async,
                      [path = std::move(utf8Path), targetSampleRate] { return loadSample(path, targetSampleRate); });
}

} // namespace klay::adapters
