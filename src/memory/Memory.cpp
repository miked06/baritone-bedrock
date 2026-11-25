#include "memory/Memory.h"

#include <Windows.h>

namespace mem {

namespace {

constexpr uintptr_t kPage = 0x1000;

int probeFilter(DWORD code, const EXCEPTION_POINTERS* info, uintptr_t& guardPage) {
    if (code == STATUS_GUARD_PAGE_VIOLATION) {
        guardPage = static_cast<uintptr_t>(info->ExceptionRecord->ExceptionInformation[1]) & ~(kPage - 1);
        return EXCEPTION_EXECUTE_HANDLER;
    }
    if (code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_IN_PAGE_ERROR) return EXCEPTION_EXECUTE_HANDLER;
    return EXCEPTION_CONTINUE_SEARCH;
}

// Reads one byte of every page of [first, last]. No C++ objects here: SEH only.
bool probe(uintptr_t first, uintptr_t last, uintptr_t& guardPage) {
    __try {
        const uintptr_t lastPage = last & ~(kPage - 1);
        for (uintptr_t page = first & ~(kPage - 1);; page += kPage) {
            (void)*reinterpret_cast<const volatile char*>(page < first ? first : page);
            if (page == lastPage) break;
        }
        return true;
    } __except (probeFilter(GetExceptionCode(), GetExceptionInformation(), guardPage)) {
        return false;
    }
}

}  // namespace

// Before, this asked VirtualQuery about every pointer. Windows computes the size of the
// whole region around the address for that, which takes milliseconds in Minecraft's
// multi-gigabyte heap and grows with it: a few dozen checks per tick made the game slower
// and slower. Touching the bytes under SEH costs nothing when the memory is valid.
bool isReadable(const void* ptr, std::size_t size) {
    if (!ptr) return false;
    const auto first = reinterpret_cast<uintptr_t>(ptr);
    const uintptr_t last = first + (size ? size - 1 : 0);
    if (last < first) return false;  // wraps around the address space
    uintptr_t guardPage = 0;
    if (probe(first, last, guardPage)) return true;
    if (guardPage) {
        // Touching a guard page (another thread's stack growth limit) removes its guard
        // flag: put it back so that thread can still grow its stack.
        MEMORY_BASIC_INFORMATION mbi{};
        DWORD old = 0;
        if (VirtualQuery(reinterpret_cast<const void*>(guardPage), &mbi, sizeof(mbi)) && mbi.State == MEM_COMMIT)
            VirtualProtect(reinterpret_cast<void*>(guardPage), kPage, mbi.Protect | PAGE_GUARD, &old);
    }
    return false;
}

}  // namespace mem
