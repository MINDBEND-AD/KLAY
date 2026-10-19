// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source. See THIRD_PARTY_LICENSES.md (C-006).
//
// RT harness for test executables (NFR-001, FR-234, plan decision 1). Only test targets compile this file; the
// shipped plugin uses the default allocator and OS calls.
//
// Two layers report violations on a thread inside an RT_GUARD scope through klay::rt::checkRealtime:
//  1. Global operator new/delete replacements (the decision-1 hook).
//  2. Import-table hooks on the test executable for the Win32 heap (HeapAlloc/HeapReAlloc/HeapFree) and the
//     blocking wait/lock calls (critical sections, SRW locks, condition variables, WaitFor*Object*). The static
//     CRT's malloc/free and the STL's std::mutex sit on top of these, so allocations through std::malloc (JUCE's
//     HeapBlock, AudioBuffer) and locks through juce::CriticalSection or std::mutex are caught as well. The other
//     Win32 allocators (VirtualAlloc, LocalAlloc, GlobalAlloc and their frees), WaitOnAddress, file I/O
//     (CreateFileA/W, ReadFile, WriteFile), logging (OutputDebugStringA/W) and sleeping or yielding (Sleep, SleepEx,
//     SwitchToThread) are hooked the same way.
//
// Blind spots (plan, M0 note 0.5): calls resolved through GetProcAddress or delay-load, calls made from inside other
// DLLs (only this executable's import table is patched), and OS calls not listed in importHooks below.
#include "tests/rt/RtHarness.h"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <malloc.h>
#include <new>

#ifdef _WIN32
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#endif

