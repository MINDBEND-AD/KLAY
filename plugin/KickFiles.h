// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source, uses JUCE (AGPLv3). See THIRD_PARTY_LICENSES.md (C-006).
#pragma once

#include "core/model/KickJson.h"

#include <juce_core/juce_core.h>

#include <optional>

namespace klay
{

// Loads a kick JSON file (dev editor, task 1.15). Relative sample paths are resolved against the file's folder and
// stored absolute, so the kick plays wherever the plugin's working directory is.
inline std::optional<model::Kick> loadKickFile(const juce::File& file)
{
    auto kick = model::kickFromJson(file.loadFileAsString().toStdString());
    if (! kick)
        return std::nullopt;
    for (auto& layer : kick->layers)
        if (! layer.samplePath.empty())
        {
            const auto path = juce::String::fromUTF8(layer.samplePath.c_str());
            if (! juce::File::isAbsolutePath(path))
                layer.samplePath = file.getParentDirectory().getChildFile(path).getFullPathName().toStdString();
        }
    return kick;
}

inline bool saveKickFile(const juce::File& file, const model::Kick& kick)
{
    return file.replaceWithText(juce::String::fromUTF8(model::toJson(kick).c_str()));
}

} // namespace klay
