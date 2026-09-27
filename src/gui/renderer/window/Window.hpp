#pragma once
#include <windows.h>
#include <string>
using NativeWindow=HWND;
enum class WindowAffinity { Disabled, Black, Invisible };
class Window {
public:
    static bool SpawnWindow();
    static void DespawnWindow();
    static bool CreateDevice();
    static void DestroyDevice();
    static bool CreateImGui();
    static void DestroyImGui();
    static void StartRender();
    static void EndRender();
    static void SetTopMost(NativeWindow window,bool enabled=true);
    static void SetBounds(const RECT& client_bounds);
    static void SetClickthrough(NativeWindow window,bool enabled=true);
    static bool SetAffinity(NativeWindow window,WindowAffinity affinity);
    static void SetVSync(bool enabled=false);
    static void SetVisible(bool visible);
    inline static HWND hwnd=nullptr;
    inline static bool vsync=false;
    inline static bool shouldRun=true;
    inline static std::string preview_screenshot;
};
