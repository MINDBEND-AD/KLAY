// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source. See THIRD_PARTY_LICENSES.md (C-006).
#pragma once

// Timing targets (NFR-002, NFR-004, NFR-015, spec §12) are defined for the reference machine. They fail the test
// locally; on CI (environment variable KLAY_CI set to anything but "0") they only report: the measured value is
// printed and a warning added when it misses the target, because a shared runner's timing says little.

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <string_view>

namespace klay::test
{

inline bool timingTargetsEnforced()
{
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4996) // getenv is fine here: read once, never stored
#endif
    const char* ci = std::getenv("KLAY_CI");
#ifdef _MSC_VER
#pragma warning(pop)
#endif
    return ci == nullptr || *ci == '\0' || std::string_view(ci) == "0";
}

} // namespace klay::test

// CHECK(condition) locally; on CI a WARN with `message` when the condition fails.
#define KLAY_TIMING_CHECK(condition, message)                                          \
    do                                                                                 \
    {                                                                                  \
        if (::klay::test::timingTargetsEnforced())                                     \
        {                                                                              \
            CHECK(condition);                                                          \
        }                                                                              \
        else if (! (condition))                                                        \
        {                                                                              \
            WARN("over target on CI, reported only (timing targets are enforced on "   \
                 "the reference machine): " << message);                               \
        }                                                                              \
    } while (false)
