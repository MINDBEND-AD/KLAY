// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source. See THIRD_PARTY_LICENSES.md (C-006).
#include "core/render/Resampler.h"

#include <cmath>

namespace klay::render
{

double semitonesToRatio(double semitones) noexcept
{
    return std::exp2(semitones / 12.0);
}

StereoBuffer repitch(const StereoBuffer& input, double semitones, const ResamplerFactory& factory)
{
    const double ratio = semitonesToRatio(semitones);
    const size_t n = input.size();
    const size_t outLength = static_cast<size_t>(std::llround(static_cast<double>(n) / ratio));
    StereoBuffer output(outLength);
    if (n == 0 || outLength == 0)
        return output;

    auto resampler = factory();
    const int latency = resampler->latencySamples();
    // Zero padding past the end, so the interpolator can always read ahead.
    const size_t padding = static_cast<size_t>(latency) + 16 + static_cast<size_t>(std::ceil(ratio)) * 8;

    std::vector<float> padded(n + padding, 0.0f);
    std::vector<float> primeOut(static_cast<size_t>(latency) + 1);
    for (int ch = 0; ch < 2; ++ch)
    {
        std::copy_n(input.channel(ch), n, padded.begin());
        resampler->reset();
        int consumed = 0;
        if (latency > 0)
            consumed = resampler->process(1.0, padded.data(), primeOut.data(), latency);
        resampler->process(ratio, padded.data() + consumed, output.channel(ch), static_cast<int>(outLength));
    }
    return output;
}

} // namespace klay::render
