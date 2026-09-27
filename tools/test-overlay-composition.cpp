#include "common.hpp"
#include "gui/frontend/menu/Menu.hpp"
#include "gui/renderer/window/Window.hpp"
#include "gui/renderer/window/OverlayVisibility.hpp"
#include <dwmapi.h>
#include <iostream>

namespace {
constexpr COLORREF background_color=RGB(37,113,181);
void require(bool ok,const char* message) {
    if(!ok)throw std::runtime_error(message);
}
COLORREF desktopPixel(int x,int y) {
    HDC desktop=GetDC(nullptr),copy=CreateCompatibleDC(desktop);
    HBITMAP bitmap=CreateCompatibleBitmap(desktop,1,1);
    auto previous=SelectObject(copy,bitmap);
    const bool captured=BitBlt(copy,0,0,1,1,desktop,x,y,SRCCOPY|CAPTUREBLT)!=0;
    const auto color=captured?GetPixel(copy,0,0):CLR_INVALID;
    SelectObject(copy,previous);DeleteObject(bitmap);DeleteDC(copy);ReleaseDC(nullptr,desktop);
    return color;
}
bool nearColor(COLORREF actual,COLORREF expected) {
    return actual!=CLR_INVALID && std::abs(int(GetRValue(actual))-int(GetRValue(expected)))<=3 &&
           std::abs(int(GetGValue(actual))-int(GetGValue(expected)))<=3 &&
           std::abs(int(GetBValue(actual))-int(GetBValue(expected)))<=3;
}
}

int main() {
    HWND background=nullptr;HBRUSH brush=nullptr;
    bool imgui=false;
    LogHelper::Init();
    try {
        OverlayVisibility stable;
        require(stable.Update(true,false,false,0),"gameplay starts visible");
        require(stable.Update(true,false,true,10),"brief cursor appearance does not hide ESP");
        require(stable.Update(true,false,false,30),"cursor noise does not flicker ESP");
        require(stable.Update(true,false,true,100),"menu transition waits for stable cursor");
        require(!stable.Update(true,false,true,175),"stable game menu hides ESP");
        require(!stable.Update(true,false,false,200),"brief capture does not flash ESP over menus");
        require(!stable.Update(true,false,true,220),"menu remains hidden through capture noise");
        require(!stable.Update(true,false,false,300),"capture settling begins");
        require(stable.Update(true,false,false,500),"stable gameplay restores ESP");
        require(!stable.Update(false,false,false,501),"focus loss hides immediately");
        require(stable.Update(true,true,true,502),"SourceSight menu opens immediately");
        Menu::SetPreviewMode(false);
        cfg::esp::wireframe=true;
        const auto monitor=MonitorFromPoint(POINT{0,0},MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO info{sizeof(info)};
        require(GetMonitorInfoW(monitor,&info)!=0,"get monitor bounds");
        const auto rect=info.rcMonitor;
        brush=CreateSolidBrush(background_color);
        WNDCLASSW cls{};cls.lpfnWndProc=DefWindowProcW;cls.hInstance=GetModuleHandleW(nullptr);
        cls.hbrBackground=brush;cls.lpszClassName=L"SourceSightCompositionFixture";
        require(RegisterClassW(&cls)!=0,"register background fixture");
        background=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW,cls.lpszClassName,L"Overlay transparency test",
            WS_POPUP,rect.left,rect.top,rect.right-rect.left,rect.bottom-rect.top,
            nullptr,nullptr,cls.hInstance,nullptr);
        require(background!=nullptr,"create background fixture");
        ShowWindow(background,SW_SHOWNOACTIVATE);UpdateWindow(background);DwmFlush();Sleep(200);
        require(nearColor(desktopPixel(rect.left+120,rect.top+120),background_color),"fixture visible before overlay");
        require(Window::SpawnWindow(),"create native overlay");
        require(Window::CreateDevice(),"initialize OpenGL");
        require(Window::CreateImGui(),"initialize ImGui");imgui=true;
        Window::SetBounds(rect);
        for(bool passthrough:{true,false,true,false,true}) {
            Window::SetClickthrough(Window::hwnd,passthrough);
            for(int frame=0;frame<6;++frame) {
                Window::SetTopMost(Window::hwnd);
                Window::StartRender();
                require(ImGui::GetIO().DisplaySize.x==float(rect.right-rect.left),"render coordinates match game width");
                auto* draw=ImGui::GetForegroundDrawList();
                draw->AddRect({40,40},{80,80},IM_COL32(230,50,20,255),0.f,0,4.f);
                draw->AddRectFilled({160,40},{200,80},IM_COL32(230,50,20,128));
                Window::EndRender();DwmFlush();Sleep(30);
            }
            const auto empty=desktopPixel(rect.left+120,rect.top+120);
            const auto drawn=desktopPixel(rect.left+40,rect.top+60);
            std::cout<<"passthrough="<<passthrough<<" background="<<empty<<" overlay="<<drawn<<'\n';
            require(nearColor(drawn,RGB(230,50,20)),"overlay drawing visible over fixture");
            require(nearColor(empty,background_color),"empty overlay pixels leave desktop visible");
            require(nearColor(desktopPixel(rect.left+60,rect.top+60),background_color),"ESP box interior stays transparent");
            require(nearColor(desktopPixel(rect.left+180,rect.top+60),RGB(134,81,100)),"translucent graphics blend with desktop");
            const auto hit=WindowFromPoint(POINT{rect.left+180,rect.top+60});
            require(hit==(passthrough?background:Window::hwnd),"mouse hit testing follows menu input mode");
            // A game queries from a different UI thread. HTTRANSPARENT alone
            // only skips windows belonging to the calling thread.
            Window::UpdateGameplayVisibility(true,!passthrough,true);
            Sleep(90);
            Window::UpdateGameplayVisibility(true,!passthrough,true);
            require((IsWindowVisible(Window::hwnd)!=FALSE)==!passthrough,
                    "game cursor hides ESP but leaves the SourceSight menu visible");
            auto external_hit=std::async(std::launch::async,[&] {
                return WindowFromPoint(POINT{rect.left+180,rect.top+60});
            });
            while(external_hit.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready) {
                MSG message{};
                while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
                    TranslateMessage(&message);DispatchMessageW(&message);
                }
                Sleep(1);
            }
            require(external_hit.get()==(passthrough?background:Window::hwnd),
                    "game cursor menus reach the underlying window from another thread");
            Window::UpdateGameplayVisibility(true,!passthrough,false);
            Sleep(220);
            Window::UpdateGameplayVisibility(true,!passthrough,false);
            require(IsWindowVisible(Window::hwnd)!=FALSE,"overlay returns when game captures cursor");
            if(passthrough) {
                SetActiveWindow(background);
                require(GetActiveWindow()==background,"activate underlying fixture");
                const auto foreground=GetForegroundWindow();
                Window::SetVisible(false);Window::SetVisible(true);
                require(GetActiveWindow()==background && GetForegroundWindow()==foreground,
                        "showing overlay preserves underlying keyboard focus");
            }
        }
        Window::DestroyImGui();imgui=false;Window::DestroyDevice();Window::DespawnWindow();
        DestroyWindow(background);DeleteObject(brush);LogHelper::Destroy();
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        if(imgui)Window::DestroyImGui();
        Window::DespawnWindow();
        if(background)DestroyWindow(background);
        if(brush)DeleteObject(brush);
        LogHelper::Destroy();return 1;
    }
}
