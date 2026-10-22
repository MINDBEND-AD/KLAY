// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source. See THIRD_PARTY_LICENSES.md (C-006).
#pragma once

#include <cstdint>

namespace klay::random
{

// FR-147 seeded RNG: xoshiro256** seeded through SplitMix64. Fully specified integer arithmetic, so a seed gives the
// same sequence on every compiler and platform (std::uniform_int_distribution is implementation-defined and is
// therefore not used).
class Rng
{
public:
    explicit Rng(std::uint64_t seed) noexcept
    {
        for (auto& word : s)
            word = splitMix(seed);
    }

    std::uint64_t next() noexcept
    {
        const std::uint64_t result = rotl(s[1] * 5, 7) * 9;
        const std::uint64_t t = s[1] << 17;
        s[2] ^= s[0];
        s[3] ^= s[1];
        s[1] ^= s[2];
        s[0] ^= s[3];
        s[2] ^= t;
        s[3] = rotl(s[3], 45);
        return result;
    }

    // Uniform in [0, n) without modulo bias: values below 2^64 mod n are rejected, so the accepted range is a
    // multiple of n. n = 0 returns 0.
    std::uint64_t below(std::uint64_t n) noexcept
    {
        if (n == 0)
            return 0;
        const std::uint64_t threshold = (0 - n) % n; // 2^64 mod n
        std::uint64_t x = next();
        while (x < threshold)
            x = next();
        return x % n;
    }

    // SplitMix64 step: also used to derive independent per-layer seeds from one roll seed.
    static std::uint64_t splitMix(std::uint64_t& state) noexcept
    {
        std::uint64_t z = (state += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }

private:
    static std::uint64_t rotl(std::uint64_t x, int k) noexcept { return (x << k) | (x >> (64 - k)); }

    std::uint64_t s[4];
};

// Seed of one layer's draw within a roll: a Forge with seed s and a Reforge of layer i with seed s draw the same
// sample for layer i, independent of the other layers.
inline std::uint64_t layerSeed(std::uint64_t rollSeed, std::uint64_t layer) noexcept
{
    std::uint64_t state = rollSeed ^ (0xD1B54A32D192ED03ull * (layer + 1));
    return Rng::splitMix(state);
}

} // namespace klay::random
