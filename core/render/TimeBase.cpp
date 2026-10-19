// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source. See THIRD_PARTY_LICENSES.md (C-006).
#include "core/render/TimeBase.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace klay::render
{

double noteValueBeats(model::KickLength length) noexcept
{
    switch (length)
    {
        case model::KickLength::sixteenth: return 0.25;
        case model::KickLength::eighth: return 0.5;
        case model::KickLength::half: return 2.0;
        case model::KickLength::bar: return 4.0;
        case model::KickLength::quarter: break;
    }
    return 1.0;
}

double beatsToMs(double beats, double bpm) noexcept
{
    return beats * 60'000.0 / bpm;
}

size_t msToSamples(double ms, double sampleRate) noexcept
{
    return static_cast<size_t>(std::llround(std::max(0.0, ms) * sampleRate / 1000.0));
}

double effectiveBpm(const model::Settings& settings, std::optional<double> hostBpm) noexcept
{
    if (settings.manualBpmOverride || ! hostBpm || ! std::isfinite(*hostBpm) || *hostBpm <= 0.0)
        return settings.manualBpm;
    return std::clamp(*hostBpm, 20.0, 999.0);
}

double kickLengthMs(model::KickLength length, double bpm) noexcept
{
    return beatsToMs(noteValueBeats(length), bpm);
}

size_t kickLengthSamples(model::KickLength length, double bpm, double sampleRate) noexcept
{
    return msToSamples(kickLengthMs(length, bpm), sampleRate);
}

float endFadeGain(size_t i, size_t fadeLength) noexcept
{
    if (fadeLength == 0)
        return 1.0f;
    const double x = static_cast<double>(i + 1) / static_cast<double>(fadeLength); // (0, 1]
    return static_cast<float>(0.5 * (1.0 + std::cos(std::numbers::pi * x)));
}

void applyEndFade(float* data, size_t numSamples, double sampleRate) noexcept
{
    const size_t fade = std::min(numSamples, msToSamples(kEndFadeMs, sampleRate));
    float* start = data + (numSamples - fade);
    for (size_t i = 0; i < fade; ++i)
        start[i] *= endFadeGain(i, fade);
}

} // namespace klay::render
