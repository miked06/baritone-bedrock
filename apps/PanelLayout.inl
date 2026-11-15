constexpr int panelWidth = 1040;
constexpr int panelHeight = 654;
constexpr int railWidth = 208;
constexpr int contentX = 234;
constexpr int contentWidth = 780;
constexpr int halfWidth = 382;
constexpr int rightX = 632;
constexpr unsigned navigation = 1 << 0;
constexpr unsigned schematics = 1 << 1;
constexpr unsigned workflows = 1 << 2;
constexpr unsigned console = 1 << 3;
constexpr std::array<const wchar_t*, 4> pageNames{L"Navigation", L"Schematics", L"Workflows", L"Console"};

struct PageControl { HWND control; unsigned pages; };
std::vector<PageControl> pageControls;
bool logRefreshPending = true;
std::string lastLog;

std::string field(std::string_view name) {
    const auto it = latestFields.find(std::string(name));
    return it == latestFields.end() ? std::string{} : it->second;
}

void updateLog(const std::string& tail) {
    if (tail == lastLog) return;
    lastLog = tail;
    std::string normalized;
    normalized.reserve(tail.size());
    for (std::size_t i = 0; i < tail.size(); ++i) {
        if (tail[i] == '\n' && (i == 0 || tail[i - 1] != '\r')) normalized += '\r';
        normalized += tail[i];
    }
    const auto text = wide(normalized);
    SetWindowTextW(logBox, text.c_str());
    SendMessageW(logBox, EM_SETSEL, text.size(), text.size());
    SendMessageW(logBox, EM_SCROLLCARET, 0, 0);
}

void switchPage(int page) {
    selectedPage = page;
    const unsigned mask = 1u << (page - NavigationPage);
    for (const auto& child : pageControls)
        ShowWindow(child.control, (child.pages & mask) ? SW_SHOWNA : SW_HIDE);
    for (int id = NavigationPage; id <= ConsolePage; ++id)
        InvalidateRect(GetDlgItem(window, id), nullptr, FALSE);
    logRefreshPending = page == ConsolePage;
    InvalidateRect(window, nullptr, FALSE);
}

HWND panelButton(unsigned pages, const wchar_t* label, int x, int y, int width, int id) {
    auto hwnd = button(label, x, y, width, id);
    pageControls.push_back({hwnd, pages});
    return hwnd;
}

HWND panelEdit(unsigned pages, const wchar_t* initial, int x, int y, int width, const wchar_t* cue = nullptr) {
    auto hwnd = control(L"EDIT", initial, ES_AUTOHSCROLL | WS_TABSTOP, x, y, width, 30);
    if (cue) SendMessageW(hwnd, EM_SETCUEBANNER, FALSE, reinterpret_cast<LPARAM>(cue));
    if (pages) pageControls.push_back({hwnd, pages});
    return hwnd;
}

LRESULT CALLBACK commandKeys(HWND hwnd, UINT message, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR) {
    if (message == WM_GETDLGCODE && wp == VK_RETURN)
        return DefSubclassProc(hwnd, message, wp, lp) | DLGC_WANTMESSAGE;
    if (message == WM_KEYDOWN && wp == VK_RETURN) {
        if ((lp & (1LL << 30)) == 0) SendMessageW(window, WM_COMMAND, MAKEWPARAM(Send, BN_CLICKED), 0);
        return 0;
    }
    if (message == WM_CHAR && wp == VK_RETURN) return 0;
    if (message == WM_NCDESTROY) RemoveWindowSubclass(hwnd, commandKeys, id);
    return DefSubclassProc(hwnd, message, wp, lp);
}

