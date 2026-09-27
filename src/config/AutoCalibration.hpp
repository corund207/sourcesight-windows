#pragma once

#include "Current.hpp"

#include <algorithm>
#include <cmath>

// Settings that can be derived safely from the overlay viewport live here so
// Simple mode behaves the same whether the menu is open or closed. Feature
// activation is deliberately untouched: automatic setup only owns presentation
// and resolution-dependent calibration.
namespace AutoCalibration {
inline bool Advanced() { return cfg::settings::advanced_controls; }

inline void TrackViewport(float height) {
    if (!std::isfinite(height) || height <= 0.f) return;
    auto& reference = cfg::world::radar::calibration_height;
    if (std::isfinite(reference) && reference > 0.f && std::abs(reference - height) > .5f) {
        const float factor = height / reference;
        cfg::world::radar::pos *= factor;
        cfg::world::radar::size *= factor;
        cfg::world::radar::offset *= factor;
    }
    reference = height;
}

inline void ApplySimple(float width, float height) {
    if (Advanced()) return;

    const float valid_height = std::isfinite(height) && height > 0.f ? height : 1080.f;
    const float valid_width = std::isfinite(width) && width > 0.f ? width : 1920.f;
    const float line_scale = std::clamp(valid_height / 1080.f, 1.f, 1.35f);
    const float pixels = valid_width * valid_height;

    cfg::esp::box_fill_alpha = .12f;
    cfg::esp::box_thickness = line_scale;
    cfg::esp::skeleton_thickness = 1.5f * line_scale;
    cfg::esp::tracer_thickness = line_scale;
    cfg::esp::head_tracker_size = 6.f * line_scale;

    cfg::esp::player_wireframe::detail = pixels >= 3000000.f ? 2 : (pixels >= 1000000.f ? 1 : 0);
    cfg::esp::player_wireframe::opacity = .8f;
    cfg::esp::player_wireframe::thickness = line_scale;
    cfg::esp::player_wireframe::max_distance = 3000.f;

    cfg::esp::bullet_tracer::duration = 1.25f;
    cfg::esp::bullet_tracer::muzzle_offset = 45.f;
    cfg::esp::bullet_tracer::thickness = 1.5f * line_scale;
    cfg::esp::bullet_tracer::style = 0;
    cfg::esp::bullet_tracer::glow = .65f;
    cfg::esp::bullet_tracer::impact = true;

    cfg::esp::wireframe_full_xray = false;
    cfg::esp::wireframe_panel_opacity = .1f;
    cfg::esp::wireframe_max_dist = 3000.f;
    cfg::esp::wireframe_opacity = .65f;
    cfg::esp::wireframe_budget = pixels >= 3000000.f ? 8000 : (pixels >= 1000000.f ? 6000 : 3000);
    cfg::esp::viewmodel_wireframe::opacity = .9f;
    cfg::esp::viewmodel_wireframe::scale = 1.f;

    cfg::world::radar::minimap = true;
    cfg::world::radar::auto_sync = true;
    cfg::world::radar::zoom = .7f;
    cfg::world::radar::hud_scale = 1.f;
    cfg::world::radar::hud_size = 1.f;
    cfg::world::radar::scale_correction = 1.f;
    cfg::world::radar::no_rotate = false;
    TrackViewport(valid_height);
}
} // namespace AutoCalibration
