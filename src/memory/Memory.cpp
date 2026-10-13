#include "memory/Memory.h"

#include <Windows.h>

namespace mem {

bool isReadable(const void* ptr, std::size_t size) {
    if (!ptr) return false;
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(ptr, &mbi, sizeof(mbi))) return false;
    if (mbi.State != MEM_COMMIT) return false;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
    auto end = reinterpret_cast<uintptr_t>(ptr) + size;
    auto regionEnd = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
    return end <= regionEnd || isReadable(reinterpret_cast<const void*>(regionEnd), end - regionEnd);
}

}  // namespace mem
