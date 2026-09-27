#pragma once
#include "core/engine/classes/Bones.hpp"
#include "core/engine/classes/Weapon.hpp"
#include "core/engine/classes/ObserverServices.hpp"

class Player {
public:
    Player() {}
    Player(int index, uintptr_t el, uintptr_t le)
        : index(index), list_entry(le), entity_list(el) {}

    bool Update();
    bool GetBounds(view_matrix_t matrix, Vec2_t size, std::pair<Vec2_t, Vec2_t>& bounds);
public:
    int8_t index = -1; // Sentinel for an uninitialized player.

    Vec3_t pos;
    Vec3_t vel;

    int ping = 0;
    int team = 0;
    int health = 0;
    int armor = 0;
    int money = 0;

    bool bot = true;
    bool alive = false;
    bool scoped = false;
    bool flashed = false;
    bool spotted = false;
    bool defusing = false;
    bool localplayer = false;
    bool has_c4 = false;

    char name[32]{};
    //std::string name;
    uint64_t steam_id{};

    Weapon weapon;
    int32_t ammo = 0;
    bool is_reloading = false;

    Vec3_t eye_angles;   // m_angEyeAngles (pitch, yaw, roll)

    std::vector<bone_pos> bone_list;

    std::uint32_t pawn_controller_addr{};
    ObserverServices observer_services;
private:
    uintptr_t list_entry{};
    uintptr_t entity_list{};        // Global entity list (shared by controllers, pawns and weapons)

    uintptr_t pawn{};
    uintptr_t controller{};

    bone_data bones[30]{};
private:
    bool GetPawn();
    bool GetController();

    bool UpdatePawn();
    bool UpdateWeapon();
    bool UpdateSkeleton();
    bool UpdateController();
    bool UpdateObserverServices();
};
