// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source, uses JUCE (AGPLv3). See THIRD_PARTY_LICENSES.md (C-006).
#pragma once

#include "core/render/StereoBuffer.h"

#include <juce_core/juce_core.h>

namespace klay::cli
{

// Writes a stereo WAV: 32 bits = float (golden files, bit-exact), otherwise integer PCM.
bool writeWav(const juce::File& file, const render::StereoBuffer& audio, double sampleRate, int bits = 32);

// klay-render listening --samples <dir> --out <dir> [--sr 48000]: LT-1, LT-2 and AC-5 material (task 1.17).
int listening(const juce::StringArray& args);

} // namespace klay::cli
