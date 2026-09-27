#include "Menu.hpp"

#include "config/Config.hpp"
#include "config/AutoCalibration.hpp"
#include "core/engine/cache/Cache.hpp"
#include "core/engine/classes/MapRaytrace.hpp"
#include "core/diagnostics/Diagnostics.hpp"
#include "gui/renderer/capture/ScreenCapture.hpp"
#include "gui/renderer/Renderer.hpp"
#include "gui/renderer/window/Window.hpp"
#include "assets/fonts/Icons.h"
#include "Theme.hpp"

#include <cmath>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <unordered_map>
#include <string>
#include <filesystem>

// ═══════════════════════════════════════════════════════════════════════════
// Animation Utilities
// ═══════════════════════════════════════════════════════════════════════════

namespace {




// ───────────────────────────────────────────────────────────────────────────
// Theme.hpp owns all menu colors, spacing, radii and typography. These short
// aliases keep the existing control code readable without creating a second
// palette in this translation unit.
// ───────────────────────────────────────────────────────────────────────────
using namespace theme;
const auto& studioAccent = kAccent;
const auto& studioCanvas = kSurfaceDeep;
const auto& studioCard = kSurfaceElev1;
const auto& studioBorder = kBorderBase;
const auto& studioText = kTextPrimary;
const auto& studioMuted = kTextMuted;
const auto& studioAccentSoft = kAccentSoft;
const auto& studioAccentStrong = kAccentStrong;
const auto& studioAccentBright = kAccentBright;

std::string g_pending_section;

struct SearchTarget {
    Tab tab;
    const char* section;
    const char* aliases;
};

constexpr SearchTarget kSearchTargets[] = {
    {Tab::PLAYER, "Player boxes", "box boxes fill thickness esp players"},
    {Tab::PLAYER, "Player wireframe", "wireframe body mesh visibility detail opacity"},
    {Tab::PLAYER, "Head tracking", "head tracker head size"},
    {Tab::PLAYER, "Bullet trails", "bullet tracer shots trail impact muzzle"},
    {Tab::PLAYER, "Player information", "health armor team distance spotted"},
    {Tab::PLAYER, "Flags", "name money weapon ammo ping scoped c4"},
    {Tab::WORLD, "Map geometry", "map full map dark map blackout weapon hands viewmodel grenade nade equipment xray lines edge budget distance panel fill"},
    {Tab::WORLD, "Radar", "radar minimap zoom range rotation"},
    {Tab::WORLD, "Crosshair", "crosshair gap length center dot outline"},
    {Tab::SOUND_ESP, "Sound ESP", "sound footsteps gunshots duration fade"},
    {Tab::SETTINGS, "Profiles", "profile load save create delete"},
    {Tab::SETTINGS, "Setup mode", "simple advanced automatic calibration autocal controls"},
    {Tab::SETTINGS, "Display", "streamproof watermark vsync cpu panic"},
    {Tab::SETTINGS, "Visual quality", "visual quality preset performance detail"},
    {Tab::SETTINGS, "Screen capture", "screenshot recording fps"},
};

bool ContainsInsensitive(const std::string& haystack, std::string needle) {
    if (needle.empty()) return true;
    std::transform(needle.begin(), needle.end(), needle.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::string value = haystack;
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value.find(needle) != std::string::npos;
}

bool SearchMatches(const SearchTarget& target, const std::string& query) {
    const std::string section = target.section;
    return ContainsInsensitive(section, query) || ContainsInsensitive(target.aliases, query);
}

bool NavigateToSearchTarget(int& active_tab, const SearchTarget& target) {
    active_tab = static_cast<int>(target.tab);
    g_pending_section = target.section;
    return true;
}

void ApplyVisualPreset(int preset) {
    // These presets touch visual presentation only. They do not enable ESP,
    // aim, trigger, macros, capture, or any other automation.
    switch (preset) {
    case 0: // Conservative
        cfg::esp::box_fill_alpha = 0.08f;
        cfg::esp::box_thickness = 1.0f;
        cfg::esp::skeleton_thickness = 1.0f;
        cfg::esp::tracer_thickness = 1.0f;
        cfg::esp::wireframe_opacity = 0.45f;
        cfg::esp::wireframe_budget = 2500;
        cfg::esp::player_wireframe::detail = 0;
        cfg::esp::player_wireframe::thickness = 1.0f;
        break;
    case 1: // Balanced
        cfg::esp::box_fill_alpha = 0.12f;
        cfg::esp::box_thickness = 1.0f;
        cfg::esp::skeleton_thickness = 1.5f;
        cfg::esp::tracer_thickness = 1.0f;
        cfg::esp::wireframe_opacity = 0.65f;
        cfg::esp::wireframe_budget = 6000;
        cfg::esp::player_wireframe::detail = 1;
        cfg::esp::player_wireframe::thickness = 1.0f;
        break;
    case 2: // Detail
        cfg::esp::box_fill_alpha = 0.16f;
        cfg::esp::box_thickness = 1.5f;
        cfg::esp::skeleton_thickness = 2.0f;
        cfg::esp::tracer_thickness = 1.5f;
        cfg::esp::wireframe_opacity = 0.82f;
        cfg::esp::wireframe_budget = 8000;
        cfg::esp::player_wireframe::detail = 2;
        cfg::esp::player_wireframe::thickness = 1.5f;
        break;
    default:
        break;
    }
}

void DrawSearchResults(int& active_tab) {
    static char query[96]{};
    ImGui::SetNextItemWidth(250.0f);
    const bool changed = ImGui::InputTextWithHint("##menu-search", "Search settings", query, sizeof(query));
    ImGui::SameLine(0.0f, 4.0f);
    if (ImGui::SmallButton("Clear##menu-search")) {
        query[0] = '\0';
        ImGui::SetKeyboardFocusHere(-1);
    }
    if (changed && query[0] != '\0')
        ImGui::OpenPopup("##menu-search-results");
    if (ImGui::IsItemActive() && query[0] != '\0')
        ImGui::OpenPopup("##menu-search-results");

    if (ImGui::BeginPopup("##menu-search-results", ImGuiWindowFlags_AlwaysAutoResize)) {
        if (query[0] == '\0') {
            ImGui::TextDisabled("Type a section or option alias");
        } else {
            int matches = 0;
            for (const auto& target : kSearchTargets) {
                if (!SearchMatches(target, query)) continue;
                ++matches;
                if (ImGui::Selectable((std::string(target.section).substr(0, std::string(target.section).find("##")) +
                                      "  /  " + tabs[static_cast<int>(target.tab)].label).c_str())) {
                    NavigateToSearchTarget(active_tab, target);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::TextDisabled("  %s", target.aliases);
            }
            if (matches == 0)
                ImGui::TextDisabled("No matching settings");
        }
        ImGui::EndPopup();
    }
}

// ── Collapsible Section State (animated) ──────────────────────────────────

// Opaque cards share a table grid; each owns its padding and natural height.
bool BeginSettingsCard(const char* label, bool body_enabled = true) {
    ImGui::TableNextColumn();
    const std::string heading = std::string(label).substr(0, std::string(label).find("##"));
    const bool search_focus = g_pending_section == label;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(kCardPad, kCardPad));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, kChildRounding);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, search_focus ? WithAlpha(kAccentStrong, 0.42f) : studioCard);
    ImGui::PushStyleColor(ImGuiCol_Border, search_focus ? kAccent : studioBorder);
    ImGui::BeginChild(label, ImVec2(-1, 0),
        ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::TextColored(studioText, "%s", heading.c_str());
    if (search_focus) {
        ImGui::SameLine();
        ImGui::TextColored(kAccentBright, "from search");
        g_pending_section.clear();
    }
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::PushItemWidth(std::min(145.0f, ImGui::GetContentRegionAvail().x * 0.42f));
    if (!body_enabled) ImGui::BeginDisabled();
    return true;
}

void EndSettingsCard(bool is_open, bool body_enabled = true) {
    if (!is_open) return;
    if (!body_enabled) ImGui::EndDisabled();
    ImGui::PopItemWidth();
    ImGui::Dummy(ImVec2(0, 4));
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
}

// Four distinct line icons, drawn in the same 20px coordinate system.
void CategoryIcon(ImDrawList* d, ImVec2 p, int kind, ImU32 c) {
    const auto line = [&](float x, float y, float xx, float yy) {
        d->AddLine(p + ImVec2(x,y), p + ImVec2(xx,yy), c, 1.6f);
    };
    if (kind == 0) {
        d->AddCircle(p + ImVec2(10,5), 3, c, 16, 1.6f);
        d->AddRect(p + ImVec2(3,11), p + ImVec2(17,19), c, 4, 0, 1.6f);
    } else if (kind == 1) {
        d->AddCircle(p + ImVec2(10,10), 8, c, 24, 1.6f);
        d->AddEllipse(p + ImVec2(10,10), ImVec2(3,8), c, 0, 24, 1.6f);
        line(2,10,18,10);
    } else if (kind == 2) {
        for (int i=0;i<5;++i) {
            float h = i==2 ? 9 : (i%2 ? 6 : 3);
            line(2+i*4,10-h,2+i*4,10+h);
        }
    } else {
        for(int i=0;i<3;++i) {
            float x=3+i*7, y=i==1?13:6;
            line(x,1,x,19);
            d->AddCircleFilled(p+ImVec2(x,y),3,c);
        }
    }
}

bool Toggle(const char* label, bool* value) {
    ImGui::PushID(label);
    const ImVec2 p=ImGui::GetCursorScreenPos();
    const std::string visible = std::string(label).substr(0, std::string(label).find("##"));
    const float width=38 + 10 + ImGui::CalcTextSize(visible.c_str()).x;
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0,0,0,0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(.35f,.45f,.35f,.08f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(.35f,.45f,.35f,.14f));
    bool changed=ImGui::Button("##switch", ImVec2(width,24));
    ImGui::PopStyleColor(3);
    if(changed) *value=!*value;
    auto* d=ImGui::GetWindowDrawList();
    const float alpha=ImGui::GetStyle().Alpha;
    d->AddRectFilled(p,p+ImVec2(36,22),
        ImGui::GetColorU32(*value ? studioAccent : ImVec4(.34f,.36f,.37f,1)),11);
    d->AddCircleFilled(p+ImVec2(*value?25:11,11),8,IM_COL32(255,255,255,int(255*alpha)));
    d->AddText(p+ImVec2(46,3),ImGui::GetColorU32(ImGuiCol_Text),visible.c_str());
    ImGui::PopID();
    return changed;
}

// ── Button Helpers ────────────────────────────────────────────────────────

// Primary action button in the Graphite Studio pale-green accent.
bool PrimaryButton(const char* label, const ImVec2& size = ImVec2(-1, 30)) {
    ImGui::PushStyleColor(ImGuiCol_Button, studioAccentSoft);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(.84f,.93f,.79f,1));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(.62f,.76f,.56f,1));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(.10f,.18f,.13f,1));
    bool p = ImGui::Button(label, size);
    ImGui::PopStyleColor(4);
    return p;
}

