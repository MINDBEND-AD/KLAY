// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source, uses JUCE (AGPLv3). See THIRD_PARTY_LICENSES.md (C-006).
#pragma once

#include "core/library/SampleIndex.h"

namespace klay::adapters
{

// FR-165 metadata of one audio file for the sample index: length, sample peak and root (the FR-120 detector on the
// mono mix at the file's own rate). Reads WAV, AIFF, FLAC through JUCE (FR-161); files the loader would reject
// (unreadable, empty, longer than kMaxSampleSeconds) give ok = false. Blocking file I/O: background threads only.
library::Analysis analyseSample(const std::string& utf8Path);

} // namespace klay::adapters
