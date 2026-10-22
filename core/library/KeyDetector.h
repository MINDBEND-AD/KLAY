// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source. See THIRD_PARTY_LICENSES.md (C-006).
#pragma once

#include "core/render/StackRenderer.h"

#include <cstddef>
#include <string>

namespace klay::library
{

inline constexpr double kKeyMinHz = 30.0;  // FR-120 search range
inline constexpr double kKeyMaxHz = 150.0;
inline constexpr double kUncertainCents = 50.0; // YIN and FFT further apart than this: uncertain

// FR-120: fundamental of a kick part. Own YIN (30–150 Hz) per frame; the key is the median over the settled tail
// (voiced frames after the attack, latter part of the sound), cross-checked against the FFT peak of the same region.
// Returns detected = false if no voiced frames are found. `parallel` spreads the YIN frames over the hardware threads
// (same result); callers that already run many detections at once (the library scan) pass false.
render::KeyInfo detectKey(const float* mono, size_t numSamples, double sampleRate, bool parallel = true);

// Note name with octave, C4 = MIDI 60 (e.g. 30 → "F#1").
std::string noteName(int midiNote);

double midiToHz(double midiNote) noexcept;
double hzToMidi(double hz) noexcept;

} // namespace klay::library
