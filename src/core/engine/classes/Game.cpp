#include "Game.hpp"

#include "core/engine/Engine.hpp"
#include "core/offsets/Dumper.hpp"

bool Game::Update() {
	if (!Engine::GetProcess())
		return false;

	if (!UpdateMatrix()) {
		LOGF(FATAL, "Failed to update view matrix");
		return false;
	}

	// No need to be updated along with the view matrix
	//if (!UpdateEntityList()) {
	//	LOGF(FATAL, "Failed to update entity list");
	//	return false;
	//}

	return true;
}

bool Game::UpdateMatrix() {
	auto p = Engine::GetProcess();
	auto client = Engine::GetClient();

	return p->read_raw(client.base+offsets::viewMatrix,&this->view_matrix,sizeof(this->view_matrix));
}

bool Game::UpdateEntityList() {
    auto p = Engine::GetProcess();
    auto client = Engine::GetClient();

    // Re-read the entity list pointer every tick.  The pointer may not be
    // ready at startup, and on some CS2 builds the pattern offset can be
    // stale.  Re-reading ensures we pick it up as soon as it becomes valid.
    const uintptr_t el_ptr = p->read<DWORD64>(client.base + offsets::entityList);

    if (el_ptr != 0) {
        this->entity_list = el_ptr;
        this->list_entry = p->read<DWORD64>(this->entity_list + offsets::entity::buckets);

        static bool logged = false;
        if (!logged) {
            logged = true;
            LOGF(INFO, "[game] entity list resolved: el=0x{:X} le=0x{:X}",
                this->entity_list, this->list_entry);
        }
    } else {
        // Entity list pointer is null — try to re-scan the pattern.
        static int null_ticks = 0;
        null_ticks++;
        if (null_ticks == 1) {
            LOGF(WARNING, "[game] entity list is NULL — attempting pattern re-scan");
            Dumper::RescanEntityList();
        }
        if (null_ticks <= 3 || null_ticks % 100 == 0) {
            LOGF(WARNING, "[game] entity list still NULL (tick {}) — "
                "offset=0x{:X} client=0x{:X}",
                null_ticks, offsets::entityList, client.base);
        }
    }

    return this->entity_list != 0;
}

uintptr_t Game::ResolveHandle(uintptr_t entity_list, std::uint32_t handle)
{
    // Handle sentinels, exactly as CS2 itself treats them:
    // 0xFFFFFFFF == invalid, 0xFFFFFFFE == entity deleted in progress.
    if (!handle || handle == 0xFFFFFFFF || handle == 0xFFFFFFFE || !entity_list)
        return 0;

    auto p = Engine::GetProcess();
    if (!p)
        return 0;

    const std::uint32_t idx = handle & 0x7FFF; // low 15 bits: entity index

    const uintptr_t chunk = p->read<uintptr_t>(entity_list + offsets::entity::buckets + 8 * ((idx >> 9) & 0x3F));
    if (!chunk)
        return 0;

    const uintptr_t slot = chunk + offsets::entity::stride * (idx & 0x1FF);

    return p->read<uintptr_t>(slot);
}
