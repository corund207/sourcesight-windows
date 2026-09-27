#include "Window.hpp"
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
namespace { GLFWwindow* window=nullptr; bool first_frame=true; }
bool Window::SpawnWindow() {
    glfwSetErrorCallback([](int code,const char* message) { LOGF(WARNING,"GLFW {}: {}",code,message); });
    if(!glfwInit())return false;
    const bool preview=Menu::IsPreviewMode();
    first_frame=true;
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
    if(!preview) {
        auto style=GetWindowLongPtrW(hwnd,GWL_EXSTYLE);
        SetWindowLongPtrW(hwnd,GWL_EXSTYLE,style|WS_EX_TOOLWINDOW);
        glfwSetWindowPos(window,0,0);
    }
    glfwMakeContextCurrent(window);glfwSwapInterval(0);
    shouldRun=true;return true;
}
void Window::DespawnWindow() {
    if(window)glfwDestroyWindow(window);
    window=nullptr;hwnd=nullptr;glfwTerminate();
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
    if(first_frame) { first_frame=false;glfwShowWindow(window); }
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
void Window::SetClickthrough(HWND,bool enabled) {
    if(window)glfwSetWindowAttrib(window,GLFW_MOUSE_PASSTHROUGH,enabled?GLFW_TRUE:GLFW_FALSE);
}
bool Window::SetAffinity(HWND target,WindowAffinity affinity) {
    const DWORD mode=affinity==WindowAffinity::Invisible?WDA_EXCLUDEFROMCAPTURE:
                     affinity==WindowAffinity::Black?WDA_MONITOR:WDA_NONE;
    return SetWindowDisplayAffinity(target,mode)!=0;
}
void Window::SetVSync(bool enabled) { vsync=enabled;glfwSwapInterval(enabled?1:0); }
void Window::SetVisible(bool visible) { if(window) { if(visible)glfwShowWindow(window);else glfwHideWindow(window); } }
