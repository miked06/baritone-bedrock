#include "core/Log.h"

#include <Windows.h>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <mutex>

namespace logger {
namespace {

std::mutex g_mutex;
std::ofstream g_file;
bool g_console = false;

const char* levelName(Level level) {
    switch (level) {
        case Level::Debug: return "DEBUG";
        case Level::Info: return "INFO ";
        case Level::Warn: return "WARN ";
        case Level::Error: return "ERROR";
    }
    return "?";
}

WORD levelColor(Level level) {
    switch (level) {
        case Level::Debug: return FOREGROUND_INTENSITY;
        case Level::Info: return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
        case Level::Warn: return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
        case Level::Error: return FOREGROUND_RED | FOREGROUND_INTENSITY;
    }
    return 7;
}

}  // namespace

void init(const std::filesystem::path& file, bool console) {
    std::lock_guard lock(g_mutex);
    g_file.open(file, std::ios::out | std::ios::trunc);
    if (console && AllocConsole()) {
        g_console = true;
        SetConsoleTitleW(L"Baritone Bedrock");
        SetConsoleOutputCP(CP_UTF8);
        // Closing a console kills its process: remove the close button so the game survives.
        if (HWND wnd = GetConsoleWindow()) {
            DeleteMenu(GetSystemMenu(wnd, FALSE), SC_CLOSE, MF_BYCOMMAND);
        }
        // A click in the console starts a QuickEdit selection, which blocks every write to it
        // (and so the game thread on its next log line) until Escape: turn QuickEdit off.
        HANDLE in = CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                OPEN_EXISTING, 0, nullptr);
        if (in != INVALID_HANDLE_VALUE) {
            DWORD mode = 0;
            if (GetConsoleMode(in, &mode)) SetConsoleMode(in, (mode | ENABLE_EXTENDED_FLAGS) & ~ENABLE_QUICK_EDIT_MODE);
            CloseHandle(in);
        }
    }
}

void shutdown() {
    std::lock_guard lock(g_mutex);
    g_file.close();
    if (g_console) {
        FreeConsole();
        g_console = false;
    }
}

void write(Level level, std::string_view message) {
    auto now = std::chrono::zoned_time{std::chrono::current_zone(), std::chrono::system_clock::now()};
    std::string line = std::format("[{:%H:%M:%S}] [{}] {}\n", now, levelName(level), message);

    std::lock_guard lock(g_mutex);
    if (g_file.is_open()) {
        g_file << line;
        g_file.flush();
    }
    if (g_console) {
        HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
        SetConsoleTextAttribute(out, levelColor(level));
        DWORD written = 0;
        WriteConsoleA(out, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
    }
    OutputDebugStringA(line.c_str());
}

}  // namespace logger
