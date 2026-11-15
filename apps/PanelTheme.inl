// Shared colors and native control drawing for the Windows panel.
constexpr COLORREF background = RGB(12, 16, 23);
constexpr COLORREF sidebar = RGB(16, 21, 30);
constexpr COLORREF surface = RGB(20, 27, 38);
constexpr COLORREF inputBackground = RGB(12, 18, 27);
constexpr COLORREF border = RGB(36, 48, 64);
constexpr COLORREF foreground = RGB(232, 240, 250);
constexpr COLORREF muted = RGB(151, 169, 191);
constexpr COLORREF accent = RGB(82, 214, 249);
constexpr COLORREF danger = RGB(255, 133, 153);
constexpr COLORREF success = RGB(104, 227, 175);

UINT dpi = 96;
int selectedPage = NavigationPage;
HFONT font, smallFont, boldFont, titleFont, valueFont, monoFont, iconFont;
HBRUSH backgroundBrush, inputBrush;
HWND hoveredButton = nullptr;

int px(int logical) { return MulDiv(logical, static_cast<int>(dpi), 96); }
RECT rect(int x, int y, int w, int h) { return {px(x), px(y), px(x + w), px(y + h)}; }

HFONT makeFont(int size, int weight, const wchar_t* family = L"Segoe UI") {
    return CreateFontW(-px(size), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, family);
}

void createTheme() {
    font = makeFont(14, FW_NORMAL);
    smallFont = makeFont(12, FW_NORMAL);
    boldFont = makeFont(14, FW_SEMIBOLD);
    titleFont = makeFont(34, FW_SEMIBOLD);
    valueFont = makeFont(20, FW_SEMIBOLD);
    monoFont = makeFont(13, FW_NORMAL, L"Consolas");
    iconFont = makeFont(16, FW_NORMAL, L"Segoe MDL2 Assets");
    backgroundBrush = CreateSolidBrush(background);
    inputBrush = CreateSolidBrush(inputBackground);
}

void destroyTheme() {
    for (auto object : {font, smallFont, boldFont, titleFont, valueFont, monoFont, iconFont}) DeleteObject(object);
    DeleteObject(backgroundBrush);
    DeleteObject(inputBrush);
}

void darkTitle(HWND hwnd) {
    constexpr DWORD immersiveDarkMode = 20;
    constexpr DWORD captionColor = 35;
    const BOOL enabled = TRUE;
    DwmSetWindowAttribute(hwnd, immersiveDarkMode, &enabled, sizeof(enabled));
    DwmSetWindowAttribute(hwnd, captionColor, &background, sizeof(background));
}

HWND panelWindow(const wchar_t* name, const wchar_t* title, int width, int height, HWND owner, HINSTANCE instance) {
    constexpr DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
    RECT client = rect(0, 0, width, height);
    AdjustWindowRectExForDpi(&client, style, FALSE, WS_EX_CONTROLPARENT, GetDpiForSystem());
    const int outerWidth = client.right - client.left;
    const int outerHeight = client.bottom - client.top;
    int x = CW_USEDEFAULT;
    int y = CW_USEDEFAULT;
    MONITORINFO monitor{sizeof(monitor)};
    if (GetMonitorInfoW(MonitorFromWindow(owner, MONITOR_DEFAULTTOPRIMARY), &monitor)) {
        const auto work = monitor.rcWork;
        RECT anchor = work;
        if (owner) GetWindowRect(owner, &anchor);
        x = std::clamp<int>((anchor.left + anchor.right - outerWidth) / 2, work.left, std::max<int>(work.left, work.right - outerWidth));
        y = std::clamp<int>((anchor.top + anchor.bottom - outerHeight) / 2, work.top, std::max<int>(work.top, work.bottom - outerHeight));
    }
    auto hwnd = CreateWindowExW(WS_EX_CONTROLPARENT, name, title, style, x, y,
        outerWidth, outerHeight, owner, nullptr, instance, nullptr);
    if (hwnd) darkTitle(hwnd);
    return hwnd;
}

void fill(HDC dc, const RECT& area, COLORREF color) {
    SetDCBrushColor(dc, color);
    FillRect(dc, &area, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
}

void roundBox(HDC dc, const RECT& area, int radius, COLORREF color, COLORREF outline) {
    const auto oldPen = SelectObject(dc, GetStockObject(DC_PEN));
    const auto oldBrush = SelectObject(dc, GetStockObject(DC_BRUSH));
    SetDCPenColor(dc, outline);
    SetDCBrushColor(dc, color);
    RoundRect(dc, area.left, area.top, area.right, area.bottom, px(radius), px(radius));
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
}

void drawText(HDC dc, const std::wstring& text, RECT bounds, HFONT face, COLORREF color,
              UINT format = DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX) {
    const auto old = SelectObject(dc, face);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &bounds, format);
    SelectObject(dc, old);
}

LRESULT CALLBACK trackButton(HWND hwnd, UINT message, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR) {
    if (message == WM_MOUSEMOVE && hoveredButton != hwnd) {
        if (hoveredButton) InvalidateRect(hoveredButton, nullptr, FALSE);
        hoveredButton = hwnd;
        TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, hwnd, 0};
        TrackMouseEvent(&tracking);
        InvalidateRect(hwnd, nullptr, FALSE);
    }
    if ((message == WM_MOUSELEAVE || message == WM_NCDESTROY) && hoveredButton == hwnd) {
        hoveredButton = nullptr;
        InvalidateRect(hwnd, nullptr, FALSE);
    }
    if (message == WM_NCDESTROY) RemoveWindowSubclass(hwnd, trackButton, id);
    return DefSubclassProc(hwnd, message, wp, lp);
}

