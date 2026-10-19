// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source. See THIRD_PARTY_LICENSES.md (C-006).
#pragma once

#include "core/model/Kick.h"
#include "core/render/StereoBuffer.h"

namespace klay::render
{

inline constexpr double kMinStretch = 0.25; // FR-105
inline constexpr double kMaxStretch = 8.0;

// Offline Signalsmith Stretch (FR-104, FR-105, FR-106 Stretch mode): changes the length by `lengthFactor`
// (0.25–8×) and the pitch by `semitones` independently. The output is exactly round(n × lengthFactor) samples and
// time-aligned with the input (input sample i lands at output i × lengthFactor). The random engine has a fixed
// seed, so the same input gives bit-identical output (NFR-009). Factor 1 and 0 semitones return the input as is.
StereoBuffer stretch(const StereoBuffer& input, double sampleRate, double lengthFactor, double semitones,
                     model::StretchPreset preset);

} // namespace klay::render
