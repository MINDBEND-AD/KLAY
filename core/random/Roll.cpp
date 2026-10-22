// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source. See THIRD_PARTY_LICENSES.md (C-006).
#include "core/random/Roll.h"

#include <algorithm>

namespace klay::random
{

std::string drawSample(const std::vector<std::string>& pool, const std::string& current, Rng& rng)
{
    if (pool.empty())
        return {};
    const auto it = std::find(pool.begin(), pool.end(), current);
    if (it == pool.end() || pool.size() == 1)
        return pool[static_cast<size_t>(rng.below(pool.size()))];
    // Draw from the others: pick among n − 1 and step over the current one.
    const auto skip = static_cast<size_t>(it - pool.begin());
    auto k = static_cast<size_t>(rng.below(pool.size() - 1));
    if (k >= skip)
        ++k;
    return pool[k];
}

namespace
{
    void rollLayer(model::Kick& kick, size_t index, const RolePools& pools, std::uint64_t seed, RollResult& result)
    {
        auto& layer = kick.layers[index];
        if (layer.lock)
        {
            result.locked.push_back(index);
            return;
        }
        const auto& pool = pools.pool(layer.role);
        if (pool.empty())
        {
            result.emptyPool.push_back(index);
            return;
        }
        Rng rng(layerSeed(seed, index));
        auto drawn = drawSample(pool, layer.samplePath, rng);
        if (drawn != layer.samplePath)
            result.changed.push_back(index);
        layer.samplePath = std::move(drawn);
    }
} // namespace

RollResult forge(model::Kick& kick, const RolePools& pools, std::uint64_t seed)
{
    RollResult result;
    result.seed = seed;
    for (size_t i = 0; i < kick.layers.size(); ++i)
        if (model::inRollTemplate(kick.settings, i))
            rollLayer(kick, i, pools, seed, result);
    return result;
}

RollResult reforge(model::Kick& kick, size_t index, const RolePools& pools, std::uint64_t seed)
{
    RollResult result;
    result.seed = seed;
    if (index < kick.layers.size())
        rollLayer(kick, index, pools, seed, result);
    return result;
}

} // namespace klay::random
