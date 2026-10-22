// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source. See THIRD_PARTY_LICENSES.md (C-006).
//
// core/random (spec §10): role pools (task 2.2).
#include "core/random/RolePools.h"

#include <catch2/catch_test_macros.hpp>

using namespace klay;
using model::Role;

namespace
{

library::SampleIndex makeIndex(std::initializer_list<std::pair<const char*, Role>> files)
{
    library::SampleIndex index;
    index.root = "lib";
    for (const auto& [path, role] : files)
    {
        library::SampleInfo s;
        s.path = path;
        s.role = role;
        s.meta.ok = true;
        index.samples.push_back(s);
    }
    return index;
}

} // namespace

TEST_CASE("Role pools hold each role's samples; Any holds the whole library", "[core][random][FR-140]")
{
    const auto index = makeIndex({ { "lib/Crunch/c1.wav", Role::crunch },
                                   { "lib/Crunch/c2.wav", Role::crunch },
                                   { "lib/Kick/k1.wav", Role::kick },
                                   { "lib/Kick/k2.wav", Role::kick },
                                   { "lib/Kick/k3.wav", Role::kick },
                                   { "lib/Texture/t1.wav", Role::texture } });
    const random::RolePools pools(index);
    CHECK(pools.pool(Role::kick) == std::vector<std::string>{ "lib/Kick/k1.wav", "lib/Kick/k2.wav", "lib/Kick/k3.wav" });
    CHECK(pools.pool(Role::crunch) == std::vector<std::string>{ "lib/Crunch/c1.wav", "lib/Crunch/c2.wav" });
    CHECK(pools.pool(Role::texture).size() == 1);
    CHECK(pools.pool(Role::rumble).empty());
    CHECK(pools.pool(Role::drive).empty());
    CHECK(pools.pool(Role::transient).empty());
    CHECK(pools.pool(Role::any).size() == 6);
    CHECK(pools.total() == 6);

    // Rolls draw only from the layer's own role (FR-140): no pool contains another role's sample.
    for (const auto role : { Role::kick, Role::rumble, Role::drive, Role::crunch, Role::transient, Role::texture })
        for (const auto& path : pools.pool(role))
            for (const auto& s : index.samples)
                if (s.path == path)
                    CHECK(s.role == role);

    CHECK(random::RolePools().total() == 0);
}

TEST_CASE("Layer roles default from the layer name; layers 7-12 are Any", "[core][random][FR-140][FR-114]")
{
    const auto kick = model::makeDefaultKick();
    const Role roles[] = { Role::kick, Role::rumble, Role::drive, Role::crunch, Role::transient, Role::texture };
    for (size_t i = 0; i < 6; ++i)
        CHECK(kick.layers[i].role == roles[i]);
    for (int i = 6; i < model::kMaxLayerCount; ++i)
        CHECK(model::makeDefaultLayer(i).role == Role::any);
}
