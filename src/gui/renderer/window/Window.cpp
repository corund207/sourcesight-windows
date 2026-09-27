#include "Window.hpp"
#include "OverlayVisibility.hpp"
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <imgui/backends/imgui_impl_glfw.h>
#include <imgui/backends/imgui_impl_opengl3.h>
#include "core/engine/cache/Cache.hpp"
#include "gui/frontend/menu/Menu.hpp"
#include "gui/renderer/FullMapRenderer.hpp"
#include <fstream>
namespace {
GLFWwindow* window=nullptr;
bool first_frame=true,clickthrough=false;
WNDPROC original_proc=nullptr;
OverlayVisibility visibility;
LRESULT CALLBACK OverlayProc(HWND target,UINT message,WPARAM wparam,LPARAM lparam) {
    if(clickthrough) {
        if(message==WM_NCHITTEST)return HTTRANSPARENT;
        if(message==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
    }
    return CallWindowProcW(original_proc,target,message,wparam,lparam);
}
}
bool Window::SpawnWindow() {
    glfwSetErrorCallback([](int code,const char* message) { LOGF(WARNING,"GLFW {}: {}",code,message); });
    if(!glfwInit())return false;
    const bool preview=Menu::IsPreviewMode();
    first_frame=true;
    visibility=OverlayVisibility{};
    glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
    glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_COMPAT_PROFILE);
    glfwWindowHint(GLFW_ALPHA_BITS,8);glfwWindowHint(GLFW_DEPTH_BITS,24);
    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER,preview?GLFW_FALSE:GLFW_TRUE);
    glfwWindowHint(GLFW_DECORATED,preview?GLFW_TRUE:GLFW_FALSE);
    glfwWindowHint(GLFW_FLOATING,preview?GLFW_FALSE:GLFW_TRUE);
    glfwWindowHint(GLFW_FOCUS_ON_SHOW,preview?GLFW_TRUE:GLFW_FALSE);
    glfwWindowHint(GLFW_MOUSE_PASSTHROUGH,preview?GLFW_FALSE:GLFW_TRUE);
    const auto* mode=glfwGetVideoMode(glfwGetPrimaryMonitor());
    if(!mode) { glfwTerminate();return false; }
    // An undecorated window matching the monitor exactly can bypass desktop
    // composition on Windows/AMD, turning zero-alpha pixels solid black.
    // Keep one transparent padding column outside the game's rendered area.
    window=glfwCreateWindow(preview?1280:mode->width+1,preview?900:mode->height,
                            "SourceSight Windows",nullptr,nullptr);
    if(!window) { glfwTerminate();return false; }
    hwnd=glfwGetWin32Window(window);
    original_proc=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(hwnd,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(OverlayProc)));
    clickthrough=false;
    if(!preview) {
        auto style=GetWindowLongPtrW(hwnd,GWL_EXSTYLE);
        SetWindowLongPtrW(hwnd,GWL_EXSTYLE,style|WS_EX_TOOLWINDOW);
        SetClickthrough(hwnd,true);
        glfwSetWindowPos(window,0,0);
    }
    glfwMakeContextCurrent(window);glfwSwapInterval(0);
    shouldRun=true;return true;
}
void Window::DespawnWindow() {
    if(window)glfwDestroyWindow(window);
    window=nullptr;hwnd=nullptr;original_proc=nullptr;clickthrough=false;glfwTerminate();
}
bool Window::CreateDevice() {
    if(!window)return false;
    glewExperimental=GL_TRUE;const auto result=glewInit();
    while(glGetError()!=GL_NO_ERROR) {}
    return result==GLEW_OK && GLEW_VERSION_3_3;
}
void Window::DestroyDevice() {}
bool Window::CreateImGui() {
    IMGUI_CHECKVERSION();ImGui::CreateContext();
    auto& io=ImGui::GetIO();io.IniFilename=nullptr;
    io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
    if(!ImGui_ImplGlfw_InitForOpenGL(window,true)) { ImGui::DestroyContext();return false; }
    if(!ImGui_ImplOpenGL3_Init("#version 330")) {
        ImGui_ImplGlfw_Shutdown();ImGui::DestroyContext();return false;
    }
    return true;
}
void Window::DestroyImGui() {
    FullMapRenderer::Destroy();
    ImGui_ImplOpenGL3_Shutdown();ImGui_ImplGlfw_Shutdown();ImGui::DestroyContext();
}
void Window::StartRender() {
    glfwPollEvents();shouldRun=window && !glfwWindowShouldClose(window);
    auto& io=ImGui::GetIO();
    if(clickthrough)io.ConfigFlags|=ImGuiConfigFlags_NoMouseCursorChange;
    else io.ConfigFlags&=~ImGuiConfigFlags_NoMouseCursorChange;
    ImGui_ImplOpenGL3_NewFrame();ImGui_ImplGlfw_NewFrame();
    if(!Menu::IsPreviewMode())ImGui::GetIO().DisplaySize.x-=1.f;
    ImGui::GetIO().AddKeyEvent(ImGuiKey_F9,(GetAsyncKeyState(VK_F9)&0x8000)!=0);
    ImGui::NewFrame();
}
void Window::EndRender() {
    ImGui::Render();int width=0,height=0;glfwGetFramebufferSize(window,&width,&height);
    if(!Menu::IsPreviewMode())--width;
    glViewport(0,0,width,height);glDisable(GL_SCISSOR_TEST);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    glClearColor(0,0,0,Menu::IsPreviewMode()?1.f:0.f);glClear(GL_COLOR_BUFFER_BIT);
    if(cfg::enabled&&cfg::esp::wireframe&&cfg::esp::wireframe_mode==1) {
        const auto snapshot=Cache::CopySnapshot();
        if(snapshot.status.ready())FullMapRenderer::Render(snapshot.game.view_matrix);
    } else FullMapRenderer::Destroy();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    static int frame=0;
    if (++frame==12 && Menu::IsPreviewMode() && !preview_screenshot.empty()) {
        const size_t stride=(size_t(width)*3+3)&~size_t(3);
        std::vector<unsigned char> pixels(stride*height);
        glPixelStorei(GL_PACK_ALIGNMENT,4);glReadPixels(0,0,width,height,GL_BGR,GL_UNSIGNED_BYTE,pixels.data());
        BITMAPFILEHEADER file{};file.bfType=0x4D42;
        file.bfOffBits=sizeof(file)+sizeof(BITMAPINFOHEADER);file.bfSize=file.bfOffBits+DWORD(pixels.size());
        BITMAPINFOHEADER info{};info.biSize=sizeof(info);info.biWidth=width;info.biHeight=height;
        info.biPlanes=1;info.biBitCount=24;info.biCompression=BI_RGB;
        std::ofstream out(preview_screenshot,std::ios::binary);
        out.write(reinterpret_cast<const char*>(&file),sizeof(file));
        out.write(reinterpret_cast<const char*>(&info),sizeof(info));
        out.write(reinterpret_cast<const char*>(pixels.data()),pixels.size());
    }
    glfwSwapBuffers(window);
    // Publish a cleared/rendered framebuffer before showing the native window.
    if(first_frame) { first_frame=false;SetVisible(true); }
}
void Window::SetBounds(const RECT& bounds) {
    if(!window)return;
    SetWindowPos(hwnd,HWND_TOPMOST,bounds.left,bounds.top,
                 bounds.right-bounds.left+(Menu::IsPreviewMode()?0:1),bounds.bottom-bounds.top,
                 SWP_NOACTIVATE);
}
void Window::SetTopMost(HWND target,bool enabled) {
    SetWindowPos(target,enabled?HWND_TOPMOST:HWND_NOTOPMOST,0,0,0,0,
                 SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
}
void Window::SetClickthrough(HWND target,bool enabled) {
    if(!window || target!=hwnd)return;
    clickthrough=enabled;
    glfwSetWindowAttrib(window,GLFW_MOUSE_PASSTHROUGH,enabled?GLFW_TRUE:GLFW_FALSE);
    auto style=GetWindowLongPtrW(target,GWL_EXSTYLE);
    if(enabled)style|=WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE;
    else style&=~(WS_EX_TRANSPARENT|WS_EX_NOACTIVATE);
    SetWindowLongPtrW(target,GWL_EXSTYLE,style);
    // Initialize layered-window attributes explicitly. GLFW's passthrough
    // path can leave a newly layered window with zero attribute flags.
    if(enabled)SetLayeredWindowAttributes(target,0,255,LWA_ALPHA);
    if(enabled && GetCapture()==target)ReleaseCapture();
}
bool Window::SetAffinity(HWND target,WindowAffinity affinity) {
    const DWORD mode=affinity==WindowAffinity::Invisible?WDA_EXCLUDEFROMCAPTURE:
                     affinity==WindowAffinity::Black?WDA_MONITOR:WDA_NONE;
    return SetWindowDisplayAffinity(target,mode)!=0;
}
void Window::SetVSync(bool enabled) { vsync=enabled;glfwSwapInterval(enabled?1:0); }
void Window::SetVisible(bool visible) {
    if(!window)return;
    if(!visible)glfwHideWindow(window);
    else if(Menu::IsPreviewMode())glfwShowWindow(window);
    else {
        ShowWindow(hwnd,SW_SHOWNOACTIVATE);
        SetTopMost(hwnd);
    }
}
void Window::UpdateGameplayVisibility(bool focused,bool menu_open,bool game_cursor_visible) {
    if(!window)return;
    // Native cursor menus must not have a foreign window above their mouse
    // targets. Raw-input gameplay resumes the overlay when it hides the cursor.
    const bool visible=visibility.Update(focused,menu_open,game_cursor_visible,GetTickCount64());
    if(visible!=(IsWindowVisible(hwnd)!=FALSE))SetVisible(visible);
}