void createControls() {
    for (std::size_t i = 0; i < pageNames.size(); ++i)
        button(pageNames[i], 15, 110 + static_cast<int>(i) * 49, 178, NavigationPage + static_cast<int>(i), 41);
    button(L"Pause", 743, 29, 79, Pause);
    button(L"Resume", 830, 29, 80, Resume);
    button(L"Stop all", 918, 29, 96, Stop);

    xBox = panelEdit(navigation | schematics, L"~", 254, 314, 106, L"X");
    yBox = panelEdit(navigation | schematics, L"~", 370, 314, 106, L"Y");
    zBox = panelEdit(navigation | schematics, L"~", 486, 314, 110, L"Z");
    panelButton(navigation, L"Go to destination", 254, 364, 142, Go);
    panelButton(navigation, L"Queue goal", 404, 364, 104, AddGoal);
    panelButton(navigation, L"Map", 516, 364, 80, WorldMap);
    itemBox = panelEdit(navigation, L"diamond", 652, 314, 218, L"Block or item ID");
    countBox = panelEdit(navigation, L"64", 880, 314, 114, L"Quantity");
    panelButton(navigation, L"Mine", 652, 364, 167, Mine);
    panelButton(navigation, L"Queue mining", 827, 364, 167, AddMine);

    panelButton(schematics, L"0°", 254, 385, 79, Rotate0);
    panelButton(schematics, L"90°", 341, 385, 79, Rotate90);
    panelButton(schematics, L"180°", 428, 385, 80, Rotate180);
    panelButton(schematics, L"270°", 516, 385, 80, Rotate270);
    panelButton(schematics, L"Mirror X", 254, 429, 167, MirrorX);
    panelButton(schematics, L"Reset mirror", 429, 429, 167, MirrorNone);
    panelButton(schematics, L"Build guide", 254, 473, 167, ManualBuild);
    panelButton(schematics, L"Export textured 3D", 429, 473, 167, TexturedPreview);
    fileBox = panelEdit(schematics, L"", 652, 314, 244, L".schem, .schematic, .litematic...");
    panelButton(schematics, L"Browse…", 904, 312, 90, Browse);
    panelButton(schematics, L"Load preview", 652, 364, 167, Preview);
    panelButton(schematics, L"Verify", 827, 364, 167, Verify);
    panelButton(schematics, L"Materials needed", 652, 408, 167, Requirements);
    panelButton(schematics, L"Technical audit", 827, 408, 167, TechnicalAudit);
    panelButton(schematics, L"Repair / build", 652, 452, 167, Repair);
    panelButton(schematics, L"Queue build", 827, 452, 167, AddBuild);
    panelButton(schematics, L"Clear placement", 652, 496, 342, Clear);

    panelButton(workflows, L"Run queue", 254, 311, 167, TaskRun);
    panelButton(workflows, L"Pause queue", 429, 311, 167, TaskPause);
    panelButton(workflows, L"List tasks", 254, 355, 167, TaskList);
    panelButton(workflows, L"Explain failure", 429, 355, 167, Why);
    waitBox = panelEdit(workflows, L"64", 652, 334, 167, L"Seconds");
    panelButton(workflows, L"Queue wait", 827, 332, 167, AddWait);
    panelButton(workflows, L"Workflow editor", 652, 376, 342, WorkflowEditor);

    panelButton(console, L"Compatibility", 712, 236, 137, Doctor);
    panelButton(console, L"Export report", 857, 236, 137, ExportReport);
    logBox = control(L"EDIT", L"No log entries yet.", ES_READONLY | WS_TABSTOP | ES_MULTILINE |
        ES_AUTOVSCROLL | ES_AUTOHSCROLL | WS_VSCROLL | WS_HSCROLL, 254, 283, 740, 272);
    SendMessageW(logBox, WM_SETFONT, reinterpret_cast<WPARAM>(monoFont), FALSE);
    pageControls.push_back({logBox, console});

    commandBox = panelEdit(0, L"#help", 335, 606, 547, L"Enter a command");
    SendMessageW(commandBox, WM_SETFONT, reinterpret_cast<WPARAM>(monoFont), FALSE);
    SendMessageW(commandBox, EM_SETLIMITTEXT, 4096, 0);
    SetWindowSubclass(commandBox, commandKeys, 1, 0);
    button(L"Send", 894, 604, 100, Send);
    switchPage(NavigationPage);
    SetTimer(window, 1, 100, nullptr);
}

