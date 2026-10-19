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

    // Signalsmith Stretch over a whole buffer: output exactly round(n × lengthFactor) samples, time-aligned.
    StereoBuffer signalsmithStretch(const StereoBuffer& input, double sampleRate, double lengthFactor, double semitones,
                                    model::StretchPreset preset)
    {
        const size_t n = input.size();
        const size_t outLength = static_cast<size_t>(std::llround(static_cast<double>(n) * lengthFactor));
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

    double meanSquare(const StereoBuffer& b)
    {
        double sum = 0.0;
        for (int ch = 0; ch < 2; ++ch)
            for (size_t i = 0; i < b.size(); ++i)
                sum += static_cast<double>(b.channel(ch)[i]) * b.channel(ch)[i];
        return b.size() > 0 ? sum / static_cast<double>(2 * b.size()) : 0.0;
    }
} // namespace

StereoBuffer stretch(const StereoBuffer& input, double sampleRate, double lengthFactor, double semitones,
                     model::StretchPreset preset, const ResamplerFactory& resamplers)
{
    lengthFactor = std::clamp(lengthFactor, kMinStretch, kMaxStretch);
    const size_t n = input.size();
    const size_t outLength = static_cast<size_t>(std::llround(static_cast<double>(n) * lengthFactor));
    if (lengthFactor == 1.0 && semitones == 0.0)
        return input;
    if (n == 0)
        return StereoBuffer(outLength);

    StereoBuffer result;
    const double ratio = semitonesToRatio(semitones);
    if (semitones != 0.0 && resamplers && lengthFactor * ratio >= 0.25 && lengthFactor * ratio <= 4.0)
    {
        // Exact pitch: Signalsmith's own pitch shift is off by up to ~45 cents at kick fundamentals (≈ 45 Hz), its
        // time-stretch is not. So stretch by the pitch ratio (pitch kept), then Repitch by the same ratio back to
        // the target length (M1 review, see docs/plan.md). Only within a 0.25–4× internal stretch (about ±24 st at
        // 1×): far transpositions use Signalsmith's own shift, which costs ~10× less there (AC-2).
        result = repitch(signalsmithStretch(input, sampleRate, lengthFactor * ratio, 0.0, preset), semitones, resamplers);
        result.resize(outLength);
    }
    else
    {
        result = signalsmithStretch(input, sampleRate, lengthFactor, semitones, preset);
    }

    // Level compensation (PO decision 2026-10-19): Signalsmith loses up to ~3.5 dB at long stretches. An ideal
    // time-stretch keeps the mean square (stretching the amplitude envelope in time does not change its average),
    // so the output is scaled to the input's RMS over both channels.
    const double inMs = meanSquare(input);
    const double outMs = meanSquare(result);
    if (inMs > 0.0 && outMs > 0.0)
    {
        const auto gain = static_cast<float>(std::sqrt(inMs / outMs));
        for (int ch = 0; ch < 2; ++ch)
            for (size_t i = 0; i < result.size(); ++i)
                result.channel(ch)[i] *= gain;
    }
    return result;
}

} // namespace klay::render
