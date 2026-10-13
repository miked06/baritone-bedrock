#pragma once
#include <cstddef>
#include <cstdint>

namespace mem {

// Field of an object we only know by offset.
template <typename T>
inline T& member(const void* base, std::ptrdiff_t offset) {
    return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(base) + offset);
}

inline void** vtable(const void* obj) {
    return *reinterpret_cast<void** const*>(obj);
}

template <typename Ret, typename... Args>
inline Ret callVirtual(const void* obj, std::size_t index, Args... args) {
    using Fn = Ret(__fastcall*)(const void*, Args...);
    return reinterpret_cast<Fn>(vtable(obj)[index])(obj, args...);
}

// True when ptr points into readable committed memory. Used to sanity-check offsets
// before dereferencing them, so a wrong offset after an update logs instead of crashing.
bool isReadable(const void* ptr, std::size_t size = sizeof(void*));

}  // namespace mem
