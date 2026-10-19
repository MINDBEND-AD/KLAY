// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source. See THIRD_PARTY_LICENSES.md (C-006).
//
// Test side of the RT harness (NFR-001, plan decision 1). Linking RtHarness.cpp replaces the global
// operator new/delete; violations inside RT_GUARD scopes are counted while a ScopedViolationRecorder exists.
#pragma once

#include "core/rt/RtGuard.h"

#include <cstdint>

namespace klay::rt::test
{

struct Counts
{
    std::uint64_t allocations = 0;
    std::uint64_t deallocations = 0;
    std::uint64_t locks = 0;

    std::uint64_t total() const noexcept { return allocations + deallocations + locks; }
};

// Records violations from construction on, starting at zero; stops recording on destruction.
class ScopedViolationRecorder
{
public:
    ScopedViolationRecorder() noexcept;
    ~ScopedViolationRecorder() noexcept;

    Counts counts() const noexcept;

    ScopedViolationRecorder(const ScopedViolationRecorder&) = delete;
    ScopedViolationRecorder& operator=(const ScopedViolationRecorder&) = delete;
};

// True when the OS-level heap and lock hooks are active (Windows test executables).
bool osHooksInstalled() noexcept;

} // namespace klay::rt::test
