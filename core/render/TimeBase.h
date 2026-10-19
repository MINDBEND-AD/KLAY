// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source. See THIRD_PARTY_LICENSES.md (C-006).
#pragma once

#include "core/model/Kick.h"

#include <cstddef>
#include <optional>

namespace klay::render
{

inline constexpr double kEndFadeMs = 5.0;   // FR-108 (Q8): soft fade at the end of the kick
inline constexpr double kNoteCutFadeMs = 5.0; // FR-115, FR-236

// Kick length in beats (quarter notes); "1 bar" assumes 4/4 (FR-108).
double noteValueBeats(model::KickLength length) noexcept;

double beatsToMs(double beats, double bpm) noexcept;

// Nearest sample index for a time in ms.
size_t msToSamples(double ms, double sampleRate) noexcept;

// FR-002: the tempo the render uses. The manual BPM wins when the override is on or the host sends no tempo.
double effectiveBpm(const model::Settings& settings, std::optional<double> hostBpm) noexcept;

double kickLengthMs(model::KickLength length, double bpm) noexcept;
size_t kickLengthSamples(model::KickLength length, double bpm, double sampleRate) noexcept;

// Gain of the end fade at sample i of an n-sample fade: raised-cosine from just below 1 down to exactly 0 at the
// last sample, so the kick always ends in silence (FR-108).
float endFadeGain(size_t i, size_t fadeLength) noexcept;

// Applies the FR-108 end fade over the last kEndFadeMs of `numSamples` samples of `data`.
void applyEndFade(float* data, size_t numSamples, double sampleRate) noexcept;

} // namespace klay::render