HWND control(const wchar_t* type, const wchar_t* label, DWORD style, int x, int y, int width, int height, int id = 0) {
    const std::wstring_view kind(type);
    const bool edit = kind == L"EDIT";
    const bool clickable = kind == L"BUTTON";
    if (clickable) style = (style & ~BS_TYPEMASK) | BS_OWNERDRAW;
    if (edit) style |= WS_BORDER;
    HWND out = CreateWindowExW(0, type, label, WS_CHILD | WS_VISIBLE | style,
        px(x), px(y), px(width), px(height), window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(out, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
    if (clickable) SetWindowSubclass(out, trackButton, 1, 0);
    if (edit || kind == L"LISTBOX") SetWindowTheme(out, L"DarkMode_Explorer", nullptr);
    if (edit) SendMessageW(out, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(px(10), px(10)));
    return out;
}

HWND button(const wchar_t* label, int x, int y, int width, int id, int height = 34) {
    return control(L"BUTTON", label, WS_TABSTOP, x, y, width, height, id);
}

void paintButton(const DRAWITEMSTRUCT& item) {
    const bool nav = item.CtlID >= NavigationPage && item.CtlID <= ConsolePage;
    const bool current = nav && static_cast<int>(item.CtlID) == selectedPage;
    const bool hover = hoveredButton == item.hwndItem;
    const bool pressed = (item.itemState & ODS_SELECTED) != 0;
    const bool disabled = (item.itemState & ODS_DISABLED) != 0;
    const HWND parent = GetParent(item.hwndItem);
    const bool mainPanel = parent == window;
    const bool stop = item.CtlID == Stop || item.CtlID == MapStop;
    const bool primary = item.CtlID == Send || item.CtlID == Go || item.CtlID == Mine ||
        item.CtlID == Preview || item.CtlID == TaskRun || item.CtlID == FlowApply || item.CtlID == MapGo;
    auto bg = nav ? sidebar : RGB(24, 34, 48);
    auto edge = nav ? sidebar : border;
    auto fg = foreground;
    if (current || primary) { bg = RGB(22, 59, 78); edge = RGB(34, 106, 132); fg = accent; }
    if (stop) { bg = RGB(54, 30, 43); edge = RGB(102, 49, 64); fg = danger; }
    if (hover && !disabled) {
        bg = (current || primary) ? RGB(30, 76, 96) : stop ? RGB(75, 36, 53) : RGB(35, 49, 68);
        if (!current && !primary && !stop) edge = RGB(64, 86, 110);
    }
    if (pressed) bg = inputBackground;
    if (disabled) { bg = inputBackground; edge = border; fg = muted; }
    const bool inCard = mainPanel && item.CtlID != Pause && item.CtlID != Resume && item.CtlID != Stop;
    fill(item.hDC, item.rcItem, nav ? sidebar : inCard ? surface : background);
    roundBox(item.hDC, item.rcItem, 10, bg, edge);
    auto label = item.rcItem;
    UINT flags = DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX;
    if (nav) {
        constexpr std::array<const wchar_t*, 4> symbols{L"\uE707", L"\uE7B8", L"\uE9D9", L"\uE756"};
        auto icon = label;
        icon.left += px(14);
        icon.right = icon.left + px(24);
        drawText(item.hDC, symbols[item.CtlID - NavigationPage], icon, iconFont, current ? accent : muted, flags | DT_CENTER);
        label.left += px(50);
        label.right -= px(8);
        if (current) {
            auto marker = item.rcItem;
            marker.right = marker.left + px(3);
            marker.top += px(9);
            marker.bottom -= px(9);
            fill(item.hDC, marker, accent);
        }
    } else {
        label.left += px(5);
        label.right -= px(5);
        flags |= DT_CENTER;
    }
    drawText(item.hDC, value(item.hwndItem), label, boldFont, fg, flags);
    if ((item.itemState & ODS_FOCUS) && !(item.itemState & ODS_NOFOCUSRECT)) {
        auto focus = item.rcItem;
        InflateRect(&focus, -px(3), -px(3));
        DrawFocusRect(item.hDC, &focus);
    }
}

std::optional<LRESULT> themeMessage(UINT message, WPARAM wp, LPARAM lp) {
    if (message == WM_DRAWITEM) {
        const auto& item = *reinterpret_cast<const DRAWITEMSTRUCT*>(lp);
        if (item.CtlType == ODT_BUTTON) { paintButton(item); return TRUE; }
    }
    if (message == WM_CTLCOLOREDIT || message == WM_CTLCOLORLISTBOX || message == WM_CTLCOLORSTATIC) {
        HDC dc = reinterpret_cast<HDC>(wp);
        wchar_t kind[16]{};
        GetClassNameW(reinterpret_cast<HWND>(lp), kind, 16);
        const bool editor = message != WM_CTLCOLORSTATIC || std::wstring_view(kind) == L"Edit";
        SetTextColor(dc, editor ? foreground : muted);
        SetBkColor(dc, editor ? inputBackground : background);
        return reinterpret_cast<LRESULT>(editor ? inputBrush : backgroundBrush);
    }
    return {};
}
