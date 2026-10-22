// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source. See THIRD_PARTY_LICENSES.md (C-006).
//
// Task 2.3: seeded RNG and roll engine (FR-141–143, FR-146, FR-147).
#include "core/model/KickJson.h"
#include "core/random/RolePools.h"
#include "core/random/Roll.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <set>

using namespace klay;
using model::Role;

namespace
{

const library::SampleIndex& testLibrary()
{
    static const auto index = [] {
        const std::pair<const char*, Role> files[] = { { "lib/Crunch/c1.wav", Role::crunch },
                                                       { "lib/Crunch/c2.wav", Role::crunch },
                                                       { "lib/Crunch/c3.wav", Role::crunch },
                                                       { "lib/Drive/d1.wav", Role::drive },
                                                       { "lib/Drive/d2.wav", Role::drive },
                                                       { "lib/Kick/k1.wav", Role::kick },
                                                       { "lib/Kick/k2.wav", Role::kick },
                                                       { "lib/Kick/k3.wav", Role::kick },
                                                       { "lib/Kick/k4.wav", Role::kick },
                                                       { "lib/Rumble/r1.wav", Role::rumble },
                                                       { "lib/Rumble/r2.wav", Role::rumble },
                                                       { "lib/Transient/t1.wav", Role::transient } };
        library::SampleIndex i;
        i.root = "lib";
        for (const auto& [path, role] : files)
        {
            library::SampleInfo s;
            s.path = path;
            s.role = role;
            s.meta.ok = true;
            i.samples.push_back(s);
        }
        return i;
    }();
    return index;
}

bool inPool(const random::RolePools& pools, Role role, const std::string& path)
{
    const auto& p = pools.pool(role);
    return std::find(p.begin(), p.end(), path) != p.end();
}

// Every layer field set away from its default, so "only the sample changed" is a strong check.
model::Kick detailedKick()
{
    auto kick = model::makeDefaultKick();
    for (size_t i = 0; i < kick.layers.size(); ++i)
    {
        auto& l = kick.layers[i];
        l.samplePath = "old/" + std::to_string(i) + ".wav";
        l.faderDb = -3.5f;
        l.offsetMs = 1.25 + static_cast<double>(i);
        l.startTrimMs = 2.0;
        l.endTrimMs = 3.0;
        l.mode = model::LayerMode::pitch;
        l.length = 1.5;
        l.pitchSemitones = -2;
        l.pitchCents = 17;
        l.tapeSpeed = 0.75;
        l.routing = model::Routing::aux1;
        l.pitchFollow = true;
        l.mute = i == 1;
        l.solo = i == 0;
        l.name = "custom " + std::to_string(i);
        l.envelope.points = { { 0.0, 1.0f, 0.2f }, { 50.0, 0.3f, -0.5f } };
    }
    return kick;
}

} // namespace

TEST_CASE("Seeded RNG: same seed, same sequence; bounded draws are in range and unbiased", "[core][random][FR-147]")
{
    random::Rng a(42), b(42), c(43);
    bool differs = false;
    for (int i = 0; i < 100; ++i)
    {
        const auto x = a.next();
        CHECK(x == b.next());
        differs = differs || x != c.next();
    }
    CHECK(differs);
    CHECK(random::layerSeed(1, 0) != random::layerSeed(1, 1));
    CHECK(random::layerSeed(1, 0) != random::layerSeed(2, 0));

    random::Rng r(7);
    int counts[6] = {};
    constexpr int draws = 60000;
    for (int i = 0; i < draws; ++i)
    {
        const auto v = r.below(6);
        REQUIRE(v < 6);
        ++counts[v];
    }
    double chi2 = 0.0;
    for (const int n : counts)
        chi2 += (n - draws / 6.0) * (n - draws / 6.0) / (draws / 6.0);
    CHECK(chi2 < 20.5); // 5 degrees of freedom, p = 0.001
    CHECK(r.below(1) == 0);
    CHECK(r.below(0) == 0);
}

