#include "Player.hpp"

#include "Weapon.hpp"
#include "Game.hpp"
#include "core/engine/Engine.hpp"
#include "core/offsets/Dumper.hpp"

#include "core/engine/classes/ObserverServices.hpp"

bool Player::Update() {
	if (!Engine::GetProcess())
		return false;

	// One-shot diagnostics: report the first failing stage so offset/struct
	// issues are identifiable from a single in-game run.
	if (!GetController()) {
		static bool diag = false;
		if (!diag) { diag = true; LOGF(WARNING, "[player] diag: GetController failed (index={})", index); }
		return false;
	}

	if (!GetPawn()) {
		static bool diag = false;
		if (!diag) { diag = true; LOGF(WARNING, "[player] diag: GetPawn failed (index={})", index); }
		return false;
	}

	if (!UpdateController()) {
		static bool diag = false;
		if (!diag) { diag = true; LOGF(WARNING, "[player] diag: UpdateController failed (index={})", index); }
		return false;
	}

	if (!UpdatePawn()) {
		static bool diag = false;
		if (!diag) { diag = true; LOGF(WARNING, "[player] diag: UpdatePawn failed (index={})", index); }
		return false;
	}

	return true;
}

bool Player::GetController() {
	auto p = Engine::GetProcess();
	this->controller = p->read<DWORD64>(list_entry + (index + 1) * offsets::entity::stride);

	return this->controller != 0;
}

bool Player::GetPawn() {
	auto p = Engine::GetProcess();

	const auto entity_pawn_handle = p->read<std::uint32_t>(controller + offsets::controller::m_hPawn);

	if (!entity_pawn_handle)
		return false;

	this->pawn_controller_addr = entity_pawn_handle;

	// Pawns live in the same global entity list as everything else.
	this->pawn = Game::ResolveHandle(this->entity_list, entity_pawn_handle);

	// A missing pawn is normal while players are spawning, spectating, or
	// changing teams. Keep this path quiet; persistent offset failures are
	// reported by the engine's offset validation instead of once per session.


	return this->pawn != 0;
}

bool Player::UpdateController() {
	auto p = Engine::GetProcess();

	this->steam_id = p->read<uint64_t>(controller + offsets::controller::m_steamID);
	this->bot = this->steam_id == 0;

	// expensive
	if (!p->read_raw(controller + offsets::controller::m_iszPlayerName, this->name, sizeof(this->name)))
		return false;

	this->localplayer = p->read<bool>(controller + offsets::controller::m_bIsLocalPlayerController);
	this->ping = p->read<int>(controller + offsets::controller::m_iPing);

	auto money_services = p->read<uintptr_t>(controller + offsets::controller::m_pInGameMoneyServices);

	if (money_services)
		this->money = p->read<int>(money_services + offsets::controller::m_iAccount);

	return true;
}

bool Player::UpdatePawn() {
	auto p = Engine::GetProcess();

	this->health = p->read<int>(pawn + offsets::pawn::m_iHealth);
	this->alive = health != 0;

	if (this->health > 255 || this->health < 0)
		LOGF(FATAL,
			"Health seems to have a random value (over 100 or under 0) with a value of ({}). Game has probably updated pawn structure",
			this->health
		);

	UpdateObserverServices();

	if (!alive) // No need to continue
		return true;

	this->pos = p->read<Vec3_t>(pawn + offsets::pawn::m_vOldOrigin);

	if (this->pos.zero()) {
		static bool diag = false;
		if (!diag) { diag = true; LOGF(WARNING, "[player] diag: pos is zero (pawn=0x{:X} origin=0x{:X})", this->pawn, this->pawn + offsets::pawn::m_vOldOrigin); }
		return false;
	}

	this->vel = p->read<Vec3_t>(pawn + offsets::pawn::m_vecAbsVelocity);

	// Used by the bullet tracer ESP: where the player is looking.
	this->eye_angles = p->read<Vec3_t>(pawn + offsets::pawn::m_angEyeAngles);

	this->team = p->read<uint8_t>(pawn + offsets::pawn::m_iTeamNum);

	this->armor = p->read<int>(pawn + offsets::pawn::m_ArmorValue);
	this->defusing = p->read<bool>(pawn + offsets::pawn::m_bIsDefusing);
	// m_bSpotted is the "is this pawn currently seen" flag. The old code read
	// m_bSpottedByMask (+0xC, a uint32[2] team mask) as a bool, which was nearly
	// always nonzero and made "visible only" useless.
	this->spotted = p->read<bool>(pawn + offsets::pawn::m_entitySpottedState + offsets::pawn::m_bSpotted);
	this->flashed = p->read<float>(pawn + offsets::pawn::m_flFlashOverlayAlpha) > 0;
	this->scoped = p->read<bool>(pawn + offsets::pawn::m_bIsScoped);

	static bool skeleton_failed = false;

	if (!UpdateSkeleton() && !skeleton_failed) {
		// Skeleton is cosmetic: don't drop the whole player if the bone read
		// fails (wrong bone offsets for this game build). Log once instead.
		skeleton_failed = true;
		LOGF(WARNING, "Failed to read player skeleton (bone offsets may need updating); box ESP still works");
	}

	// Shows errors when player just respawned
	if (!UpdateWeapon()) {
		//LOGF(FATAL, "Failed to update weapon"); // too verbose
		return false;
	}


	return true;
}

