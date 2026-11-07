#include <Windows.h>
#include <CommCtrl.h>
#include <commdlg.h>
#include <TlHelp32.h>
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
#include "baritone/task/Workflow.h"
#include "baritone/world/MapFrame.h"
#include <optional>
#include "baritone/utils/CommandLine.h"

namespace {
HWND window, statusBox, logBox, progress, commandBox, xBox, yBox, zBox, itemBox, countBox, fileBox;
HFONT font;
DWORD minecraftPid = 0;
std::future<std::string> statusJob, commandJob;
std::deque<std::string> commands;
std::map<std::string, std::string> latestFields;
enum Id { Send = 100, Stop, Pause, Resume, Go, Mine, Browse, Preview, Verify, Requirements, Repair, Clear,
    Rotate0, Rotate90, Rotate180, Rotate270, MirrorX, MirrorNone, AddGoal, AddMine, AddBuild, AddWait, TaskRun, TaskPause, TaskList, Why, Doctor, WorkflowEditor, TexturedPreview, ExportReport, ManualBuild, TechnicalAudit, WorldMap };
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
    if (!pid) return "connection=Minecraft is not running\n";
    auto name = L"\\\\.\\pipe\\BaritoneBedrock_" + std::to_wstring(pid);
    HANDLE pipe = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 20; ++attempt) {
        pipe = CreateFileW(name.c_str(), GENERIC_WRITE | (response ? GENERIC_READ : 0), 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (pipe != INVALID_HANDLE_VALUE || GetLastError() != ERROR_PIPE_BUSY) break;
        WaitNamedPipeW(name.c_str(), 100);
    }
    if (pipe == INVALID_HANDLE_VALUE) return "connection=Disconnected. Inject BaritoneBedrock.dll into Minecraft\n";
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
        if (result.empty()) result = "connection=Status request timed out\n";
    }
    CloseHandle(pipe); return ok ? result : "connection=Command could not be sent\n";
}
void send(std::string command) {
    if (command.size() > 4096 || commands.size() >= 64) { SetWindowTextW(statusBox, L"Command too long or too many commands pending"); return; }
    if (command == "#stop") commands.push_front(std::move(command)); else commands.push_back(std::move(command));
}
HWND control(const wchar_t* type, const wchar_t* label, DWORD style, int x, int y, int width, int height, int id = 0) {
    HWND out = CreateWindowExW(std::wstring(type) == L"EDIT" ? WS_EX_CLIENTEDGE : 0, type, label, WS_CHILD | WS_VISIBLE | style,
        x, y, width, height, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(out, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE); return out;
}
void button(const wchar_t* label, int x, int y, int width, int id) { control(L"BUTTON", label, WS_TABSTOP | BS_PUSHBUTTON, x, y, width, 28, id); }
#include "WorkflowEditor.inl"
#include "MapPanel.inl"
void createControls() {
    font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    statusBox = control(L"EDIT", L"Connecting to Minecraft...", ES_READONLY | ES_MULTILINE | WS_VSCROLL, 16, 16, 868, 140);
    progress = control(PROGRESS_CLASSW, L"", 0, 16, 164, 868, 16);
    button(L"Stop all", 16, 190, 110, Stop); button(L"Pause", 134, 190, 90, Pause); button(L"Resume", 232, 190, 90, Resume); button(L"Explain failure", 330, 190, 150, Why);
    button(L"Compatibility", 490, 190, 155, Doctor);
    button(L"Export report", 655, 190, 215, ExportReport);
    control(L"STATIC", L"Destination X / Y / Z", 0, 16, 232, 190, 24);
    xBox = control(L"EDIT", L"~", ES_AUTOHSCROLL | WS_TABSTOP, 210, 228, 90, 27); yBox = control(L"EDIT", L"~", ES_AUTOHSCROLL | WS_TABSTOP, 310, 228, 90, 27); zBox = control(L"EDIT", L"~", ES_AUTOHSCROLL | WS_TABSTOP, 410, 228, 90, 27);
    button(L"Go", 510, 228, 80, Go); button(L"Queue goal", 600, 228, 120, AddGoal);
    button(L"Map",730,228,140,WorldMap);
    control(L"STATIC", L"Item / quantity or wait seconds", 0, 16, 270, 220, 24);
    itemBox = control(L"EDIT", L"diamond", ES_AUTOHSCROLL | WS_TABSTOP, 242, 266, 175, 27); countBox = control(L"EDIT", L"64", ES_AUTOHSCROLL | WS_TABSTOP, 427, 266, 73, 27);
    button(L"Mine", 510, 266, 80, Mine); button(L"Queue mine", 600, 266, 120, AddMine); button(L"Queue wait", 730, 266, 140, AddWait);
    control(L"STATIC", L"Schematic file", 0, 16, 311, 150, 24); fileBox = control(L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, 160, 306, 600, 27); button(L"Browse...", 770, 306, 100, Browse);
    button(L"Load preview", 16, 345, 140, Preview); button(L"Verify", 166, 345, 100, Verify); button(L"Materials needed", 276, 345, 170, Requirements); button(L"Repair / build", 456, 345, 140, Repair); button(L"Queue build", 606, 345, 140, AddBuild); button(L"Clear", 756, 345, 114, Clear);
    button(L"Rotate 0", 16, 383, 100, Rotate0); button(L"90", 126, 383, 70, Rotate90); button(L"180", 206, 383, 70, Rotate180); button(L"270", 286, 383, 70, Rotate270); button(L"Mirror X", 376, 383, 110, MirrorX); button(L"No mirror", 496, 383, 120, MirrorNone);
    button(L"Build guide",626,383,115,ManualBuild);button(L"Technical audit",751,383,119,TechnicalAudit);
    button(L"Run queue", 16, 425, 140, TaskRun); button(L"Pause queue", 166, 425, 140, TaskPause); button(L"List tasks", 316, 425, 140, TaskList);
    button(L"Workflow editor", 466, 425, 160, WorkflowEditor);
    button(L"Export textured 3D", 636, 425, 234, TexturedPreview);
    control(L"STATIC", L"Command", 0, 16, 469, 100, 24); commandBox = control(L"EDIT", L"#help", ES_AUTOHSCROLL | WS_TABSTOP, 116, 464, 644, 27); button(L"Send", 770, 464, 100, Send);
    logBox = control(L"EDIT", L"", ES_READONLY | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | WS_HSCROLL, 16, 505, 868, 210);
    SetTimer(window, 1, 100, nullptr);
}
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
        case Send: { auto text = utf8(value(commandBox)); if (!text.starts_with('#')) text.insert(text.begin(), '#'); send(text); break; }
        case Go: send("#goto " + coordinates()); break; case Mine: send("#mine " + utf8(value(countBox)) + " " + quoted(itemBox)); break;
        case Browse: { wchar_t path[32768]{}; OPENFILENAMEW ofn{}; ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = window; ofn.lpstrFile = path; ofn.nMaxFile = 32768; ofn.lpstrFilter = L"Schematics\0*.schem;*.schematic;*.litematic;*.mcstructure\0All files\0*.*\0"; ofn.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR; if (GetOpenFileNameW(&ofn)) SetWindowTextW(fileBox, path); break; }
        case Preview: send("#schematic load " + quoted(fileBox) + " " + coordinates()); break;
        case Verify: send("#schematic verify"); break; case Requirements: send("#schematic requirements"); break; case Repair: send("#schematic repair"); break; case Clear: send("#schematic clear"); break;
        case Rotate0: send("#schematic rotate 0"); break; case Rotate90: send("#schematic rotate 90"); break; case Rotate180: send("#schematic rotate 180"); break; case Rotate270: send("#schematic rotate 270"); break;
        case MirrorX: send("#schematic mirror x"); break; case MirrorNone: send("#schematic mirror none"); break;
        case AddGoal: send("#task add goto " + coordinates()); break; case AddMine: send("#task add mine " + utf8(value(countBox)) + " " + quoted(itemBox)); break; case AddBuild: send("#task add build " + quoted(fileBox) + " " + coordinates()); break; case AddWait: send("#task add wait " + utf8(value(countBox))); break;
        case TaskRun: send("#task resume"); break; case TaskPause: send("#task pause"); break; case TaskList: send("#task list"); break; case Why: send("#why"); break;
    }
}
void refresh() {
    static unsigned tick = 0;
    ++tick;
    if (statusJob.valid() && statusJob.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        auto status = statusJob.get(); std::wstring display; std::istringstream lines(status); std::string line;
        std::map<std::string, std::string> fields;
        while (std::getline(lines, line)) {
            auto separator = line.find('='); if (separator == std::string::npos) continue;
            auto key = line.substr(0, separator), text = line.substr(separator + 1);
            fields[key] = text;
            if (key == "queue_progress") {
                unsigned completed = 0, total = 0; char slash = 0; std::istringstream numbers(text); numbers >> completed >> slash >> total;
                SendMessageW(progress, PBM_SETRANGE32, 0, total ? total : 1); SendMessageW(progress, PBM_SETPOS, completed, 0); key = "Tasks completed";
            }
        }
        latestFields = fields;
        display = wide("Connection: " + fields["connection"] + " | Version: " + fields["version"] + "\r\nWorld: " + fields["world"] + " | Dimension: " + fields["dimension"]
            + "\r\nPosition: " + fields["position"] + "\r\nActivity: " + fields["activity"] + "\r\nTasks: " + fields["queue_progress"] + " | " + fields["queue"]
            + "\r\nSchematic: " + fields["schematic"] + "\r\nLatest: " + (fields["build_assistance"].empty()?fields["latest"]:fields["build_assistance"]));
        SetWindowTextW(statusBox, display.c_str());
    }
    if (!statusJob.valid() && tick % 10 == 0) { minecraftPid = findMinecraft(); statusJob = std::async(std::launch::async, [pid = minecraftPid] { return transact(pid, "@status", true); }); }
    if (commandJob.valid() && commandJob.wait_for(std::chrono::seconds(0)) == std::future_status::ready) { auto error = commandJob.get(); if (!error.empty()) SetWindowTextW(statusBox, wide(error).c_str()); }
    if (!commandJob.valid() && !commands.empty()) {
        auto command = std::move(commands.front()); commands.pop_front();
        commandJob = std::async(std::launch::async, [pid = minecraftPid, command = std::move(command)] { return transact(pid, command, false); });
    }
    if (tick % 10 != 0) return;
    wchar_t appData[32768]{};
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", appData, 32768)) {
        std::ifstream log(std::filesystem::path(appData) / "BaritoneBedrock" / "log.txt", std::ios::binary | std::ios::ate);
        if (log) { auto size = log.tellg(); log.seekg(std::max<std::streamoff>(0, static_cast<std::streamoff>(size) - 24000)); auto text = wide(std::string(std::istreambuf_iterator<char>(log), {})); SetWindowTextW(logBox, text.c_str()); SendMessageW(logBox, EM_SETSEL, text.size(), text.size()); SendMessageW(logBox, EM_SCROLLCARET, 0, 0); }
    }
}
LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_CREATE) { window = hwnd; createControls(); return 0; }
    if (message == WM_COMMAND && HIWORD(wparam) == BN_CLICKED) { action(LOWORD(wparam)); return 0; }
    if (message == WM_TIMER) { refresh(); return 0; }
    if (message == WM_DESTROY) { KillTimer(hwnd, 1); DeleteObject(font); PostQuitMessage(0); return 0; }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    SetProcessDPIAware(); INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_PROGRESS_CLASS}; InitCommonControlsEx(&controls);
    WNDCLASSW cls{}; cls.hInstance = instance; cls.lpszClassName = L"BaritoneControlPanel"; cls.lpfnWndProc = windowProc; cls.hCursor = LoadCursorW(nullptr, IDC_ARROW); cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1); RegisterClassW(&cls);
    window = CreateWindowExW(0, cls.lpszClassName, L"Baritone Bedrock - Control Panel", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT, CW_USEDEFAULT, 920, 770, nullptr, nullptr, instance, nullptr);
    if (!window) return 1;
    ShowWindow(window, show); MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) if (!(flowWindow && IsDialogMessageW(flowWindow, &message)) && !IsDialogMessageW(window, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    return 0;
}
