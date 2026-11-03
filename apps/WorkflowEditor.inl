// Included within the panel's anonymous namespace after the control helpers.
HWND flowWindow = nullptr, flowList, flowIndex, flowSuccess, flowFailure, flowVisits, flowTimeout, flowKind, flowArgs, flowMessage;
baritone::Workflow flowDraft;
enum { FlowApply = 600, FlowImport, FlowLoadCurrent, FlowSaveNode, FlowNew, FlowRun, FlowPause };
HWND flowControl(const wchar_t* type, const wchar_t* label, DWORD style, int x, int y, int width, int height, int id = 0) {
    auto previous = window; window = flowWindow; auto out = control(type, label, style, x, y, width, height, id); window = previous; return out;
}
void flowButton(const wchar_t* label, int x, int y, int width, int id) { flowControl(L"BUTTON", label, WS_TABSTOP | BS_PUSHBUTTON, x, y, width, 28, id); }
void flowRefresh() {
    SendMessageW(flowList, LB_RESETCONTENT, 0, 0);
    for (std::size_t i = 0; i < flowDraft.nodes().size(); ++i) {
        const auto& node = flowDraft.nodes()[i];
        auto row = std::to_wstring(i) + L"   " + wide(node.task.kind) + L"   success -> " + std::to_wstring(node.success) + L"   failure -> " + std::to_wstring(node.failure) + L"   max visits " + std::to_wstring(node.visitLimit);
        SendMessageW(flowList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(row.c_str()));
    }
}
std::filesystem::path flowPickFile() {
    wchar_t path[32768]{}; OPENFILENAMEW ofn{}; ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = flowWindow; ofn.lpstrFile = path; ofn.nMaxFile = 32768;
    ofn.lpstrFilter = L"Workflow files\0*.txt\0All files\0*.*\0"; ofn.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameW(&ofn) ? std::filesystem::path(path) : std::filesystem::path{};
}
void flowRead(const std::filesystem::path& file) {
    if (file.empty()) return;
    if (std::filesystem::file_size(file) > 2 * 1024 * 1024) throw std::runtime_error("Workflow exceeds two MiB");
    std::ifstream input(file); if (!input) throw std::runtime_error("Cannot read workflow file");
    auto parsed = baritone::Workflow::decode(std::string(std::istreambuf_iterator<char>(input), {})); parsed.restart(); flowDraft = std::move(parsed); flowRefresh();
    SetWindowTextW(flowMessage, L"Draft loaded. Edit nodes, then Apply to Minecraft. Applying leaves it paused.");
}
LRESULT CALLBACK flowProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
    if (message == WM_CLOSE) { DestroyWindow(hwnd); return 0; }
    if (message == WM_DESTROY) { flowWindow = nullptr; return 0; }
    if (message != WM_COMMAND) return DefWindowProcW(hwnd, message, wp, lp);
    try {
        if (reinterpret_cast<HWND>(lp) == flowList && HIWORD(wp) == LBN_SELCHANGE) {
            const auto index = SendMessageW(flowList, LB_GETCURSEL, 0, 0);
            if (index >= 0 && std::size_t(index) < flowDraft.nodes().size()) {
                const auto& n = flowDraft.nodes()[std::size_t(index)];
                SetWindowTextW(flowIndex, std::to_wstring(index).c_str()); SetWindowTextW(flowSuccess, std::to_wstring(n.success).c_str()); SetWindowTextW(flowFailure, std::to_wstring(n.failure).c_str());
                SetWindowTextW(flowVisits, std::to_wstring(n.visitLimit).c_str()); SetWindowTextW(flowTimeout, std::to_wstring(n.task.timeoutTicks / 20).c_str()); SetWindowTextW(flowKind, wide(n.task.kind).c_str());
                std::ostringstream args; for (const auto& arg : n.task.arguments) args << std::quoted(arg) << ' '; SetWindowTextW(flowArgs, wide(args.str()).c_str());
            }
            return 0;
        }
        if (HIWORD(wp) != BN_CLICKED) return 0;
        switch (LOWORD(wp)) {
            case FlowSaveNode: {
                auto number = [](HWND field) { const auto text = value(field); std::size_t used = 0; int number = std::stoi(text, &used); if (used != text.size()) throw std::invalid_argument("Use whole numbers in numeric fields"); return number; };
                const int index = number(flowIndex), visits = number(flowVisits), seconds = number(flowTimeout);
                if (index < 0 || visits < 1 || seconds < 1 || seconds > 86400) throw std::invalid_argument("Invalid index, visits or timeout");
                baritone::Workflow::Node node; node.success = number(flowSuccess); node.failure = number(flowFailure); node.visitLimit = unsigned(visits); node.task.timeoutTicks = uint64_t(seconds) * 20;
                node.task.kind = utf8(value(flowKind)); node.task.arguments = baritone::commandArguments(utf8(value(flowArgs)));
                flowDraft.set(std::size_t(index), std::move(node)); flowRefresh(); SetWindowTextW(flowMessage, L"Node saved in the local draft. Apply sends the whole graph atomically."); break;
            }
            case FlowNew: flowDraft = {}; flowDraft.bind({latestFields["world"], std::stoi(latestFields["dimension"])}); flowRefresh(); break;
            case FlowImport: flowRead(flowPickFile()); break;
            case FlowLoadCurrent: flowRead(std::filesystem::u8path(latestFields["workflow_file"])); break;
            case FlowApply: {
                if (latestFields["world"].empty()) throw std::runtime_error("Connect to an existing Minecraft world first");
                flowDraft.bind({latestFields["world"], std::stoi(latestFields["dimension"])});
                wchar_t local[32768]{}; if (!GetEnvironmentVariableW(L"LOCALAPPDATA", local, 32768)) throw std::runtime_error("LOCALAPPDATA unavailable");
                auto directory = std::filesystem::path(local) / "BaritoneBedrock" / "workflow-drafts"; std::filesystem::create_directories(directory);
                auto file = directory / (L"panel-" + std::to_wstring(GetCurrentProcessId()) + L".txt"); auto text = flowDraft.encode();
                std::ofstream output(file); output << text; output.close(); if (!output) throw std::runtime_error("Cannot save the workflow draft");
                std::ostringstream request; auto path = file.u8string(); request << "#workflow load " << std::quoted(std::string(path.begin(), path.end())); send(request.str());
                SetWindowTextW(flowMessage, L"Graph queued for validation. Read the Minecraft log before clicking Run."); break;
            }
            case FlowRun: send("#workflow run"); break;
            case FlowPause: send("#workflow pause"); break;
        }
    } catch (const std::exception& e) { SetWindowTextW(flowMessage, wide(e.what()).c_str()); }
    return 0;
}
void openWorkflowEditor() {
    if (flowWindow) { ShowWindow(flowWindow, SW_SHOW); SetForegroundWindow(flowWindow); return; }
    if (flowDraft.nodes().empty() && !latestFields["world"].empty()) {
        try { flowDraft.bind({latestFields["world"], std::stoi(latestFields["dimension"])}); } catch (...) {}
    }
    WNDCLASSW cls{}; cls.hInstance = GetModuleHandleW(nullptr); cls.lpszClassName = L"BaritoneWorkflowEditor"; cls.lpfnWndProc = flowProc; cls.hCursor = LoadCursorW(nullptr, IDC_ARROW); cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1); RegisterClassW(&cls);
    flowWindow = CreateWindowExW(WS_EX_CONTROLPARENT, cls.lpszClassName, L"Baritone Bedrock - Workflow Editor", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 830, 580, window, nullptr, cls.hInstance, nullptr);
    flowList = flowControl(L"LISTBOX", L"", LBS_NOTIFY | WS_VSCROLL | WS_TABSTOP | WS_BORDER, 16, 16, 780, 205);
    const wchar_t* labels[] = {L"Node", L"Success", L"Failure", L"Max visits", L"Timeout (s)"}; HWND* boxes[] = {&flowIndex, &flowSuccess, &flowFailure, &flowVisits, &flowTimeout}; const wchar_t* defaults[] = {L"0", L"-1", L"-2", L"1", L"600"};
    for (int i = 0; i < 5; ++i) { flowControl(L"STATIC", labels[i], 0, 16 + i * 155, 232, 140, 22); *boxes[i] = flowControl(L"EDIT", defaults[i], ES_AUTOHSCROLL | WS_TABSTOP, 16 + i * 155, 255, 140, 27); }
    flowControl(L"STATIC", L"Action / condition", 0, 16, 294, 170, 22); flowKind = flowControl(L"EDIT", L"ifitem", ES_AUTOHSCROLL | WS_TABSTOP, 16, 320, 170, 27);
    flowControl(L"STATIC", L"Arguments (quote paths; wait uses ticks; coordinates are absolute)", 0, 200, 294, 590, 22); flowArgs = flowControl(L"EDIT", L"stone 64", ES_AUTOHSCROLL | WS_TABSTOP, 200, 320, 590, 27);
    flowButton(L"Save node", 16, 362, 120, FlowSaveNode); flowButton(L"New graph", 146, 362, 120, FlowNew); flowButton(L"Import file", 276, 362, 120, FlowImport); flowButton(L"Load current", 406, 362, 140, FlowLoadCurrent); flowButton(L"Apply to Minecraft", 556, 362, 230, FlowApply);
    flowButton(L"Run", 16, 402, 120, FlowRun); flowButton(L"Pause", 146, 402, 120, FlowPause);
    flowControl(L"STATIC", L"Branches: -1 finishes, -2 stops. Loops need a visit limit on every revisited node.\r\nActions: ifitem, inventory, restock, goto, wait, mine, build, verify, withdraw, deposit.", 0, 16, 445, 780, 42);
    flowMessage = flowControl(L"STATIC", L"Create node 0 first. Saving an edit resets local progress. Apply never starts execution.", 0, 16, 494, 780, 35);
    flowRefresh(); ShowWindow(flowWindow, SW_SHOW);
}