void paintPanel(HWND hwnd) {
    PAINTSTRUCT paint;
    HDC dc = BeginPaint(hwnd, &paint);
    RECT client;
    GetClientRect(hwnd, &client);
    fill(dc, client, background);
    fill(dc, rect(0, 0, railWidth, panelHeight), sidebar);
    fill(dc, rect(railWidth - 1, 0, 1, panelHeight), border);
    fill(dc, rect(0, 0, railWidth, 2), accent);

    const auto label = [&](const std::wstring& text, int x, int y, int width, int height = 20) {
        drawText(dc, text, rect(x, y, width, height), smallFont, muted);
    };
    const auto card = [&](int x, int y, int w, int h) {
        roundBox(dc, rect(x, y, w, h), 16, surface, border);
    };
    const auto heading = [&](const wchar_t* text, int x, int y, int w) {
        drawText(dc, text, rect(x, y, w, 29), valueFont, foreground);
    };

    roundBox(dc, rect(20, 25, 41, 41), 12, RGB(21, 61, 78), RGB(29, 106, 130));
    drawText(dc, L"\uE81D", rect(20, 25, 41, 41), iconFont, accent, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
    drawText(dc, L"BARITONE", rect(73, 25, 118, 23), boldFont, foreground);
    label(L"BEDROCK", 73, 46, 116);
    fill(dc, rect(20, 84, 168, 1), border);

    const auto connection = field("connection");
    const auto world = field("world");
    const auto version = field("version");
    const bool online = connection == "Connected" && !world.empty();
    const bool waiting = connection.empty() || connection == "Loading world";
    const auto connectionTitle = online ? L"Connected" : waiting ? L"Connecting" : L"Disconnected";
    const auto connectionColor = online ? success : waiting ? accent : danger;
    roundBox(dc, rect(15, 550, 178, 83), 13, inputBackground, border);
    roundBox(dc, rect(29, 567, 8, 8), 8, connectionColor, connectionColor);
    drawText(dc, connectionTitle, rect(46, 559, 133, 25), boldFont, connectionColor);
    label(version.empty() ? L"Minecraft Bedrock" : L"Baritone " + wide(version), 29, 593, 147);

    drawText(dc, pageNames[selectedPage - NavigationPage], rect(contentX, 25, 478, 45), titleFont, foreground);
    fill(dc, rect(contentX, 91, contentWidth, 1), border);

    const auto stat = [&](int x, int width, const wchar_t* name, std::string text, std::string detail) {
        card(x, 111, width, 92);
        label(name, x + 16, 122, width - 32);
        drawText(dc, text.empty() ? L"—" : wide(text), rect(x + 16, 145, width - 32, 24), valueFont, foreground);
        label(wide(detail), x + 16, 175, width - 32);
    };
    const auto pos = field("position");
    const auto dimension = field("dimension");
    const auto queue = field("queue_progress");
    stat(contentX, 250, L"WORLD", world, dimension.empty() ? "No active world" : "Dimension " + dimension);
    stat(499, 250, L"POSITION  /  X Y Z", pos, pos.empty() ? "Waiting for player" : "Player coordinates");
    stat(764, 250, L"TASK QUEUE", queue, field("queue"));
    unsigned done = 0, total = 0;
    char slash = 0;
    std::istringstream numbers(queue);
    if (numbers >> done >> slash >> total && slash == '/' && total > 0) {
        fill(dc, rect(906, 184, 90, 3), border);
        fill(dc, rect(906, 184, static_cast<int>(90ull * std::min(done, total) / total), 3), accent);
    }

    if (selectedPage == NavigationPage || selectedPage == SchematicsPage) {
        const bool building = selectedPage == SchematicsPage;
        card(contentX, 222, halfWidth, building ? 326 : 204);
        card(rightX, 222, halfWidth, building ? 326 : 204);
        heading(building ? L"Placement" : L"Destination", 254, 239, 342);
        heading(building ? L"Schematic file" : L"Mining", 652, 239, 342);
        label(L"X", 254, 287, 106);
        label(L"Y", 370, 287, 106);
        label(L"Z", 486, 287, 110);
        if (building) {
            label(L"ROTATION", 254, 356, 342);
            label(L"Local file path", 652, 287, 342);
        } else {
            label(L"Block or item ID", 652, 287, 218);
            label(L"Quantity", 880, 287, 114);
        }
    }
    if (selectedPage == WorkflowsPage) {
        card(contentX, 222, halfWidth, 205);
        card(rightX, 222, halfWidth, 205);
        heading(L"Task queue", 254, 239, 342);
        heading(L"Workflow tools", 652, 239, 342);
        label(L"Wait duration in seconds", 652, 300, 342);
    }
    if (selectedPage == ConsolePage) {
        card(contentX, 222, contentWidth, 347);
        heading(L"Activity log", 254, 237, 440);
    }
    if (selectedPage == NavigationPage || selectedPage == WorkflowsPage) {
        card(contentX, 444, contentWidth, 114);
        label(L"CURRENT ACTIVITY", 254, 454, 740);
        const auto task = field("activity");
        drawText(dc, task.empty() ? L"No active session" : wide(task), rect(254, 476, 740, 27), valueFont, foreground);
        auto latest = field("latest");
        if (latest.empty()) latest = online ? "Waiting for a command" : connection;
        label(wide(latest), 254, 516, 740);
    }
    if (selectedPage == SchematicsPage) {
        auto loaded = field("schematic");
        const auto hint = field("build_assistance");
        if (!hint.empty()) loaded = hint;
        if (loaded.empty()) loaded = "No schematic loaded";
        label(wide(loaded), 254, 552, 740);
    }

    card(contentX, 589, contentWidth, 56);
    drawText(dc, L"Command", rect(254, 605, 77, 32), boldFont, foreground);
    if (!panelMessage.empty())
        drawText(dc, wide(panelMessage), rect(contentX + 5, 567, contentWidth - 10, 20), smallFont, danger);
    EndPaint(hwnd, &paint);
}
