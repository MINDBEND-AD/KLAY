// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source. See THIRD_PARTY_LICENSES.md (C-006).
#pragma once

#include "core/model/Kick.h"

#include <deque>
#include <optional>
#include <string>

namespace klay::model
{

// FR-145 undo/redo of all edits. Each step stores the whole kick before and after the edit, so every model and
// settings change (layers, envelopes, routing, rolls, settings; FX chains once they join the kick in M4) is covered
// by construction, and an undo restores the exact earlier state. A kick is a few kilobytes, so 100 steps cost well
// under a megabyte. Gestures (FR-239) are grouped by the caller: it records one step per completed gesture.
class UndoHistory
{
public:
    static constexpr size_t kDefaultCapacity = 100; // FR-145: at least 50 steps

    explicit UndoHistory(size_t capacity = kDefaultCapacity) : capacity(capacity < 1 ? 1 : capacity) {}

    // Records one completed edit. An edit that changed nothing is not recorded. Clears the redo steps; drops the
    // oldest step beyond the capacity.
    void record(const Kick& before, const Kick& after, std::string label);

    // The state to restore, or nullopt if there is nothing to undo / redo.
    std::optional<Kick> undo();
    std::optional<Kick> redo();

    bool canUndo() const noexcept { return ! undoSteps.empty(); }
    bool canRedo() const noexcept { return ! redoSteps.empty(); }
    size_t undoCount() const noexcept { return undoSteps.size(); }
    size_t redoCount() const noexcept { return redoSteps.size(); }
    // Label of the step the next undo / redo would apply ("" if none).
    std::string undoLabel() const { return canUndo() ? undoSteps.back().label : std::string(); }
    std::string redoLabel() const { return canRedo() ? redoSteps.back().label : std::string(); }
    void clear();

private:
    struct Step
    {
        Kick before;
        Kick after;
        std::string label;
    };

    size_t capacity;
    std::deque<Step> undoSteps; // oldest first
    std::deque<Step> redoSteps; // next redo last
};

} // namespace klay::model
