#include "core/DevPipe.h"

#include <Windows.h>

#include <atomic>
#include <deque>
#include <format>
#include <mutex>

#include "core/Log.h"
#include "core/Security.h"

namespace devpipe {
namespace {

std::mutex g_mutex;
std::deque<std::string> g_queue;
std::atomic<bool> g_running{false};
// Raw handle, not a static std::thread: when the game exits without unloading us, a still
// joinable std::thread destroyed at process exit calls std::terminate.
HANDLE g_thread = nullptr;

std::wstring pipeName() {
    return std::format(L"\\\\.\\pipe\\BaritoneBedrock_{}", GetCurrentProcessId());
}

void serve() {
    OpenSecurity security;
    while (g_running) {
        HANDLE pipe = CreateNamedPipeW(pipeName().c_str(), PIPE_ACCESS_INBOUND,
                                       PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT, 1, 0, 4096, 0,
                                       security.get());
        if (pipe == INVALID_HANDLE_VALUE) {
            logger::warn("Dev pipe unavailable ({})", GetLastError());
            return;
        }
        bool connected = ConnectNamedPipe(pipe, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED;
        if (connected && g_running) {
            char buffer[4096];
            DWORD read = 0;
            while (ReadFile(pipe, buffer, sizeof(buffer), &read, nullptr) && read > 0) {
                std::lock_guard lock(g_mutex);
                g_queue.emplace_back(buffer, read);
            }
        }
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }
}

}  // namespace

void start() {
    g_running = true;
    g_thread = CreateThread(
        nullptr, 0,
        [](LPVOID) -> DWORD {
            serve();
            return 0;
        },
        nullptr, 0, nullptr);
}

void stop() {
    if (!g_running.exchange(false)) return;
    // Unblock ConnectNamedPipe by connecting to ourselves.
    HANDLE client = CreateFileW(pipeName().c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (client != INVALID_HANDLE_VALUE) CloseHandle(client);
    if (g_thread) {
        WaitForSingleObject(g_thread, 2000);
        CloseHandle(g_thread);
        g_thread = nullptr;
    }
}

std::optional<std::string> poll() {
    std::lock_guard lock(g_mutex);
    if (g_queue.empty()) return std::nullopt;
    std::string line = std::move(g_queue.front());
    g_queue.pop_front();
    return line;
}

}  // namespace devpipe
