#pragma once

#include <string>

namespace cfg {
	inline bool enabled = true;

	namespace esp {
		inline bool team = true;

		inline bool box = true;
		inline bool box_filled = false;
		inline float box_fill_alpha = 0.12f;
		inline float box_thickness = 1.0f;
		inline float skeleton_thickness = 1.5f;
		inline float head_tracker_size = 6.0f;
		inline float tracer_thickness = 1.0f;
		inline bool armor = true;
		inline bool health = true;
		inline bool skeleton = true;
		inline bool head_tracker = true;
		inline bool head_tracker_filled = false;
		inline bool health_number = false;

		inline bool spotted = false;
		inline bool spotted_only = false;   // Only render spotted (visible) enemies
		inline bool distance = true;       // Show distance to player
		inline bool headshot_line = false;  // Line from crosshair to enemy head

		inline bool tracers = false;

		namespace bullet_tracer {
			inline bool enabled = false;
			inline float duration = 1.25f;
			inline float muzzle_offset = 45.0f;
			inline float thickness = 1.5f;
			inline int style = 0; // Ion / Streak / Minimal
			inline float glow = 0.65f;
			inline bool impact = true;

			inline color_t team{ 0.f, 1.f, 0.5f, 0.6f };
			inline color_t enemy{ 1.f, 0.3f, 0.3f, 0.6f };
		}

		namespace player_wireframe {
			inline bool enabled = false;
			inline bool visible_only = false;
			inline int detail = 1; // Standard / Detailed / Ultra, with screen-size LOD.
			inline float opacity = 0.8f;
			inline float thickness = 1.0f;
			inline float max_distance = 3000.0f;
			inline color_t visible{0.65f, 0.86f, 0.72f, 1.f};
			inline color_t blocked{0.85f, 0.48f, 0.42f, 1.f};
			inline color_t unknown{0.55f, 0.58f, 0.62f, 1.f};
		}

		// Debug wireframe of map collision geometry
		inline bool wireframe = false;
		inline int wireframe_mode = 0; // Overlay / translucent full-map GPU view.
		inline bool wireframe_full_xray = false;
		inline bool wireframe_blackout = false;
		inline float wireframe_panel_opacity = 0.10f;
		inline float wireframe_max_dist = 3000.0f;
		inline int wireframe_budget = 6000;
		inline float wireframe_opacity = 0.65f;
		inline color_t wireframe_color{ 100.f / 255.f, 215.f / 255.f, 220.f / 255.f, 1.f };

		namespace viewmodel_wireframe {
			inline bool enabled = true;
			inline float opacity = 0.9f;
			inline float scale = 1.0f;
		}

		inline bool bomb = true;

		namespace flags {
			inline bool name = true;
			inline bool ping = true;
			inline bool weapon = true;        // Now on by default
			inline bool ammo = false;
			inline bool reloading = false;
			inline bool defusing = false;
			inline bool money = false;
			inline bool flashed = false;
			inline bool scoped = false;
			inline bool has_c4 = false;
		}

		namespace colors {
			inline color_t box_team{ 0.f, 1.f, 0.29f, 0.5f };
			inline color_t box_enemy{ 1.f, 0.f, 0.f, 0.5f };

			inline color_t skeleton_team{ 0.f, 1.f, 0.f, 0.5f };
			inline color_t skeleton_enemy{ 1.f, 0.f, 0.f, 0.5f };

			inline color_t tracker_team{ 1.f, 1.f, 1.f, 0.5f };
			inline color_t tracker_enemy{ 1.f, 0.25f, 0.25f, 0.5f };

			inline color_t tracer_team{ 0.f, 1.f, 0.f, 0.5f };
			inline color_t tracer_enemy{ 1.f, 0.f, 0.f, 0.5f };

			inline color_t bomb{ 1.f, 0.84f, 0.f, 1.f };

			inline color_t headshot_line{ 1.f, 0.2f, 0.2f, 0.7f };

