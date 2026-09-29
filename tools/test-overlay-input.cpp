// Independent native-input regression. Only the overlay-under-test uses the
// production renderer. The receiver is a separate process with a Win32 queue.
// Never synthesize WM_* mouse messages: SendInput must traverse USER32 routing.
#include "common.hpp"
#include "gui/frontend/menu/Menu.hpp"
#include "gui/renderer/window/Window.hpp"
#include <dwmapi.h>
#include <windowsx.h>
#include <stdexcept>

namespace {
constexpr UINT query=WM_APP+1;
constexpr ULONG_PTR marker=0x53534954;
unsigned downs=0,ups=0,buttons=0;
DWORD controller_pid=0;
WNDPROC previous=nullptr;
void require(bool ok,const char* why) { if(!ok)throw std::runtime_error(why); }
LRESULT CALLBACK receiver(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    if(msg==WM_ACTIVATE && LOWORD(wp)!=WA_INACTIVE)AllowSetForegroundWindow(controller_pid);
    if(msg==WM_LBUTTONDOWN && GetMessageExtraInfo()==marker)++downs;
    if(msg==WM_LBUTTONUP && GetMessageExtraInfo()==marker)++ups;
    if(msg==query) {
        if(wp==0)return downs;
        if(wp==1)return ups;
        if(wp==2)return reinterpret_cast<LRESULT>(WindowFromPoint(POINT{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)}));
    }
    if(msg==WM_DESTROY) { PostQuitMessage(0);return 0; }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
LRESULT CALLBACK observe(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    if(msg==WM_LBUTTONDOWN && GetMessageExtraInfo()==marker)++downs;
    if(msg==WM_LBUTTONUP && GetMessageExtraInfo()==marker)++ups;
    return CallWindowProcW(previous,hwnd,msg,wp,lp);
}
int fixture(const wchar_t* name,bool fullscreen) {
    controller_pid=std::stoul(std::wstring(name).substr(std::wstring(name).find_last_of(L' ')+1));
    WNDCLASSW wc{};wc.lpfnWndProc=receiver;wc.hInstance=GetModuleHandleW(nullptr);
    wc.lpszClassName=L"SourceSightIndependentInputReceiver";
    wc.hCursor=LoadCursorW(nullptr,MAKEINTRESOURCEW(32512));wc.hbrBackground=CreateSolidBrush(RGB(37,113,181));
    require(RegisterClassW(&wc)!=0,"register receiver");
    const int w=fullscreen?GetSystemMetrics(SM_CXSCREEN):640;
    const int h=fullscreen?GetSystemMetrics(SM_CYSCREEN):400;
    HWND hwnd=CreateWindowExW(WS_EX_TOPMOST,wc.lpszClassName,name,WS_POPUP,
        fullscreen?0:100,fullscreen?0:100,w,h,nullptr,nullptr,wc.hInstance,nullptr);
    require(hwnd!=nullptr,"create receiver");
    ShowWindow(hwnd,SW_SHOW);UpdateWindow(hwnd);
    // Parent PID in window property isn't needed: parent verifies process ID.
    MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0) { TranslateMessage(&msg);DispatchMessageW(&msg); }
    DeleteObject(wc.hbrBackground);return 0;
}
LRESULT ask(HWND hwnd,WPARAM what,LPARAM arg=0) {
    DWORD_PTR result=0;
    require(SendMessageTimeoutW(hwnd,query,what,arg,SMTO_ABORTIFHUNG,2000,&result)!=0,"receiver did not answer");
    return static_cast<LRESULT>(result);
}
void frame() {
    Window::StartRender();
    ImGui::SetNextWindowPos({20,20});ImGui::SetNextWindowSize({300,160});
    ImGui::Begin("Independent input test",nullptr,ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoResize);
    ImGui::TextUnformatted("Clicks must reach exactly one process.");
    ImGui::SetCursorPos({20,60});
    if(ImGui::Button("Test button",{180,50}))++buttons;
    ImGui::End();
    auto* draw=ImGui::GetForegroundDrawList();
    draw->AddRectFilled({400,200},{420,220},IM_COL32(230,50,20,255));
    draw->AddRectFilled({440,200},{460,220},IM_COL32(230,50,20,128));
    Window::EndRender();
}
void checkPixels(const RECT& bounds) {
    DwmFlush();
    HDC screen=GetDC(nullptr),copy=CreateCompatibleDC(screen);
    HBITMAP bitmap=CreateCompatibleBitmap(screen,1,1);
    auto old=SelectObject(copy,bitmap);
    const POINT samples[]={{410,210},{450,210},{500,210}};
    const COLORREF expected[]={RGB(230,50,20),RGB(134,81,100),RGB(37,113,181)};
    bool ok=true;
    for(int i=0;i<3;++i) {
        const bool copied=BitBlt(copy,0,0,1,1,screen,bounds.left+samples[i].x,bounds.top+samples[i].y,SRCCOPY|CAPTUREBLT)!=0;
        const auto actual=GetPixel(copy,0,0);
        const bool matched=copied && actual!=CLR_INVALID && abs(int(GetRValue(actual))-int(GetRValue(expected[i])))<=3
              && abs(int(GetGValue(actual))-int(GetGValue(expected[i])))<=3
              && abs(int(GetBValue(actual))-int(GetBValue(expected[i])))<=3;
        if(!matched)std::cerr<<"pixel sample="<<i<<" expected="<<expected[i]<<" actual="<<actual
                            <<" foreground="<<GetForegroundWindow()<<std::endl;
        ok &= matched;
    }
    SelectObject(copy,old);DeleteObject(bitmap);DeleteDC(copy);ReleaseDC(nullptr,screen);
    require(ok,"desktop pixels do not preserve opaque/translucent/empty overlay composition");
}
void pump(unsigned milliseconds) {
    const auto end=GetTickCount64()+milliseconds;
    do { frame();Sleep(1); } while(GetTickCount64()<end);
}
struct Session {
    PROCESS_INFORMATION child{};HWND target=nullptr;bool spawned=false,imgui=false;
    POINT cursor{};HWND foreground=GetForegroundWindow();
    Session() { GetCursorPos(&cursor); }
    ~Session() {
        const bool still_ours=GetForegroundWindow()==target || GetForegroundWindow()==Window::hwnd;
        if(Window::hwnd && previous)SetWindowLongPtrW(Window::hwnd,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(previous));
        if(imgui)Window::DestroyImGui();
        if(spawned) { Window::DestroyDevice();Window::DespawnWindow(); }
        if(target)PostMessageW(target,WM_CLOSE,0,0);
        if(child.hProcess) { WaitForSingleObject(child.hProcess,3000);CloseHandle(child.hProcess);CloseHandle(child.hThread); }
        if(still_ours) {
            SetCursorPos(cursor.x,cursor.y);
            if(IsWindow(foreground))SetForegroundWindow(foreground);
        }
    }
};
void click(HWND fixture,POINT point) {
    const HWND fg=GetForegroundWindow();
    require(fg==fixture || fg==Window::hwnd,"desktop focus changed; refusing test input");
    const HWND hit=WindowFromPoint(point);
    require(hit==fixture || hit==Window::hwnd,"point covered by another app; refusing test input");
    RECT clip{};require(GetClipCursor(&clip)!=0 && PtInRect(&clip,point),"test point outside current cursor clip");
    require(!(GetAsyncKeyState(VK_LBUTTON)&0x8000),"user is holding mouse button; refusing input");
    require(SetCursorPos(point.x+2,point.y+2)!=0,"establish mouse motion after focus change");
    pump(20);
    require(SetCursorPos(point.x,point.y)!=0,"move test cursor");
    pump(20);
    INPUT in{};in.type=INPUT_MOUSE;in.mi.dwExtraInfo=marker;in.mi.dwFlags=MOUSEEVENTF_LEFTDOWN;
    require(SendInput(1,&in,sizeof(in))==1,"queue test mouse down");
    pump(30); // Give ImGui a frame with the button held, then release it.
    in.mi.dwFlags=MOUSEEVENTF_LEFTUP;
    require(SendInput(1,&in,sizeof(in))==1,"queue test mouse up");
    pump(50);
}
void run(bool fullscreen) {
    Session session;
    wchar_t exe[MAX_PATH]{};require(GetModuleFileNameW(nullptr,exe,MAX_PATH)!=0,"locate harness");
    const auto title=L"SourceSight input receiver "+std::to_wstring(GetCurrentProcessId());
    std::wstring command=L"\""+std::wstring(exe)+L"\" --fixture \""+title+L"\""+(fullscreen?L" --fullscreen":L"");
    STARTUPINFOW startup{sizeof(startup)};
    require(CreateProcessW(exe,command.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&startup,&session.child)!=0,"launch receiver process");
    const auto deadline=GetTickCount64()+5000;
    while(!session.target && GetTickCount64()<deadline) {
        session.target=FindWindowW(L"SourceSightIndependentInputReceiver",title.c_str());Sleep(10);
    }
    require(session.target!=nullptr,"receiver ready");
    DWORD pid=0;const DWORD target_thread=GetWindowThreadProcessId(session.target,&pid);
    require(pid==session.child.dwProcessId && pid!=GetCurrentProcessId(),"receiver must belong to separate process");
    require(target_thread!=GetCurrentThreadId(),"receiver must own separate UI thread");
    SetForegroundWindow(session.target);
    std::cout<<"Waiting for receiver foreground: "<<session.target<<std::endl;
    const auto focus_deadline=GetTickCount64()+30000;
    while(GetForegroundWindow()!=session.target && GetTickCount64()<focus_deadline)Sleep(50);
    require(GetForegroundWindow()==session.target,"receiver foreground unavailable");
    Menu::SetPreviewMode(false);
    require(Window::SpawnWindow(),"spawn production overlay");session.spawned=true;
    require(Window::CreateDevice(),"create production GL device");
    require(Window::CreateImGui(),"initialize production ImGui backend");session.imgui=true;
    previous=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(Window::hwnd,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(observe)));
    RECT bounds{};GetWindowRect(session.target,&bounds);Window::SetBounds(bounds);
    pump(100);
    RECT overlay_bounds{};GetWindowRect(Window::hwnd,&overlay_bounds);
    require(overlay_bounds.right-overlay_bounds.left==bounds.right-bounds.left+1,"one-pixel composition padding lost");
    const POINT point{bounds.left+100,bounds.top+100};
    Window::SetVisible(false);
    click(session.target,point);
    std::cout<<"positive control without overlay: "<<ask(session.target,0)<<'/'<<ask(session.target,1)<<std::endl;
    require(ask(session.target,0)==1 && ask(session.target,1)==1,"test environment cannot deliver clicks without overlay");
    Window::SetVisible(true);
    COLORREF key=0;BYTE alpha=0;DWORD flags=0;
    const BOOL attributes=GetLayeredWindowAttributes(Window::hwnd,&key,&alpha,&flags);
    std::cout<<"receiver_pid="<<pid<<" class_style="<<GetClassLongPtrW(Window::hwnd,GCL_STYLE)
             <<" exstyle="<<GetWindowLongPtrW(Window::hwnd,GWL_EXSTYLE)
             <<" layered_attributes="<<attributes<<" alpha="<<int(alpha)<<" flags="<<flags<<std::endl;
    // Repeat both directions, including closing while the overlay owns capture.
    for(int cycle=0;cycle<12;++cycle) {
        const bool passthrough=(cycle%2)==0;
        if(passthrough && cycle>0) { SetCapture(Window::hwnd);require(GetCapture()==Window::hwnd,"establish stale capture"); }
        Window::SetClickthrough(Window::hwnd,passthrough);
        if(!passthrough)SetForegroundWindow(Window::hwnd);
        else SetForegroundWindow(session.target);
        require(GetForegroundWindow()==(passthrough?session.target:Window::hwnd),"focus handoff failed");
        require(!passthrough || GetCapture()!=Window::hwnd,"closing must release overlay capture");
        const auto before=Window::VisibilityTransitions();
        for(int noise=0;noise<8;++noise) { Window::UpdateGameplayVisibility(true,!passthrough,noise%2);frame(); }
        const auto td=ask(session.target,0),tu=ask(session.target,1);
        const auto od=downs,ou=ups,ib=buttons;
        const auto probe=ask(session.target,2,MAKELPARAM(point.x,point.y));
        click(session.target,point);
        const auto received_down=ask(session.target,0)-td,received_up=ask(session.target,1)-tu;
        std::cout<<"cycle="<<cycle<<" passthrough="<<passthrough
                 <<" native_probe="<<reinterpret_cast<HWND>(probe)
                 <<" receiver="<<session.target<<" overlay="<<Window::hwnd
                 <<" receiver_down/up="<<received_down<<'/'<<received_up
                 <<" overlay_down/up="<<downs-od<<'/'<<ups-ou<<" imgui="<<buttons-ib<<std::endl;
        require(received_down==(passthrough?1:0) && received_up==(passthrough?1:0),"wrong real click delivery to receiver");
        require(downs-od==(passthrough?0u:1u) && ups-ou==(passthrough?0u:1u),"wrong real click delivery to overlay");
        require(buttons-ib==(passthrough?0u:1u),"ImGui button not following menu input mode");
        require(reinterpret_cast<HWND>(probe)==(passthrough?session.target:Window::hwnd),"receiver UI thread point lookup reached wrong process");
        const auto exstyle=GetWindowLongPtrW(Window::hwnd,GWL_EXSTYLE);
        require(((exstyle&WS_EX_TRANSPARENT)!=0)==passthrough,"render frames overwrote native input policy");
        require(((exstyle&WS_EX_NOACTIVATE)!=0)==passthrough,"render frames overwrote activation policy");
        require(IsWindowVisible(Window::hwnd) && before==Window::VisibilityTransitions(),"input changes or cursor noise toggled visibility");
        checkPixels(bounds);
    }
    std::cout<<"PASS: separate-process delivered clicks, ImGui, capture release, repeated transitions ("
             <<(fullscreen?"borderless monitor":"windowed")<<")"<<std::endl;
}
}
int main(int argc,char** argv) {
    SetProcessDPIAware();
    if(argc>=3 && std::string_view(argv[1])=="--fixture") {
        const std::string name=argv[2];const std::wstring wide(name.begin(),name.end());
        return fixture(wide.c_str(),argc>3);
    }
    if(argc!=2 || (std::string_view(argv[1])!="--run" && std::string_view(argv[1])!="--fullscreen")) {
        std::cerr<<"Run on an idle interactive desktop: test-overlay-input --run | --fullscreen\n";return 2;
    }
    LogHelper::Init();
    try { run(std::string_view(argv[1])=="--fullscreen");LogHelper::Destroy();return 0; }
    catch(const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<std::endl;LogHelper::Destroy();return 1; }
}
