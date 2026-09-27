#include "gui/frontend/overlays/RadarProjection.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>

void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
void near_value(float actual, float expected, const char* message) {
    require(std::abs(actual - expected) < .001f, message);
}
int main() {
    // Fixed world displacement: doubling zoom must double screen distance.
    near_value(radar::WorldRadius(2000, true, .7f, 1), 2000, "reference zoom calibration");
    near_value(radar::WorldRadius(2000, true, .35f, 1), 4000, "zooming out expands world coverage");
    near_value(radar::WorldRadius(2000, false, .35f, 1.25f), 2000, "standalone range ignores CS2 controls");
    require(radar::WorldRadius(2000, true, .7f, 1.25f) < 2000, "positive correction spreads dots out");
    near_value(radar::HudScale(true, .8f, 1.25f), 1, "HUD and radar size compose");
    near_value(radar::HudScale(false, .8f, 1.25f), 1, "standalone size preserved");

    near_value(radar::ResolutionScale(true, 1024, 1024), 1, "calibrated viewport");
    near_value(radar::ResolutionScale(true, 2048, 1024), 2, "double resolution doubles HUD geometry");
    near_value(radar::ResolutionScale(true, 512, 1024), .5f, "half resolution halves HUD geometry");
    near_value(radar::ResolutionScale(true, 1080, 0), 1, "legacy profiles keep pixel coordinates");
    near_value(radar::ResolutionScale(false, 2048, 1024), 1, "standalone radar retains explicit size");
    // Facing east: east is ahead (screen up), south is screen right.
    auto ahead = radar::Rotate(100, 0, true, 0, -1);
    near_value(ahead.x, 0, "east heading horizontal"); near_value(ahead.y, -100, "east heading forward");
    auto right = radar::Rotate(0, -100, true, 0, -1);
    near_value(right.x, 100, "east heading right"); near_value(right.y, 0, "east heading vertical");
    // Facing north: north is ahead, east is right. Camera FOV scaling cancels.
    auto north = radar::Rotate(0, 100, true, 2, 0);
    near_value(north.x, 0, "north heading horizontal"); near_value(north.y, -100, "north heading forward");
    for (int degrees = 0; degrees < 360; ++degrees) {
        const float angle = degrees * 3.14159265f / 180.f;
        auto p = radar::Rotate(30, 40, true, std::cos(angle), std::sin(angle));
        near_value(std::hypot(p.x, p.y), 50, "rotation must preserve distances without shear");
    }
    const float nan = std::numeric_limits<float>::quiet_NaN();
    require(std::isfinite(radar::WorldRadius(0, true, nan, 0)), "invalid settings remain finite");
    auto fallback = radar::Rotate(30, 40, true, 0, 0);
    near_value(std::hypot(fallback.x, fallback.y), 50, "invalid camera must not collapse markers");
    std::cout << "radar projection passed: zoom, HUD scaling, cardinal directions, 360-degree distance preservation, invalid inputs\n";
}
