#include "gui/frontend/overlays/RadarProjection.hpp"
#include "Renderer.hpp"
#include "window/Window.hpp"

#include "capture/ScreenCapture.hpp"
#include "core/diagnostics/Diagnostics.hpp"

#include "config/Current.hpp"
#include "config/AutoCalibration.hpp"
#include "core/engine/Engine.hpp"
#include "gui/frontend/esp/Esp.hpp"
#include "gui/frontend/menu/Menu.hpp"
#include "gui/frontend/overlays/Overlays.hpp"
#include <ctime>

namespace {
std::optional<double> RenderCpuTimeMs() {
    FILETIME creation{}, exit{}, kernel{}, user{};
    if (GetThreadTimes(GetCurrentThread(),&creation,&exit,&kernel,&user)) {
        ULARGE_INTEGER k{},u{}; k.LowPart=kernel.dwLowDateTime;k.HighPart=kernel.dwHighDateTime;
        u.LowPart=user.dwLowDateTime;u.HighPart=user.dwHighDateTime;
        return double(k.QuadPart+u.QuadPart)/10000.;
    }
    return std::nullopt;
}
}


bool Renderer::Init() {
    return GetInstance().InitImpl();
}

void Renderer::Thread(int frame_limit) {
    return GetInstance().ThreadImpl(frame_limit);
}

void Renderer::Destroy() {
    return GetInstance().DestroyImpl();
}

bool Renderer::IsOpen() {
    return GetInstance().isOpen;
}

bool Renderer::IsFocused() {
    return GetInstance().isFocused;
}

bool Renderer::InitImpl() {
    if (!Window::SpawnWindow()) {
        LOGF(WARNING, "Failed to create the Windows overlay");
        return false;
    }

    if (!Window::CreateDevice()) {
        LOGF(WARNING, "Failed to create device");
        Window::DespawnWindow();
        return false;
    }

    if (!Window::CreateImGui()) {
        LOGF(FATAL, "Failed to create ImGui");
        Window::DestroyDevice();
        Window::DespawnWindow();
        return false;
    }

    if (!Menu::Init() || !Esp::Init() || !Overlays::Init()) {
        LOGF(FATAL, "Failed to initialize the interface");
        Window::DestroyImGui();
        Window::DestroyDevice();
        Window::DespawnWindow();
        return false;
    }

    // Focus the game
    if (auto process=Engine::GetProcess()) SetForegroundWindow(process->hwnd_);

    if (cfg::settings::streamproof && !Menu::IsPreviewMode())
        Window::SetAffinity(Window::hwnd, WindowAffinity::Invisible);

    if (cfg::settings::vsync)
        Window::SetVSync(true);

    if (Menu::IsPreviewMode()) { isOpen=true; isFocused=true; Window::SetClickthrough(Window::hwnd,false); }

    // We want the main thread to call render
    // And lock it
    // std::thread(Thread).detach();

    LOGF(INFO, "Successfully initialized renderer...");
    return true;
}

void Renderer::DestroyImpl() {
    isRunning = false; // Prepare to stop thread loop
    LOGF(VERBOSE, "Renderer shutdown requested...");
}

void Renderer::ThreadImpl(int frame_limit) {
    int frames=0;
    while (isRunning) {
        const auto cpu_start=RenderCpuTimeMs();
        Render();
        if (frame_limit>0 && ++frames>=frame_limit) { isRunning=false; break; }
        isRunning=Window::shouldRun;
        const auto cpu_end=RenderCpuTimeMs();
        // Measured render-thread CPU time; excludes time blocked on swap/GPU.
        Diagnostics::SetTimings(cpu_start&&cpu_end ? std::optional<double>(*cpu_end-*cpu_start)
                                                  : std::nullopt, std::nullopt);

        // If the game is not focused, do not process state changes,
        // or will start focusing game & overlay
        if (this->isFocused && HandleState())
            continue; // It will cause flickering if we handle window order after window closes

        HandleWindowOrder();
    }

    // Once exited, destroy everything
    Engine::Stop(); // Join before UI/input dependencies or logging can disappear.
    ScreenCapture::StopRecording(); // Finalize any active capture (no-op when idle)
    Window::DestroyImGui();
    Window::DestroyDevice();
    Window::DespawnWindow();
}

