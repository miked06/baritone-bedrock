#include "memory/Hooks.h"

#include <Windows.h>
#include <MinHook.h>

#include <string>
#include <vector>

#include "core/Log.h"

namespace hooks {

std::atomic<int> g_activeCalls{0};

namespace {

struct Entry {
    std::string name;
    void* target;
};

std::vector<Entry> g_hooks;
bool g_initialized = false;

}  // namespace

bool initialize() {
    MH_STATUS status = MH_Initialize();
    if (status != MH_OK) {
        logger::error("MH_Initialize failed: {}", MH_StatusToString(status));
        return false;
    }
    g_initialized = true;
    return true;
}

bool create(std::string_view name, void* target, void* detour, void** original) {
    if (!target) {
        logger::error("Hook {}: null target", name);
        return false;
    }
    MH_STATUS status = MH_CreateHook(target, detour, original);
    if (status != MH_OK) {
        logger::error("Hook {}: MH_CreateHook failed: {}", name, MH_StatusToString(status));
        return false;
    }
    status = MH_EnableHook(target);
    if (status != MH_OK) {
        logger::error("Hook {}: MH_EnableHook failed: {}", name, MH_StatusToString(status));
        MH_RemoveHook(target);
        return false;
    }
    g_hooks.push_back({std::string(name), target});
    logger::debug("Hooked {}", name);
    return true;
}

void shutdown() {
    if (!g_initialized) return;
    MH_DisableHook(MH_ALL_HOOKS);

    // A detour that started before the disable may still be running (e.g. a tick that is
    // executing a command). Wait for all of them to return before the DLL is freed.
    for (int i = 0; i < 200 && g_activeCalls.load() > 0; ++i) Sleep(10);
    if (g_activeCalls.load() > 0) logger::warn("{} detours still running at unload", g_activeCalls.load());
    Sleep(50);  // a caller may be between the trampoline and the guard

    for (const Entry& hook : g_hooks) MH_RemoveHook(hook.target);
    g_hooks.clear();
    MH_Uninitialize();
    g_initialized = false;
}

}  // namespace hooks
