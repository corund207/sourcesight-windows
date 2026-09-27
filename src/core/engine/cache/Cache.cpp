#include "Cache.hpp"

#include "core/engine/Engine.hpp" // Circular dep
#include "core/engine/classes/MapExtractor.hpp"
#include "core/engine/classes/MapRaytrace.hpp"

#include <cstring>
#include <future>

namespace {

void Clear(Game& game, Bomb& bomb, Player& local, Globals& globals, std::vector<Player>& players) {
    game = Game{};
    game.view_matrix = {};
    game.entity_list = 0;
    game.list_entry = 0;
    bomb = Bomb{};
    local = Player{};
    std::memset(local.name, 0, sizeof(local.name));
    globals = Globals{};
    globals.max_clients = 0;
    globals.current_time = 0;
    std::memset(globals.map_name, 0, sizeof(globals.map_name));
    globals.in_match = false;
    globals.localplayer = 0;
    players.clear();
}

} // namespace

bool Cache::Refresh() { return Get().RefreshImpl(); }

Snapshot Cache::CopySnapshot() {
    std::lock_guard<std::mutex> lock(Get().mtx);
    Snapshot snapshot{Get().game, Get().bomb, Get().local, Get().globals, Get().players, Get().status};
    if (snapshot.status.published_at != steady_clock::time_point{})
        snapshot.status.age = duration_cast<milliseconds>(steady_clock::now() - snapshot.status.published_at);
    return snapshot;
}

CacheSnapshotStatus Cache::Status() {
    std::lock_guard<std::mutex> lock(Get().mtx);
    auto status = Get().status;
    if (status.published_at != steady_clock::time_point{})
        status.age = duration_cast<milliseconds>(steady_clock::now() - status.published_at);
    return status;
}

void Cache::StopBackgroundWork() {
    auto& cache = Get();
    MapRaytrace::SetDesiredMap(""); // Cancel obsolete publication before waiting.
    if (cache.map_job.valid()) {
        try { cache.map_job.get(); }
        catch (const std::exception& e) { LOGF(WARNING, "[raytrace] map task stopped with error: {}", e.what()); }
        catch (...) { LOGF(WARNING, "[raytrace] map task stopped with an unknown error"); }
    }
    cache.requested_map.clear();
    cache.retry_at = steady_clock::time_point{};
    MapRaytrace::SetDesiredMap("");
}

bool Cache::PublishForTesting(const CacheTestFrame& frame) {
    const auto now = steady_clock::now() - std::max(frame.age, 0ms);
    if (!frame.process_available) {
        Get().PublishFailure(CacheStatus::ProcessUnavailable, now);
        return false;
    }
    if (!frame.game_readable) {
        Get().PublishFailure(CacheStatus::GameReadFailed, now);
        return false;
    }
    if (!frame.entity_list_available) {
        Get().PublishFailure(CacheStatus::EntityListUnavailable, now);
        return false;
    }
    if (!frame.globals_readable) {
        Get().PublishFailure(CacheStatus::GlobalsReadFailed, now);
        return false;
    }

    Game game;
    Bomb bomb;
    Player local;
    Globals globals;
    std::vector<Player> players;
    Clear(game, bomb, local, globals, players);
    globals.in_match = frame.in_match;
    const CacheStatus state = !frame.in_match ? CacheStatus::NoMatch
        : frame.local_present ? CacheStatus::Ready : CacheStatus::MissingLocal;
    Get().Publish(std::move(game), std::move(bomb), std::move(local), std::move(globals),
                  std::move(players), state, now, 0ms);
    return Get().Status().ready();
}

void Cache::PublishFailure(CacheStatus state, steady_clock::time_point now) {
    std::lock_guard<std::mutex> lock(mtx);
    Clear(game, bomb, local, globals, players);
    status.state = state;
    ++status.generation;
    status.published_at = now;
    status.age = 0ms;
    status.refresh_duration = duration_cast<milliseconds>(steady_clock::now() - now);
}

