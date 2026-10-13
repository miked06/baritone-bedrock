#pragma once
#include <atomic>
#include <string_view>

namespace hooks {

bool initialize();

// Creates and enables a detour. `original` receives the trampoline to call the game function.
bool create(std::string_view name, void* target, void* detour, void** original);

template <typename Fn>
bool create(std::string_view name, uintptr_t target, Fn detour, Fn* original) {
    return create(name, reinterpret_cast<void*>(target), reinterpret_cast<void*>(detour),
                  reinterpret_cast<void**>(original));
}

// Disables every hook, waits for the detours still running to return, then removes them.
void shutdown();

// Number of detours currently executing. Every detour holds a Guard so that unloading
// never frees our code while the game is still inside it.
extern std::atomic<int> g_activeCalls;

struct Guard {
    Guard() { g_activeCalls.fetch_add(1, std::memory_order_acq_rel); }
    ~Guard() { g_activeCalls.fetch_sub(1, std::memory_order_acq_rel); }
    Guard(const Guard&) = delete;
    Guard& operator=(const Guard&) = delete;
};

}  // namespace hooks
