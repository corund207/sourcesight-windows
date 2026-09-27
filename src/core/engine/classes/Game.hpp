#pragma once

#include <cstdint>

class Game {
public:
    Game() {};

    bool Update();
    bool UpdateMatrix();
    bool UpdateEntityList();

    // Resolves a CS2 entity handle (controller, pawn or weapon) to an entity
    // address through the Windows global entity list:
    //
    //   chunk = *(entity_list + 0x10 + 8 * ((handle >> 9) & 0x3F))
    //   slot  = chunk + 0x70 * (handle & 0x1FF)
    //   entity = *(void**)slot                           (instance pointer at +0x0)
    //
    // Handles 0xFFFFFFFF/-2 are sentinels.
    static uintptr_t ResolveHandle(uintptr_t entity_list, std::uint32_t handle);

public:
    view_matrix_t view_matrix{};

    uintptr_t entity_list{};        // Global entity system base
    uintptr_t list_entry{};         // Bucket 0 (*(entity_list + 0x10))
private:
    uintptr_t address{};
};