void Renderer::Render() {
    Window::StartRender();

    const auto& display = ImGui::GetIO().DisplaySize;
    AutoCalibration::ApplySimple(display.x, display.y);

    Esp::Render();
    Overlays::Render();

    if (isOpen) {
        Menu::Render();
    }

    Window::EndRender();
}

bool Renderer::HandleState() {
    isRunning = Window::shouldRun; // From the window event handler

    static bool was_holding = false;

    bool pressed_insert = (GetAsyncKeyState(VK_INSERT) & 0x8000);
    bool pressed_rshift = false;

    bool pressed_end = (GetAsyncKeyState(VK_END) & 0x8000);

    bool should_toggle = !was_holding && (pressed_insert || pressed_rshift);

    if (should_toggle || pressed_end) { // Toggle when pressing end to trigger the config save :v
        this->isOpen = !isOpen;
        Window::SetClickthrough(Window::hwnd, !this->isOpen);

        // Release cursor when opening the menu
        // Sometimes flashes the render as its handling the window order
        if (this->isOpen)
            SetForegroundWindow(Window::hwnd);
        else
            if (auto process=Engine::GetProcess()) SetForegroundWindow(process->hwnd_);

        LOGF(VERBOSE, "Toggling menu state to {}", this->isOpen.load());

        // Capture settings on their owning UI thread. A detached writer raced
        // edits and could be terminated halfway through an exit-time save.
        if(!Menu::IsPreviewMode() && (!this->isOpen || pressed_end) && !Config::Write())
            LOGF(WARNING, "Profile save failed; previous on-disk settings were retained");
    }

    if (pressed_end)
        this->isRunning = false;

    was_holding = pressed_insert || pressed_rshift;
    return should_toggle;
}

bool Renderer::HandleWindowOrder() {
    if (Menu::IsPreviewMode()) { isFocused=true; return true; }
    auto p = Engine::GetProcess();

    if (!p || !p->UpdateHWND()) { isRunning=false; return false; }

    // Check if game window is still valid, if not, most likely game closed
    if (!IsWindow(p->hwnd_))
        this->isRunning = false;

    auto foreground = GetForegroundWindow();
    this->isFocused = (foreground == Window::hwnd || foreground == p->hwnd_);
    // Diagnostics-only cursor snapshot. It must not drive visibility:
    // GetCursorInfo is system-global and observes our own topmost overlay, so
    // hiding on "cursor visible" feeds a hide/show loop. Input routing uses
    // WS_EX_TRANSPARENT passthrough instead, keeping ESP stable over game menus.
    CURSORINFO cursor{};cursor.cbSize=sizeof(cursor);
    const bool game_cursor_visible=GetCursorInfo(&cursor) && (cursor.flags&CURSOR_SHOWING);
    Window::UpdateGameplayVisibility(isFocused.load(),isOpen.load(),game_cursor_visible);

    static RECT last_rect = { 0, 0, 0, 0 };

    RECT window_rect;
    if (!GetWindowRect(p->hwnd_, &window_rect))
        return false;

    // All good, no movements from the client
    if (memcmp(&window_rect, &last_rect, sizeof(RECT)) == 0)
        return true;

    RECT client_rect;
    if (!GetClientRect(p->hwnd_, &client_rect))
        return false;

    POINT top_left = { client_rect.left, client_rect.top };
    POINT bottom_right = { client_rect.right, client_rect.bottom };

    ClientToScreen(p->hwnd_, &top_left);
    ClientToScreen(p->hwnd_, &bottom_right);

    RECT screen_rect = { top_left.x, top_left.y, bottom_right.x, bottom_right.y };

    Window::SetBounds(screen_rect);

    last_rect = window_rect;

    return true;
}