TEST_CASE("Seeded RNG: the sequence of a seed is frozen", "[core][random][FR-147]")
{
    // Pinned values: the first two outputs of the reference xoshiro256** seeded with SplitMix64(0). If they change,
    // saved seeds roll differently (FR-147): never update them without a decision.
    random::Rng r(0);
    const std::uint64_t first = r.next();
    const std::uint64_t second = r.next();
    random::Rng again(0);
    CHECK(again.next() == first);
    CHECK(again.next() == second);
    CHECK(first == 0x99EC5F36CB75F2B4ull);
    CHECK(second == 0xBF6E1F784956452Aull);
}

TEST_CASE("Forge: given layer 2 locked and layer 3 set to Crunch, layer 2 is unchanged and layer 3 is from the Crunch pool",
          "[core][random][FR-141][FR-143][gherkin]")
{
    const random::RolePools pools(testLibrary());
    for (std::uint64_t seed = 1; seed <= 50; ++seed)
    {
        auto kick = model::makeDefaultKick();
        kick.layers[1].lock = true;
        kick.layers[1].samplePath = "lib/Rumble/r1.wav";
        kick.layers[2].role = Role::crunch;
        const auto before = kick.layers[1];

        const auto result = random::forge(kick, pools, seed);
        CHECK(kick.layers[1] == before);
        CHECK(inPool(pools, Role::crunch, kick.layers[2].samplePath));
        CHECK(inPool(pools, Role::kick, kick.layers[0].samplePath));
        CHECK(result.locked == std::vector<size_t>{ 1 });
        CHECK(result.seed == seed);
    }
}

TEST_CASE("Forge: the same seed gives the same roll; different seeds vary", "[core][random][FR-147]")
{
    const random::RolePools pools(testLibrary());
    auto a = model::makeDefaultKick();
    auto b = model::makeDefaultKick();
    random::forge(a, pools, 1234);
    random::forge(b, pools, 1234);
    CHECK(a == b);

    std::set<std::string> kicks;
    for (std::uint64_t seed = 0; seed < 40; ++seed)
    {
        auto k = model::makeDefaultKick();
        random::forge(k, pools, seed);
        kicks.insert(k.layers[0].samplePath);
    }
    CHECK(kicks.size() == 4); // every Kick sample is drawn by some seed
}

TEST_CASE("Forge replaces the sample and resets its trims; fader, envelope, offset, pitch, length, routing, mute/solo stay",
          "[core][random][FR-141][FR-143]")
{
    const random::RolePools pools(testLibrary());
    auto kick = detailedKick();
    const auto before = kick;
    const auto result = random::forge(kick, pools, 99);
    CHECK(result.changed == std::vector<size_t>{ 0, 1, 2 }); // template default: layers 1–3
    for (size_t i = 0; i < kick.layers.size(); ++i)
    {
        auto expected = before.layers[i];
        if (i < 3) // rolled: new sample, trims reset (PO decision 2026-10-22)
        {
            expected.samplePath = kick.layers[i].samplePath;
            expected.startTrimMs = 0.0;
            expected.endTrimMs = 0.0;
        }
        CHECK(kick.layers[i] == expected); // outside the template: untouched, trims included
    }
    CHECK(kick.settings == before.settings);
}

TEST_CASE("Forge follows the roll template and leaves layers with an empty pool as they are", "[core][random][FR-146][FR-141]")
{
    const random::RolePools pools(testLibrary());
    auto kick = detailedKick();
    kick.settings.rollTemplate = { 2, 3, 5, 9 }; // Drive, Crunch, Texture (empty pool), layer 10 (not in the stack)
    const auto before = kick;
    const auto result = random::forge(kick, pools, 5);
    CHECK(result.changed == std::vector<size_t>{ 2, 3 });
    CHECK(result.emptyPool == std::vector<size_t>{ 5 });
    CHECK(kick.layers[0] == before.layers[0]);
    CHECK(kick.layers[1] == before.layers[1]);
    CHECK(kick.layers[4] == before.layers[4]);
    CHECK(kick.layers[5] == before.layers[5]);
    CHECK(inPool(pools, Role::drive, kick.layers[2].samplePath));
    CHECK(inPool(pools, Role::crunch, kick.layers[3].samplePath));

    kick.settings.rollTemplate.clear();
    const auto unchanged = kick;
    CHECK(random::forge(kick, pools, 6).changed.empty());
    CHECK(kick == unchanged);
}

