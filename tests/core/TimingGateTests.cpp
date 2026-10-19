// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source. See THIRD_PARTY_LICENSES.md (C-006).
#include "tests/TimingGate.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <optional>
#include <string>

namespace
{

void setCiVariable(const char* value)
{
#ifdef _WIN32
    _putenv_s("KLAY_CI", value == nullptr ? "" : value);
#else
    if (value == nullptr)
        unsetenv("KLAY_CI");
    else
        setenv("KLAY_CI", value, 1);
#endif
}

// Restores KLAY_CI when the test ends, so the rest of the run keeps the caller's setting.
struct ScopedCiVariable
{
    std::optional<std::string> saved;
    explicit ScopedCiVariable(const char* value)
    {
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
        if (const char* old = std::getenv("KLAY_CI"))
            saved = old;
#ifdef _MSC_VER
#pragma warning(pop)
#endif
        setCiVariable(value);
    }
    ~ScopedCiVariable() { setCiVariable(saved ? saved->c_str() : nullptr); }
};

} // namespace

// Spec §12, NFR-015: timing targets fail locally and are only reported on CI.
TEST_CASE("Timing targets are enforced locally and reported on CI", "[core][timing]")
{
    {
        ScopedCiVariable ci(nullptr);
        CHECK(klay::test::timingTargetsEnforced());
    }
    {
        ScopedCiVariable ci("0");
        CHECK(klay::test::timingTargetsEnforced());
    }
    {
        ScopedCiVariable ci("1");
        CHECK_FALSE(klay::test::timingTargetsEnforced());
        // A missed target on CI warns and does not fail this test.
        const double measuredMs = 108.0;
        KLAY_TIMING_CHECK(measuredMs < 100.0, "self-test: " << measuredMs << " ms, target < 100 ms");
    }
}
