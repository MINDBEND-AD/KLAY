// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source. See THIRD_PARTY_LICENSES.md (C-006).
#include "core/model/UndoHistory.h"

namespace klay::model
{

void UndoHistory::record(const Kick& before, const Kick& after, std::string label)
{
    if (before == after)
        return;
    undoSteps.push_back({ before, after, std::move(label) });
    while (undoSteps.size() > capacity)
        undoSteps.pop_front();
    redoSteps.clear();
}

std::optional<Kick> UndoHistory::undo()
{
    if (undoSteps.empty())
        return std::nullopt;
    redoSteps.push_back(std::move(undoSteps.back()));
    undoSteps.pop_back();
    return redoSteps.back().before;
}

std::optional<Kick> UndoHistory::redo()
{
    if (redoSteps.empty())
        return std::nullopt;
    undoSteps.push_back(std::move(redoSteps.back()));
    redoSteps.pop_back();
    return undoSteps.back().after;
}

void UndoHistory::clear()
{
    undoSteps.clear();
    redoSteps.clear();
}

} // namespace klay::model
