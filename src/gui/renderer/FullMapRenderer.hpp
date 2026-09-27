#pragma once
namespace FullMapRenderer {
// First color pass after clearing the transparent GL framebuffer, before ImGui.
// Requires a depth attachment; called on the render thread, not the cache worker.
bool Render(const view_matrix_t& matrix);
void Destroy();
}
