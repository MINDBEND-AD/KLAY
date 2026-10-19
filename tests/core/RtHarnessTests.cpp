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
#include <string_view>
#include <thread>
#include <vector>

#ifdef _WIN32
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#endif

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

#ifdef _WIN32

// One self-test per OS hook kind beyond the heap and lock ones above (NFR-001, FR-234). Each calls the Win32 API
// directly, so the test executable imports it and the import-table hook is exercised.

TEST_CASE("RT harness: VirtualAlloc, LocalAlloc and GlobalAlloc inside RT_GUARD are caught", "[rt][NFR-001]")
{
    ScopedViolationRecorder recorder;
    {
        RT_GUARD;
        void* virtualMem = ::VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        ::VirtualFree(virtualMem, 0, MEM_RELEASE);
        HLOCAL localMem = ::LocalAlloc(LMEM_FIXED, allocationSize);
        ::LocalFree(localMem);
        HGLOBAL globalMem = ::GlobalAlloc(GMEM_FIXED, allocationSize);
        ::GlobalFree(globalMem);
    }
    CHECK(recorder.counts().allocations == 3);
    CHECK(recorder.counts().deallocations == 3);
}

TEST_CASE("RT harness: file I/O inside RT_GUARD is caught", "[rt][NFR-001]")
{
    wchar_t dirW[MAX_PATH] = {};
    REQUIRE(::GetTempPathW(MAX_PATH, dirW) > 0);
    wchar_t pathW[MAX_PATH] = {};
    REQUIRE(::GetTempFileNameW(dirW, L"klr", 0, pathW) != 0);
    char pathA[MAX_PATH] = {};
    REQUIRE(::WideCharToMultiByte(CP_ACP, 0, pathW, -1, pathA, MAX_PATH, nullptr, nullptr) > 0);

    const char data[] = "klay";
    char readBack[sizeof(data)] = {};
    DWORD done = 0;

    ScopedViolationRecorder recorder;
    {
        RT_GUARD;
        HANDLE out = ::CreateFileW(pathW, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        ::WriteFile(out, data, sizeof(data), &done, nullptr);
        ::CloseHandle(out);
        HANDLE in = ::CreateFileA(pathA, GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        ::ReadFile(in, readBack, sizeof(readBack), &done, nullptr);
        ::CloseHandle(in);
    }
    const auto counts = recorder.counts();
    ::DeleteFileW(pathW);

    CHECK(counts.io == 4);
    CHECK(std::string_view(readBack) == "klay");
}

TEST_CASE("RT harness: OutputDebugString inside RT_GUARD is caught", "[rt][NFR-001]")
{
    ScopedViolationRecorder recorder;
    {
        RT_GUARD;
        ::OutputDebugStringA("KLAY RT harness self-test\n");
        ::OutputDebugStringW(L"KLAY RT harness self-test\n");
    }
    CHECK(recorder.counts().logging == 2);
}

TEST_CASE("RT harness: Sleep, SleepEx and SwitchToThread inside RT_GUARD are caught", "[rt][NFR-001]")
{
    ScopedViolationRecorder recorder;
    {
        RT_GUARD;
        ::Sleep(0);
        ::SleepEx(0, FALSE);
        ::SwitchToThread();
    }
    CHECK(recorder.counts().sleeps == 3);
}

TEST_CASE("RT harness: a blocking WaitOnAddress inside RT_GUARD is caught, a zero-timeout poll is not",
          "[rt][NFR-001]")
{
    volatile long value = 0;
    long differs = 1; // the value differs, so the wait returns at once
    ScopedViolationRecorder recorder;
    {
        RT_GUARD;
        ::WaitOnAddress(&value, &differs, sizeof(value), 0);
    }
    CHECK(recorder.counts().locks == 0);
    {
        RT_GUARD;
        ::WaitOnAddress(&value, &differs, sizeof(value), 10);
    }
    CHECK(recorder.counts().locks == 1);
}

#endif // _WIN32
