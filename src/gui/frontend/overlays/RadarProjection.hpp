#pragma once

#include <algorithm>
#include <cmath>

namespace radar {
inline float Positive(float value, float fallback) {
    return std::isfinite(value) && value > 0.f ? value : fallback;
}

// The calibrated world radius is measured at cl_radar_scale 0.7.
// Increasing CS2 zoom must increase pixel displacement, not world coverage.
inline float WorldRadius(float calibrated, bool apply_zoom, float zoom, float correction) {
    const float base = Positive(calibrated, 2000.f);
    if (!apply_zoom) return base;
    return base * .7f / std::clamp(Positive(zoom, .7f), .25f, 1.f)
        / std::clamp(Positive(correction, 1.f), .75f, 1.25f);
}

inline float HudScale(bool minimap, float hud, float radar_size) {
    if (!minimap) return 1.f;
    return std::clamp(Positive(hud, 1.f), .5f, 2.f)
        * std::clamp(Positive(radar_size, 1.f), .5f, 2.f);
}

// HUD dimensions scale uniformly with viewport height, not desktop width.
// Zero reference height preserves profiles made before resolution calibration.
inline float ResolutionScale(bool minimap, float height, float reference) {
    if (!minimap || !std::isfinite(reference) || reference <= 0.f) return 1.f;
    return Positive(height, reference) / reference;
}

struct Point { float x, y; };
inline Point Rotate(float x, float y, bool rotate, float right_x, float right_y) {
    if (!rotate) return {x, -y};
    const float length = std::hypot(right_x, right_y);
    if (!std::isfinite(length) || length < .001f) return {x, -y};
    right_x /= length;
    right_y /= length;
    // Derive the orthogonal forward axis from screen-right. It stays stable
    // when looking straight up/down, unlike the horizontal view-depth row.
    return {x * right_x + y * right_y, x * right_y - y * right_x};
}
} // namespace radar
