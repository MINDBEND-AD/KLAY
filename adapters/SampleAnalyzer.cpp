// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source, uses JUCE (AGPLv3). See THIRD_PARTY_LICENSES.md (C-006).
#include "adapters/SampleAnalyzer.h"
#include "adapters/SampleLoader.h"
#include "core/library/KeyDetector.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace klay::adapters
{

library::Analysis analyseSample(const std::string& utf8Path)
{
    library::Analysis a;
    const juce::File file(juce::String::fromUTF8(utf8Path.c_str()));
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(file.existsAsFile() ? formats.createReaderFor(file) : nullptr);
    if (reader == nullptr)
    {
        a.error = "unsupported, corrupt or missing audio file";
        return a;
    }
    const double rate = reader->sampleRate;
    const auto length = reader->lengthInSamples;
    if (rate <= 0.0 || reader->numChannels == 0 || length <= 0 || static_cast<double>(length) > kMaxSampleSeconds * rate)
    {
        a.error = "empty, invalid or over-long audio file";
        return a;
    }

    const int n = static_cast<int>(length);
    const int channels = static_cast<int>(std::min<unsigned>(reader->numChannels, 2));
    juce::AudioBuffer<float> buffer(channels, n);
    if (! reader->read(&buffer, 0, n, 0, true, channels > 1))
    {
        a.error = "could not read audio data";
        return a;
    }

    std::vector<float> mono(static_cast<size_t>(n), 0.0f);
    float peak = 0.0f;
    for (int ch = 0; ch < channels; ++ch)
    {
        const float* src = buffer.getReadPointer(ch);
        for (int i = 0; i < n; ++i)
        {
            peak = std::max(peak, std::abs(src[i]));
            mono[static_cast<size_t>(i)] += src[i] / static_cast<float>(channels);
        }
    }

    a.ok = true;
    a.lengthSeconds = static_cast<double>(n) / rate;
    a.peak = peak;
    const auto key = library::detectKey(mono.data(), mono.size(), rate);
    a.rootDetected = key.detected;
    a.rootNote = key.midiNote;
    a.rootHz = key.hz;
    a.rootUncertain = key.uncertain;
    return a;
}

} // namespace klay::adapters
