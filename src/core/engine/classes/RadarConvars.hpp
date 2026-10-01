#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>

class pProcess;

// Live, read-only view of the CS2 HUD radar convars (cl_radar_scale and
// cl_hud_radar_scale). The convar objects are located by name in the game's
// heap, then only the cached float is polled. Every candidate is validated
// against its min/max/default slots, so a layout change after a game update
// yields "unavailable" instead of a wrong scale.
namespace RadarConvars {
struct Values {
    float radar_scale{};     // 0 when unavailable
    float hud_radar_scale{}; // 0 when unavailable
};

void Start(std::shared_ptr<pProcess> process);
void Stop();
Values Get();
} // namespace RadarConvars