// Destructive / disable action — jewel-red, muted enough to signal danger
// without shouting.
bool DangerButton(const char* label, const ImVec2& size = ImVec2(-1, 26)) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.18f, 0.24f, 0.90f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.72f, 0.26f, 0.32f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.42f, 0.12f, 0.16f, 1.00f));
    bool p = ImGui::Button(label, size);
    ImGui::PopStyleColor(3);
    return p;
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════
// Menu Implementation
// ═══════════════════════════════════════════════════════════════════════════

bool Menu::Init() { return GetInstance().InitImpl(); }
void Menu::Render() { return GetInstance().RenderImpl(); }
void Menu::SetPreviewMode(bool enabled) { GetInstance().preview_mode = enabled; }
bool Menu::IsPreviewMode() { return GetInstance().preview_mode; }
int Menu::PreviewActiveTab() { return GetInstance().active_tab; }
bool Menu::PreviewNavigateSearch(const char* query, int match_index) {
    if (!query || match_index < 0) return false;
    const std::string needle(query);
    int match = 0;
    for (const auto& target : kSearchTargets) {
        if (!SearchMatches(target, needle)) continue;
        if (match++ == match_index)
            return NavigateToSearchTarget(GetInstance().active_tab, target);
    }
    return false;
}
void Menu::PreviewRequestVisualPreset(int preset) {
    GetInstance().pending_visual_preset = (preset >= 0 && preset < 3) ? preset : -1;
}
bool Menu::PreviewHasPendingVisualPreset() { return GetInstance().pending_visual_preset >= 0; }
bool Menu::PreviewConfirmVisualPreset() {
    auto& instance = GetInstance();
    if (instance.pending_visual_preset < 0) return false;
    ApplyVisualPreset(instance.pending_visual_preset);
    instance.pending_visual_preset = -1;
    return true;
}
void Menu::RenderStartupHelp() { return GetInstance().RenderStartupHelpImpl(); }
ImVec2 Menu::GetPos() { return GetInstance().pos; }
ImVec2 Menu::GetSize() { return GetInstance().size; }

bool Menu::InitImpl() {
    SetupStyles();
    LOGF(INFO, "Successfully initialized menu...");
    return true;
}

void Menu::RenderImpl() {
    if (!isSetup) return;

    auto& io = ImGui::GetIO();
    const auto screen = io.DisplaySize;
    AutoCalibration::ApplySimple(screen.x, screen.y);
    static auto color_flags = ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel;

#ifdef _DEBUG
    static auto title = "SourceSight [DEV]";
#else
    static auto title = "SourceSight";
#endif

    static float saved_until = 0;
    static bool save_ok = true;
    const char* descriptions[] = {
        "Shape the information you see in the field.",
        "Keep your surroundings in view.",
        "See the sounds around you.",
        "Your profiles, preferences and application controls."
    };
    const ImVec2 maximum(std::max(940.f,std::min(1600.f,screen.x)),
                         std::max(600.f,std::min(1100.f,screen.y)));
    ImGui::SetNextWindowSize(ImVec2(std::min(1160.f,maximum.x),std::min(760.f,maximum.y)), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(940, 600), maximum);
    ImGui::SetNextWindowPos(screen * 0.5f, ImGuiCond_FirstUseEver, ImVec2(0.5f,0.5f));
    if(size.x>0 && size.y>0)
        ImGui::SetNextWindowPos(ImVec2(std::clamp(pos.x,0.f,std::max(0.f,screen.x-std::min(size.x,maximum.x))),
                                      std::clamp(pos.y,0.f,std::max(0.f,screen.y-std::min(size.y,maximum.y)))));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 24);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0,0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, studioCanvas);
    if (ImGui::Begin(title, nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar)) {
        pos=ImGui::GetWindowPos();
        size=ImGui::GetWindowSize();
        constexpr float rail=196;
        auto* d=ImGui::GetWindowDrawList();
        d->AddRectFilled(pos, pos+ImVec2(rail,size.y), IM_COL32(24,27,28,255),24,
                         ImDrawFlags_RoundCornersLeft);
        // Aperture mark and wordmark.
        const ImVec2 mark=pos+ImVec2(34,38);
        d->AddCircle(mark,14,IM_COL32(195,224,178,255),6,2);
        d->AddCircleFilled(mark,4,IM_COL32(195,224,178,255));
        d->AddText(pos+ImVec2(58,23),IM_COL32(248,249,243,255),"SOURCESIGHT");
        d->AddText(pos+ImVec2(58,44),IM_COL32(156,173,162,255),"CONTROL STUDIO");
        ImGui::SetCursorPos(ImVec2(22,103));
        ImGui::TextColored(ImVec4(.57f,.66f,.60f,1),"WORKSPACE");
        for(int i=0;i<int(std::size(tabs));++i) {
            ImGui::PushID(i);
            ImGui::SetCursorPos(ImVec2(12,134+i*52));
            const bool selected=active_tab==i;
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,12);
            ImGui::PushStyleColor(ImGuiCol_Button,selected?ImVec4(.77f,.88f,.70f,1):ImVec4(.094f,.106f,.110f,1));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,selected?ImVec4(.82f,.92f,.76f,1):ImVec4(.23f,.31f,.26f,1));
            ImGui::PushStyleColor(ImGuiCol_Text,selected?ImVec4(.10f,.18f,.13f,1):ImVec4(.80f,.85f,.80f,1));
            const ImVec2 p=ImGui::GetCursorScreenPos();
            if(ImGui::Button(("      "+tabs[i].label).c_str(),ImVec2(172,42))) active_tab=i;
            CategoryIcon(d,p+ImVec2(14,11),i,ImGui::GetColorU32(ImGuiCol_Text));
            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar();
            ImGui::PopID();
        }
        ImGui::SetCursorPos(ImVec2(22,size.y-67));
