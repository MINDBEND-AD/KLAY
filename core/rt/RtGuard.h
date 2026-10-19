// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source. See THIRD_PARTY_LICENSES.md (C-006).
//
// Real-time safety guard (NFR-001, FR-234, plan decision 1).
//
// RT_GUARD marks the enclosing scope as audio-thread code by raising a thread-local depth counter. In the shipped
// plugin nothing else happens. Test builds link tests/rt/RtHarness.cpp, which replaces the global operator
// new/delete and installs a violation handler, so any allocation, deallocation or klay::rt::Mutex lock inside a
// guarded scope is counted and fails the test. On Windows the harness also reports OS-level heap, lock, file I/O,
// logging and sleep calls (see tests/rt/RtHarness.cpp).
#pragma once

#include <atomic>

namespace klay::rt
{

enum class Violation
{
    allocation,
    deallocation,
    lock,
    io,      // file I/O (CreateFile, ReadFile, WriteFile)
    logging, // OutputDebugString
    sleep    // Sleep, SleepEx, SwitchToThread
};

using ViolationHandler = void (*)(Violation) noexcept;

namespace detail
{
    inline int& audioThreadDepth() noexcept
    {
        thread_local int depth = 0;
        return depth;
    }

    inline std::atomic<ViolationHandler>& violationHandler() noexcept
    {
        static std::atomic<ViolationHandler> handler{ nullptr };
        return handler;
    }
} // namespace detail

// True while the calling thread is inside an RT_GUARD scope.
inline bool isAudioThread() noexcept
{
    return detail::audioThreadDepth() > 0;
}

// Installs the handler that receives violations (test harness only). Pass nullptr to remove it.
inline void setViolationHandler(ViolationHandler handler) noexcept
{
    detail::violationHandler().store(handler, std::memory_order_release);
}

// Reports a violation if the calling thread is inside an RT_GUARD scope and a handler is installed.
inline void checkRealtime(Violation kind) noexcept
{
    if (! isAudioThread())
        return;
    if (auto* handler = detail::violationHandler().load(std::memory_order_acquire))
        handler(kind);
}

class ScopedAudioThread
{
public:
    ScopedAudioThread() noexcept { ++detail::audioThreadDepth(); }
    ~ScopedAudioThread() noexcept { --detail::audioThreadDepth(); }

    ScopedAudioThread(const ScopedAudioThread&) = delete;
    ScopedAudioThread& operator=(const ScopedAudioThread&) = delete;
};

} // namespace klay::rt

#define KLAY_RT_CONCAT_INNER(a, b) a##b
#define KLAY_RT_CONCAT(a, b) KLAY_RT_CONCAT_INNER(a, b)

// Marks the rest of the enclosing scope as real-time audio code (NFR-001).
#define RT_GUARD ::klay::rt::ScopedAudioThread KLAY_RT_CONCAT(klayRtGuard_, __LINE__)
