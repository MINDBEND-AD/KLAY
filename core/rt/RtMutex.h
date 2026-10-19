// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source. See THIRD_PARTY_LICENSES.md (C-006).
//
// Lock-detecting mutex (NFR-001, plan decision 1). Project code that needs a mutex uses klay::rt::Mutex instead of
// std::mutex, so a lock taken inside an RT_GUARD scope is reported by the test harness. The audio thread itself
// must never lock; UI <-> audio communication goes through lock-free FIFOs or atomics (NFR-013).
#pragma once

#include "core/rt/RtGuard.h"

#include <mutex>

namespace klay::rt
{

class Mutex
{
public:
    void lock()
    {
        checkRealtime(Violation::lock);
        mutex.lock();
    }

    bool try_lock()
    {
        checkRealtime(Violation::lock);
        return mutex.try_lock();
    }

    void unlock() { mutex.unlock(); }

private:
    std::mutex mutex;
};

} // namespace klay::rt