void Cache::Publish(Game&& next_game, Bomb&& next_bomb, Player&& next_local,
                    Globals&& next_globals, std::vector<Player>&& next_players,
                    CacheStatus state, steady_clock::time_point now, milliseconds refresh_duration) {
    std::lock_guard<std::mutex> lock(mtx);
    game = std::move(next_game);
    bomb = std::move(next_bomb);
    local = std::move(next_local);
    globals = std::move(next_globals);
    players = std::move(next_players);
    status.state = state;
    ++status.generation;
    status.published_at = now;
    status.age = 0ms;
    status.refresh_duration = refresh_duration;
}

void Cache::PublishMatrix(const Game& next_game) {
    std::lock_guard<std::mutex> lock(mtx);
    game.view_matrix = next_game.view_matrix;
}

bool Cache::RefreshImpl() {
    const auto now = steady_clock::now();
    if (!Engine::GetProcess()) {
        PublishFailure(CacheStatus::ProcessUnavailable, now);
        return false;
    }

    // Matrix freshness is intentionally independent from the slower entity
    // scan, but a failed matrix read invalidates the whole published frame.
    Game next_game;
    if (!next_game.Update()) {
        PublishFailure(CacheStatus::GameReadFailed, now);
        return false;
    }

#ifdef _DEBUG
    if (now - last < (cfg::dev::cache_refresh_rate * 1ms)) {
        PublishMatrix(next_game);
        return Status().ready();
    }
#else
    if (now - last < 5ms) {
        PublishMatrix(next_game);
        return Status().ready();
    }
#endif

    if (!next_game.UpdateEntityList()) {
        PublishFailure(CacheStatus::EntityListUnavailable, now);
        return false;
    }

    Globals next_globals;
    if (!next_globals.Update() || next_globals.max_clients < 0 || next_globals.max_clients > 64) {
        PublishFailure(CacheStatus::GlobalsReadFailed, now);
        return false;
    }

    Bomb next_bomb;
    // Bomb availability is optional (for example in warmup), so an unreadable
    // bomb clears only bomb data rather than preserving a previous carrier.
    if (!next_bomb.Update()) next_bomb = Bomb{};

    // Extraction and BVH construction never run under the publication mutex.
    // At most one task is outstanding and failed maps back off for 30 seconds.
    const std::string current_map = next_globals.in_match
        ? std::filesystem::path(std::string(next_globals.map_name,
              strnlen(next_globals.map_name, sizeof(next_globals.map_name)))).stem().string()
        : "";
    if (requested_map != current_map) {
        requested_map = current_map;
        MapRaytrace::SetDesiredMap(current_map);
        retry_at = now;
    }
    if (map_job.valid() && map_job.wait_for(0ms) == std::future_status::ready) map_job.get();
    if (!map_job.valid() && !current_map.empty() && !MapRaytrace::IsReady() && now >= retry_at) {
        retry_at = now + 30s;
        map_job = std::async(std::launch::async, [current_map] {
            try { return MapExtractor::EnsureMapLoaded(current_map); }
            catch (const std::exception& e) {
                LOGF(WARNING, "[raytrace] map load failed: {}", e.what());
                return false;
            }
        });
    }

    std::vector<Player> scan;
    scan.reserve(static_cast<std::size_t>(next_globals.max_clients));
    Player next_local;
    std::memset(next_local.name, 0, sizeof(next_local.name));
    for (int i = 0; i < next_globals.max_clients; ++i) {
        Player player(i, next_game.entity_list, next_game.list_entry);
        if (!player.Update()) continue;
        player.has_c4 = next_bomb.carrier != 0 && player.pawn_controller_addr == next_bomb.carrier;
        if (player.localplayer) next_local = player;
        scan.push_back(std::move(player));
    }

    // A missing local is a fresh absence, not a failed cache: spectating and
    // transitions may legitimately have no local pawn. Publishing the clean
    // sentinel prevents the previous local player from leaking across it.
    duration = duration_cast<milliseconds>(steady_clock::now() - now);
    last = now;
    const CacheStatus state = !next_globals.in_match ? CacheStatus::NoMatch
        : next_local.localplayer ? CacheStatus::Ready : CacheStatus::MissingLocal;
    Publish(std::move(next_game), std::move(next_bomb), std::move(next_local),
            std::move(next_globals), std::move(scan), state, now, duration);
    return Status().ready();
}