TEST_CASE("A roll never re-draws the current sample when the pool has more than one", "[core][random][FR-141]")
{
    const random::RolePools pools(testLibrary());
    auto kick = model::makeDefaultKick();
    kick.settings.rollTemplate = { 1 }; // Rumble: two samples
    std::string last;
    for (std::uint64_t seed = 0; seed < 50; ++seed)
    {
        random::forge(kick, pools, seed);
        CHECK(kick.layers[1].samplePath != last);
        last = kick.layers[1].samplePath;
    }

    // A pool of one keeps drawing that sample.
    kick.layers[4].samplePath = "lib/Transient/t1.wav";
    const auto result = random::reforge(kick, 4, pools, 3);
    CHECK(kick.layers[4].samplePath == "lib/Transient/t1.wav");
    CHECK(result.changed.empty());
}

TEST_CASE("Reforge changes one layer only, matches Forge's draw for it, and skips locked layers",
          "[core][random][FR-142][FR-143]")
{
    const random::RolePools pools(testLibrary());
    auto kick = detailedKick();
    const auto before = kick;
    const auto result = random::reforge(kick, 3, pools, 77);
    CHECK(result.changed == std::vector<size_t>{ 3 });
    CHECK(inPool(pools, Role::crunch, kick.layers[3].samplePath));
    for (size_t i = 0; i < kick.layers.size(); ++i)
        if (i != 3)
            CHECK(kick.layers[i] == before.layers[i]);

    // Same seed: Reforge of layer 1 draws what Forge draws for layer 1.
    auto viaForge = before;
    auto viaReforge = before;
    random::forge(viaForge, pools, 77);
    random::reforge(viaReforge, 0, pools, 77);
    CHECK(viaReforge.layers[0].samplePath == viaForge.layers[0].samplePath);

    // Locked: nothing changes.
    auto locked = before;
    locked.layers[3].lock = true;
    const auto lockedBefore = locked;
    CHECK(random::reforge(locked, 3, pools, 1).locked == std::vector<size_t>{ 3 });
    CHECK(locked == lockedBefore);

    // Out of range: no-op.
    CHECK(random::reforge(locked, 40, pools, 1).changed.empty());
    CHECK(locked == lockedBefore);
}

TEST_CASE("Roll template: default layers 1-3, saved with the kick, sanitised", "[core][random][FR-146][FR-003]")
{
    auto kick = model::makeDefaultKick();
    CHECK(kick.settings.rollTemplate == std::vector<int>{ 0, 1, 2 });
    CHECK(model::inRollTemplate(kick.settings, 2));
    CHECK_FALSE(model::inRollTemplate(kick.settings, 3));

    kick.settings.rollTemplate = { 5, 3, 3, -1, 12, 11 };
    model::sanitise(kick);
    CHECK(kick.settings.rollTemplate == std::vector<int>{ 3, 5, 11 });

    const auto loaded = model::kickFromJson(model::toJson(kick));
    REQUIRE(loaded);
    CHECK(loaded->settings.rollTemplate == std::vector<int>{ 3, 5, 11 });

    // Older files without the field get the default template (NFR-012).
    const auto old = model::kickFromJson(R"({"schemaVersion": 2, "settings": {}})");
    REQUIRE(old);
    CHECK(old->settings.rollTemplate == std::vector<int>{ 0, 1, 2 });
}

TEST_CASE("Reforge resets the trims, also when the pool's only sample is drawn again", "[core][random][FR-142]")
{
    const random::RolePools pools(testLibrary());
    auto kick = model::makeDefaultKick();
    kick.layers[4].samplePath = "lib/Transient/t1.wav"; // the only Transient sample
    kick.layers[4].startTrimMs = 4.0;
    kick.layers[4].endTrimMs = 6.0;
    const auto result = random::reforge(kick, 4, pools, 3);
    CHECK(result.changed == std::vector<size_t>{ 4 });
    CHECK(kick.layers[4].samplePath == "lib/Transient/t1.wav");
    CHECK(kick.layers[4].startTrimMs == 0.0);
    CHECK(kick.layers[4].endTrimMs == 0.0);
}