namespace
{

std::atomic<std::uint64_t> allocationCount{ 0 };
std::atomic<std::uint64_t> deallocationCount{ 0 };
std::atomic<std::uint64_t> lockCount{ 0 };
std::atomic<std::uint64_t> ioCount{ 0 };
std::atomic<std::uint64_t> loggingCount{ 0 };
std::atomic<std::uint64_t> sleepCount{ 0 };

void countViolation(klay::rt::Violation kind) noexcept
{
    switch (kind)
    {
        case klay::rt::Violation::allocation:   allocationCount.fetch_add(1, std::memory_order_relaxed); break;
        case klay::rt::Violation::deallocation: deallocationCount.fetch_add(1, std::memory_order_relaxed); break;
        case klay::rt::Violation::lock:         lockCount.fetch_add(1, std::memory_order_relaxed); break;
        case klay::rt::Violation::io:           ioCount.fetch_add(1, std::memory_order_relaxed); break;
        case klay::rt::Violation::logging:      loggingCount.fetch_add(1, std::memory_order_relaxed); break;
        case klay::rt::Violation::sleep:        sleepCount.fetch_add(1, std::memory_order_relaxed); break;
    }
}

// Set while operator new/delete forwards to the CRT, so the heap hooks below don't count the same call twice.
thread_local bool insideOperatorNew = false;

struct ForwardingToCrt
{
    ForwardingToCrt() noexcept { insideOperatorNew = true; }
    ~ForwardingToCrt() noexcept { insideOperatorNew = false; }
};

void* allocate(std::size_t size) noexcept
{
    klay::rt::checkRealtime(klay::rt::Violation::allocation);
    ForwardingToCrt forwarding;
    return std::malloc(size == 0 ? 1 : size);
}

void* allocateAligned(std::size_t size, std::align_val_t alignment) noexcept
{
    klay::rt::checkRealtime(klay::rt::Violation::allocation);
    ForwardingToCrt forwarding;
    return _aligned_malloc(size == 0 ? 1 : size, static_cast<std::size_t>(alignment));
}

void release(void* p) noexcept
{
    if (p == nullptr)
        return;
    klay::rt::checkRealtime(klay::rt::Violation::deallocation);
    ForwardingToCrt forwarding;
    std::free(p);
}

void releaseAligned(void* p) noexcept
{
    if (p == nullptr)
        return;
    klay::rt::checkRealtime(klay::rt::Violation::deallocation);
    ForwardingToCrt forwarding;
    _aligned_free(p);
}

void* allocateOrThrow(std::size_t size)
{
    if (auto* p = allocate(size))
        return p;
    throw std::bad_alloc();
}

void* allocateAlignedOrThrow(std::size_t size, std::align_val_t alignment)
{
    if (auto* p = allocateAligned(size, alignment))
        return p;
    throw std::bad_alloc();
}

#ifdef _WIN32

void checkHeap(klay::rt::Violation kind) noexcept
{
    if (! insideOperatorNew)
        klay::rt::checkRealtime(kind);
}

// Originals, filled in when the import table is patched.
decltype(&::HeapAlloc) realHeapAlloc = nullptr;
decltype(&::HeapReAlloc) realHeapReAlloc = nullptr;
decltype(&::HeapFree) realHeapFree = nullptr;
decltype(&::EnterCriticalSection) realEnterCriticalSection = nullptr;
decltype(&::AcquireSRWLockExclusive) realAcquireSRWLockExclusive = nullptr;
decltype(&::AcquireSRWLockShared) realAcquireSRWLockShared = nullptr;
decltype(&::SleepConditionVariableCS) realSleepConditionVariableCS = nullptr;
decltype(&::SleepConditionVariableSRW) realSleepConditionVariableSRW = nullptr;
decltype(&::WaitForSingleObject) realWaitForSingleObject = nullptr;
decltype(&::WaitForSingleObjectEx) realWaitForSingleObjectEx = nullptr;
decltype(&::WaitForMultipleObjects) realWaitForMultipleObjects = nullptr;
decltype(&::WaitForMultipleObjectsEx) realWaitForMultipleObjectsEx = nullptr;
decltype(&::WaitOnAddress) realWaitOnAddress = nullptr;
decltype(&::VirtualAlloc) realVirtualAlloc = nullptr;
decltype(&::VirtualFree) realVirtualFree = nullptr;
decltype(&::LocalAlloc) realLocalAlloc = nullptr;
decltype(&::LocalReAlloc) realLocalReAlloc = nullptr;
decltype(&::LocalFree) realLocalFree = nullptr;
decltype(&::GlobalAlloc) realGlobalAlloc = nullptr;
decltype(&::GlobalReAlloc) realGlobalReAlloc = nullptr;
decltype(&::GlobalFree) realGlobalFree = nullptr;
decltype(&::CreateFileA) realCreateFileA = nullptr;
decltype(&::CreateFileW) realCreateFileW = nullptr;
decltype(&::ReadFile) realReadFile = nullptr;
decltype(&::WriteFile) realWriteFile = nullptr;
decltype(&::OutputDebugStringA) realOutputDebugStringA = nullptr;
decltype(&::OutputDebugStringW) realOutputDebugStringW = nullptr;
decltype(&::Sleep) realSleep = nullptr;
decltype(&::SleepEx) realSleepEx = nullptr;
decltype(&::SwitchToThread) realSwitchToThread = nullptr;

LPVOID WINAPI hookHeapAlloc(HANDLE heap, DWORD flags, SIZE_T bytes)
{
    checkHeap(klay::rt::Violation::allocation);
    return realHeapAlloc(heap, flags, bytes);
}

LPVOID WINAPI hookHeapReAlloc(HANDLE heap, DWORD flags, LPVOID mem, SIZE_T bytes)
{
    checkHeap(klay::rt::Violation::allocation);
    return realHeapReAlloc(heap, flags, mem, bytes);
}

BOOL WINAPI hookHeapFree(HANDLE heap, DWORD flags, LPVOID mem)
{
    if (mem != nullptr)
        checkHeap(klay::rt::Violation::deallocation);
    return realHeapFree(heap, flags, mem);
}

void WINAPI hookEnterCriticalSection(LPCRITICAL_SECTION section)
{
    klay::rt::checkRealtime(klay::rt::Violation::lock);
    realEnterCriticalSection(section);
}

void WINAPI hookAcquireSRWLockExclusive(PSRWLOCK lock)
{
    klay::rt::checkRealtime(klay::rt::Violation::lock);
    realAcquireSRWLockExclusive(lock);
}

void WINAPI hookAcquireSRWLockShared(PSRWLOCK lock)
{
    klay::rt::checkRealtime(klay::rt::Violation::lock);
    realAcquireSRWLockShared(lock);
}

BOOL WINAPI hookSleepConditionVariableCS(PCONDITION_VARIABLE cv, PCRITICAL_SECTION section, DWORD ms)
{
    klay::rt::checkRealtime(klay::rt::Violation::lock);
    return realSleepConditionVariableCS(cv, section, ms);
}

BOOL WINAPI hookSleepConditionVariableSRW(PCONDITION_VARIABLE cv, PSRWLOCK lock, DWORD ms, ULONG flags)
{
    klay::rt::checkRealtime(klay::rt::Violation::lock);
    return realSleepConditionVariableSRW(cv, lock, ms, flags);
}

// A zero timeout is a non-blocking poll and is allowed.
DWORD WINAPI hookWaitForSingleObject(HANDLE handle, DWORD ms)
{
    if (ms != 0)
        klay::rt::checkRealtime(klay::rt::Violation::lock);
    return realWaitForSingleObject(handle, ms);
}

DWORD WINAPI hookWaitForSingleObjectEx(HANDLE handle, DWORD ms, BOOL alertable)
{
    if (ms != 0)
        klay::rt::checkRealtime(klay::rt::Violation::lock);
    return realWaitForSingleObjectEx(handle, ms, alertable);
}

DWORD WINAPI hookWaitForMultipleObjects(DWORD count, const HANDLE* handles, BOOL waitAll, DWORD ms)
{
    if (ms != 0)
        klay::rt::checkRealtime(klay::rt::Violation::lock);
    return realWaitForMultipleObjects(count, handles, waitAll, ms);
}

DWORD WINAPI hookWaitForMultipleObjectsEx(DWORD count, const HANDLE* handles, BOOL waitAll, DWORD ms, BOOL alertable)
{
    if (ms != 0)
        klay::rt::checkRealtime(klay::rt::Violation::lock);
    return realWaitForMultipleObjectsEx(count, handles, waitAll, ms, alertable);
}

// std::atomic::wait sits on WaitOnAddress.
BOOL WINAPI hookWaitOnAddress(volatile VOID* address, PVOID compare, SIZE_T size, DWORD ms)
{
    if (ms != 0)
        klay::rt::checkRealtime(klay::rt::Violation::lock);
    return realWaitOnAddress(address, compare, size, ms);
}

LPVOID WINAPI hookVirtualAlloc(LPVOID address, SIZE_T size, DWORD type, DWORD protect)
{
    checkHeap(klay::rt::Violation::allocation);
    return realVirtualAlloc(address, size, type, protect);
}

BOOL WINAPI hookVirtualFree(LPVOID address, SIZE_T size, DWORD type)
{
    checkHeap(klay::rt::Violation::deallocation);
    return realVirtualFree(address, size, type);
}

HLOCAL WINAPI hookLocalAlloc(UINT flags, SIZE_T bytes)
{
    checkHeap(klay::rt::Violation::allocation);
    return realLocalAlloc(flags, bytes);
}

HLOCAL WINAPI hookLocalReAlloc(HLOCAL mem, SIZE_T bytes, UINT flags)
{
    checkHeap(klay::rt::Violation::allocation);
    return realLocalReAlloc(mem, bytes, flags);
}

HLOCAL WINAPI hookLocalFree(HLOCAL mem)
{
    if (mem != nullptr)
        checkHeap(klay::rt::Violation::deallocation);
    return realLocalFree(mem);
}

HGLOBAL WINAPI hookGlobalAlloc(UINT flags, SIZE_T bytes)
{
    checkHeap(klay::rt::Violation::allocation);
    return realGlobalAlloc(flags, bytes);
}

HGLOBAL WINAPI hookGlobalReAlloc(HGLOBAL mem, SIZE_T bytes, UINT flags)
{
    checkHeap(klay::rt::Violation::allocation);
    return realGlobalReAlloc(mem, bytes, flags);
}

HGLOBAL WINAPI hookGlobalFree(HGLOBAL mem)
{
    if (mem != nullptr)
        checkHeap(klay::rt::Violation::deallocation);
    return realGlobalFree(mem);
}

HANDLE WINAPI hookCreateFileA(LPCSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES security, DWORD disposition,
                              DWORD flags, HANDLE templateFile)
{
    klay::rt::checkRealtime(klay::rt::Violation::io);
    return realCreateFileA(name, access, share, security, disposition, flags, templateFile);
}

HANDLE WINAPI hookCreateFileW(LPCWSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES security,
                              DWORD disposition, DWORD flags, HANDLE templateFile)
{
    klay::rt::checkRealtime(klay::rt::Violation::io);
    return realCreateFileW(name, access, share, security, disposition, flags, templateFile);
}

BOOL WINAPI hookReadFile(HANDLE file, LPVOID buffer, DWORD bytes, LPDWORD bytesRead, LPOVERLAPPED overlapped)
{
    klay::rt::checkRealtime(klay::rt::Violation::io);
    return realReadFile(file, buffer, bytes, bytesRead, overlapped);
}

BOOL WINAPI hookWriteFile(HANDLE file, LPCVOID buffer, DWORD bytes, LPDWORD bytesWritten, LPOVERLAPPED overlapped)
{
    klay::rt::checkRealtime(klay::rt::Violation::io);
    return realWriteFile(file, buffer, bytes, bytesWritten, overlapped);
}

void WINAPI hookOutputDebugStringA(LPCSTR text)
{
    klay::rt::checkRealtime(klay::rt::Violation::logging);
    realOutputDebugStringA(text);
}

void WINAPI hookOutputDebugStringW(LPCWSTR text)
{
    klay::rt::checkRealtime(klay::rt::Violation::logging);
    realOutputDebugStringW(text);
}

// Sleep(0) and SwitchToThread give up the time slice, so they count too.
void WINAPI hookSleep(DWORD ms)
{
    klay::rt::checkRealtime(klay::rt::Violation::sleep);
    realSleep(ms);
}

DWORD WINAPI hookSleepEx(DWORD ms, BOOL alertable)
{
    klay::rt::checkRealtime(klay::rt::Violation::sleep);
    return realSleepEx(ms, alertable);
}

BOOL WINAPI hookSwitchToThread()
{
    klay::rt::checkRealtime(klay::rt::Violation::sleep);
    return realSwitchToThread();
}

struct ImportHook
{
    const char* name;
    void* replacement;
    void** original;
};

#define KLAY_IMPORT_HOOK(fn) ImportHook{ #fn, reinterpret_cast<void*>(&hook##fn), reinterpret_cast<void**>(&real##fn) }

const ImportHook importHooks[] = {
    KLAY_IMPORT_HOOK(HeapAlloc),
    KLAY_IMPORT_HOOK(HeapReAlloc),
    KLAY_IMPORT_HOOK(HeapFree),
    KLAY_IMPORT_HOOK(EnterCriticalSection),
    KLAY_IMPORT_HOOK(AcquireSRWLockExclusive),
    KLAY_IMPORT_HOOK(AcquireSRWLockShared),
    KLAY_IMPORT_HOOK(SleepConditionVariableCS),
    KLAY_IMPORT_HOOK(SleepConditionVariableSRW),
    KLAY_IMPORT_HOOK(WaitForSingleObject),
    KLAY_IMPORT_HOOK(WaitForSingleObjectEx),
    KLAY_IMPORT_HOOK(WaitForMultipleObjects),
    KLAY_IMPORT_HOOK(WaitForMultipleObjectsEx),
    KLAY_IMPORT_HOOK(WaitOnAddress),
    KLAY_IMPORT_HOOK(VirtualAlloc),
    KLAY_IMPORT_HOOK(VirtualFree),
    KLAY_IMPORT_HOOK(LocalAlloc),
    KLAY_IMPORT_HOOK(LocalReAlloc),
    KLAY_IMPORT_HOOK(LocalFree),
    KLAY_IMPORT_HOOK(GlobalAlloc),
    KLAY_IMPORT_HOOK(GlobalReAlloc),
    KLAY_IMPORT_HOOK(GlobalFree),
    KLAY_IMPORT_HOOK(CreateFileA),
    KLAY_IMPORT_HOOK(CreateFileW),
    KLAY_IMPORT_HOOK(ReadFile),
    KLAY_IMPORT_HOOK(WriteFile),
    KLAY_IMPORT_HOOK(OutputDebugStringA),
    KLAY_IMPORT_HOOK(OutputDebugStringW),
    KLAY_IMPORT_HOOK(Sleep),
    KLAY_IMPORT_HOOK(SleepEx),
    KLAY_IMPORT_HOOK(SwitchToThread),
};

#undef KLAY_IMPORT_HOOK

// Redirects the matching import-address-table entries of the test executable to the hooks above. The static CRT,
// the STL and JUCE are all linked into the executable, so its import table covers their OS calls.
void patchImportTable() noexcept
{
    auto* base = reinterpret_cast<BYTE*>(::GetModuleHandleW(nullptr));
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (dir.VirtualAddress == 0)
        return;

    for (auto* desc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); desc->Name != 0; ++desc)
    {
        if (desc->OriginalFirstThunk == 0)
            continue;

        auto* nameThunk = reinterpret_cast<IMAGE_THUNK_DATA*>(base + desc->OriginalFirstThunk);
        auto* addressThunk = reinterpret_cast<IMAGE_THUNK_DATA*>(base + desc->FirstThunk);

        for (; nameThunk->u1.AddressOfData != 0; ++nameThunk, ++addressThunk)
        {
            if (IMAGE_SNAP_BY_ORDINAL(nameThunk->u1.Ordinal))
                continue;

            const auto* byName = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + nameThunk->u1.AddressOfData);
            for (const auto& hook : importHooks)
            {
                if (std::strcmp(reinterpret_cast<const char*>(byName->Name), hook.name) != 0)
                    continue;

                if (*hook.original == nullptr)
                    *hook.original = reinterpret_cast<void*>(addressThunk->u1.Function);

                DWORD oldProtect = 0;
                ::VirtualProtect(&addressThunk->u1.Function, sizeof(addressThunk->u1.Function), PAGE_READWRITE,
                                 &oldProtect);
                addressThunk->u1.Function = reinterpret_cast<ULONG_PTR>(hook.replacement);
                ::VirtualProtect(&addressThunk->u1.Function, sizeof(addressThunk->u1.Function), oldProtect,
                                 &oldProtect);
            }
        }
    }
}

