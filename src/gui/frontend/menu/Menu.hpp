#pragma once
#include <string>

enum Tab {
    PLAYER,
    WORLD,
    SOUND_ESP,
    SETTINGS
};

struct TabItem
{
    Tab id;
    std::string label;
};

static const TabItem tabs[] =
{
    { Tab::PLAYER,     "Player" },
    { Tab::WORLD,      "World" },
    { Tab::SOUND_ESP,  "Sound ESP" },
    { Tab::SETTINGS,   "Settings" }
};

class Menu {
public:
    ~Menu() = default;
    Menu(const Menu&) = delete;
    Menu(Menu&&) = delete;
    Menu& operator=(const Menu&) = delete;
    Menu& operator=(Menu&&) = delete;

    static bool Init();
    static void Render();

    // Used by the isolated offline preview. It keeps the real widget tree
    // inspectable while disabling filesystem/network-adjacent menu actions.
    static void SetPreviewMode(bool enabled);
    static bool IsPreviewMode();
    static int PreviewActiveTab();
    static bool PreviewNavigateSearch(const char* query, int match_index = 0);
    static void PreviewRequestVisualPreset(int preset);
    static bool PreviewHasPendingVisualPreset();
    static bool PreviewConfirmVisualPreset();

    static void RenderStartupHelp();

    static ImVec2 GetPos();
    static ImVec2 GetSize();
private:
    Menu() {};

    static Menu& GetInstance()
    {
        static Menu i{};
        return i;
    }

    bool InitImpl();
    void RenderImpl();
    void RenderStartupHelpImpl();

    void SetupStyles();
private:
    bool isSetup = true;
    bool preview_mode = false;
    int active_tab = 0;
    int pending_visual_preset = -1;

    ImVec2 pos;
    ImVec2 size;

    ImFont* font_heading = nullptr;
};
