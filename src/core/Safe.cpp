#include "core/Safe.h"

#include <Windows.h>

#include <atomic>

#include "core/Chat.h"
#include "core/Log.h"

namespace safe {
namespace {

std::atomic<bool> g_faulted{false};

std::string describeAddress(void* address) {
    HMODULE module = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           static_cast<LPCWSTR>(address), &module)) {
        char name[MAX_PATH]{};
        GetModuleFileNameA(module, name, MAX_PATH);
        std::string_view file(name);
        file = file.substr(file.find_last_of("\\/") + 1);
        return std::format("{}+0x{:X}", file,
                           reinterpret_cast<uintptr_t>(address) - reinterpret_cast<uintptr_t>(module));
    }
    return std::format("0x{:X}", reinterpret_cast<uintptr_t>(address));
}

}  // namespace

bool faulted() {
    return g_faulted.load(std::memory_order_relaxed);
}

void resetFault() {
    g_faulted = false;
}

namespace detail {

static int filter(EXCEPTION_POINTERS* info, void** faultAddress) {
    *faultAddress = info->ExceptionRecord->ExceptionAddress;
    return EXCEPTION_EXECUTE_HANDLER;
}

unsigned long invokeSeh(Thunk thunk, void* ctx, void** faultAddress) {
    __try {
        thunk(ctx);
    } __except (filter(GetExceptionInformation(), faultAddress)) {
        return GetExceptionCode();
    }
    return 0;
}

void reportSeh(std::string_view where, unsigned long code, void* address) {
    g_faulted = true;
    logger::error("Fault 0x{:08X} in {} at {} -> bot disabled (use #reset after fixing)", code, where,
                  describeAddress(address));
    chat::error(std::format("Crash evite dans {} ({}). Bot desactive, voir log.txt", where, describeAddress(address)));
}

void reportException(std::string_view where, const char* what) {
    logger::error("Exception in {}: {}", where, what ? what : "?");
}

}  // namespace detail
}  // namespace safe