			namespace flags {
				inline color_t flashed_team{ 1.f, 1.f, 1.f, 0.5f };
				inline color_t flashed_enemy{ 1.f, 1.f, 1.f, 0.8f };

				inline color_t reloading_team{ 1.f, 1.f, 1.f, 0.5f };
				inline color_t reloading_enemy{ 1.f, 1.f, 1.f, 0.8f };

				inline color_t defusing_team{ 1.f, 1.f, 1.f, 0.5f };
				inline color_t defusing_enemy{ 1.f, 1.f, 1.f, 0.8f };

				inline color_t scoped_team{ 1.f, 1.f, 1.f, 0.5f };
				inline color_t scoped_enemy{ 1.f, 1.f, 1.f, 0.8f };

				inline color_t c4_team{ 1.f, 0.84f, 0.f, 1.f };
				inline color_t c4_enemy{ 1.f, 0.84f, 0.f, 1.f };
			}
		}
	}

	namespace world {
		namespace spectators {
			inline bool enabled = false;
			inline bool detailed = false;
			inline bool self_only = true;
			inline Vec2_t pos{ 10.f, 100.f };
		}

		namespace bomb {
			inline bool location = true;
			inline bool timer = true;
			inline Vec2_t pos{ 10.f, 300.f };
		}

		namespace crosshair {
			inline bool enabled = true;       // On by default now
			inline bool sniper_only = false;   // Show for all weapons
			inline bool center_dot = true;
			inline bool outline = true;
			inline float gap = 6.0f;
			inline float length = 6.0f;
			inline float thickness = 1.0f;
			inline float center_dot_size = 2.0f;
			inline float outline_thickness = 1.0f;
			inline color_t color{ 0.f, 0.7f, 1.f, 1.f };
		}

		namespace radar {
			inline bool minimap = true;
			inline bool auto_sync = true;
			inline float zoom = .7f;
			inline float hud_scale = 1.f;
			inline float hud_size = 1.f;
			inline float calibration_height = 0.f; // 0 preserves legacy pixel coordinates
			inline float scale_correction = 1.f;
			inline Vec2_t offset{0.f,0.f};
			inline bool enabled = true;
			inline float opacity = 0.20f;
			inline bool no_rotate = false;
			inline float range = 2000.f;
			inline Vec2_t pos{ 10.f, 10.f };
			inline Vec2_t size{ 200.f, 200.f };
		}

		namespace velocity {
			inline bool enabled = false;
			inline int sample_rate = 35;
			inline float sample_length = 5.f;
			inline Vec2_t size{ 400.f, 100.f };
			inline Vec2_t pos{ 10.f, 400.f };
		}
	}

	namespace settings {
		inline bool advanced_controls = false;
		inline bool watermark = true;
		inline bool streamproof = true;    // On by default for safety
		inline bool vsync = false;
		inline bool free_cpu = true;
		inline bool panic_key = true;      // Press F9 to disable everything
	}

	// Sound ESP (footsteps, gunshots, etc.)
	namespace sound_esp {
		inline bool enabled = false;
		inline bool footsteps = true;
		inline bool gunshots = true;
		inline float max_distance = 1000.0f;
		inline float duration = 3.0f;
		inline float fade_time = 1.0f;
		inline float footprint_size = 8.0f;
		inline color_t footsteps_color{ 1.f, 1.f, 1.f, 0.8f };
		inline color_t gunshots_color{ 1.f, 0.3f, 0.3f, 0.9f };
	}

	// Screen capture (game + overlay composited output).
	namespace capture {
		inline int fps = 60;                              // Video framerate for recordings.
		inline std::string output_dir = "captures";       // Folder for screenshots/recordings.
	}

	// Not stored, just for testing
	namespace dev {
		inline bool console = true;
		inline int open_menu_key = false;
		inline int cache_refresh_rate = 5;
		inline bool force_show_flags = false;
	}
}
