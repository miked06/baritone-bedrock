#include "core/Watchdog.h"

#include <Windows.h>

#include <atomic>
#include <cstdint>
#include <filesystem>

#include "core/Log.h"

namespace watchdog {

namespace {

constexpr int kStallSeconds = 5;
constexpr int kMaxFrames = 48;

std::atomic<DWORD> g_gameThread{0};
std::atomic<uint64_t> g_beats{0};
HANDLE g_thread = nullptr;
HANDLE g_stopEvent = nullptr;

// Reads the return address at rsp; the stack may be garbage if unwinding went wrong.
bool readQword(DWORD64 address, DWORD64& out) {
    __try {
        out = *reinterpret_cast<const DWORD64*>(address);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Walks the suspended thread's stack with the x64 unwind data. No allocation while the
// thread is suspended: it may hold the heap lock.
int captureStack(CONTEXT& ctx, DWORD64* frames) {
    int n = 0;
    while (n < kMaxFrames && ctx.Rip) {
        frames[n++] = ctx.Rip;
        DWORD64 imageBase = 0;
        PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(ctx.Rip, &imageBase, nullptr);
        if (!fn) {  // leaf function: the return address is on top of the stack
            if (!readQword(ctx.Rsp, ctx.Rip)) break;
            ctx.Rsp += 8;
            continue;
        }
        PVOID handlerData = nullptr;
        DWORD64 establisher = 0;
        __try {
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, ctx.Rip, fn, &ctx, &handlerData, &establisher, nullptr);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            break;
        }
    }
    return n;
}

void dumpGameThread(DWORD tid) {
    HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, tid);
    if (!h) {
        logger::warn("Watchdog: OpenThread failed ({})", GetLastError());
        return;
    }
    DWORD64 frames[kMaxFrames];
    int n = 0;
    if (SuspendThread(h) != static_cast<DWORD>(-1)) {
        CONTEXT ctx{};
        ctx.ContextFlags = CONTEXT_FULL;
        if (GetThreadContext(h, &ctx)) n = captureStack(ctx, frames);
        ResumeThread(h);
    }
    CloseHandle(h);

    logger::warn("Watchdog: the game thread has not drawn a frame for {} s. Its call stack:", kStallSeconds);
    for (int i = 0; i < n; ++i) {
        HMODULE module = nullptr;
        wchar_t path[MAX_PATH] = L"?";
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCWSTR>(frames[i]), &module)) {
            GetModuleFileNameW(module, path, MAX_PATH);
        }
        logger::warn("  #{:02} {}+0x{:X}", i, std::filesystem::path(path).filename().string(),
                     frames[i] - reinterpret_cast<DWORD64>(module));
    }
}

DWORD WINAPI run(LPVOID) {
    uint64_t last = g_beats.load();
    int stalled = 0;
    while (WaitForSingleObject(g_stopEvent, 1000) == WAIT_TIMEOUT) {
        uint64_t now = g_beats.load(std::memory_order_relaxed);
        DWORD tid = g_gameThread.load(std::memory_order_relaxed);
        if (now != last || !tid) {
            if (stalled >= kStallSeconds) logger::info("Watchdog: the game thread is running again");
            last = now;
            stalled = 0;
            continue;
        }
        if (++stalled == kStallSeconds) dumpGameThread(tid);  // once per stall
    }
    return 0;
}

}  // namespace

void heartbeat() {
    g_gameThread.store(GetCurrentThreadId(), std::memory_order_relaxed);
    g_beats.fetch_add(1, std::memory_order_relaxed);
}

void start() {
    if (g_thread) return;
    g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_thread = CreateThread(nullptr, 0, run, nullptr, 0, nullptr);
}

void stop() {
    if (!g_thread) return;
    SetEvent(g_stopEvent);
    WaitForSingleObject(g_thread, INFINITE);
    CloseHandle(g_thread);
    CloseHandle(g_stopEvent);
    g_thread = nullptr;
    g_stopEvent = nullptr;
}

}  // namespace watchdog
