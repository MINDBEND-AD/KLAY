// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source. See THIRD_PARTY_LICENSES.md (C-006).
#pragma once

#include "core/library/SampleIndex.h"
#include "core/model/Kick.h"

#include <array>
#include <string>
#include <vector>

namespace klay::random
{

// FR-140: the samples a roll may draw for each role. A role's pool is every indexed sample of that role; the Any
// pool is the whole library. Pools keep the index's path order, so the same index and seed give the same roll
// (FR-147).
class RolePools
{
public:
    RolePools() = default;
    explicit RolePools(const library::SampleIndex& index);

    const std::vector<std::string>& pool(model::Role role) const noexcept { return pools[static_cast<size_t>(role)]; }
    size_t total() const noexcept { return pool(model::Role::any).size(); }

private:
    std::array<std::vector<std::string>, 7> pools; // indexed by model::Role, Any last
};

} // namespace klay::random
