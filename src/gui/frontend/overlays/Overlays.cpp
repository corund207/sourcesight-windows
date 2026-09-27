#include "Overlays.hpp"
#include "RadarProjection.hpp"

#include "gui/renderer/Renderer.hpp" // Circular dependency
#include "gui/frontend/menu/Menu.hpp" // Circular dependency
#include "gui/frontend/menu/Theme.hpp"
#include "assets/fonts/WeaponIcons.h"
#include "core/engine/classes/MapRaytrace.hpp"

bool Overlays::Init() {
    return GetInstance().InitImpl();
}

void Overlays::Render() {
    return GetInstance().RenderImpl();
}

bool Overlays::InitImpl() {
    auto& io = ImGui::GetIO();

    ImFontConfig file_font_cfg{};
    ImFontConfig embedded_font_cfg{};
    embedded_font_cfg.FontDataOwnedByAtlas = false;

	this->font = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\consola.ttf", 12.0f, &file_font_cfg);
	this->font_alt = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\arial.ttf", 14.0f, &file_font_cfg);
    this->font_icons = io.Fonts->AddFontFromMemoryTTF(weapon_icon_font, weapon_icon_font_len, 16.0f, &embedded_font_cfg);

	ImFontConfig merge_icon_cfg{};
	merge_icon_cfg.FontDataOwnedByAtlas = false;
	merge_icon_cfg.MergeMode = true;

	static const ImWchar icon_ranges[] = { 0xE000, 0xE046, 0 };
	io.Fonts->AddFontFromMemoryTTF(weapon_icon_font, weapon_icon_font_len, 12.f, &merge_icon_cfg, icon_ranges);

    // Pre allocate buffer
    const int rate = std::clamp(cfg::world::velocity::sample_rate, 1, 100);
    const float length = std::clamp(cfg::world::velocity::sample_length, 1.0f, 20.0f);
    this->vel_buffer.resize(static_cast<size_t>(rate * length));

    return this->font && this->font_alt && this->font_icons;
}

void Overlays::RenderImpl() {
    ImGui::PushFont(this->font);
    {
        RenderWatermark();



    #ifdef _DEBUG
        RenderDebugWindow();
    #endif

    }
    ImGui::PopFont();

    // Keep application status visible, but never display stale world/player data.
    if (!Cache::Status().ready()) return;

    ImGui::PushFont(this->font_alt);
    {
        RenderSpectatorList();
        RenderSpeedChart();
        RenderRadar();
        RenderBomb();
        RenderBombBorder();
    }
    ImGui::PopFont();
}

void Overlays::RenderWatermark() {
    if (!cfg::settings::watermark)
        return;

    auto& io = ImGui::GetIO();
    auto d = ImGui::GetBackgroundDrawList();

    auto snapshot = Cache::CopySnapshot();
    auto& globals = snapshot.globals;

    static int margin = 10;
    static int padding = 10;
    std::string watermark_string = "SourceSight";

    watermark_string += std::format(" | {}fps", (int)io.Framerate);

    if (globals.in_match)
        watermark_string += std::format(" | {}", globals.map_name);

    auto size = ImGui::CalcTextSize(watermark_string.data());

    auto rect_start = ImVec2(io.DisplaySize.x - margin - padding * 2 - size.x, margin);
    auto rect_end = ImVec2(io.DisplaySize.x - margin, margin + size.y + padding);
    auto pos = ImVec2(rect_start.x + padding, rect_start.y + padding * 0.6/* compensate font */);

    theme::DrawGlass(d, rect_start, ImVec2(rect_end.x - rect_start.x, rect_end.y - rect_start.y),
                     8.0f, theme::kSurfaceElev1, 0.85f);

    d->AddText(
        pos,
        IM_COL32(255, 255, 255, 255),
        watermark_string.data()
    );
}


namespace {
Player* FindPlayerByPawnIndex(std::vector<Player>& players, std::uint32_t index) {
    for (auto& player:players)
        if ((player.pawn_controller_addr & 0x7FFF)==(index & 0x7FFF)) return &player;
    return nullptr;
}
}

