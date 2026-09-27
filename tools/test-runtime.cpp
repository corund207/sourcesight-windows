#include "common.hpp"
#include "core/diagnostics/Diagnostics.hpp"
#include "core/engine/Engine.hpp"
#include "core/engine/cache/Cache.hpp"

#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main() {
    // Must remain safe before initialization and must not attach a process.
    Engine::Stop();
    Engine::Stop();

    const auto initial=Cache::CopySnapshot();
    require(initial.players.empty() && !initial.local.alive && initial.local.name[0]=='\0', "initialized local sentinel");
    require(initial.game.entity_list==0 && initial.globals.max_clients==0 &&
            initial.globals.map_name[0]=='\0', "initialized world sentinel");
    for(const auto& row:initial.game.view_matrix.matrix)
        for(float value:row) require(value==0.f, "initial camera is zero");

    require(!Cache::PublishForTesting({.process_available = false}), "disconnect transition");
    require(!Cache::Status().ready(), "disconnect is never ready");
    require(Cache::PublishForTesting({.local_present = false}), "spectator transition");
    require(Cache::Status().ready(), "fresh spectator data usable");
    require(!Cache::PublishForTesting({.local_present = false, .age = 251ms}), "stale transition");
    require(Diagnostics::Snapshot().cache.state == "stale", "stale diagnostic state");
    return 0;
}