#ifdef SOURCESIGHT_VERSION
        ImGui::TextColored(kTextSecondary,"SourceSight / %s",SOURCESIGHT_VERSION);
#else
        ImGui::TextColored(kTextSecondary,"SourceSight / development");
#endif
        ImGui::SetCursorPos(ImVec2(22,size.y-42));
        ImGui::TextColored(kTextMuted,"Insert to close");

        ImGui::SetCursorPos(ImVec2(rail+28,24));
        ImGui::TextDisabled("WORKSPACE  /  %s",tabs[active_tab].label.c_str());
        ImGui::SetCursorPos(ImVec2(size.x-418,22));
        if (ImGui::Button(cfg::settings::advanced_controls ? "Advanced mode" : "Simple mode", ImVec2(130,32)))
            cfg::settings::advanced_controls = !cfg::settings::advanced_controls;
        ImGui::SetItemTooltip(cfg::settings::advanced_controls
            ? "Advanced mode exposes granular rendering and calibration controls."
            : "Simple mode automatically tunes presentation and viewport scaling. Click for Advanced.");
        ImGui::SetCursorPos(ImVec2(size.x-270,22));
        Toggle("Overlay", &cfg::enabled);
        ImGui::SameLine(0,18);
        ImGui::BeginDisabled(preview_mode);
        if(ImGui::Button("Save profile",ImVec2(116,32))) {
            save_ok=Config::Write();
            saved_until=float(ImGui::GetTime())+3;
        }
        ImGui::EndDisabled();
        ImGui::SetCursorPos(ImVec2(rail+28,69));
        ImGui::PushFont(font_heading);
        ImGui::TextUnformatted(tabs[active_tab].label.c_str());
        ImGui::PopFont();
        ImGui::SetCursorPos(ImVec2(rail+28,108));
        ImGui::TextDisabled("%s",descriptions[active_tab]);
        ImGui::SetCursorPos(ImVec2(size.x - 340, 104));
        DrawSearchResults(active_tab);
        ImGui::SetCursorPos(ImVec2(rail+28,146));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(0,0));
        ImGui::BeginChild("##workspace",ImVec2(size.x-rail-56,size.y-198),ImGuiChildFlags_None);
        ImGui::PopStyleVar();
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding,ImVec2(8,8));
        const int columns=ImGui::GetContentRegionAvail().x>=790?2:1;
        if(ImGui::BeginTable("##cards",columns,ImGuiTableFlags_SizingStretchSame)) {
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha,1.0f);
                if (active_tab == Tab::PLAYER)
                {

                    if (BeginSettingsCard("Player boxes")) {
                        ImGui::BeginGroup();
                        {
                            Toggle("Box", &cfg::esp::box);
                            ImGui::BeginDisabled(!cfg::esp::box);
                            {
                                ImGui::SameLine();
                                ImGui::ColorEdit4("Team##box", cfg::esp::colors::box_team.data(), color_flags);
                                ImGui::SameLine();
                                ImGui::ColorEdit4("Enemy##box", cfg::esp::colors::box_enemy.data(), color_flags);
                                Toggle("Filled", &cfg::esp::box_filled);
                                if (AutoCalibration::Advanced()) {
                                    if (cfg::esp::box_filled)
                                        ImGui::SliderFloat("Fill alpha", &cfg::esp::box_fill_alpha, 0.02f, 1.0f, "%.2f");
                                    ImGui::SliderFloat("Box thickness", &cfg::esp::box_thickness, 1.0f, 4.0f, "%.1f");
                                }
                            }
                            ImGui::EndDisabled();

                            ImGui::EndGroup();
                            EndSettingsCard(true);
                            BeginSettingsCard("Skeleton");
                            ImGui::BeginGroup();
                            Toggle("Skeleton", &cfg::esp::skeleton);
                            ImGui::BeginDisabled(!cfg::esp::skeleton);
                            {
                                ImGui::SameLine();
                                ImGui::ColorEdit4("Team##skel", cfg::esp::colors::skeleton_team.data(), color_flags);
                                ImGui::SameLine();
                                ImGui::ColorEdit4("Enemy##skel", cfg::esp::colors::skeleton_enemy.data(), color_flags);
                                if (AutoCalibration::Advanced())
                                    ImGui::SliderFloat("Skeleton thickness", &cfg::esp::skeleton_thickness, 1.0f, 4.0f, "%.1f");
                            }
                            ImGui::EndDisabled();

                            ImGui::EndGroup();
                            EndSettingsCard(true);
                            BeginSettingsCard("Player wireframe");
                            Toggle("3D body wireframe", &cfg::esp::player_wireframe::enabled);
                            ImGui::TextWrapped("Contoured body mesh with map visibility coloring.");
                            ImGui::BeginDisabled(!cfg::esp::player_wireframe::enabled);
                            Toggle("Visible edges only", &cfg::esp::player_wireframe::visible_only);
                            ImGui::SetItemTooltip("Hide blocked and unknown surface cells. Visibility samples static map collision; smoke and moving objects are not included. This is a bone-driven approximation, not the game model.");
                            if (AutoCalibration::Advanced()) {
                                ImGui::Combo("Detail##player-wire", &cfg::esp::player_wireframe::detail, "Standard\0Detailed\0Ultra\0");
                                ImGui::SetItemTooltip("8 / 12 / 16 sides with 3 / 5 / 7 body rings. Distant players automatically use fewer subdivisions to reduce clutter and rendering work.");
                                ImGui::SliderFloat("Opacity##player-wire", &cfg::esp::player_wireframe::opacity, 0.f, 1.f, "%.2f");
                                ImGui::SliderFloat("Thickness##player-wire", &cfg::esp::player_wireframe::thickness, 1.f, 3.f, "%.1f");
                                ImGui::SliderFloat("Distance##player-wire", &cfg::esp::player_wireframe::max_distance, 100.f, 10000.f, "%.0f u");
                            } else {
                                ImGui::TextColored(kSignalOK, "AUTO  mesh detail + distance");
                            }
                            ImGui::ColorEdit3("Visible##player-wire", cfg::esp::player_wireframe::visible.data(), ImGuiColorEditFlags_NoInputs);
                            ImGui::ColorEdit3("Blocked##player-wire", cfg::esp::player_wireframe::blocked.data(), ImGuiColorEditFlags_NoInputs);
                            ImGui::ColorEdit3("Unknown##player-wire", cfg::esp::player_wireframe::unknown.data(), ImGuiColorEditFlags_NoInputs);
                            ImGui::EndDisabled();
                            ImGui::TextWrapped(MapRaytrace::IsReady() ? "Map collision ready. World > Map geometry controls world lines." : "Map collision unavailable: edges are unknown (gray by default).");
                            EndSettingsCard(true);
                            BeginSettingsCard("Head tracking");
                            ImGui::BeginGroup();
                            Toggle("Head Tracker", &cfg::esp::head_tracker);
                            ImGui::BeginDisabled(!cfg::esp::head_tracker);
                            {
                                Toggle("Filled Head", &cfg::esp::head_tracker_filled);
                                if (AutoCalibration::Advanced())
                                    ImGui::SliderFloat("Head size", &cfg::esp::head_tracker_size, 2.0f, 14.0f, "%.1f");
                                ImGui::ColorEdit4("Team##head", cfg::esp::colors::tracker_team.data(), color_flags & ~ImGuiColorEditFlags_NoLabel);
                                ImGui::ColorEdit4("Enemy##head", cfg::esp::colors::tracker_enemy.data(), color_flags & ~ImGuiColorEditFlags_NoLabel);
                            }
                            ImGui::EndDisabled();

                            ImGui::EndGroup();
                            EndSettingsCard(true);
                            BeginSettingsCard("Player tracers");
                            ImGui::BeginGroup();
                            Toggle("Tracers", &cfg::esp::tracers);
                            ImGui::BeginDisabled(!cfg::esp::tracers);
                            {
                                ImGui::SameLine();
                                ImGui::ColorEdit4("Team##trc", cfg::esp::colors::tracer_team.data(), color_flags);
                                ImGui::SameLine();
                                ImGui::ColorEdit4("Enemy##trc", cfg::esp::colors::tracer_enemy.data(), color_flags);
                                if (AutoCalibration::Advanced())
                                    ImGui::SliderFloat("Tracer thickness", &cfg::esp::tracer_thickness, 1.0f, 4.0f, "%.1f");
                            }
                            ImGui::EndDisabled();

                            ImGui::EndGroup();
                            EndSettingsCard(true);
                            BeginSettingsCard("Bullet trails");
                            ImGui::BeginGroup();
                            Toggle("Bullet Tracer", &cfg::esp::bullet_tracer::enabled);
                            ImGui::SetItemTooltip("Estimated shots stop at the nearest static-world triangle or bone-based player capsule, including teammates. Reach is automatic; there is no distance cutoff.");
                            ImGui::BeginDisabled(!cfg::esp::bullet_tracer::enabled);
                            {
                                if (AutoCalibration::Advanced()) {
                                    ImGui::SameLine();
                                    ImGui::ColorEdit4("Team##bt", cfg::esp::bullet_tracer::team.data(), color_flags);
                                    ImGui::SameLine();
                                    ImGui::ColorEdit4("Enemy##bt", cfg::esp::bullet_tracer::enemy.data(), color_flags);
                                    ImGui::Combo("Trail style", &cfg::esp::bullet_tracer::style, "Ion\0Streak\0Minimal\0");
                                    ImGui::BeginDisabled(cfg::esp::bullet_tracer::style!=0);
                                    ImGui::SliderFloat("Glow", &cfg::esp::bullet_tracer::glow, 0.f, 1.f, "%.2f");
                                    ImGui::EndDisabled();
                                    Toggle("Impact markers", &cfg::esp::bullet_tracer::impact);
                                    ImGui::SliderFloat("Muzzle offset", &cfg::esp::bullet_tracer::muzzle_offset, 10.0f, 150.0f, "%.0f u");
                                    ImGui::SliderFloat("Bullet duration", &cfg::esp::bullet_tracer::duration, 0.1f, 10.0f, "%.1f s");
                                    ImGui::SliderFloat("Bullet thickness", &cfg::esp::bullet_tracer::thickness, 1.0f, 4.0f, "%.1f");
                                } else {
                                    ImGui::TextColored(kSignalOK, "AUTO  collision reach + presentation");
                                }
                                ImGui::TextWrapped(MapRaytrace::IsReady()
                                    ? "Stops at the nearest map surface or player."
                                    : "Map collision unavailable: new trails are paused.");
                            }
                            ImGui::EndDisabled();

                        }
                        ImGui::EndGroup();
                        ImGui::Spacing();
                        ImGui::BeginGroup();
                        {
                            ImGui::EndGroup();
                            EndSettingsCard(true);
                            BeginSettingsCard("Player information");
                            ImGui::BeginGroup();
                            Toggle("Health", &cfg::esp::health);
                            if (cfg::esp::health)
                                Toggle("Health Number", &cfg::esp::health_number);
                            Toggle("Armor", &cfg::esp::armor);
                            Toggle("Game-spotted only", &cfg::esp::spotted);
                            ImGui::SetItemTooltip("Uses CS2's spotted state.");
                            Toggle("Show Team", &cfg::esp::team);
                            Toggle("Spotted only", &cfg::esp::spotted_only);
                            ImGui::SetItemTooltip("Only render enemies you can actually see");
                            Toggle("Distance", &cfg::esp::distance);
                            Toggle("Headshot line", &cfg::esp::headshot_line);
                            ImGui::SetItemTooltip("Line from crosshair to enemy head");
                        }
                        ImGui::EndGroup();
                        EndSettingsCard(true);
                    }

                    if (BeginSettingsCard("Flags")) {
                        ImGui::BeginGroup();
                        {
                            Toggle("Flashed", &cfg::esp::flags::flashed);
                            ImGui::BeginDisabled(!cfg::esp::flags::flashed);
                            {
                                ImGui::SameLine();
                                ImGui::ColorEdit4("Team##fl", cfg::esp::colors::flags::flashed_team.data(), color_flags);
                                ImGui::SameLine();
                                ImGui::ColorEdit4("Enemy##fl", cfg::esp::colors::flags::flashed_enemy.data(), color_flags);
                            }
                            ImGui::EndDisabled();

                            Toggle("Reloading", &cfg::esp::flags::reloading);
                            ImGui::BeginDisabled(!cfg::esp::flags::reloading);
                            {
                                ImGui::SameLine();
                                ImGui::ColorEdit4("Team##rl", cfg::esp::colors::flags::reloading_team.data(), color_flags);
                                ImGui::SameLine();
                                ImGui::ColorEdit4("Enemy##rl", cfg::esp::colors::flags::reloading_enemy.data(), color_flags);
                            }
                            ImGui::EndDisabled();

                            Toggle("Defusing", &cfg::esp::flags::defusing);
                            ImGui::BeginDisabled(!cfg::esp::flags::defusing);
                            {
                                ImGui::SameLine();
                                ImGui::ColorEdit4("Team##df", cfg::esp::colors::flags::defusing_team.data(), color_flags);
                                ImGui::SameLine();
                                ImGui::ColorEdit4("Enemy##df", cfg::esp::colors::flags::defusing_enemy.data(), color_flags);
                            }
                            ImGui::EndDisabled();

                            Toggle("Scoped", &cfg::esp::flags::scoped);
                            ImGui::BeginDisabled(!cfg::esp::flags::scoped);
                            {
                                ImGui::SameLine();
                                ImGui::ColorEdit4("Team##sc", cfg::esp::colors::flags::scoped_team.data(), color_flags);
                                ImGui::SameLine();
                                ImGui::ColorEdit4("Enemy##sc", cfg::esp::colors::flags::scoped_enemy.data(), color_flags);
                            }
                            ImGui::EndDisabled();

                            Toggle("Has C4", &cfg::esp::flags::has_c4);
                            ImGui::BeginDisabled(!cfg::esp::flags::has_c4);
                            {
                                ImGui::SameLine();
                                ImGui::ColorEdit4("Team##c4", cfg::esp::colors::flags::c4_team.data(), color_flags);
                                ImGui::SameLine();
                                ImGui::ColorEdit4("Enemy##c4", cfg::esp::colors::flags::c4_enemy.data(), color_flags);
                            }
                            ImGui::EndDisabled();
                        }
                        ImGui::EndGroup();
                        ImGui::Spacing();
                        ImGui::BeginGroup();
                        {
                            Toggle("Name", &cfg::esp::flags::name);
                            Toggle("Money", &cfg::esp::flags::money);
                            Toggle("Weapon", &cfg::esp::flags::weapon);
                            Toggle("Ammo", &cfg::esp::flags::ammo);
                            Toggle("Ping", &cfg::esp::flags::ping);
                        }
                        ImGui::EndGroup();
                        EndSettingsCard(true);
                    }
                }
                else if (active_tab == Tab::WORLD)
                {

                    if (BeginSettingsCard("Bomb")) {
                        Toggle("Bomb ESP", &cfg::esp::bomb);
                        ImGui::SameLine();
                        ImGui::ColorEdit4("Bomb color", cfg::esp::colors::bomb.data(), color_flags);
                        Toggle("Bomb Location", &cfg::world::bomb::location);
                        Toggle("Bomb Timer", &cfg::world::bomb::timer);
                        EndSettingsCard(true);
                    }

                    if (BeginSettingsCard("Spectators")) {
                        Toggle("Enable", &cfg::world::spectators::enabled);
                        if (cfg::world::spectators::enabled) {
                            Toggle("Detailed", &cfg::world::spectators::detailed);
                            Toggle("Only Self", &cfg::world::spectators::self_only);
                            ImGui::SetItemTooltip("Only display users spectating you");
                        }
                        EndSettingsCard(true);
                    }

                    if (BeginSettingsCard("Crosshair")) {
                        Toggle("Enable", &cfg::world::crosshair::enabled);
                        ImGui::BeginDisabled(!cfg::world::crosshair::enabled);
                        {
                            Toggle("Snipers only", &cfg::world::crosshair::sniper_only);
                            Toggle("Center dot", &cfg::world::crosshair::center_dot);
                            Toggle("Outline", &cfg::world::crosshair::outline);
                            ImGui::ColorEdit4("Color", cfg::world::crosshair::color.data(), color_flags);
                            ImGui::SliderFloat("Gap", &cfg::world::crosshair::gap, 0.0f, 20.0f, "%.1f");
                            ImGui::SliderFloat("Length", &cfg::world::crosshair::length, 1.0f, 20.0f, "%.1f");
                            ImGui::SliderFloat("Thickness", &cfg::world::crosshair::thickness, 1.0f, 5.0f, "%.1f");
                            if (cfg::world::crosshair::center_dot)
                                ImGui::SliderFloat("Dot size", &cfg::world::crosshair::center_dot_size, 0.5f, 5.0f, "%.1f");
                            if (cfg::world::crosshair::outline)
                                ImGui::SliderFloat("Outline size", &cfg::world::crosshair::outline_thickness, 0.5f, 3.0f, "%.1f");
                        }
                        ImGui::EndDisabled();
                        EndSettingsCard(true);
                    }

                    if (BeginSettingsCard("Velocity")) {
                        Toggle("Enable", &cfg::world::velocity::enabled);
                        ImGui::BeginDisabled(!cfg::world::velocity::enabled);
                        {
                            ImGui::SliderInt("Sample rate", &cfg::world::velocity::sample_rate, 1, 100);
                            ImGui::SliderFloat("Sample length", &cfg::world::velocity::sample_length, 1.0f, 20.0f, "%.1f");
                        }
                        ImGui::EndDisabled();
                        EndSettingsCard(true);
                    }

                    if (BeginSettingsCard("Map geometry")) {
                        Toggle("Wireframe Map", &cfg::esp::wireframe);
                        ImGui::BeginDisabled(!cfg::esp::wireframe);
                        ImGui::Combo("Mode", &cfg::esp::wireframe_mode, "Overlay\0Full map\0");
                        Toggle("Dark map", &cfg::esp::wireframe_blackout);
                        ImGui::SetItemTooltip("Hides the game behind solid black, leaving the wireframe, ESP, radar and SourceSight UI visible.");
                        ImGui::BeginDisabled(!cfg::esp::wireframe_blackout);
                        Toggle("Weapon & hands", &cfg::esp::viewmodel_wireframe::enabled);
                        ImGui::SetItemTooltip("Draws a stylized wireframe viewmodel for your active gun, knife, grenade or C4, with its exact name and ammo.");
                        ImGui::EndDisabled();
                        if (AutoCalibration::Advanced()) {
                            if (ImGui::CollapsingHeader("Advanced rendering", ImGuiTreeNodeFlags_DefaultOpen)) {
                                Toggle("X-ray lines", &cfg::esp::wireframe_full_xray);
                                ImGui::SetItemTooltip("Draws rear edges through the map. Keep off to show only the nearest surface; the depth pass suppresses geometry behind it.");
                                ImGui::BeginDisabled(!cfg::esp::wireframe_blackout || !cfg::esp::viewmodel_wireframe::enabled);
                                ImGui::SliderFloat("Viewmodel size", &cfg::esp::viewmodel_wireframe::scale, .7f, 1.35f, "%.2f");
                                ImGui::SliderFloat("Viewmodel opacity", &cfg::esp::viewmodel_wireframe::opacity, .2f, 1.f, "%.2f");
                                ImGui::EndDisabled();
                                if (cfg::esp::wireframe_mode == 1) {
                                    ImGui::SliderFloat("Panel fill", &cfg::esp::wireframe_panel_opacity, 0.f, .35f, "%.2f");
                                    ImGui::SetItemTooltip("Graphite fill on the nearest surface only (default 0.10 = 10%%). Independent of line color.");
                                }
                            }
                        } else {
                            ImGui::TextColored(kSignalOK, "AUTO  detail + line scale + depth");
                        }
                        ImGui::EndDisabled();
                        if (MapRaytrace::IsReady())
                            ImGui::TextDisabled("%s / %zu triangles", MapRaytrace::CurrentMap().c_str(),
                                                MapRaytrace::TriangleCount());
                        else
                            ImGui::TextWrapped("Geometry unavailable or loading. Missing map files are retried automatically.");
                        // Full-map rendering uses the depth-tested mesh pass;
                        // overlay-only distance/budget controls do not apply.
                        if (AutoCalibration::Advanced()) {
                            ImGui::BeginDisabled(!cfg::esp::wireframe || cfg::esp::wireframe_mode == 1);
                            ImGui::SliderFloat("Max distance", &cfg::esp::wireframe_max_dist, 500, 10000, "%.0f u");
                            ImGui::SliderFloat("Opacity", &cfg::esp::wireframe_opacity, 0, 1, "%.2f");
                            ImGui::ColorEdit3("Color", cfg::esp::wireframe_color.data(), ImGuiColorEditFlags_NoInputs);
                            ImGui::SliderInt("Detail", &cfg::esp::wireframe_budget, 500, 8000, "%d edges");
                            ImGui::SetItemTooltip("Maximum edges per frame. Lower detail reduces rendering cost; nearby geometry is prioritized.");
                            ImGui::EndDisabled();
                        }
                        if (cfg::esp::wireframe_mode == 1)
                            ImGui::TextDisabled("Full-map mode uses the complete depth mesh; overlay distance and edge budget are not used.");
                        EndSettingsCard(true);
                    }

                    if (BeginSettingsCard("Radar")) {
                        Toggle("Enable", &cfg::world::radar::enabled);
                        ImGui::BeginDisabled(!cfg::world::radar::enabled);
                        {
                            if (AutoCalibration::Advanced()) {
                                ImGui::SliderFloat("Opacity", &cfg::world::radar::opacity, 0, 1, "%.2f");
                                Toggle("Match CS2 minimap", &cfg::world::radar::minimap);
                                Toggle("Apply CS2 zoom", &cfg::world::radar::auto_sync);
                                ImGui::SliderFloat("CS2 radar zoom", &cfg::world::radar::zoom, .25f, 1.f, "%.2f");
                                ImGui::SliderFloat("Scale correction", &cfg::world::radar::scale_correction, .75f, 1.25f, "%.2f");
                                ImGui::SliderFloat("Calibrated range", &cfg::world::radar::range, 100.f, 8000.f, "%.0f u");
                                ImGui::SetItemTooltip("World radius at zoom 0.70. Lower this if dots cluster too close to the center. Calibrate again after changing maps; collision bounds cannot supply the minimap scale.");
                                ImGui::SliderFloat("HUD scaling", &cfg::world::radar::hud_scale, .5f, 2.f, "%.2f");
                                ImGui::SetItemTooltip("Match hud_scaling. Scales the overlay rectangle from its top-left corner.");
                                ImGui::SliderFloat("Radar HUD size", &cfg::world::radar::hud_size, .5f, 2.f, "%.2f");
                                ImGui::SetItemTooltip("Match cl_hud_radar_scale. Adjust position separately if the HUD moves.");
                                ImGui::DragFloat2("Position", &cfg::world::radar::pos.x, 1.f);
                                ImGui::DragFloat2("Base size", &cfg::world::radar::size.x, 1.f, 40.f, 1000.f, "%.0f", ImGuiSliderFlags_AlwaysClamp);
                                ImGui::DragFloat2("Offset", &cfg::world::radar::offset.x, 1.f);
                                if (ImGui::Button("Use current resolution"))
                                    AutoCalibration::TrackViewport(ImGui::GetIO().DisplaySize.y);
                                ImGui::SetItemTooltip("Remember this game-window height so the calibrated radar scales with resolution changes.");
                                ImGui::TextWrapped("Align the guide circle and center with CS2 first, then adjust calibrated range until teammate dots match. Use centered radar with dynamic zoom off.");
                                Toggle("Disable Rotation", &cfg::world::radar::no_rotate);
                            } else {
                                ImGui::TextColored(kSignalOK, "AUTO  live viewport scaling");
                                ImGui::TextWrapped("Uses the standard CS2 baseline: cl_radar_scale 0.7, cl_hud_radar_scale 1, hud_scaling 1. Drag the radar itself to place it; use Advanced if your CS2 values differ.");
                                ImGui::TextDisabled("Reference height  %.0f px", cfg::world::radar::calibration_height);
                                if (!MapRaytrace::IsReady())
                                    ImGui::TextWrapped("Map overview scale cannot be inferred until collision data is ready; the saved range remains in use.");
                                }
                        }
                        ImGui::EndDisabled();
                        EndSettingsCard(true);
                    }
                }
                else if (active_tab == Tab::SOUND_ESP)
                {

                    BeginSettingsCard("Activation");
                    // Enable checkbox outside the section so it's always clickable
                    Toggle("Enable Sound ESP", &cfg::sound_esp::enabled);
                    ImGui::Spacing();

                    EndSettingsCard(true);
                    if (BeginSettingsCard("Sound ESP", cfg::sound_esp::enabled)) {
                        ImGui::BeginGroup();
                        {
                            Toggle("Footsteps", &cfg::sound_esp::footsteps);
                            ImGui::BeginDisabled(!cfg::sound_esp::footsteps);
                            {
                                ImGui::SameLine();
                                ImGui::ColorEdit4("Footsteps color", cfg::sound_esp::footsteps_color.data(), color_flags);
                            }
                            ImGui::EndDisabled();

                            Toggle("Gunshots", &cfg::sound_esp::gunshots);
                            ImGui::BeginDisabled(!cfg::sound_esp::gunshots);
                            {
                                ImGui::SameLine();
                                ImGui::ColorEdit4("Gunshots color", cfg::sound_esp::gunshots_color.data(), color_flags);
                            }
                            ImGui::EndDisabled();

                            ImGui::SliderFloat("Max distance", &cfg::sound_esp::max_distance, 100.0f, 5000.0f, "%.0f u");
                            ImGui::SliderFloat("Duration", &cfg::sound_esp::duration, 0.5f, 10.0f, "%.1f s");
                            ImGui::SliderFloat("Fade time", &cfg::sound_esp::fade_time, 0.1f, 5.0f, "%.1f s");
                            ImGui::SliderFloat("Footprint size", &cfg::sound_esp::footprint_size, 2.0f, 32.0f, "%.1f");
                        }
                        ImGui::EndGroup();
                        EndSettingsCard(true, cfg::sound_esp::enabled);
                    }

                    ImGui::Spacing();

                }
                else if (active_tab == Tab::SETTINGS)
                {

                    if (BeginSettingsCard("Setup mode")) {
                        ImGui::TextWrapped("Simple keeps feature switches in your hands while automatically tuning visual density, line scale, tracer presentation and resolution scaling.");
                        ImGui::Spacing();
                        const float mode_width = (ImGui::GetContentRegionAvail().x - 8.f) * .5f;
                        const bool simple_pressed = cfg::settings::advanced_controls
                            ? ImGui::Button("Simple", ImVec2(mode_width, 30))
                            : PrimaryButton("Simple", ImVec2(mode_width, 30));
                        ImGui::SameLine(0.f, 8.f);
                        const bool advanced_pressed = cfg::settings::advanced_controls
                            ? PrimaryButton("Advanced", ImVec2(mode_width, 30))
                            : ImGui::Button("Advanced", ImVec2(mode_width, 30));
                        if (simple_pressed) {
                            cfg::settings::advanced_controls = false;
                            AutoCalibration::ApplySimple(screen.x, screen.y);
                        }
                        if (advanced_pressed) cfg::settings::advanced_controls = true;
                        ImGui::Spacing();
                        ImGui::TextDisabled(cfg::settings::advanced_controls
                            ? "Granular appearance and calibration controls are visible."
                            : "Automatic setup is active and updates with the game viewport.");
                        ImGui::TextWrapped("Exact minimap range cannot be derived from map collision bounds. Simple keeps your saved range; Advanced exposes manual correction.");
                        EndSettingsCard(true);
                    }

                    if (BeginSettingsCard("Profiles")) {
                        static std::vector<std::string> profiles;
                        static int sel = -1;
                        static char new_name[48]{};
                        static std::string pending_delete;
                        static bool profiles_ready = false;

                        auto refresh_profiles = [&]() {
                            if(preview_mode) { profiles.clear(); sel=-1; return; }
                            profiles = Config::ListProfiles();
                            const std::string active = Config::GetActiveProfile();
                            sel = 0;
                            for (size_t i = 0; i < profiles.size(); ++i)
                                if (profiles[i] == active) { sel = (int)i; break; }
                            if (profiles.empty()) sel = -1;
                        };

                        if (!profiles_ready) {
                            profiles_ready = true;
                            refresh_profiles();
                        }

                        if (preview_mode)
                            ImGui::TextDisabled("Profile actions disabled in offline preview.");
                        ImGui::BeginDisabled(preview_mode);
                        ImGui::TextColored(studioMuted, "Active profile: %s",
                                           Config::GetActiveProfile().c_str());
                        ImGui::SetNextItemWidth(-1);
                        const char* preview = (sel >= 0 && sel < (int)profiles.size())
                                                  ? profiles[sel].c_str()
                                                  : "none";
                        if (ImGui::BeginCombo("##profiles", preview)) {
                            for (int i = 0; i < (int)profiles.size(); ++i) {
                                if (ImGui::Selectable(profiles[i].c_str(), i == sel))
                                    sel = i;
                            }
                            ImGui::EndCombo();
                        }

                        if (PrimaryButton("Load Profile", ImVec2(-1, 30))) {
                            if (sel >= 0 && Config::LoadProfile(profiles[sel]))
                                refresh_profiles();
                        }
                        if (PrimaryButton("Save to this Profile", ImVec2(-1, 30))) {
                            if (sel >= 0 && Config::SaveProfile(profiles[sel]))
                                refresh_profiles();
                        }
                        ImGui::SetItemTooltip(
                            "Overwrite the selected profile with the current settings.");

                        ImGui::Spacing();
                        ImGui::Separator();
                        ImGui::Spacing();

                        ImGui::SetNextItemWidth(-1);
                        ImGui::InputTextWithHint("##new_profile", "New profile name",
                                                 new_name, sizeof(new_name));
                        if (PrimaryButton("Create New Profile from Current Settings",
                                           ImVec2(-1, 30))) {
                            const std::string name(new_name);
                            if (!name.empty() && Config::SaveProfile(name)) {
                                new_name[0] = '\0';
                                refresh_profiles();
                            }
                        }

                        if (sel >= 0) {
                            ImGui::Spacing();
                            if (DangerButton("Delete Selected Profile", ImVec2(-1, 26))) {
                                pending_delete = profiles[sel];
                                ImGui::OpenPopup("Delete Profile?");
                            }
                        }

                        if (ImGui::BeginPopupModal("Delete Profile?", nullptr,
                                                   ImGuiWindowFlags_AlwaysAutoResize)) {
                            ImGui::TextWrapped("Delete profile '%s'?\nThis cannot be undone.",
                                               pending_delete.c_str());
                            ImGui::Spacing();
                            if (DangerButton("Delete", ImVec2(110, 26))) {
                                if (Config::DeleteProfile(pending_delete))
                                    refresh_profiles();
                                ImGui::CloseCurrentPopup();
                            }
                            ImGui::SameLine();
                            if (PrimaryButton("Cancel", ImVec2(110, 26)))
                                ImGui::CloseCurrentPopup();
                            ImGui::EndPopup();
                        }

                        ImGui::EndDisabled();
                        EndSettingsCard(true);
                    }

                    if (BeginSettingsCard("Display")) {
                        if (Toggle("Streamproof", &cfg::settings::streamproof))
                        {
                            Window::SetAffinity(
                                Window::hwnd,
                                cfg::settings::streamproof ? WindowAffinity::Invisible : WindowAffinity::Disabled
                            );
                        }
                        Toggle("Watermark", &cfg::settings::watermark);
                        if (Toggle("VSync", &cfg::settings::vsync))
                            Window::vsync = cfg::settings::vsync;
                        Toggle("Free CPU", &cfg::settings::free_cpu);
                        ImGui::SetItemTooltip("Let the CPU sleep to free resources.");
                        Toggle("Panic key (F9)", &cfg::settings::panic_key);
                        ImGui::SetItemTooltip("Press F9 to instantly disable all cheats.");
                        EndSettingsCard(true);
                    }

                    if (BeginSettingsCard("Visual quality")) {
                        if (!AutoCalibration::Advanced()) {
                            ImGui::TextColored(kSignalOK, "AUTO  balanced for this viewport");
                            ImGui::TextWrapped("Switch to Advanced mode to choose a fixed visual-quality preset.");
                            EndSettingsCard(true);
                        } else {
                        static int selected_preset = 1;
                        static const char* preset_names[] = {"Conservative", "Balanced", "Detail"};
                        ImGui::TextWrapped("Tune visual density and line quality together. Presets change presentation only; they never enable features or automation.");
                        ImGui::SetNextItemWidth(-1);
                        ImGui::Combo("##visual-preset", &selected_preset, preset_names, 3);
                        if (PrimaryButton("Apply visual preset", ImVec2(-1, 30))) {
                            pending_visual_preset = selected_preset;
                            ImGui::OpenPopup("Confirm visual preset");
                        }
                        if (ImGui::BeginPopupModal("Confirm visual preset", nullptr,
                                                   ImGuiWindowFlags_AlwaysAutoResize)) {
                            ImGui::TextWrapped("Apply the %s preset to visual quality settings?",
                                               preset_names[std::clamp(pending_visual_preset, 0, 2)]);
                            ImGui::TextDisabled("Existing enabled/disabled states and automation settings stay unchanged.");
                            ImGui::Spacing();
                            if (PrimaryButton("Apply", ImVec2(110, 28))) {
                                PreviewConfirmVisualPreset();
                                ImGui::CloseCurrentPopup();
                            }
                            ImGui::SameLine();
                            if (ImGui::Button("Cancel", ImVec2(110, 28))) {
                                PreviewRequestVisualPreset(-1);
                                ImGui::CloseCurrentPopup();
                            }
                            ImGui::EndPopup();
                        }
                        EndSettingsCard(true);
                        }
                    }

                    if (BeginSettingsCard("Diagnostics")) {
                        const auto snapshot = Diagnostics::Snapshot();
                        ImGui::TextColored(kTextSecondary, "Version  %s", snapshot.version.c_str());
                        ImGui::TextWrapped("Cache state  %s", snapshot.cache.state.c_str());
                        ImGui::Text("Cache generation  %llu", static_cast<unsigned long long>(snapshot.cache.generation));
                        ImGui::Text("Cache age  %lld ms  / refresh  %lld ms",
                                    static_cast<long long>(snapshot.cache.age_ms),
                                    static_cast<long long>(snapshot.cache.refresh_ms));
                        const ImVec4 config_color = snapshot.config.code == "none" ? kSignalOK : kSignalWarn;
                        ImGui::TextColored(config_color, "Config  %s", snapshot.config.code.c_str());
                        if (!snapshot.config.message.empty())
                            ImGui::TextWrapped("%s", snapshot.config.message.c_str());
                        if (snapshot.cpu_ms)
                            ImGui::Text("Render CPU  %.3f ms", *snapshot.cpu_ms);
                        else
                            ImGui::TextDisabled("Render CPU  unavailable");
                        if (snapshot.gpu_ms)
                            ImGui::Text("GPU  %.3f ms", *snapshot.gpu_ms);
                        else
                            ImGui::TextDisabled("GPU  unavailable");

                        ImGui::Spacing();
                        static char report_destination[256] = "sourcesight-diagnostics.json";
                        static std::string pending_destination;
                        static std::string export_status;
                        ImGui::TextDisabled("Sanitized report (user-invoked, no automatic export)");
                        ImGui::BeginDisabled(preview_mode);
                        ImGui::SetNextItemWidth(-1);
                        ImGui::InputTextWithHint("##diagnostics-destination", "Report filename",
                                                 report_destination, sizeof(report_destination));
                        if (PrimaryButton("Export sanitized report", ImVec2(-1, 30))) {
                            pending_destination = report_destination;
                            std::error_code exists_error;
                            const bool already_exists = !pending_destination.empty() &&
                                std::filesystem::exists(pending_destination, exists_error);
                            if (already_exists) {
                                export_status = "Destination already exists; choose a new filename.";
                            } else {
                                std::string error;
                                export_status = Diagnostics::ExportSanitizedReport(pending_destination, error)
                                    ? "Diagnostic report exported."
                                    : error;
                            }
                        }
                        ImGui::EndDisabled();
                        if (preview_mode)
                            ImGui::TextDisabled("Export disabled in offline preview.");
                        if (!export_status.empty())
                            ImGui::TextDisabled("%s", export_status.c_str());

                        EndSettingsCard(true);
                    }

                    if (BeginSettingsCard("Screen capture")) {
                        ImGui::BeginDisabled(preview_mode);
                        ImGui::TextWrapped("Captures the composited screen, so the game and the overlay end up in the same image.");
                        static std::string shot_status;
                        if (PrimaryButton("Take screenshot")) {
                            const std::string out = ScreenCapture::DefaultScreenshotPath();
                            shot_status = ScreenCapture::CaptureScreenshot(out)
                                ? ("Saved " + out)
                                : "Screenshot failed (see log)";
                        }
                        if (!shot_status.empty())
                            ImGui::TextDisabled("%s", shot_status.c_str());
                        ImGui::Spacing();
                        ImGui::SliderInt("Recording FPS", &cfg::capture::fps, 15, 120, "%d fps");
                        cfg::capture::fps = std::clamp(cfg::capture::fps, 1, 240);
                        static char dir_buf[256]{};
                        static bool dir_init = false;
                        if (!dir_init) {
                            std::strncpy(dir_buf, cfg::capture::output_dir.c_str(), sizeof(dir_buf) - 1);
                            dir_buf[sizeof(dir_buf) - 1] = '\0';
                            dir_init = true;
                        }
                        if (ImGui::InputText("Folder##capture", dir_buf, sizeof(dir_buf)))
                            cfg::capture::output_dir = dir_buf[0] ? dir_buf : "captures";
                        else if (!ImGui::IsItemActive() && std::string(dir_buf) != cfg::capture::output_dir) {
                            std::strncpy(dir_buf, cfg::capture::output_dir.c_str(), sizeof(dir_buf) - 1);
                            dir_buf[sizeof(dir_buf) - 1] = '\0';
                        }
                        ImGui::SetItemTooltip("Folder for screenshots and recordings.");
                        static std::string rec_status;
                        const bool recording = ScreenCapture::IsRecording();
                        if (recording)
                            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "REC  %s",
                                ScreenCapture::ActiveRecordingPath().c_str());
                        if (!recording) {
                            if (PrimaryButton("Start recording (game + overlay)")) {
                                if (ScreenCapture::RecordEntireScreen("", cfg::capture::fps))
                                    rec_status = "Recording to " + ScreenCapture::ActiveRecordingPath();
                                else
                                    rec_status = "Could not start (install ffmpeg and add it to PATH)";
                            }
                        } else if (DangerButton("Stop recording", ImVec2(-1, 30))) {
                            const std::string stopped = ScreenCapture::ActiveRecordingPath();
                            ScreenCapture::StopRecording();
                            rec_status = stopped.empty() ? "Recording stopped" : ("Saved " + stopped);
                        }
                        if (!rec_status.empty() && !recording)
                            ImGui::TextDisabled("%s", rec_status.c_str());
                        ImGui::EndDisabled();
                        if (preview_mode)
                            ImGui::TextDisabled("Capture actions disabled in offline preview.");
                        EndSettingsCard(true);
                    }

                    if (BeginSettingsCard("Notes")) {
                        ImGui::TextWrapped(
                            "If you experience bad performance/lag:\n"
                            "  - Disable ESP VSync\n"
                            "  - Disable VSync in game: Advanced Video > V-Sync: Disabled\n"
                            "  - Last resort: Disable Free CPU option"
                        );
                        EndSettingsCard(true);
                    }

