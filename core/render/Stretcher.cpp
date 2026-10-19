// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source. See THIRD_PARTY_LICENSES.md (C-006).
#include "core/render/Stretcher.h"

#include <signalsmith-stretch/signalsmith-stretch.h>

#include <algorithm>
#include <cmath>

namespace klay::render
{

namespace
{
    constexpr long kStretchSeed = 0x4b4c4159; // "KLAY": fixed seed for NFR-009
}

StereoBuffer stretch(const StereoBuffer& input, double sampleRate, double lengthFactor, double semitones,
                     model::StretchPreset preset)
{
    lengthFactor = std::clamp(lengthFactor, kMinStretch, kMaxStretch);
    const size_t n = input.size();
    const size_t outLength = static_cast<size_t>(std::llround(static_cast<double>(n) * lengthFactor));
    if (lengthFactor == 1.0 && semitones == 0.0)
        return input;
    if (n == 0)
        return StereoBuffer(outLength);

    signalsmith::stretch::SignalsmithStretch<float> stretcher(kStretchSeed);
    if (preset == model::StretchPreset::cheaper)
        stretcher.presetCheaper(2, static_cast<float>(sampleRate));
    else
        stretcher.presetDefault(2, static_cast<float>(sampleRate));
    stretcher.setTransposeSemitones(static_cast<float>(semitones));

    // Kick parts are often shorter than the stretcher's seek window, so pad both ends with silence and cut the
    // aligned middle out afterwards. exact() maps input time linearly onto output time.
    const size_t pad = static_cast<size_t>(2 * (stretcher.blockSamples() + stretcher.intervalSamples()));
    const size_t paddedIn = pad + n + pad;
    const size_t paddedOut = static_cast<size_t>(std::llround(static_cast<double>(paddedIn) * lengthFactor));
    const size_t outStart = static_cast<size_t>(std::llround(static_cast<double>(pad) * lengthFactor));

    std::vector<std::vector<float>> in(2, std::vector<float>(paddedIn, 0.0f));
    std::vector<std::vector<float>> out(2, std::vector<float>(paddedOut, 0.0f));
    for (int ch = 0; ch < 2; ++ch)
        std::copy_n(input.channel(ch), n, in[static_cast<size_t>(ch)].begin() + static_cast<std::ptrdiff_t>(pad));

    stretcher.exact(in, static_cast<int>(paddedIn), out, static_cast<int>(paddedOut));

    StereoBuffer result(outLength);
    for (int ch = 0; ch < 2; ++ch)
    {
        const auto& src = out[static_cast<size_t>(ch)];
        const size_t available = std::min(outLength, paddedOut - std::min(paddedOut, outStart));
        std::copy_n(src.begin() + static_cast<std::ptrdiff_t>(outStart), available, result.channel(ch));
    }
    return result;
}

} // namespace klay::render
