#pragma once
#include <future>
#include <string>

#include "core/engine/classes/Game.hpp"
#include "core/engine/classes/Bomb.hpp"
#include "core/engine/classes/Player.hpp"
#include "core/engine/classes/Globals.hpp"

using namespace std::chrono;

enum class CacheStatus {
	Unavailable,
	Refreshing,
	Ready,
	ProcessUnavailable,
	GameReadFailed,
	EntityListUnavailable,
	GlobalsReadFailed,
	NoMatch,
	MissingLocal,
};

struct CacheSnapshotStatus {
	CacheStatus state{CacheStatus::Unavailable};
	std::uint64_t generation{};
	steady_clock::time_point published_at{};
	milliseconds age{};
	milliseconds refresh_duration{};

	[[nodiscard]] bool ready() const {
		return age <= 250ms && (state == CacheStatus::Ready || state == CacheStatus::MissingLocal);
	}
};

// Deterministic lifecycle seam for tools/test-runtime. It never touches the
// game process and intentionally publishes only cleared/sentinel game data.
struct CacheTestFrame {
	bool process_available{true};
	bool game_readable{true};
	bool entity_list_available{true};
	bool globals_readable{true};
	bool in_match{true};
	bool local_present{true};
	milliseconds age{};
};

struct Snapshot {
	Game game;
	Bomb bomb;
	Player local;
	Globals globals;
	std::vector<Player> players;
	CacheSnapshotStatus status;
};

class Cache {
public:
	Game game;
	Bomb bomb;
	Player local;
	Globals globals;
	std::vector<Player> players;
public:
	static Cache& Get()
	{
		static Cache instance{};
		return instance;
	}

	static Snapshot CopySnapshot();
	static CacheSnapshotStatus Status();
	static bool PublishForTesting(const CacheTestFrame& frame);
	// Must be called after the refresh worker is joined and before logger
	// teardown. It drains the one bounded map extraction task.
	static void StopBackgroundWork();

	static bool Refresh();
private:
	std::mutex mtx;
	milliseconds duration{1};
	steady_clock::time_point last{};
	CacheSnapshotStatus status{};
	std::future<bool> map_job;
	std::string requested_map;
	steady_clock::time_point retry_at{};
private:
	bool RefreshImpl();
	void PublishFailure(CacheStatus state, steady_clock::time_point now);
	void Publish(Game&& next_game, Bomb&& next_bomb, Player&& next_local,
		Globals&& next_globals, std::vector<Player>&& next_players,
		CacheStatus state, steady_clock::time_point now, milliseconds refresh_duration);
	void PublishMatrix(const Game& next_game);
};
