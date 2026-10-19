// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source. See THIRD_PARTY_LICENSES.md (C-006).
#pragma once

#include "core/render/StereoBuffer.h"

#include <functional>
#include <memory>

namespace klay::render
{

// Streaming varispeed resampler for one channel (FR-106 Repitch). The core only sees this interface; the JUCE
// LagrangeInterpolator implementation lives in adapters/ (C-007). Implementations must be real-time safe in
// process() and reset(), since the Key Track fallback (FR-125) runs them on the audio thread.
class Resampler
{
public:
    virtual ~Resampler() = default;

    virtual void reset() noexcept = 0;

    // Delay of the output behind the input, in whole input samples. Callers compensate by priming the resampler
    // with that many samples at ratio 1, so output sample j lands on input position j × ratio.
    virtual int latencySamples() const noexcept = 0;

    // Produces numOut samples, reading input at speedRatio input samples per output sample; returns the number of
    // input samples consumed. `in` must hold at least that many samples.
    virtual int process(double speedRatio, const float* in, float* out, int numOut) noexcept = 0;
};

using ResamplerFactory = std::function<std::unique_ptr<Resampler>()>;

// 2^(semitones / 12): the playback speed for a pitch change.
double semitonesToRatio(double semitones) noexcept;

// Offline Repitch (FR-106): pitch and length change together. +12 st plays twice as fast, so the output is
// round(n / ratio) samples long. Output sample j is the input at position j × ratio.
StereoBuffer repitch(const StereoBuffer& input, double semitones, const ResamplerFactory& factory);

} // namespace klay::render
