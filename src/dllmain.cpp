#include <Windows.h>

#include <format>
#include <string>

#include "core/Bot.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "core/Security.h"
#include "memory/Hooks.h"
#include "memory/Signatures.h"
#include "platform/WorldTracker.h"

namespace {

HMODULE g_module = nullptr;

// Named event so a dev tool can ask for an unload without typing in the game.
std::wstring unloadEventName() {
    return std::format(L"BaritoneBedrock_Unload_{}", GetCurrentProcessId());
}

DWORD WINAPI mainThread(LPVOID) {
    logger::init(paths::dataDir() / "log.txt", true);
    logger::info("Baritone Bedrock - data dir: {}", paths::dataDir().string());

    OpenSecurity security;
    HANDLE unloadEvent = CreateEventW(security.get(), TRUE, FALSE, unloadEventName().c_str());
    bool started = sigs::resolveAll() && hooks::initialize() && Bot::get().start();
    if (!started) {
        logger::error("Initialisation failed, unloading. Is the game version supported?");
    } else {
        logger::info("Ready. Type #help in the chat.");
        while (!Bot::get().unloadRequested() && WaitForSingleObject(unloadEvent, 100) == WAIT_TIMEOUT) {
        }
    }

    logger::info("Unloading...");
    Bot::get().stop();
    hooks::shutdown();
    platform::WorldTracker::get().reset();  // writes the long-distance cache
    logger::info("Unloaded.");
    logger::shutdown();
    if (unloadEvent) CloseHandle(unloadEvent);
    FreeLibraryAndExitThread(g_module, 0);
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = module;
        DisableThreadLibraryCalls(module);
        if (HANDLE thread = CreateThread(nullptr, 0, mainThread, nullptr, 0, nullptr)) CloseHandle(thread);
    }
    return TRUE;
}
