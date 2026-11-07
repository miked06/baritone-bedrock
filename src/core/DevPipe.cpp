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
std::string g_status = "connection=Initializing\n";
std::string g_map;
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
        HANDLE pipe = CreateNamedPipeW(pipeName().c_str(), PIPE_ACCESS_DUPLEX,
                                       PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT, 1, 16384, 4096, 0,
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
                if (std::string_view(buffer, read) == "@status" || std::string_view(buffer,read)=="@map") {
                    std::string status;
                    { std::lock_guard lock(g_mutex); status = std::string_view(buffer,read)=="@map"?g_map:g_status; }
                    DWORD written = 0;
                    WriteFile(pipe, status.data(), static_cast<DWORD>(status.size()), &written, nullptr);
                } else {
                    std::lock_guard lock(g_mutex);
                    if (g_queue.size() < 256) g_queue.emplace_back(buffer, read);
                }
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
        CancelSynchronousIo(g_thread);
        WaitForSingleObject(g_thread, INFINITE);
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
void setStatus(std::string status) {
    std::lock_guard lock(g_mutex); g_status = status.substr(0, 16000);
}
void setMap(std::string map) {std::lock_guard lock(g_mutex);g_map=map.size()<=15000?std::move(map):std::string{};}

}  // namespace devpipe