#ifdef _DEBUG
                    if (BeginSettingsCard("Dev")) {
                        if (Toggle("Console", &cfg::dev::console))
                            if (!cfg::dev::console) LogHelper::Free();

                        static int key_out;
                        if (ImGui::Button("Open Menu Key"))
                        {
                            for (int i = ImGuiKey_NamedKey_BEGIN; i < ImGuiKey_NamedKey_END; i++)
                            {
                                if (ImGui::IsKeyPressed((ImGuiKey)i))
                                {
                                    key_out = i;
                                    LOGF(VERBOSE, "Changed the open menu key to {}", key_out);
                                    break;
                                }
                            }
                        }

                        ImGui::SliderInt("Cache Refresh Rate", &cfg::dev::cache_refresh_rate, 0, 100, "%dms");
                        Toggle("Force Show Flags", &cfg::dev::force_show_flags);
                        EndSettingsCard(true);
                    }
#endif
                }

            ImGui::PopStyleVar();
            ImGui::EndTable();
        }
        ImGui::PopStyleVar();
        ImGui::EndChild();
        ImGui::SetCursorPos(ImVec2(rail+28,size.y-34));
        const auto profile=Config::GetActiveProfile();
        ImGui::TextDisabled("PROFILE  /  %s",profile.c_str());
        ImGui::SameLine(0,24);
        if(float(ImGui::GetTime())<saved_until)
            ImGui::TextColored(save_ok?kSignalOK:kSignalErr,save_ok?"Changes saved":"Save failed");
        else
            ImGui::TextDisabled(cfg::enabled?"Overlay active":"Overlay paused");
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
}

