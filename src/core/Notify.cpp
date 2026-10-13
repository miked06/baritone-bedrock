#include "core/Notify.h"

#include <Windows.h>
#include <shellapi.h>

#include <algorithm>
#include <mutex>
#include <string>

#include "baritone/Settings.h"
#include "core/Log.h"

namespace notify {
namespace {

std::mutex g_mutex;
bool g_added = false;
NOTIFYICONDATAW g_icon{};
constexpr UINT kIconId = 0xBA51;

std::wstring widen(std::string_view s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

// The game's main window ("Bedrock" class) in this process.
HWND gameWindow() {
    struct Search {
        DWORD pid;
        HWND found;
    } search{GetCurrentProcessId(), nullptr};
    EnumWindows(
        [](HWND h, LPARAM l) -> BOOL {
            auto* s = reinterpret_cast<Search*>(l);
            DWORD pid = 0;
            GetWindowThreadProcessId(h, &pid);
            wchar_t cls[64]{};
            GetClassNameW(h, cls, 64);
            if (pid == s->pid && std::wstring_view(cls) == L"Bedrock") {
                s->found = h;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&search));
    return search.found;
}

template <std::size_t N>
void copy(wchar_t (&dst)[N], const std::wstring& src) {
    wcsncpy_s(dst, src.c_str(), _TRUNCATE);
}

}  // namespace

void show(std::string_view title, std::string_view message, bool error) {
    std::lock_guard lock(g_mutex);
    if (!g_added) {
        HWND wnd = gameWindow();
        if (!wnd) return;
        g_icon = {};
        g_icon.cbSize = sizeof(g_icon);
        g_icon.hWnd = wnd;
        g_icon.uID = kIconId;
        g_icon.uFlags = NIF_ICON | NIF_TIP;
        g_icon.hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32516));  // IDI_INFORMATION
        copy(g_icon.szTip, L"Baritone Bedrock");
        if (!Shell_NotifyIconW(NIM_ADD, &g_icon)) {
            logger::warn("Notification: tray icon not added");
            return;
        }
        g_added = true;
    }
    g_icon.uFlags = NIF_INFO;
    // Java toastTimer (ms). Windows 10+ keeps the balloon by its own accessibility settings
    g_icon.uTimeout = static_cast<UINT>(std::clamp<int64_t>(baritone::Settings::get().toastTimer, 1000, 30000));
    copy(g_icon.szInfoTitle, widen(title));
    copy(g_icon.szInfo, widen(message));
    g_icon.dwInfoFlags = error ? NIIF_ERROR : NIIF_INFO;
    Shell_NotifyIconW(NIM_MODIFY, &g_icon);
}

void shutdown() {
    std::lock_guard lock(g_mutex);
    if (!g_added) return;
    Shell_NotifyIconW(NIM_DELETE, &g_icon);
    g_added = false;
}

}  // namespace notify
