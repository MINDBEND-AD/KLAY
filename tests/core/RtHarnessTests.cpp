// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source. See THIRD_PARTY_LICENSES.md (C-006).
//
// Self-tests of the RT harness (NFR-001, FR-234, plan decision 1): it must catch what it claims to catch, and
// nothing outside a guarded scope.
#include "core/rt/RtGuard.h"
#include "core/rt/RtMutex.h"
#include "tests/rt/RtHarness.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

using klay::rt::test::ScopedViolationRecorder;

namespace
{

// volatile keeps the optimiser from removing the new/delete pairs below.
volatile std::size_t allocationSize = 64;

void allocateSomething()
{
    std::vector<float> buffer(allocationSize);
    buffer[0] = 1.0f;
}

} // namespace

TEST_CASE("RT harness: an allocation inside RT_GUARD is caught", "[rt][NFR-001]")
{
    ScopedViolationRecorder recorder;
    {
        RT_GUARD;
        allocateSomething();
    }
    CHECK(recorder.counts().allocations >= 1);
    CHECK(recorder.counts().deallocations >= 1);
}

TEST_CASE("RT harness: aligned and array allocations inside RT_GUARD are caught", "[rt][NFR-001]")
{
    struct alignas(64) Aligned
    {
        float data[16];
    };

    ScopedViolationRecorder recorder;
    {
        RT_GUARD;
        auto aligned = std::make_unique<Aligned>();
        auto array = std::make_unique<int[]>(allocationSize);
        aligned->data[0] = static_cast<float>(array[0]);
    }
    CHECK(recorder.counts().allocations == 2);
    CHECK(recorder.counts().deallocations == 2);
}

TEST_CASE("RT harness: a klay::rt::Mutex lock inside RT_GUARD is caught", "[rt][NFR-001]")
{
    klay::rt::Mutex mutex;
    ScopedViolationRecorder recorder;
    {
        RT_GUARD;
        std::scoped_lock lock(mutex);
    }
    CHECK(recorder.counts().locks >= 1);
    CHECK(recorder.counts().allocations == 0);
}

TEST_CASE("RT harness: allocations and locks outside RT_GUARD are not reported", "[rt][NFR-001]")
{
    klay::rt::Mutex mutex;
    ScopedViolationRecorder recorder;
    allocateSomething();
    {
        std::scoped_lock lock(mutex);
    }
    CHECK(recorder.counts().total() == 0);
}

TEST_CASE("RT harness: the guard is per thread and nests", "[rt][NFR-001]")
{
    ScopedViolationRecorder recorder;
    {
        RT_GUARD;
        CHECK(klay::rt::isAudioThread());
        {
            RT_GUARD;
            CHECK(klay::rt::isAudioThread());
        }
        CHECK(klay::rt::isAudioThread());
    }
    CHECK_FALSE(klay::rt::isAudioThread());

    // A worker thread allocating while this thread is guarded does not count: the flag is thread-local.
    // (Joining inside the guard would itself be reported as a blocking wait, so this thread spins on an atomic.)
    std::atomic<bool> guardActive{ false };
    std::atomic<bool> workerDone{ false };
    bool workerSawAudioThread = true;
    std::thread worker([&] {
        while (! guardActive.load())
            std::this_thread::yield();
        workerSawAudioThread = klay::rt::isAudioThread();
        allocateSomething();
        workerDone = true;
    });
    {
        RT_GUARD;
        guardActive = true;
        while (! workerDone.load())
            ;
    }
    worker.join();
    CHECK_FALSE(workerSawAudioThread);
    CHECK(recorder.counts().total() == 0);
}

TEST_CASE("RT harness: without a recorder nothing is reported", "[rt][NFR-001]")
{
    {
        RT_GUARD;
        allocateSomething();
    }
    ScopedViolationRecorder recorder;
    CHECK(recorder.counts().total() == 0);
}

TEST_CASE("RT harness: OS-level hooks are installed", "[rt][NFR-001]")
{
    CHECK(klay::rt::test::osHooksInstalled());
}

TEST_CASE("RT harness: std::malloc inside RT_GUARD is caught", "[rt][NFR-001]")
{
    ScopedViolationRecorder recorder;
    {
        RT_GUARD;
        void* volatile p = std::malloc(allocationSize);
        std::free(p);
    }
    CHECK(recorder.counts().allocations >= 1);
    CHECK(recorder.counts().deallocations >= 1);
}

TEST_CASE("RT harness: a std::mutex lock inside RT_GUARD is caught", "[rt][NFR-001]")
{
    std::mutex mutex;
    ScopedViolationRecorder recorder;
    {
        RT_GUARD;
        std::scoped_lock lock(mutex);
    }
    CHECK(recorder.counts().locks >= 1);
}

TEST_CASE("RT harness: lock-free atomics inside RT_GUARD are not reported", "[rt][NFR-001]")
{
    std::atomic<int> value{ 0 };
    ScopedViolationRecorder recorder;
    {
        RT_GUARD;
        for (int i = 0; i < 1000; ++i)
            value.fetch_add(1, std::memory_order_relaxed);
    }
    CHECK(value.load() == 1000);
    CHECK(recorder.counts().total() == 0);
}