void Overlays::RenderSpectatorList() {
    if (!cfg::world::spectators::enabled)
        return;

    auto snapshot = Cache::CopySnapshot();
    auto& players = snapshot.players;

    const bool is_menu_open = Renderer::IsOpen();
    const bool detailed = cfg::world::spectators::detailed;
    const bool self_only = cfg::world::spectators::self_only;

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize;
    ImGuiTableFlags flags_table = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersOuterH | ImGuiTableFlags_BordersV;

    bool should_render = false;
    for (Player& p : players) {
        if (auto i = p.observer_services.target) {
            Player* target = FindPlayerByPawnIndex(players, i);

            if (self_only && (!target || !target->localplayer))
                continue;

            should_render = true;
            break;
        }
    }

    if (!should_render && !is_menu_open)
        return;

    // Window
    ImGui::SetNextWindowPos(cfg::world::spectators::pos, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(150.f, 50.f), ImVec2(FLT_MAX, FLT_MAX));

    if (!ImGui::Begin("Spectator list", nullptr, flags)) {
        ImGui::End();
        return;
    }

    if (is_menu_open)
        cfg::world::spectators::pos = ImGui::GetWindowPos();

    if (!should_render && is_menu_open) {
        ImGui::TextDisabled("No spectators");
        return ImGui::End();
    }

    if (detailed) {
        if (ImGui::BeginTable("##detailed", 3, flags_table)) {
            ImGui::TableSetupColumn("Name");
            ImGui::TableSetupColumn("Mode");
            ImGui::TableSetupColumn("Target");
            ImGui::TableHeadersRow();

            for (Player& player : players) {
                if (player.alive) continue;

                int targetIndex = player.observer_services.target;
                if (targetIndex == 0) continue;

                Player* target = FindPlayerByPawnIndex(players, targetIndex);

                if (self_only && (!target || !target->localplayer))
                    continue;

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%s", player.name);

                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%s", player.observer_services.ToString());

                ImGui::TableSetColumnIndex(2);
                if (self_only) ImGui::Text("You");
                else if (player.observer_services.mode == ObserverMode::Free) ImGui::Text("No One");
                else ImGui::Text("%s", target ? target->name : "Invalid/bomb");
            }

            ImGui::EndTable();
        }
    }
    else {
        for (Player& player : players) {
            if (player.alive) continue;
            int targetIndex = player.observer_services.target;
            if (targetIndex == 0) continue;
            Player* target = FindPlayerByPawnIndex(players, targetIndex);

            if (self_only && (!target || !target->localplayer)) continue;

            ImGui::Text("%s", player.name);
        }
    }

    ImGui::End();
}

void Overlays::RenderSpeedChart() {
    if (!cfg::world::velocity::enabled)
        return;

    auto& io = ImGui::GetIO();
    auto d = ImGui::GetBackgroundDrawList();

    auto snapshot = Cache::CopySnapshot();
    auto& local = snapshot.local;

    const static float padding = 10.0f;
    const bool is_menu_open = Renderer::IsOpen();

    auto& pos = cfg::world::velocity::pos;
    auto& size = cfg::world::velocity::size;

    int rate = std::clamp(cfg::world::velocity::sample_rate, 1, 100);
    float length = std::clamp(cfg::world::velocity::sample_length, 1.0f, 20.0f);

    static int prev_rate = rate;
    static float prev_length = length;

    float left = pos.x + padding;
    float right = pos.x + size.x - padding;
    float bottom = pos.y + size.y - padding;
    float top = pos.y + padding;

    float width = right - left;
    float height = bottom - top;

    if (!is_menu_open && !local.alive)
        return;

    if (is_menu_open) {
        auto height_padding = 25; // some padding to keep the speed number inside the area
        auto altitude_padding = 10; // so it doesnt go under the titlebar

        ImGui::SetNextWindowBgAlpha(0.1f);
        ImGui::SetNextWindowPos(pos - Vec2_t(0, altitude_padding), ImGuiCond_Once);
        ImGui::SetNextWindowSize(size + Vec2_t(0, height_padding), ImGuiCond_Once);
        if (ImGui::Begin("Velocity Graph", nullptr, ImGuiWindowFlags_NoCollapse))
        {
            pos = ImGui::GetWindowPos() + ImVec2(0, altitude_padding);
            size = ImGui::GetWindowSize() - ImVec2(0, height_padding);
            ImGui::End();
        }
    }

    // Cache menu values and resize when changed
    if (prev_rate != rate || prev_length != length || vel_buffer.empty()) {
        prev_rate = rate;
        prev_length = length;

        vel_buffer.assign(static_cast<size_t>(std::max(1.0f, rate * length)), 0);
        vel_index = 0;
        vel_accumulator = 0.0f;
    }

    Vec2_t speed_2d(local.vel.x, local.vel.y);
    int speed = floor(speed_2d.len());

    vel_accumulator += io.DeltaTime;
    size_t buff_size = vel_buffer.size();

    std::vector<ImVec2> points;
    points.reserve(buff_size);

    float sample_interval = 1.0f / static_cast<float>(rate);

    while (vel_accumulator >= sample_interval)
    {
        vel_accumulator -= sample_interval;
        vel_buffer.at(vel_index % buff_size) = speed;
        vel_index = (vel_index + 1) % buff_size;
    }

    int max_speed = 1;
    for (int v : vel_buffer)
        max_speed = std::max(max_speed, v);

    for (size_t i = 0; i < buff_size; ++i) {
        float t = buff_size > 1 ? i / float(buff_size - 1) : 0.0f;

        float x = left + t * width;

        float normalized =
            vel_buffer[(i + vel_index) % buff_size] / float(max_speed);

        float y = bottom - (normalized * height);

        points.emplace_back(x, y);
    }

    d->AddPolyline(
        points.data(),
        static_cast<int>(points.size()),
        IM_COL32(255, 255, 255, 255),
        ImDrawFlags_None,
        1.0f
    );

    auto center = ImVec2(
        pos.x + size.x / 2,
        pos.y + size.y
    );

    d->AddText(
        center,
        IM_COL32(255, 255, 255, 255),
        std::to_string(speed).c_str());
}

