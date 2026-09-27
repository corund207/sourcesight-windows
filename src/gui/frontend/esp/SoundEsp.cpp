#include "SoundEsp.hpp"

#include "gui/renderer/Renderer.hpp"
#include "core/engine/classes/Player.hpp"
#include "config/Current.hpp"

#include <cmath>
#include <algorithm>

std::vector<SoundEsp::SoundEvent> SoundEsp::sound_events;
std::unordered_map<int, SoundEsp::PlayerSoundState> SoundEsp::player_states;

void SoundEsp::AddGunshot(const Vec3_t& position, bool is_enemy) {
    if (!cfg::sound_esp::enabled || !cfg::sound_esp::gunshots)
        return;

    const float now = static_cast<float>(ImGui::GetTime());
    sound_events.push_back({
        position,
        now,
        false,  // not a footstep
        -1,     // no specific player index
        is_enemy
    });
}

void SoundEsp::Update(const std::vector<Player>& players, const Player& local) {
    if (!cfg::sound_esp::enabled)
        return;

    const float now = static_cast<float>(ImGui::GetTime());
    const float duration = std::max(0.1f, cfg::sound_esp::duration);
    const float max_dist = std::max(1.0f, cfg::sound_esp::max_distance);

    for (const auto& player : players) {
        if (!player.alive || player.localplayer)
            continue;

        if (player.team == local.team)
            continue;

        const float dx = player.pos.x - local.pos.x;
        const float dy = player.pos.y - local.pos.y;
        const float dz = player.pos.z - local.pos.z;
        const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);

        if (dist > max_dist)
            continue;

        auto& state = player_states[player.index];

        // Footstep detection based on velocity and position change
        if (cfg::sound_esp::footsteps) {
            const float vel_2d = std::sqrt(player.vel.x * player.vel.x + player.vel.y * player.vel.y);
            const float pos_change = std::sqrt(
                (player.pos.x - state.last_position.x) * (player.pos.x - state.last_position.x) +
                (player.pos.y - state.last_position.y) * (player.pos.y - state.last_position.y)
            );

            // Detect footsteps: moving on ground with velocity
            bool is_moving = vel_2d > 50.0f; // ~walking speed
            bool pos_changed = pos_change > 1.0f;

            // Simple heuristic: if moving and enough time passed since last footstep
            if (is_moving && pos_changed) {
                const float footstep_interval = 0.4f; // approximate footstep interval
                if (now - state.last_footstep_time > footstep_interval) {
                    sound_events.push_back({
                        player.pos,
                        now,
                        true,
                        player.index,
                        true
                    });
                    state.last_footstep_time = now;
                }
            }
        }

        // Gunshot detection is handled by bullet tracers in Esp.cpp
        // We could add additional visual indicators here if needed

        state.last_position = player.pos;
    }

    // Clean up old sound events
    const float fade_time = std::max(0.1f, cfg::sound_esp::fade_time);
    for (auto it = sound_events.begin(); it != sound_events.end();) {
        float age = now - it->start_time;
        if (age > duration + fade_time) {
            it = sound_events.erase(it);
        } else {
            ++it;
        }
    }
}

void SoundEsp::Render(view_matrix_t& matrix, const ImGuiIO& io, ImDrawList* d, const Player& local) {
    if (!cfg::sound_esp::enabled)
        return;

    const float now = static_cast<float>(ImGui::GetTime());
    const float duration = std::max(0.1f, cfg::sound_esp::duration);
    const float fade_time = std::max(0.1f, cfg::sound_esp::fade_time);
    const float footprint_size = std::clamp(cfg::sound_esp::footprint_size, 2.0f, 32.0f);

    for (const auto& event : sound_events) {
        float age = now - event.start_time;
        if (age > duration + fade_time)
            continue;

        // Calculate alpha based on age
        float alpha = 1.0f;
        if (age > duration) {
            alpha = 1.0f - (age - duration) / fade_time;
        }

        if (alpha <= 0.0f)
            continue;

        Vec2_t screen_pos;
        if (!matrix.wts(event.position, io.DisplaySize, screen_pos))
            continue;

        // Only render if in front of camera
        Vec3_t cam_to_event = event.position - local.pos;
        Vec3_t view_dir;
        view_dir.x = matrix[0][0] * cam_to_event.x + matrix[0][1] * cam_to_event.y + matrix[0][2] * cam_to_event.z;
        view_dir.y = matrix[1][0] * cam_to_event.x + matrix[1][1] * cam_to_event.y + matrix[1][2] * cam_to_event.z;
        view_dir.z = matrix[2][0] * cam_to_event.x + matrix[2][1] * cam_to_event.y + matrix[2][2] * cam_to_event.z;

        if (view_dir.z <= 0.0f)
            continue;

        if (event.is_footstep) {
            auto color = cfg::sound_esp::footsteps_color;
            color.a *= alpha;

            // Draw footstep indicator (circle with direction arrow)
            d->AddCircle(screen_pos, footprint_size * alpha, ImColor(color), 12, 2.0f);

            // Draw inner dot
            d->AddCircleFilled(screen_pos, footprint_size * 0.3f * alpha, ImColor(color), 8);

            // Draw direction indicator (arrow pointing from sound source to local player)
            Vec2_t local_screen;
            if (matrix.wts(local.pos, io.DisplaySize, local_screen)) {
                Vec2_t dir = local_screen - screen_pos;
                float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
                if (len > 1.0f) {
                    dir = dir / len * footprint_size * 1.5f;
                    Vec2_t arrow_start = screen_pos + dir * 0.5f;
                    Vec2_t arrow_end = screen_pos + dir;
                    d->AddLine(arrow_start, arrow_end, ImColor(color), 2.0f);

                    // Arrow head
                    Vec2_t perp(-dir.y, dir.x);
                    perp = perp / std::sqrt(perp.x * perp.x + perp.y * perp.y) * footprint_size * 0.5f;
                    d->AddLine(arrow_end, arrow_end - dir * 0.3f + perp, ImColor(color), 2.0f);
                    d->AddLine(arrow_end, arrow_end - dir * 0.3f - perp, ImColor(color), 2.0f);
                }
            }
        } else {
            // Gunshot indicator
            auto color = cfg::sound_esp::gunshots_color;
            color.a *= alpha;

            // Draw gunshot indicator (expanding ring + crosshair)
            float ring_radius = footprint_size * 2.0f * (1.0f + age * 0.5f);
            d->AddCircle(screen_pos, ring_radius * alpha, ImColor(color), 24, 2.0f);

            // Draw crosshair
            float cross_size = footprint_size * 1.5f * alpha;
            d->AddLine(
                { screen_pos.x - cross_size, screen_pos.y },
                { screen_pos.x + cross_size, screen_pos.y },
                ImColor(color), 2.0f
            );
            d->AddLine(
                { screen_pos.x, screen_pos.y - cross_size },
                { screen_pos.x, screen_pos.y + cross_size },
                ImColor(color), 2.0f
            );
        }
    }
}