bool Player::UpdateSkeleton() {
	auto p = Engine::GetProcess();

	// One-time diagnostic: reports exactly where the bone chain fails (or if
	// the bones read OK), so offset/struct issues are identifiable from a
	// single in-game run instead of silently producing no skeleton.
	static bool skeleton_diag_done = false;
	const auto diag = [](const std::string& msg) {
		if (!skeleton_diag_done) {
			skeleton_diag_done = true;
			LOGF(WARNING, "Skeleton debug: {}", msg);
		}
	};

	auto game_scene = p->read<DWORD64>(this->pawn + offsets::pawn::m_pGameSceneNode);

	if (!game_scene) {
		diag(std::format("m_pGameSceneNode read returned 0 (pawn=0x{:X} + 0x{:X})", this->pawn, offsets::pawn::m_pGameSceneNode));
		return false;
	}

	const auto bone_array_addr = game_scene + (offsets::bone::m_modelState + 0x80);
	auto bone_array = p->read<DWORD64>(bone_array_addr);

	if (!bone_array) {
		diag(std::format("bone_array read returned 0 (game_scene=0x{:X}, ptr at 0x{:X})", game_scene, bone_array_addr));
		return false;
	}

	if (!p->read_raw(bone_array, bones, sizeof(bones))) {
		diag(std::format("bone read_raw failed (bone_array=0x{:X}, bytes={})", bone_array, sizeof(bones)));
		return false;
	}

	for (int i = 0; i < 30; i++)
		this->bone_list.push_back({ bones[i].pos });

	return true;
}

bool Player::UpdateWeapon() {
	auto p = Engine::GetProcess();

	auto weapon_services = p->read<uintptr_t>(this->pawn + offsets::pawn::m_pWeaponServices);

	if (!weapon_services) {
		static bool diag = false;
		if (!diag) { diag = true; LOGF(WARNING, "[player] diag: weapon_services=0 (pawn=0x{:X})", this->pawn); }
		return false;
	}

	auto active_weapon_index = p->read<int>(weapon_services + offsets::pawn::m_hActiveWeapon);

	if (!active_weapon_index) {
		static bool diag = false;
		if (!diag) { diag = true; LOGF(WARNING, "[player] diag: active_weapon_index=0 (ws=0x{:X})", weapon_services); }
		return false;
	}

	auto weapon = Weapon(this->entity_list, active_weapon_index);

	if (!weapon.Update()) {
		static bool diag = false;
		if (!diag) { diag = true; LOGF(WARNING, "[player] diag: Weapon::Update failed (handle=0x{:X})", active_weapon_index); }
		return false;
	}

	this->weapon = weapon;
	this->ammo = weapon.ammo;
	this->is_reloading = weapon.is_reloading;

	return true;
}

bool Player::GetBounds(view_matrix_t matrix, Vec2_t size, std::pair<Vec2_t, Vec2_t>& bounds) {
	Vec2_t origin;
	bool pt1 = matrix.wts(this->pos, size, origin);


	Vec3_t pos_top;
	if (this->bone_list.empty())
		pos_top = this->pos + Vec3_t(0, 0, 65.f); // 75.f
	else
		pos_top = this->bone_list[bone_index::head].pos;

	//auto head_bone = this->bone_list[bone_index::head];
	//head_bone.pos.z *= 1.09; // little offset to cover the entire head
	//bone_pos head_bone = origin + ImVec3

	Vec2_t top;
	bool pt2 = matrix.wts(pos_top, size, top);

	float height = origin.y - top.y;
	float width = height / 2.4f;

	top.x -= width / 2;
	origin.x += width / 2;

	top.y -= width / 4;

	// Top to bottom
	bounds = { top, origin };

	return pt1 || pt2;
}

// Does not update if match is started
bool Player::UpdateObserverServices() {
	auto p = Engine::GetProcess();
	if (!p)
		return false;

	DWORD64 address = p->read<DWORD64>(this->pawn + offsets::pawn::m_pObserverServices);
	if (!address)
		return false;

	this->observer_services.SetAddress(address);
	return this->observer_services.Update();
}
