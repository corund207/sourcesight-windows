#pragma once

#include "core/engine/classes/Player.hpp"
#include <imgui.h>

namespace ViewmodelWireframe {

enum class Shape {
    Rifle,
    Pistol,
    Sniper,
    Shotgun,
    Heavy,
    Grenade,
    Knife,
    Bomb,
    Utility,
};

Shape Classify(short item_index);
void Render(const Player& local, ImVec2 display_size, ImDrawList* draw, ImFont* icon_font);

} // namespace ViewmodelWireframe
