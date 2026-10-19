// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source. See THIRD_PARTY_LICENSES.md (C-006).
#pragma once

#include <cstddef>
#include <vector>

namespace klay::render
{

// Non-interleaved stereo float audio. Every part is stereo after loading (FR-161: mono is copied to both channels).
struct StereoBuffer
{
    std::vector<float> left;
    std::vector<float> right;

    StereoBuffer() = default;
    explicit StereoBuffer(size_t numSamples) : left(numSamples, 0.0f), right(numSamples, 0.0f) {}

    size_t size() const noexcept { return left.size(); }
    void resize(size_t numSamples) { left.resize(numSamples, 0.0f); right.resize(numSamples, 0.0f); }
    float* channel(int ch) noexcept { return ch == 0 ? left.data() : right.data(); }
    const float* channel(int ch) const noexcept { return ch == 0 ? left.data() : right.data(); }

    bool operator==(const StereoBuffer&) const = default;
};

// A loaded part, already converted to the session sample rate (plan decision 18).
struct SampleData
{
    double sampleRate = 0.0;
    StereoBuffer audio;
};

} // namespace klay::render
