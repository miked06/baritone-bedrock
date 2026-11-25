// mem::isReadable checks, in their own file: Windows.h macros clash with the other tests.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <chrono>
#include <cstdint>
#include <format>
#include <string>
#include <vector>

#include "memory/Memory.h"

std::string memoryProbeSelfTest() {
    int local = 7;
    if (!mem::isReadable(&local, sizeof(local))) return "a stack variable is not readable";
    if (mem::isReadable(nullptr)) return "nullptr is readable";
    if (mem::isReadable(reinterpret_cast<const void*>(~uintptr_t{0} - 4), 16)) return "a range wrapping around is readable";
    auto* base = static_cast<char*>(VirtualAlloc(nullptr, 0x4000, MEM_RESERVE, PAGE_NOACCESS));
    if (!base) return "VirtualAlloc failed";
    std::string failure;
    if (mem::isReadable(base, 8)) failure = "reserved memory is readable";
    VirtualAlloc(base, 0x1000, MEM_COMMIT, PAGE_READWRITE);
    if (failure.empty() && !mem::isReadable(base, 0x1000)) failure = "a committed page is not readable";
    if (failure.empty() && mem::isReadable(base + 0xFF8, 16)) failure = "a range running into reserved memory is readable";
    VirtualAlloc(base + 0x1000, 0x1000, MEM_COMMIT, PAGE_NOACCESS);
    if (failure.empty() && mem::isReadable(base + 0x1000, 8)) failure = "a PAGE_NOACCESS page is readable";
    VirtualAlloc(base + 0x2000, 0x1000, MEM_COMMIT, PAGE_READWRITE | PAGE_GUARD);
    for (int attempt = 0; attempt < 2 && failure.empty(); ++attempt) {
        if (mem::isReadable(base + 0x2000, 8)) failure = "a guard page is readable";
        MEMORY_BASIC_INFORMATION mbi{};
        VirtualQuery(base + 0x2000, &mbi, sizeof(mbi));
        if (failure.empty() && !(mbi.Protect & PAGE_GUARD)) failure = "the guard flag was not restored";
    }
    VirtualFree(base, 0, MEM_RELEASE);
    if (!failure.empty()) return failure;
    // cheap enough to call many times per tick
    std::vector<char> heap(1 << 20);
    const auto start = std::chrono::steady_clock::now();
    bool all = true;
    for (int i = 0; i < 100000; ++i) all &= mem::isReadable(heap.data() + (static_cast<std::size_t>(i) * 97) % (heap.size() - 64), 64);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    if (!all) return "heap memory is not readable";
    if (ms > 200) return std::format("100000 checks took {:.1f} ms", ms);
    return {};
}