void Menu::SetupStyles() {
    ImGuiStyle& style = ImGui::GetStyle();

    ImGui::StyleColorsDark(&style);
    style.Colors[ImGuiCol_Text] = studioText;
    style.Colors[ImGuiCol_TextDisabled] = studioMuted;
    style.Colors[ImGuiCol_WindowBg] = studioCanvas;
    style.Colors[ImGuiCol_ChildBg] = ImVec4(0,0,0,0);
    style.Colors[ImGuiCol_PopupBg] = studioCard;
    style.Colors[ImGuiCol_Border] = studioBorder;
    style.Colors[ImGuiCol_FrameBg] = ImVec4(.22f,.24f,.25f,1);
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(.28f,.31f,.30f,1);
    style.Colors[ImGuiCol_FrameBgActive] = ImVec4(.33f,.38f,.34f,1);
    style.Colors[ImGuiCol_Button] = studioCard;
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(.29f,.34f,.30f,1);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(.35f,.41f,.35f,1);
    style.Colors[ImGuiCol_Header] = ImVec4(.26f,.32f,.27f,1);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(.33f,.40f,.33f,1);
    style.Colors[ImGuiCol_HeaderActive] = ImVec4(.36f,.44f,.36f,1);
    style.Colors[ImGuiCol_CheckMark] = studioAccent;
    style.Colors[ImGuiCol_SliderGrab] = studioAccent;
    style.Colors[ImGuiCol_SliderGrabActive] = studioAccentBright;
    style.Colors[ImGuiCol_Separator] = studioBorder;
    style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0,0,0,0);
    style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(.39f,.43f,.41f,1);
    style.Colors[ImGuiCol_ScrollbarGrabHovered] = studioAccent;
    style.Colors[ImGuiCol_ScrollbarGrabActive] = studioAccent;
    style.Colors[ImGuiCol_ResizeGrip] = ImVec4(0,0,0,0);
    style.Colors[ImGuiCol_ResizeGripHovered] = studioAccentSoft;
    style.Colors[ImGuiCol_ResizeGripActive] = studioAccent;
    style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(.65f,.80f,.58f,.5f);
    style.FrameBorderSize = 0;
    style.ChildBorderSize = 1;
    style.WindowPadding = ImVec2(20,20);
    style.FramePadding = ImVec2(12,7);
    style.ItemSpacing = ImVec2(12,12);
    style.ItemInnerSpacing = ImVec2(10,6);
    style.ScrollbarSize = 7;
    style.WindowRounding = 24;
    style.ChildRounding = 18;
    style.FrameRounding = 10;
    style.PopupRounding = 12;
    style.GrabRounding = 8;
    style.DisabledAlpha = .48f;

    auto& io = ImGui::GetIO();
    io.Fonts->Clear();

    // Prefer JetBrainsMono Nerd Font when installed. Keep platform fallbacks
    // so the UI remains usable on machines without the optional font package.
    const char* font_paths[] = {
        "C:\\Windows\\Fonts\\JetBrainsMonoNerdFont-Regular.ttf",
        "C:\\Windows\\Fonts\\JetBrainsMonoNerdFontMono-Regular.ttf",
        "C:\\Windows\\Fonts\\arial.ttf",
    };

    ImFont* ui_font = nullptr;
    for (const char* path : font_paths) {
        if (std::filesystem::exists(path)) {
            ui_font = io.Fonts->AddFontFromFileTTF(path, 15.0f);
            if (ui_font)
                break;
        }
    }
    if (!ui_font)
        ui_font = io.Fonts->AddFontDefault();

    ImFontConfig merge_icon_cfg{};
    merge_icon_cfg.FontDataOwnedByAtlas = false;
    merge_icon_cfg.MergeMode = true;
    merge_icon_cfg.GlyphOffset = Vec2_t(0, 3.5f);

    static const ImWchar icon_ranges[] = { 0xE100, 0xE108, 0 };
    io.Fonts->AddFontFromMemoryTTF(icons_font, icons_font_len, 18.f, &merge_icon_cfg, icon_ranges);
    io.FontDefault = ui_font;
    font_heading = ui_font;
    for (const char* path : font_paths) {
        if (std::filesystem::exists(path)) {
            if (auto* heading = io.Fonts->AddFontFromFileTTF(path, 27.0f)) {
                font_heading = heading;
                break;
            }
        }
    }
}

void Menu::RenderStartupHelpImpl() {
    static bool has_opened_menu = false;
    if (has_opened_menu) return;

    auto& io = ImGui::GetIO();
    auto screen = io.DisplaySize;
    auto d = ImGui::GetBackgroundDrawList();

    if (Renderer::IsOpen())
        has_opened_menu = true;

    auto help = "To OPEN the menu: Insert key"
        "\n\t\t\t\tWhile the menu is open, all clicks go to the menu/overlay, none to the game"
        "\n\t\t\t\tKeyboard navigation also works: Arrow/Tab to move, Space/Enter to toggle"
        "\n\t\t\t\tEnd: save config and exit";
    auto size = ImGui::CalcTextSize(help);

    ImVec2 help_pos(screen.x / 2 - size.x / 2 - 20, 70);
    ImVec2 help_size(size.x + 40, size.y + 20);
    DrawGlass(d, help_pos, help_size, 8.0f, kSurfaceDeep, 0.95f);

    d->AddText(
        ImVec2(screen.x / 2 - size.x / 2, 80),
        IM_COL32(220, 225, 235, 255),
        help
    );
}