#ifdef _DEBUG
void Overlays::RenderDebugWindow() {
    auto& io = ImGui::GetIO();
    auto d = ImGui::GetBackgroundDrawList();

    auto snapshot = Cache::CopySnapshot();
    auto& game = snapshot.game;
    auto& bomb = snapshot.bomb;
    auto& globals = snapshot.globals;
    auto& players = snapshot.players;

    static int margin = 10;
    static int padding = 10;
    std::string debug_string = "> Game Debug Window\n";

    debug_string += std::format("Map: {}\n", globals.map_name);
    debug_string += std::format("Max Clients: {}\n", globals.max_clients);
    debug_string += std::format("Cache Refresh: {}ms\n", cfg::dev::cache_refresh_rate);

    if (bomb.is_planted) {
        debug_string += "Bomb:\n";
        debug_string += std::format("- Planted Site: {}\n", bomb.site == BombSite::A ? "A" : "B");
    }

    if (!players.empty())
        debug_string += std::format("Players ({}):\n", players.size());

	for (auto& player : players)
		debug_string += std::format(
			"- [{}] {} {}hp {} {}\n",
			player.index, player.name,
			player.health, player.weapon.name,
			player.weapon.icon
		);

    auto size = ImGui::CalcTextSize(debug_string.data());

    d->AddRectFilled(
        ImVec2(10 + margin - padding, io.DisplaySize.y - size.y - 20 - margin - padding),
        ImVec2(10 + size.x + margin + padding, io.DisplaySize.y - 20 - margin + padding),
        IM_COL32(0, 0, 0, 200),
        10.f
    );

    d->AddRect(
        ImVec2(10 + margin - padding, io.DisplaySize.y - size.y - 20 - margin - padding),
        ImVec2(10 + size.x + margin + padding, io.DisplaySize.y - 20 - margin + padding),
        IM_COL32(100, 100, 100, 200),
        10.f
    );

    d->AddText(
        ImVec2(10 + margin, io.DisplaySize.y - size.y - 20 - margin),
        IM_COL32(255, 255, 255, 255),
        debug_string.data()
    );
}
#endif

