#pragma once

class Player;

namespace PlayerWireframe {
// Recover the actual perspective camera, including spectator views. Row 2 is
// deliberately unused: CS2's world-to-screen path only requires rows 0, 1, 3.
bool CameraPosition(const view_matrix_t& matrix, Vec3_t& camera);
void Render(const Player& player, const view_matrix_t& matrix,
            const ImVec2& display, ImDrawList* draw);
}
