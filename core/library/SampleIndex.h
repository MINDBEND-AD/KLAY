// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source. See THIRD_PARTY_LICENSES.md (C-006).
#pragma once

#include "core/model/Kick.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace klay::library
{

// What the analyser finds in one audio file (FR-165 metadata). Decoding needs JUCE, so the analyser is injected
// (adapters/SampleAnalyzer, C-007).
struct Analysis
{
    bool ok = false;
    std::string error;
    double lengthSeconds = 0.0;
    float peak = 0.0f;         // linear sample peak over all channels
    bool rootDetected = false; // FR-120 detector on the whole file
    int rootNote = -1;         // nearest MIDI note, C4 = 60
    double rootHz = 0.0;
    bool rootUncertain = false;
};

using Analyzer = std::function<Analysis(const std::string& utf8Path)>;

// One indexed sample (FR-160, FR-165): role from the top-level folder, sub-folder below it, file stamp for the
// disk-cache invalidation, metadata from the analyser.
struct SampleInfo
{
    std::string path;   // absolute, UTF-8
    model::Role role = model::Role::any;
    std::string folder; // sub-folder below the role folder, '/'-separated; empty for files directly in it
    std::int64_t mtime = 0; // last write time, file-clock ticks
    std::uint64_t size = 0; // bytes
    Analysis meta;

    bool operator==(const SampleInfo& o) const
    {
        return path == o.path && role == o.role && folder == o.folder && mtime == o.mtime && size == o.size
               && meta.ok == o.meta.ok && meta.lengthSeconds == o.meta.lengthSeconds && meta.peak == o.meta.peak
               && meta.rootNote == o.meta.rootNote && meta.rootHz == o.meta.rootHz;
    }
};

struct SampleIndex
{
    std::string root; // library folder, UTF-8
    std::vector<SampleInfo> samples; // sorted by path (deterministic pools, FR-147)
};

struct BuildStats
{
    int analysed = 0; // new or changed files run through the analyser
    int reused = 0;   // taken from the previous index (same mtime and size)
    int removed = 0;  // in the previous index, gone from disk
    int failed = 0;   // the analyser could not read them; kept out of the index
    bool cancelled = false;
};

// FR-160 layout: <root>/<Role>/<sub-folder…>/<file>. Role folders match the role names case-insensitively (Kick,
// Rumble, Drive, Crunch, Transient, Texture); other top-level folders are ignored. Files directly in a role folder
// count too (sub-folder empty). FR-161 formats: .wav .aif .aiff .flac.
struct FoundFile
{
    std::filesystem::path path;
    model::Role role = model::Role::any;
    std::string folder;
    std::int64_t mtime = 0;
    std::uint64_t size = 0;
};
std::vector<FoundFile> scanLibrary(const std::filesystem::path& root);

// Role of a top-level folder name, or nullopt if it is not a role folder.
std::optional<model::Role> roleFromFolderName(std::string_view name);

// FR-165: builds the index of `root`. Files whose path, mtime and size match an entry of `previous` (the disk cache)
// reuse its metadata; all others are analysed, on `threads` worker threads. `cancel` (optional) stops early: the
// result then holds what was done so far and stats.cancelled is set.
SampleIndex buildIndex(const std::filesystem::path& root, const Analyzer& analyzer, const SampleIndex* previous,
                       BuildStats* stats = nullptr, unsigned threads = 0, const std::atomic<bool>* cancel = nullptr);

// Disk cache (FR-165, NFR-008): versioned JSON. load returns nullopt for a missing, malformed or other-version file.
std::string indexToJson(const SampleIndex& index);
std::optional<SampleIndex> indexFromJson(std::string_view json);
bool saveIndex(const std::filesystem::path& file, const SampleIndex& index);
std::optional<SampleIndex> loadIndex(const std::filesystem::path& file);

// UTF-8 <-> path helpers (std::filesystem uses wide strings on Windows).
std::string toUtf8(const std::filesystem::path& path);
std::filesystem::path fromUtf8(const std::string& utf8);

} // namespace klay::library
