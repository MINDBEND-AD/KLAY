// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source. See THIRD_PARTY_LICENSES.md (C-006).
#pragma once

#include "core/model/Kick.h"
#include "core/random/RolePools.h"
#include "core/random/Rng.h"

#include <cstdint>
#include <string>
#include <vector>

namespace klay::random
{

struct RollResult
{
    std::uint64_t seed = 0;          // FR-147: the same seed, kick and pools give the same roll
    std::vector<size_t> changed;     // layers that got a new sample (or had their trims reset)
    std::vector<size_t> emptyPool;   // layers in the roll whose role pool is empty; left as they are
    std::vector<size_t> locked;      // layers in the roll skipped because they are locked (FR-143)
};

// One draw from `pool`. Plan decision 18: when the pool has more than one sample, the layer's current sample is not
// drawn again. Returns an empty string for an empty pool.
std::string drawSample(const std::vector<std::string>& pool, const std::string& current, Rng& rng);

// FR-141, FR-146: Forge draws a new sample for every unlocked layer in the roll template from its role's pool; all
// other layers stay as they are. Only the sample changes, and its start and end trim reset to 0 (PO decision
// 2026-10-22): fader, envelope, offset, mode values, routing, mute/solo, name and role stay (FR-141, FR-143).
// Each layer draws with layerSeed(seed, index).
RollResult forge(model::Kick& kick, const RolePools& pools, std::uint64_t seed);

// FR-142: Reforge one layer from its role's pool, as Forge does for that layer (same seed → same sample as Forge's
// draw for it). A locked layer is not changed (FR-143).
RollResult reforge(model::Kick& kick, size_t index, const RolePools& pools, std::uint64_t seed);

} // namespace klay::random
