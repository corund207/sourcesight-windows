#include "common.hpp"
#include "gui/frontend/menu/Menu.hpp"
#include "imgui_internal.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {

void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

void CheckDrawData() {
    const ImDrawData* data = ImGui::GetDrawData();
    Require(data != nullptr, "menu did not produce draw data");
    Require(data->TotalVtxCount > 0, "menu produced no vertices");
    for (const ImDrawList* list : data->CmdLists) {
        for (const ImDrawVert& vertex : list->VtxBuffer)
            Require(std::isfinite(vertex.pos.x) && std::isfinite(vertex.pos.y), "non-finite menu vertex");
        for (const ImDrawCmd& command : list->CmdBuffer) {
            Require(std::isfinite(command.ClipRect.x) && std::isfinite(command.ClipRect.y) &&
                    std::isfinite(command.ClipRect.z) && std::isfinite(command.ClipRect.w),
                    "non-finite menu clip rectangle");
        }
    }
}

void CheckStacks() {
    ImGuiContext& context = *ImGui::GetCurrentContext();
    Require(context.ColorStack.empty(), "unbalanced color stack");
    Require(context.StyleVarStack.empty(), "unbalanced style stack");
    Require(context.FontStack.empty(), "unbalanced font stack");
    Require(context.GroupStack.empty(), "unbalanced group stack");
    Require(context.CurrentWindowStack.empty(), "unbalanced window/ID stack");
}

void Frame() {
    ImGui::NewFrame();
    Menu::Render();
    ImGui::Render();
    CheckDrawData();
    CheckStacks();
}

void SelectTab(int tab) {
    const ImVec2 pos = Menu::GetPos();
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(pos.x + 80.0f, pos.y + 155.0f + tab * 52.0f);
    io.AddMouseButtonEvent(0, true);
    Frame();
    io.AddMouseButtonEvent(0, false);
    Frame();
    Frame(); // Settle the newly selected tab's auto-sized child cards.
    Require(Menu::PreviewActiveTab()==tab,"tab click did not select the expected tab");
}

} // namespace

int main() {
    try {
        LogHelper::Init();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(1280.0f, 900.0f);
        io.DeltaTime = 1.0f / 60.0f;
        Menu::SetPreviewMode(true);
        Require(Menu::Init(), "menu initialization failed");
        unsigned char* font_pixels = nullptr;
        int font_width = 0, font_height = 0;
        io.Fonts->GetTexDataAsRGBA32(&font_pixels, &font_width, &font_height);
        io.Fonts->SetTexID(static_cast<ImTextureID>(1));
        Frame();
        cfg::settings::advanced_controls = false;
        cfg::esp::wireframe_budget = 1234;
        Frame();
        Require(cfg::esp::wireframe_budget == 6000 && cfg::world::radar::calibration_height == 900.f,
                "simple mode automatically tunes the current viewport");
        const ImVec2 mode_pos = Menu::GetPos() + ImVec2(Menu::GetSize().x - 353.f, 38.f);
        io.AddMousePosEvent(mode_pos.x, mode_pos.y);
        io.AddMouseButtonEvent(0, true);
        Frame();
        io.AddMouseButtonEvent(0, false);
        Frame();
        Require(cfg::settings::advanced_controls, "header mode button enables advanced controls");
        cfg::esp::wireframe_budget = 4321;
        Frame();
        Require(cfg::esp::wireframe_budget == 4321, "advanced mode preserves granular rendering values");
        for (int tab = 0; tab < int(std::size(tabs)); ++tab)
            SelectTab(tab);

        io.DisplaySize = ImVec2(940.0f, 600.0f);
        Frame();
        for (int tab = 0; tab < int(std::size(tabs)); ++tab) {
            SelectTab(tab);
            io.AddMouseWheelEvent(0.0f, -5.0f);
            Frame();
        }
        Require(Menu::GetPos().x>=0 && Menu::GetPos().y>=0 &&
                Menu::GetPos().x+Menu::GetSize().x<=io.DisplaySize.x &&
                Menu::GetPos().y+Menu::GetSize().y<=io.DisplaySize.y,"menu must fit the smaller viewport");
        Require(Menu::PreviewNavigateSearch("radar"),"search matches option aliases");
        Frame();
        Require(Menu::PreviewActiveTab()==Tab::WORLD,"search navigates to world");
        Require(!Menu::PreviewNavigateSearch("dark map"),"blackout controls removed from search");
        Require(Menu::PreviewNavigateSearch("weapon hands"),"search finds the viewmodel toggle");
        Require(Menu::PreviewNavigateSearch("automatic calibration"),"search finds setup mode");
        Require(!Menu::PreviewNavigateSearch("nothing-matches-this"),"search has no-result state");
        Require(!Menu::PreviewNavigateSearch("aim"),"aim controls removed from search");
        Require(!Menu::PreviewNavigateSearch("trigger"),"trigger controls removed from search");
        Require(!Menu::PreviewNavigateSearch("quickswitch"),"macro controls removed from search");
        const bool enabled=cfg::enabled;
        const auto budget=cfg::esp::wireframe_budget;
        Menu::PreviewRequestVisualPreset(0);
        Require(Menu::PreviewHasPendingVisualPreset() && cfg::esp::wireframe_budget==budget,
                "preset request does not apply until confirmed");
        Require(Menu::PreviewConfirmVisualPreset() && cfg::esp::wireframe_budget==2500,
                "confirmed preset applies visual quality");
        Require(cfg::enabled==enabled,"presets preserve activation states");
        Require(!Menu::PreviewConfirmVisualPreset(),"confirmation cannot reapply consumed request");
        std::cout << "menu smoke passed: 4 tabs at 1280x900 and 940x600, finite geometry, stack balance\n";
        ImGui::DestroyContext();
        LogHelper::Destroy();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "test-menu: " << error.what() << '\n';
        if (ImGui::GetCurrentContext()) ImGui::DestroyContext();
        LogHelper::Destroy();
        return 1;
    }
}
