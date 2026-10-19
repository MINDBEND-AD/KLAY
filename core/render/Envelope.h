// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source. See THIRD_PARTY_LICENSES.md (C-006).
#pragma once

#include "core/model/Kick.h"

#include <cstddef>

namespace klay::render
{

// Power-curve segment (Q10): value at fraction t (0…1) from `from` to `to` with tension −1…+1. Tension 0 is a
// straight line; positive tension holds the start value longer (t^p), negative moves away faster (1 − (1 − t)^p),
// with p = 8^|tension|.
float curveValue(float from, float to, float tension, double t) noexcept;

// Envelope gain at an absolute time on the kick timeline (FR-280, FR-283). No points: flat 1. Before the first
// point and after the last one the gain holds that point's value.
float envelopeValue(const model::Envelope& envelope, double timeMs) noexcept;

// Writes the gain for kick samples [firstSample, firstSample + numSamples) at `sampleRate`, evaluated exactly at
// each sample's time, so the envelope is sample-accurate and continuous between points (FR-285).
void renderEnvelope(const model::Envelope& envelope, double sampleRate, size_t firstSample, float* gains,
                    size_t numSamples) noexcept;

// FR-282: snaps a dragged point to the nearest intersection of a grid×grid grid that spans the kick length
// horizontally and 0–100 % vertically (FR-283). `freePlacement` (Alt held) returns the point unchanged, clamped to
// the grid area.
model::EnvelopePoint snapToGrid(model::EnvelopePoint point, double kickLengthMs, int grid, bool freePlacement) noexcept;

} // namespace klay::render