void Overlays::RenderRadar() {
    if (!cfg::world::radar::enabled)
        return;

    auto snapshot = Cache::CopySnapshot();
    auto& local = snapshot.local;
    auto& players = snapshot.players;
    auto& matrix = snapshot.game.view_matrix;

    const bool is_menu_open = Renderer::IsOpen();

    if (!is_menu_open && !local.alive)
        return;

    auto& pos = cfg::world::radar::pos;
    auto& size = cfg::world::radar::size;
    const bool minimap = cfg::world::radar::minimap;
    const float resolution_scale = radar::ResolutionScale(minimap,
        ImGui::GetIO().DisplaySize.y, cfg::world::radar::calibration_height);
    const float scale = resolution_scale * radar::HudScale(minimap, cfg::world::radar::hud_scale,
                                        cfg::world::radar::hud_size);
    // Collision bounds include skyboxes and outlying geometry. They do not
    // describe the radar overview and must never determine marker scale.
    const float range = radar::WorldRadius(cfg::world::radar::range,
        minimap && cfg::world::radar::auto_sync, cfg::world::radar::zoom,
        cfg::world::radar::scale_correction);
    ImVec2 draw_pos = (pos + cfg::world::radar::offset) * resolution_scale;
    ImVec2 draw_size(radar::Positive(size.x, 200.f) * scale,
                     radar::Positive(size.y, 200.f) * scale);

    if (is_menu_open) {
        ImGui::SetNextWindowBgAlpha(0.0f);
        ImGui::SetNextWindowPos(draw_pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(draw_size, ImGuiCond_Always);
        const auto flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
            (minimap ? ImGuiWindowFlags_NoTitleBar : ImGuiWindowFlags_None);
        if (ImGui::Begin("Radar", nullptr, flags)) {
            draw_pos = ImGui::GetWindowPos();
            draw_size = ImGui::GetWindowSize();
            pos = draw_pos / resolution_scale - cfg::world::radar::offset;
            size = draw_size / scale;
        }
        ImGui::End();
    }

    auto d = ImGui::GetBackgroundDrawList();
    const float cx = draw_pos.x + draw_size.x * .5f;
    const float cy = draw_pos.y + draw_size.y * .5f;
    const float rx = draw_size.x * .5f;
    const float ry = draw_size.y * .5f;
    const float radius = std::min(rx, ry);
    const float marker_rx = std::max(1.f, (minimap ? radius : rx) - 6.f * scale);
    const float marker_ry = std::max(1.f, (minimap ? radius : ry) - 6.f * scale);
    if (!minimap) {
        theme::DrawGlass(d, draw_pos, draw_size, 6.f, theme::kSurfaceDeep,
                         std::clamp(cfg::world::radar::opacity, 0.f, 1.f));
        d->AddCircle(ImVec2(cx, cy), radius * .333f, theme::Pack(theme::kBorderBase));
        d->AddCircle(ImVec2(cx, cy), radius * .666f, theme::Pack(theme::kBorderBase));
    } else if (is_menu_open) {
        d->AddCircle(ImVec2(cx, cy), radius, theme::Pack(theme::kAccent));
        d->AddLine(ImVec2(cx - 6, cy), ImVec2(cx + 6, cy), theme::Pack(theme::kAccent));
        d->AddLine(ImVec2(cx, cy - 6), ImVec2(cx, cy + 6), theme::Pack(theme::kAccent));
    }

    for (auto& player : players) {
        if (!player.alive)
            continue;

        if (player.localplayer)
            continue;

        Vec3_t delta = player.pos - local.pos;
        float dist = sqrtf(delta.x * delta.x + delta.y * delta.y);

        if (dist > range)
            continue;

        const auto projected = radar::Rotate(delta.x, delta.y,
            !cfg::world::radar::no_rotate, matrix[0][0], matrix[0][1]);
        const float sx = cx + projected.x / range * marker_rx;
        const float sy = cy + projected.y / range * marker_ry;
        if (!std::isfinite(sx) || !std::isfinite(sy)) continue;

        bool mate = player.team == local.team;
        ImU32 col = mate
            ? IM_COL32(0, 220, 80, 255)
            : IM_COL32(220, 50, 50, 255);

        d->AddCircleFilled(ImVec2(sx, sy), 4.f, col);
        d->AddCircle(ImVec2(sx, sy), 4.f, IM_COL32(0, 0, 0, 180));
    }

    if (!minimap) {
        d->AddCircleFilled(ImVec2(cx, cy), 5.f, IM_COL32(100, 180, 255, 255));
        d->AddCircle(ImVec2(cx, cy), 5.f, IM_COL32(0, 0, 0, 180));
        d->AddText(draw_pos + ImVec2(6, 4), IM_COL32(180, 180, 180, 200), "Radar");
    }
}

void Overlays::RenderBomb() {
    if (!cfg::world::bomb::location && !cfg::world::bomb::timer)
        return;

    auto& io = ImGui::GetIO();
    auto snapshot = Cache::CopySnapshot();

    auto& bomb = snapshot.bomb;
    auto& local = snapshot.local;
    auto& matrix = snapshot.game.view_matrix;

    const bool is_menu_open = Renderer::IsOpen();

    float width = 20.f;
    float height = 20.f;
    float rounding = 4.f;

    static int margin = 4;
    static int padding = 6;
    float bar_height = 3.f;
    float element_gap = 6.f;

    auto duration_str = std::format("{}s", bomb.is_planted ? std::max(0.f, bomb.time_left) : 40.0f);
    auto bombsite_str = std::string(!bomb.is_planted || bomb.site == BombSite::A ? "A" : "B");

    std::string bomb_string = "";

    if (cfg::world::bomb::location)
        bomb_string += "SITE " + bombsite_str;

    if (cfg::world::bomb::timer)
    {
        if (cfg::world::bomb::location)
            bomb_string += " | ";

        bomb_string += duration_str;
    }

    auto text_size = ImGui::CalcTextSize(bomb_string.data());

    ImGui::PushFont(this->font_icons);
    auto icon_size = ImGui::CalcTextSize(WeaponIcons::C4);
    ImGui::PopFont();

    float content_width = icon_size.x + element_gap + text_size.x;
    float content_height = std::max(icon_size.y, text_size.y);

    width = content_width + (padding * 2);
    height = content_height + (padding * 2) + (cfg::world::bomb::timer ? bar_height + 2.f : 0.f);

    if (is_menu_open) {
        ImGui::SetNextWindowBgAlpha(0.0f);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

        float title_bar_height = ImGui::GetFrameHeight();
        ImGui::SetNextWindowPos(cfg::world::bomb::pos, ImGuiCond_Once);
        ImGui::SetNextWindowSize(ImVec2(width, height + title_bar_height), ImGuiCond_Always);

        if (ImGui::Begin("Bomb Window", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoResize)) {
            cfg::world::bomb::pos = ImGui::GetWindowPos();
            ImGui::End();
        }

        ImGui::PopStyleVar();
    }

    if (!bomb.is_planted && !is_menu_open)
        return;

    // A planted bomb stays visible even in the death cam; the timer is most
    // important there.
    if (!local.alive && !bomb.is_planted && !is_menu_open)
        return;

    Vec2_t screen_pos;
    bool on_top = bomb.is_planted ? matrix.wts(bomb.pos, io.DisplaySize, screen_pos) : false;

    auto dist = local.pos.dist_to(bomb.pos);

    if (is_menu_open) on_top = false;
    if (dist > 1500.f) on_top = false;

    float render_x = 0.f;
    float render_y = 0.f;

    // If we use bomb esp the overlay will be sticky
    if (on_top && bomb.is_planted && !cfg::esp::bomb) {
        render_x = screen_pos.x - (width * 0.5f);
        render_y = screen_pos.y + margin;
    }
    else {
        float title_bar_height = ImGui::GetFrameHeight();
        render_x = cfg::world::bomb::pos.x;
        render_y = cfg::world::bomb::pos.y + title_bar_height;
    }

    auto d = ImGui::GetBackgroundDrawList();

    d->AddRectFilled(
        ImVec2(render_x, render_y),
        ImVec2(render_x + width, render_y + height),
        IM_COL32(15, 15, 15, 220),
        rounding
    );

    d->AddRect(
        ImVec2(render_x, render_y),
        ImVec2(render_x + width, render_y + height),
        IM_COL32(45, 45, 45, 255),
        rounding
    );

    d->AddText(
        this->font_icons,
        16.0f,
        ImVec2(render_x + padding, render_y + padding + (content_height - icon_size.y) * 0.5f),
        IM_COL32(255, 60, 60, 255),
        WeaponIcons::C4
    );

    d->AddText(
        ImVec2(render_x + padding + icon_size.x + element_gap, render_y + padding + (content_height - text_size.y) * 0.5f),
        IM_COL32(240, 240, 240, 255),
        bomb_string.data()
    );

    if (cfg::world::bomb::timer)
    {
        float time_left = bomb.is_planted ? bomb.time_left : 40.f;
        float progress = std::clamp(time_left / 40.f, 0.f, 1.f);

        ImU32 bar_color = progress > 0.5f
            ? IM_COL32((int)((1.f - progress) * 2.f * 255), 220, 50, 255)
            : IM_COL32(220, (int)(progress * 2.f * 220), 50, 255);

        ImVec2 bar_start(render_x + rounding, render_y + height - bar_height - 4.f);
        ImVec2 bar_end(render_x + width - rounding, render_y + height - bar_height - 4.f);
        float max_bar_width = bar_end.x - bar_start.x;

        d->AddLine(bar_start, bar_end, IM_COL32(40, 40, 40, 255), bar_height);

        if (progress > 0.f)
        {
            ImVec2 bar_filled_end(bar_start.x + (max_bar_width * progress), bar_end.y);
            d->AddLine(bar_start, bar_filled_end, bar_color, bar_height);
        }
    }
}

// ── Pulsing red border around screen edges when bomb is planted ────────
// Flashes in sync with the bomb timer for situational awareness.
void Overlays::RenderBombBorder() {
    auto snapshot = Cache::CopySnapshot();
    auto& bomb = snapshot.bomb;

    if (!bomb.is_planted)
        return;

    auto& io = ImGui::GetIO();
    auto d = ImGui::GetBackgroundDrawList();
    auto screen = io.DisplaySize;

    // Pulse rate accelerates as bomb timer runs down
    const float time_left = std::max(0.0f, bomb.time_left);
    const float urgency = 1.0f - (time_left / 40.0f); // 0 = just planted, 1 = about to explode

    // Flash once per whole bomb second, aligned to the same tick the in-game
    // beep sounds. time_left is wall-clock derived, so a free-running sine
    // would drift out of phase with it; instead the flash decays across each
    // second, guaranteeing the screen pulses exactly in time with the bomb.
    const float frac = time_left - std::floor(time_left);
    const float pulse = std::max(0.0f, 1.0f - frac); // 1 right after each tick, decays to 0

    // Alpha: always visible but pulses harder near explosion
    const float base_alpha = 0.15f + urgency * 0.25f;
    const float alpha = base_alpha + pulse * (0.20f + urgency * 0.40f);

    // Red intensifies as time runs out
    const int r = 220 + (int)(urgency * 35); // 220 -> 255
    const int g = (int)(30 * (1.0f - urgency)); // 30 -> 0
    const int b = 20;
    const int a = (int)(alpha * 255.0f);

    const float border = 3.0f + pulse * 2.0f; // 3-5px border

    // Top edge
    d->AddRectFilled(ImVec2(0, 0), ImVec2(screen.x, border), IM_COL32(r, g, b, a));
    // Bottom edge
    d->AddRectFilled(ImVec2(0, screen.y - border), ImVec2(screen.x, screen.y), IM_COL32(r, g, b, a));
    // Left edge
    d->AddRectFilled(ImVec2(0, 0), ImVec2(border, screen.y), IM_COL32(r, g, b, a));
    // Right edge
    d->AddRectFilled(ImVec2(screen.x - border, 0), ImVec2(screen.x, screen.y), IM_COL32(r, g, b, a));

    // Corner glow accents (brighter at corners)
    const float corner_size = 20.0f + pulse * 15.0f;
    const int corner_a = (int)(alpha * 355.0f); // brighter than edges
    // Top-left
    d->AddRectFilled(ImVec2(0, 0), ImVec2(corner_size, border + 1), IM_COL32(r, g, b, std::min(corner_a, 255)));
    d->AddRectFilled(ImVec2(0, 0), ImVec2(border + 1, corner_size), IM_COL32(r, g, b, std::min(corner_a, 255)));
    // Top-right
    d->AddRectFilled(ImVec2(screen.x - corner_size, 0), ImVec2(screen.x, border + 1), IM_COL32(r, g, b, std::min(corner_a, 255)));
    d->AddRectFilled(ImVec2(screen.x - border - 1, 0), ImVec2(screen.x, corner_size), IM_COL32(r, g, b, std::min(corner_a, 255)));
    // Bottom-left
    d->AddRectFilled(ImVec2(0, screen.y - border - 1), ImVec2(corner_size, screen.y), IM_COL32(r, g, b, std::min(corner_a, 255)));
    d->AddRectFilled(ImVec2(0, screen.y - corner_size), ImVec2(border + 1, screen.y), IM_COL32(r, g, b, std::min(corner_a, 255)));
    // Bottom-right
    d->AddRectFilled(ImVec2(screen.x - corner_size, screen.y - border - 1), ImVec2(screen.x, screen.y), IM_COL32(r, g, b, std::min(corner_a, 255)));
    d->AddRectFilled(ImVec2(screen.x - border - 1, screen.y - corner_size), ImVec2(screen.x, screen.y), IM_COL32(r, g, b, std::min(corner_a, 255)));
}
