#include <Windows.h>
#include <CommCtrl.h>
#include <commdlg.h>
#include <TlHelp32.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <array>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <deque>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
#include "baritone/task/Workflow.h"
#include "baritone/world/MapFrame.h"
#include <optional>
#include "baritone/utils/CommandLine.h"

namespace {
HWND window, logBox, commandBox, xBox, yBox, zBox, itemBox, countBox, fileBox, waitBox;
DWORD minecraftPid = 0;
std::future<std::string> statusJob, commandJob;
std::deque<std::string> commands;
std::map<std::string, std::string> latestFields;
enum Id { Send = 100, Stop, Pause, Resume, Go, Mine, Browse, Preview, Verify, Requirements, Repair, Clear,
    Rotate0, Rotate90, Rotate180, Rotate270, MirrorX, MirrorNone, AddGoal, AddMine, AddBuild, AddWait, TaskRun, TaskPause, TaskList, Why, Doctor, WorkflowEditor, TexturedPreview, ExportReport, ManualBuild, TechnicalAudit, WorldMap,
    NavigationPage = 200, SchematicsPage, WorkflowsPage, ConsolePage, FlowApply = 600, MapGo = 750, MapStop };
std::string panelMessage;
std::wstring wide(const std::string& text) {
    int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(n, 0); if (n) MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), n); return out;
}
std::string utf8(const std::wstring& text) {
    int n = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(n, 0); if (n) WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), n, nullptr, nullptr); return out;
}
std::wstring value(HWND control) {
    std::wstring out(static_cast<std::size_t>(GetWindowTextLengthW(control)) + 1, 0);
    GetWindowTextW(control, out.data(), static_cast<int>(out.size())); out.pop_back(); return out;
}
std::string quoted(HWND control) { std::ostringstream out; out << std::quoted(utf8(value(control))); return out.str(); }
DWORD findMinecraft() {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry); DWORD pid = 0;
    if (Process32FirstW(snapshot, &entry)) do {
        if (_wcsicmp(entry.szExeFile, L"Minecraft.Windows.exe") == 0) { pid = entry.th32ProcessID; break; }
    } while (Process32NextW(snapshot, &entry));
    CloseHandle(snapshot); return pid;
}
std::string transact(DWORD pid, const std::string& request, bool response) {
    const auto error = [response](const char* message) {
        return response ? std::string("connection=") + message + "\n" : std::string(message);
    };
    if (!pid) return error("Minecraft is not running");
    auto name = L"\\\\.\\pipe\\BaritoneBedrock_" + std::to_wstring(pid);
    HANDLE pipe = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 20; ++attempt) {
        pipe = CreateFileW(name.c_str(), GENERIC_WRITE | (response ? GENERIC_READ : 0), 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (pipe != INVALID_HANDLE_VALUE || GetLastError() != ERROR_PIPE_BUSY) break;
        WaitNamedPipeW(name.c_str(), 100);
    }
    if (pipe == INVALID_HANDLE_VALUE) return error("Baritone is disconnected. Load the DLL in Minecraft.");
    DWORD count = 0;
    bool ok = WriteFile(pipe, request.data(), static_cast<DWORD>(request.size()), &count, nullptr) && count == request.size();
    std::string result;
    if (ok && response) {
        DWORD available = 0;
        for (int i = 0; i < 100; ++i) {
            if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr)) break;
            if (available) break;
            Sleep(10);
        }
        if (available) { char buffer[16384]; if (ReadFile(pipe, buffer, sizeof(buffer), &count, nullptr)) result.assign(buffer, count); }
        if (result.empty()) result = error("Status request timed out");
    }
    CloseHandle(pipe); return ok ? result : error("Command could not be sent");
}
void send(std::string command) {
    if (command.size() > 4096 || commands.size() >= 64) {
        panelMessage = "Command too long or too many commands pending";
        InvalidateRect(window, nullptr, FALSE);
        return;
    }
    panelMessage.clear();
    InvalidateRect(window, nullptr, FALSE);
    if (command == "#stop") commands.push_front(std::move(command)); else commands.push_back(std::move(command));
}
#include "PanelTheme.inl"
#include "WorkflowEditor.inl"
#include "MapPanel.inl"
#include "PanelLayout.inl"
std::string coordinates() { return utf8(value(xBox)) + " " + utf8(value(yBox)) + " " + utf8(value(zBox)); }
void action(int id) {
    switch (id) {
        case Stop: send("#stop"); break; case Pause: send("#pause"); break; case Resume: send("#resume"); break;
        case Doctor: send("#doctor"); break;
        case WorldMap:openMap();break;
        case ManualBuild:send("#assist on");break;
        case TechnicalAudit:send("#technical");break;
        case ExportReport: {
            wchar_t path[32768]{}; OPENFILENAMEW ofn{}; ofn.lStructSize=sizeof(ofn);ofn.hwndOwner=window;ofn.lpstrFile=path;ofn.nMaxFile=32768;ofn.lpstrDefExt=L"zip";ofn.lpstrFilter=L"Diagnostic archive\0*.zip\0";ofn.Flags=OFN_NOCHANGEDIR;
            if(GetSaveFileNameW(&ofn)){std::ostringstream request;request<<"#report "<<std::quoted(utf8(path));send(request.str());}break;
        }
        case WorkflowEditor: openWorkflowEditor(); break;
        case TexturedPreview: {
            wchar_t path[32768]{}; OPENFILENAMEW ofn{}; ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = window; ofn.lpstrFile = path; ofn.nMaxFile = 32768; ofn.lpstrDefExt = L"html"; ofn.lpstrFilter = L"Local 3D viewer\0*.html\0"; ofn.Flags = OFN_NOCHANGEDIR;
            if (GetSaveFileNameW(&ofn)) { std::ostringstream request; request << "#preview3d " << std::quoted(utf8(path)); send(request.str()); } break;
        }
        case Send: { auto text = utf8(value(commandBox)); if (text.empty()) break; if (!text.starts_with('#')) text.insert(text.begin(), '#'); send(text); break; }
        case Go: send("#goto " + coordinates()); break; case Mine: send("#mine " + utf8(value(countBox)) + " " + quoted(itemBox)); break;
        case Browse: { wchar_t path[32768]{}; OPENFILENAMEW ofn{}; ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = window; ofn.lpstrFile = path; ofn.nMaxFile = 32768; ofn.lpstrFilter = L"Schematics\0*.schem;*.schematic;*.litematic;*.mcstructure\0All files\0*.*\0"; ofn.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR; if (GetOpenFileNameW(&ofn)) SetWindowTextW(fileBox, path); break; }
        case Preview: send("#schematic load " + quoted(fileBox) + " " + coordinates()); break;
        case Verify: send("#schematic verify"); break; case Requirements: send("#schematic requirements"); break; case Repair: send("#schematic repair"); break; case Clear: send("#schematic clear"); break;
        case Rotate0: send("#schematic rotate 0"); break; case Rotate90: send("#schematic rotate 90"); break; case Rotate180: send("#schematic rotate 180"); break; case Rotate270: send("#schematic rotate 270"); break;
        case MirrorX: send("#schematic mirror x"); break; case MirrorNone: send("#schematic mirror none"); break;
        case AddGoal: send("#task add goto " + coordinates()); break; case AddMine: send("#task add mine " + utf8(value(countBox)) + " " + quoted(itemBox)); break; case AddBuild: send("#task add build " + quoted(fileBox) + " " + coordinates()); break; case AddWait: send("#task add wait " + utf8(value(waitBox))); break;
        case TaskRun: send("#task resume"); break; case TaskPause: send("#task pause"); break; case TaskList: send("#task list"); break; case Why: send("#why"); break;
    }
}
void refresh() {
    static unsigned tick = 9;
    ++tick;
    if (statusJob.valid() && statusJob.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        auto status = statusJob.get(); std::istringstream lines(status); std::string line;
        std::map<std::string, std::string> fields;
        while (std::getline(lines, line)) {
            auto separator = line.find('='); if (separator == std::string::npos) continue;
            fields[line.substr(0, separator)] = line.substr(separator + 1);
        }
        constexpr std::array visibleFields{"connection", "version", "world", "dimension", "position",
            "queue", "queue_progress", "activity", "latest", "schematic", "build_assistance"};
        const bool changed = std::any_of(visibleFields.begin(), visibleFields.end(), [&](const char* key) {
            return fields[key] != field(key);
        });
        latestFields = std::move(fields);
        if (changed) InvalidateRect(window, nullptr, FALSE);
    }
    if (!statusJob.valid() && tick % 10 == 0) { minecraftPid = findMinecraft(); statusJob = std::async(std::launch::async, [pid = minecraftPid] { return transact(pid, "@status", true); }); }
    if (commandJob.valid() && commandJob.wait_for(std::chrono::seconds(0)) == std::future_status::ready) { auto error = commandJob.get(); if (!error.empty()) { panelMessage = std::move(error); InvalidateRect(window, nullptr, FALSE); } }
    if (!commandJob.valid() && !commands.empty()) {
        auto command = std::move(commands.front()); commands.pop_front();
        commandJob = std::async(std::launch::async, [pid = minecraftPid, command = std::move(command)] { return transact(pid, command, false); });
    }
    if (tick % 10 != 0 && !logRefreshPending) return;
    if (selectedPage != ConsolePage) return;
    logRefreshPending = false;
    wchar_t appData[32768]{};
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", appData, 32768)) {
        std::ifstream log(std::filesystem::path(appData) / "BaritoneBedrock" / "log.txt", std::ios::binary | std::ios::ate);
        if (log) { auto size = log.tellg(); log.seekg(std::max<std::streamoff>(0, static_cast<std::streamoff>(size) - 24000)); auto tail = std::string(std::istreambuf_iterator<char>(log), {});
            if (size > 24000 && tail.find('\n') != std::string::npos) tail.erase(0, tail.find('\n') + 1);
            updateLog(tail); }
    }
}
LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_CREATE) { window = hwnd; darkTitle(hwnd); createControls(); return 0; }
    if (message == WM_PAINT) { paintPanel(hwnd); return 0; }
    if (message == WM_ERASEBKGND) return 1;
    if (auto result = themeMessage(message, wparam, lparam)) return *result;
    if (message == WM_COMMAND && HIWORD(wparam) == BN_CLICKED) {
        const int id = LOWORD(wparam);
        if (id >= NavigationPage && id <= ConsolePage) switchPage(id);
        else action(id);
        return 0;
    }
    if (message == WM_TIMER) { refresh(); return 0; }
    if (message == WM_DESTROY) { KillTimer(hwnd, 1); PostQuitMessage(0); return 0; }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    SetProcessDPIAware();
    dpi = GetDpiForSystem();
    RECT workArea{};
    if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0))
        dpi = std::max(60u, std::min({dpi, unsigned(MulDiv(workArea.right - workArea.left - 24, 96, panelWidth)),
            unsigned(MulDiv(workArea.bottom - workArea.top - 64, 96, panelHeight + 24))}));
    createTheme();
    WNDCLASSW cls{}; cls.hInstance = instance; cls.lpszClassName = L"BaritoneControlPanel"; cls.lpfnWndProc = windowProc; cls.hCursor = LoadCursorW(nullptr, IDC_ARROW); cls.hbrBackground = backgroundBrush; RegisterClassW(&cls);
    window = panelWindow(cls.lpszClassName, L"Baritone Bedrock - Control Panel", panelWidth, panelHeight, nullptr, instance);
    if (!window) { destroyTheme(); return 1; }
    ShowWindow(window, show); MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        const auto root = GetAncestor(message.hwnd, GA_ROOT);
        if (root && (root == window || root == flowWindow || root == mapWindow) && IsDialogMessageW(root, &message)) continue;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    destroyTheme();
    return 0;
}
