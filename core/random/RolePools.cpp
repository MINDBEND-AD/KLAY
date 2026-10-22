// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source. See THIRD_PARTY_LICENSES.md (C-006).
#include "core/random/RolePools.h"

namespace klay::random
{

static_assert(static_cast<size_t>(model::Role::any) == 6, "RolePools indexes pools by model::Role");

RolePools::RolePools(const library::SampleIndex& index)
{
    for (const auto& sample : index.samples)
    {
        if (sample.role != model::Role::any)
            pools[static_cast<size_t>(sample.role)].push_back(sample.path);
        pools[static_cast<size_t>(model::Role::any)].push_back(sample.path);
    }
}

} // namespace klay::random
