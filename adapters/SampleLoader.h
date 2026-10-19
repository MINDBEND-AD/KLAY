// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source, uses JUCE (AGPLv3). See THIRD_PARTY_LICENSES.md (C-006).
#pragma once

#include "core/render/StereoBuffer.h"

#include <future>
#include <memory>
#include <string>

namespace klay::adapters
{

inline constexpr double kMaxSampleSeconds = 30.0; // longer files are rejected (a kick part is far shorter)

struct LoadResult
{
    std::shared_ptr<const render::SampleData> sample; // null on error
    std::string error;
};

// FR-161, FR-162 (backend): reads WAV, AIFF or FLAC through JUCE's AudioFormatManager into a float stereo buffer
// (mono is copied to both channels, extra channels are dropped) and resamples it to `targetSampleRate` with JUCE's
// windowed-sinc interpolator (plan decision 18). A missing, corrupt or over-long file gives an error, never a crash.
// Blocking file I/O: never call it on the audio thread (NFR-001).
LoadResult loadSample(const std::string& utf8Path, double targetSampleRate);

// Runs loadSample on a background thread.
std::future<LoadResult> loadSampleAsync(std::string utf8Path, double targetSampleRate);

} // namespace klay::adapters
