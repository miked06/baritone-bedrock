// Included after native panel helpers. The map follows the player; it depicts
// chunk knowledge rather than terrain heights. Only Go issues a navigation job.
HWND mapWindow=nullptr,mapMessage=nullptr;
std::optional<baritone::MapFrame> mapFrame;
std::optional<baritone::BlockPos> mapDestination;
std::future<std::string> mapJob;
enum {MapGo=750,MapStop};
LRESULT CALLBACK mapProc(HWND hwnd,UINT message,WPARAM wp,LPARAM lp) {
    constexpr int left=16,top=88,cell=20,side=baritone::MapFrame::side,extent=side*cell;
    if(message==WM_CLOSE){DestroyWindow(hwnd);return 0;}
    if(message==WM_DESTROY){KillTimer(hwnd,1);mapWindow=nullptr;return 0;}
    if(message==WM_TIMER) {
        if(mapJob.valid() && mapJob.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
            try {
                auto next=baritone::MapFrame::decode(mapJob.get());
                if(!mapFrame || !(next.identity==mapFrame->identity)){mapDestination.reset();EnableWindow(GetDlgItem(hwnd,MapGo),FALSE);}
                mapFrame=std::move(next);SetWindowTextW(mapMessage,wide(mapFrame->identity.name+" | Dimension "+std::to_string(mapFrame->identity.dimension)+" | Click to select a destination").c_str());
            } catch(const std::exception&) {mapFrame.reset();mapDestination.reset();EnableWindow(GetDlgItem(hwnd,MapGo),FALSE);SetWindowTextW(mapMessage,L"Map unavailable. Connect to a supported Minecraft world.");}
            InvalidateRect(hwnd,nullptr,FALSE);
        }
        if(!mapJob.valid())mapJob=std::async(std::launch::async,[pid=minecraftPid]{return transact(pid,"@map",true);});
        return 0;
    }
    if(message==WM_LBUTTONDOWN && mapFrame) {
        const int x=int(short(LOWORD(lp)))-left,z=int(short(HIWORD(lp)))-top;
        if(x>=0 && z>=0 && x<extent && z<extent) {
            const int ox=((mapFrame->player.x>>4)-baritone::MapFrame::radius)*16,oz=((mapFrame->player.z>>4)-baritone::MapFrame::radius)*16;
            mapDestination=baritone::BlockPos{ox+x*16/cell,mapFrame->player.y,oz+z*16/cell};
            EnableWindow(GetDlgItem(hwnd,MapGo),TRUE);
            SetWindowTextW(mapMessage,wide("Destination X "+std::to_string(mapDestination->x)+", Z "+std::to_string(mapDestination->z)+". Click Go to walk there.").c_str());InvalidateRect(hwnd,nullptr,FALSE);
        }
        return 0;
    }
    if(message==WM_COMMAND && HIWORD(wp)==BN_CLICKED) {
        if(LOWORD(wp)==MapStop)send("#stop");
        if(LOWORD(wp)==MapGo && mapFrame && mapDestination) {
            std::ostringstream request;request<<"#mapgoto "<<std::quoted(mapFrame->identity.name)<<' '<<mapFrame->identity.dimension<<' '<<mapDestination->x<<' '<<mapDestination->z;send(request.str());
        }
        return 0;
    }
    if(message==WM_PAINT) {
        PAINTSTRUCT paint;HDC screen=BeginPaint(hwnd,&paint);RECT bounds;GetClientRect(hwnd,&bounds);
        HDC dc=CreateCompatibleDC(screen);HBITMAP bitmap=CreateCompatibleBitmap(screen,bounds.right,bounds.bottom);const auto oldBitmap=SelectObject(dc,bitmap);FillRect(dc,&bounds,GetSysColorBrush(COLOR_WINDOW));
        if(mapFrame) {
            const int saved=SaveDC(dc);IntersectClipRect(dc,left,top,left+extent,top+extent);
            const int ox=((mapFrame->player.x>>4)-baritone::MapFrame::radius)*16,oz=((mapFrame->player.z>>4)-baritone::MapFrame::radius)*16;
            auto px=[&](int x){return left+int((int64_t(x)-ox)*cell/16);};auto pz=[&](int z){return top+int((int64_t(z)-oz)*cell/16);};
            HBRUSH colors[]={CreateSolidBrush(RGB(35,38,44)),CreateSolidBrush(RGB(105,111,119)),CreateSolidBrush(RGB(57,121,83))};
            for(int z=0;z<side;++z)for(int x=0;x<side;++x){RECT tile{left+x*cell,top+z*cell,left+(x+1)*cell-1,top+(z+1)*cell-1};FillRect(dc,&tile,colors[mapFrame->chunks[z*side+x]-'0']);}
            for(auto brush:colors)DeleteObject(brush);
            auto oldBrush=SelectObject(dc,GetStockObject(NULL_BRUSH));HPEN zonePen=CreatePen(PS_SOLID,2,RGB(235,88,86));auto oldPen=SelectObject(dc,zonePen);
            for(const auto& zone:mapFrame->zones){const auto low=zone.min(),high=zone.max();Rectangle(dc,px(low.x),pz(low.z),px(high.x+1),pz(high.z+1));}
            SelectObject(dc,oldPen);DeleteObject(zonePen);SelectObject(dc,oldBrush);
            HPEN pathPen=CreatePen(PS_SOLID,2,RGB(80,175,255));oldPen=SelectObject(dc,pathPen);
            bool first=true;for(const auto pos:mapFrame->path){if(first)MoveToEx(dc,px(pos.x),pz(pos.z),nullptr);else LineTo(dc,px(pos.x),pz(pos.z));first=false;}
            SelectObject(dc,oldPen);DeleteObject(pathPen);
            SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(255,255,255));SelectObject(dc,font);
            for(const auto& marker:mapFrame->markers) {
                const int x=px(marker.position.x),z=pz(marker.position.z);RECT dot{x-3,z-3,x+4,z+4};FillRect(dc,&dot,GetSysColorBrush(COLOR_HIGHLIGHT));
                const auto label=wide(std::string(1,marker.kind)+": "+marker.label);TextOutW(dc,x+5,z-8,label.c_str(),int(label.size()));
            }
            const int x=px(mapFrame->player.x),z=pz(mapFrame->player.z);RECT player{x-4,z-4,x+5,z+5};FillRect(dc,&player,GetSysColorBrush(COLOR_HIGHLIGHTTEXT));
            if(mapDestination){const int tx=px(mapDestination->x),tz=pz(mapDestination->z);HPEN pen=CreatePen(PS_SOLID,2,RGB(255,220,30));oldPen=SelectObject(dc,pen);MoveToEx(dc,tx-5,tz-5,nullptr);LineTo(dc,tx+5,tz+5);MoveToEx(dc,tx-5,tz+5,nullptr);LineTo(dc,tx+5,tz-5);SelectObject(dc,oldPen);DeleteObject(pen);}
            RestoreDC(dc,saved);
        }
        BitBlt(screen,left,top,extent,extent,dc,left,top,SRCCOPY);SelectObject(dc,oldBitmap);DeleteObject(bitmap);DeleteDC(dc);EndPaint(hwnd,&paint);return 0;
    }
    return DefWindowProcW(hwnd,message,wp,lp);
}
void openMap() {
    if(mapWindow){ShowWindow(mapWindow,SW_SHOW);SetForegroundWindow(mapWindow);return;}
    WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"BaritoneMap";cls.lpfnWndProc=mapProc;cls.hCursor=LoadCursorW(nullptr,IDC_CROSS);cls.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);RegisterClassW(&cls);
    mapWindow=CreateWindowExW(0,cls.lpszClassName,L"Baritone Bedrock - Map",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,550,780,window,nullptr,cls.hInstance,nullptr);
    const auto previous=window;window=mapWindow;
    mapMessage=control(L"STATIC",L"Loading map...",0,16,12,500,30);button(L"Go to selection",16,48,160,MapGo);button(L"Stop",186,48,100,MapStop);
    EnableWindow(GetDlgItem(mapWindow,MapGo),FALSE);
    control(L"STATIC",L"Green: loaded | Gray: visited this session | Dark: unknown\r\nRed: protected area at any height | Blue: path | White: player\r\nS: storage | P: portal | W: waypoint | B: building\r\nNorth is up. Map follows the player; no terrain heights are shown.",0,16,602,500,100);
    window=previous;SetTimer(mapWindow,1,1000,nullptr);ShowWindow(mapWindow,SW_SHOW);
}
