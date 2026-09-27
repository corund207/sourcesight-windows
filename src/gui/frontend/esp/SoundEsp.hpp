#pragma once

#include "core/engine/cache/Cache.hpp"
#include <vector>
#include <unordered_map>

class SoundEsp {
public:
    ~SoundEsp() = default;
    SoundEsp(const SoundEsp&) = delete;
    SoundEsp(SoundEsp&&) = delete;
    SoundEsp& operator=(const SoundEsp&) = delete;
    SoundEsp& operator=(SoundEsp&&) = delete;

    static void Update(const std::vector<Player>& players, const Player& local);
    static void Render(view_matrix_t& matrix, const ImGuiIO& io, ImDrawList* d, const Player& local);
    static void AddGunshot(const Vec3_t& position, bool is_enemy);

private:
    SoundEsp() = default;

    struct SoundEvent {
        Vec3_t position;
        float start_time;
        bool is_footstep;
        int player_index;
        bool is_enemy;
    };

    struct PlayerSoundState {
        Vec3_t last_position;
        float last_footstep_time = 0.0f;
        bool was_on_ground = false;
    };

    static std::vector<SoundEvent> sound_events;
    static std::unordered_map<int, PlayerSoundState> player_states;
};