// Patch once, during static initialisation of the test executable.
const bool importTablePatched = (patchImportTable(), true);

#endif // _WIN32

} // namespace

namespace klay::rt::test
{

ScopedViolationRecorder::ScopedViolationRecorder() noexcept
{
    allocationCount = 0;
    deallocationCount = 0;
    lockCount = 0;
    ioCount = 0;
    loggingCount = 0;
    sleepCount = 0;
    setViolationHandler(&countViolation);
}

ScopedViolationRecorder::~ScopedViolationRecorder() noexcept
{
    setViolationHandler(nullptr);
}

Counts ScopedViolationRecorder::counts() const noexcept
{
    return { allocationCount.load(), deallocationCount.load(), lockCount.load(),
             ioCount.load(),         loggingCount.load(),      sleepCount.load() };
}

bool osHooksInstalled() noexcept
{
#ifdef _WIN32
    return importTablePatched && realHeapAlloc != nullptr && realHeapFree != nullptr
           && realAcquireSRWLockExclusive != nullptr && realEnterCriticalSection != nullptr;
#else
    return false;
#endif
}

} // namespace klay::rt::test

// clang-format off
void* operator new(std::size_t size)                                                        { return allocateOrThrow(size); }
void* operator new[](std::size_t size)                                                      { return allocateOrThrow(size); }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept                        { return allocate(size); }
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept                      { return allocate(size); }
void* operator new(std::size_t size, std::align_val_t al)                                   { return allocateAlignedOrThrow(size, al); }
void* operator new[](std::size_t size, std::align_val_t al)                                 { return allocateAlignedOrThrow(size, al); }
void* operator new(std::size_t size, std::align_val_t al, const std::nothrow_t&) noexcept   { return allocateAligned(size, al); }
void* operator new[](std::size_t size, std::align_val_t al, const std::nothrow_t&) noexcept { return allocateAligned(size, al); }

void operator delete(void* p) noexcept                                                      { release(p); }
void operator delete[](void* p) noexcept                                                    { release(p); }
void operator delete(void* p, std::size_t) noexcept                                         { release(p); }
void operator delete[](void* p, std::size_t) noexcept                                       { release(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept                               { release(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept                             { release(p); }
void operator delete(void* p, std::align_val_t) noexcept                                    { releaseAligned(p); }
void operator delete[](void* p, std::align_val_t) noexcept                                  { releaseAligned(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept                       { releaseAligned(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept                     { releaseAligned(p); }
void operator delete(void* p, std::align_val_t, const std::nothrow_t&) noexcept             { releaseAligned(p); }
void operator delete[](void* p, std::align_val_t, const std::nothrow_t&) noexcept           { releaseAligned(p); }
// clang-format on
