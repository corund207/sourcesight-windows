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
void checkVisibilityUnit() {
    // The live flicker was a sensor-actuator loop: GetCursorInfo observes the
    // overlay itself, so hiding on "cursor visible" toggled the next snapshot.
    // These checks fail on the old debounce logic and pass only when cursor
    // noise causes zero visibility transitions.
    OverlayVisibility stable;
    require(stable.Update(true,false,false,0),"gameplay starts visible");
    require(stable.Transitions()==0,"no transition on first visible snapshot");
    for(int i=0;i<50;++i) {
        const bool cursor=(i%2)==0;
        const bool visible=stable.Update(true,false,cursor,static_cast<std::uint64_t>(10+i*5));
        require(visible,"cursor noise never hides stable ESP");
    }
    require(stable.Transitions()==0,"oscillating cursor causes no hide/show cycle");
    require(stable.Update(true,true,true,1000),"SourceSight menu stays visible with cursor");
    require(stable.Update(true,true,false,1010),"SourceSight menu stays visible without cursor");
    require(stable.Transitions()==0,"menu open causes no visibility transition");
    // Game cursor menus keep ESP visible; clicks pass via WS_EX_TRANSPARENT.
    require(stable.Update(true,false,true,1020),"game cursor menus keep ESP visible");
    require(stable.Transitions()==0,"visible cursor no longer hides ESP");
    require(!stable.Update(false,false,false,1030),"focus loss hides immediately");
    require(stable.Transitions()==1,"focus loss is exactly one transition");
    require(!stable.Update(false,false,true,1040),"unfocused stays hidden through cursor noise");
    require(stable.Transitions()==1,"unfocused cursor noise causes no extra transition");
    require(stable.Update(true,false,true,1050),"focus gain restores even with cursor visible");
    require(stable.Transitions()==2,"focus gain is the second transition");
    require(stable.Update(true,false,false,1060),"gameplay capture stays visible");
    require(stable.Transitions()==2,"stable gameplay adds no further transitions");
}
}

int main() {
    HWND background=nullptr;HBRUSH brush=nullptr;
    bool imgui=false;
    LogHelper::Init();
    try {
        checkVisibilityUnit();
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
            // Native styles genuinely support cross-process passthrough:
            // WS_EX_LAYERED is retained, WS_EX_TRANSPARENT follows the input
            // mode for system-wide mouse routing, and the GLFW "GLFW30"
            // (CS_OWNDC) window subclass answers HTTRANSPARENT to WM_NCHITTEST.
            // WindowFromPoint is thread-sensitive and cannot prove this, so
            // input is verified via WM_NCHITTEST from both UI threads while
            // the overlay stays visible.
            const auto exstyle=GetWindowLongPtrW(Window::hwnd,GWL_EXSTYLE);
            require((exstyle&WS_EX_LAYERED)!=0,"layered style retained for composition");
            require(((exstyle&WS_EX_TRANSPARENT)!=0)==passthrough,"transparent style follows menu input mode");
            require(((exstyle&WS_EX_NOACTIVATE)!=0)==passthrough,"noactivate follows passthrough mode");
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
            // Same-thread mouse routing follows the input mode without hiding.
            const POINT pt{rect.left+180,rect.top+60};
            const LPARAM hit_param=MAKELPARAM(pt.x,pt.y);
            const LRESULT hit_same=SendMessageW(Window::hwnd,WM_NCHITTEST,0,hit_param);
            require((hit_same==HTTRANSPARENT)==passthrough,"WM_NCHITTEST follows menu input mode");
            // A game queries from a different UI thread. SendMessage still
            // reaches the overlay WndProc on its owning thread, proving the
            // HTTRANSPARENT answer (and WS_EX_TRANSPARENT style) work
            // cross-thread without hiding the window.
            auto external_hit=std::async(std::launch::async,[&] {
                return SendMessageW(Window::hwnd,WM_NCHITTEST,0,hit_param);
            });
            while(external_hit.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready) {
                MSG message{};
                while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
                    TranslateMessage(&message);DispatchMessageW(&message);
                }
                Sleep(1);
            }
            require((external_hit.get()==HTTRANSPARENT)==passthrough,
                    "cross-thread hit testing follows menu input mode");
            // Oscillating cursor snapshots must not produce a hide/show cycle
            // while focused. This is the live flicker regression.
            require((IsWindowVisible(Window::hwnd)!=FALSE),"overlay visible while focused before cursor noise");
            const unsigned before_transitions=Window::VisibilityTransitions();
            for(int i=0;i<20;++i) {
                Window::UpdateGameplayVisibility(true,!passthrough,(i%2)==0);
                Sleep(5);
            }
            require((IsWindowVisible(Window::hwnd)!=FALSE),"cursor noise causes no hide/show cycle");
            require(Window::VisibilityTransitions()==before_transitions,
                    "cursor noise causes no visibility transitions");
            // Game cursor menus keep ESP visible; clicks route via passthrough.
            Window::UpdateGameplayVisibility(true,!passthrough,true);
            Sleep(90);
            Window::UpdateGameplayVisibility(true,!passthrough,true);
            require((IsWindowVisible(Window::hwnd)!=FALSE),
                    "game cursor keeps ESP visible; menus clickable via passthrough");
            Window::UpdateGameplayVisibility(true,!passthrough,false);
            Sleep(10);
            Window::UpdateGameplayVisibility(true,!passthrough,false);
            require(IsWindowVisible(Window::hwnd)!=FALSE,"overlay stable when game captures cursor");
            if(passthrough) {
                SetActiveWindow(background);
                require(GetActiveWindow()==background,"activate underlying fixture");
                const auto foreground=GetForegroundWindow();
                Window::SetVisible(false);Window::SetVisible(true);
                require(GetActiveWindow()==background && GetForegroundWindow()==foreground,
                        "showing overlay preserves underlying keyboard focus");
                // Restore visible state for the next iteration.
                Window::UpdateGameplayVisibility(true,false,false);
                require((IsWindowVisible(Window::hwnd)!=FALSE),"overlay restored after focus check");
            }
        }
        // Focus loss/gain remain the only legitimate hide/show transitions.
        Window::UpdateGameplayVisibility(false,false,false);
        require((IsWindowVisible(Window::hwnd)==FALSE),"focus loss hides overlay");
        Window::UpdateGameplayVisibility(true,false,false);
        require((IsWindowVisible(Window::hwnd)!=FALSE),"focus gain restores overlay");
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
