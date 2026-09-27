#include "Bomb.hpp"

#include "core/engine/Engine.hpp"
#include "core/offsets/Dumper.hpp"

bool Bomb::Update() {

	auto p = Engine::GetProcess();

	if (!p)
		return false;

	auto client = Engine::GetClient();

	carrier=0;
	const auto carried=p->read<uintptr_t>(client.base+offsets::weaponC4);
	if(carried)carrier=p->read<std::uint32_t>(carried+offsets::pawn::m_hOwnerEntity);
	this->address = p->read<uintptr_t>(client.base + offsets::plantedC4);
	this->address=this->address?p->read<uintptr_t>(this->address):0;
	this->is_planted = this->address && p->read<bool>(this->address+offsets::bomb::m_bC4Activated);

	if (!this->is_planted) {
		Bomb::prev_is_planted = false;
		return true;
	}

	auto site = p->read<uint32_t>(this->address + offsets::bomb::m_nBombSite);
	this->site = (site == 1) ? BombSite::B : BombSite::A;

	auto node = p->read<uintptr_t>(this->address + offsets::pawn::m_pGameSceneNode);

	if (node)
		this->pos = p->read<Vec3_t>(node + offsets::bomb::m_vecAbsOrigin);

	if (!Bomb::prev_is_planted)
		plant_time = std::time(nullptr);

	this->time_left = 41 - (std::time(nullptr) - plant_time);

	Bomb::prev_is_planted = true;
	return true;
}

std::time_t Bomb::plant_time{};
bool Bomb::prev_is_planted = false;
