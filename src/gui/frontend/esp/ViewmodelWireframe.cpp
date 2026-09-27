#include "ViewmodelWireframe.hpp"

#include "config/Current.hpp"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <format>
#include <string>

namespace ViewmodelWireframe {
namespace {

struct Canvas {
    ImVec2 origin;
    float scale;

    ImVec2 point(float x, float y) const {
        return {origin.x + x * scale, origin.y + y * scale};
    }
};

ImU32 WithAlpha(color_t color, float alpha) {
    color.a = std::clamp(alpha, 0.f, 1.f);
    return ImGui::ColorConvertFloat4ToU32({color.r, color.g, color.b, color.a});
}

void Line(ImDrawList* draw, const Canvas& canvas, ImU32 color,
          float x1, float y1, float x2, float y2, float thickness = 1.35f) {
    draw->AddLine(canvas.point(x1, y1), canvas.point(x2, y2), color,
                  std::max(1.f, thickness * canvas.scale));
}

template <size_t N>
void Loop(ImDrawList* draw, const Canvas& canvas, ImU32 color,
          const std::array<ImVec2, N>& points, float thickness = 1.35f) {
    std::array<ImVec2, N> transformed;
    for (size_t i = 0; i < N; ++i)
        transformed[i] = canvas.point(points[i].x, points[i].y);
    draw->AddPolyline(transformed.data(), static_cast<int>(N), color,
                      ImDrawFlags_Closed, std::max(1.f, thickness * canvas.scale));
}

void DrawHand(ImDrawList* draw, const Canvas& canvas, ImU32 color, ImU32 faint,
              ImVec2 grip, bool mirrored) {
    const float side = mirrored ? -1.f : 1.f;
    const auto p = [&](float x, float y) { return ImVec2(grip.x + side * x, grip.y + y); };
    const std::array<ImVec2, 7> hand{
        p(-28, 20), p(-15, -8), p(8, -18), p(31, -5),
        p(38, 31), p(27, 82), p(-15, 102)
    };
    Loop(draw, canvas, color, hand);
    Line(draw, canvas, faint, p(-15, -8).x, p(-15, -8).y, p(27, 82).x, p(27, 82).y);
    Line(draw, canvas, faint, p(8, -18).x, p(8, -18).y, p(38, 31).x, p(38, 31).y);
    Line(draw, canvas, faint, p(-28, 20).x, p(-28, 20).y, p(31, -5).x, p(31, -5).y);
    Line(draw, canvas, faint, p(-15, 102).x, p(-15, 102).y, p(38, 31).x, p(38, 31).y);
}

void DrawLongGun(ImDrawList* draw, const Canvas& c, ImU32 color, ImU32 faint, Shape shape) {
    const bool sniper = shape == Shape::Sniper;
    const bool shotgun = shape == Shape::Shotgun;
    const bool heavy = shape == Shape::Heavy;
    const float barrel = sniper ? 525.f : shotgun ? 475.f : heavy ? 455.f : 430.f;
    const float body_top = heavy ? 35.f : 48.f;
    const std::array<ImVec2, 12> body{{
        {18, 72}, {118, 48}, {250, body_top}, {318, 55}, {barrel, 61}, {barrel, 78},
        {317, 83}, {286, 108}, {241, 106}, {224, 170}, {165, 166}, {178, 105}
    }};
    Loop(draw, c, color, body, 1.5f);
    Line(draw, c, faint, 18, 72, 178, 105);
    Line(draw, c, faint, 118, 48, 165, 166);
    Line(draw, c, faint, 250, body_top, 224, 170);
    Line(draw, c, faint, 317, 83, 250, body_top);
    Line(draw, c, faint, 286, 108, 318, 55);
    Line(draw, c, faint, 178, 105, 317, 83);
    Line(draw, c, faint, 18, 72, 165, 166);
    if (sniper) {
        const std::array<ImVec2, 6> scope{{{172, 22}, {266, 22}, {282, 39}, {263, 54}, {176, 54}, {158, 38}}};
        Loop(draw, c, color, scope);
        Line(draw, c, faint, 172, 22, 263, 54);
        Line(draw, c, faint, 266, 22, 176, 54);
        Line(draw, c, color, 216, 54, 216, 72);
    } else if (heavy) {
        const std::array<ImVec2, 6> box{{{222, 103}, {310, 101}, {325, 157}, {300, 185}, {229, 173}, {208, 128}}};
        Loop(draw, c, color, box);
        Line(draw, c, faint, 222, 103, 300, 185);
        Line(draw, c, faint, 310, 101, 229, 173);
    } else if (shotgun) {
        Line(draw, c, color, 301, 85, 376, 101, 1.7f);
        Line(draw, c, faint, 301, 85, 365, 58);
    } else {
        const std::array<ImVec2, 4> magazine{{{230, 104}, {286, 107}, {274, 174}, {240, 171}}};
        Loop(draw, c, color, magazine);
        Line(draw, c, faint, 230, 104, 274, 174);
        Line(draw, c, faint, 286, 107, 240, 171);
    }
    DrawHand(draw, c, color, faint, {230, 105}, false);
    DrawHand(draw, c, color, faint, {354, 83}, true);
}

void DrawPistol(ImDrawList* draw, const Canvas& c, ImU32 color, ImU32 faint) {
    const std::array<ImVec2, 9> body{{
        {115, 61}, {344, 57}, {380, 73}, {351, 92}, {247, 99},
        {230, 174}, {174, 169}, {183, 98}, {115, 89}
    }};
    Loop(draw, c, color, body, 1.55f);
    Line(draw, c, faint, 115, 61, 247, 99);
    Line(draw, c, faint, 344, 57, 183, 98);
    Line(draw, c, faint, 247, 99, 174, 169);
    Line(draw, c, faint, 230, 174, 183, 98);
    Line(draw, c, color, 276, 99, 269, 124);
    Line(draw, c, faint, 269, 124, 230, 106);
    DrawHand(draw, c, color, faint, {210, 112}, false);
    DrawHand(draw, c, color, faint, {298, 88}, true);
}

void DrawGrenade(ImDrawList* draw, const Canvas& c, ImU32 color, ImU32 faint) {
    const std::array<ImVec2, 10> shell{{
        {205, 66}, {238, 36}, {287, 36}, {320, 68}, {328, 126},
        {300, 173}, {245, 184}, {198, 154}, {186, 105}, {190, 80}
    }};
    Loop(draw, c, color, shell, 1.55f);
    Line(draw, c, faint, 205, 66, 300, 173);
    Line(draw, c, faint, 287, 36, 198, 154);
    Line(draw, c, faint, 186, 105, 328, 126);
    Line(draw, c, color, 238, 36, 242, 14);
    Line(draw, c, color, 242, 14, 307, 9);
    Line(draw, c, color, 307, 9, 320, 35);
    Line(draw, c, faint, 263, 13, 292, -11);
    DrawHand(draw, c, color, faint, {204, 111}, false);
    DrawHand(draw, c, color, faint, {314, 111}, true);
}

void DrawKnife(ImDrawList* draw, const Canvas& c, ImU32 color, ImU32 faint) {
    const std::array<ImVec2, 7> knife{{
        {102, 84}, {334, 38}, {418, 57}, {332, 82}, {219, 104}, {166, 141}, {137, 125}
    }};
    Loop(draw, c, color, knife, 1.55f);
    Line(draw, c, faint, 102, 84, 332, 82);
    Line(draw, c, faint, 334, 38, 219, 104);
    Line(draw, c, color, 166, 141, 137, 125);
    Line(draw, c, color, 137, 125, 111, 153);
    DrawHand(draw, c, color, faint, {151, 120}, false);
}

void DrawBomb(ImDrawList* draw, const Canvas& c, ImU32 color, ImU32 faint) {
    const std::array<ImVec2, 8> body{{
        {176, 48}, {326, 48}, {353, 77}, {350, 171},
        {315, 194}, {181, 184}, {154, 151}, {155, 77}
    }};
    Loop(draw, c, color, body, 1.55f);
    for (float x : {190.f, 235.f, 280.f, 325.f}) Line(draw, c, faint, x, 50, x - 10, 182);
    Line(draw, c, faint, 155, 77, 350, 171);
    Line(draw, c, faint, 326, 48, 154, 151);
    const std::array<ImVec2, 4> display{{{202, 77}, {306, 77}, {306, 113}, {202, 113}}};
    Loop(draw, c, color, display);
    DrawHand(draw, c, color, faint, {181, 137}, false);
    DrawHand(draw, c, color, faint, {329, 137}, true);
}

void DrawHands(ImDrawList* draw, const Canvas& c, ImU32 color, ImU32 faint) {
    DrawHand(draw, c, color, faint, {215, 92}, false);
    DrawHand(draw, c, color, faint, {315, 92}, true);
    Line(draw, c, faint, 202, 84, 328, 84);
}

} // namespace

Shape Classify(short item_index) {
    switch (item_index) {
    case weapon_deagle: case weapon_elite: case weapon_fiveseven: case weapon_glock:
    case weapon_tec9: case weapon_hkp2000: case weapon_p250: case weapon_usp_silencer:
    case weapon_cz75a: case weapon_revolver: case weapon_taser:
        return Shape::Pistol;
    case weapon_awp: case weapon_g3sg1: case weapon_scar20: case weapon_ssg08:
        return Shape::Sniper;
    case weapon_xm1014: case weapon_mag7: case weapon_sawedoff: case weapon_nova:
        return Shape::Shotgun;
    case weapon_m249: case weapon_negev:
        return Shape::Heavy;
    case weapon_flashbang: case weapon_hegrenade: case weapon_smokegrenade: case weapon_molotov:
    case weapon_decoy: case weapon_incgrenade: case weapon_tagrenade: case weapon_firebomb:
    case weapon_diversion: case weapon_frag_grenade: case weapon_snowball: case weapon_bumpmine:
        return Shape::Grenade;
    case weapon_knife: case weapon_knifegg: case weapon_knife_t: case weapon_knife_ghost:
    case weapon_knife_bayonet: case weapon_knife_css: case weapon_knife_flip: case weapon_knife_gut:
    case weapon_knife_karambit: case weapon_knife_m9_bayonet: case weapon_knife_tactical:
    case weapon_knife_falchion: case weapon_knife_survival_bowie: case weapon_knife_butterfly:
    case weapon_knife_push: case weapon_knife_cord: case weapon_knife_canis: case weapon_knife_ursus:
    case weapon_knife_gypsy_jackknife: case weapon_knife_outdoor: case weapon_knife_stiletto:
    case weapon_knife_widowmaker: case weapon_knife_skeleton: case weapon_knife_kukri:
        return Shape::Knife;
    case weapon_c4:
        return Shape::Bomb;
    case weapon_healthshot: case weapon_breachcharge: case weapon_tablet: case weapon_melee:
    case weapon_axe: case weapon_hammer: case weapon_spanner: case weapon_fists:
        return Shape::Utility;
    default:
        return Shape::Rifle;
    }
}

void Render(const Player& local, ImVec2 display_size, ImDrawList* draw, ImFont* icon_font) {
    namespace settings = cfg::esp::viewmodel_wireframe;
    if (!draw || !local.alive || local.weapon.item_index < 0 || !cfg::esp::wireframe ||
        !cfg::esp::wireframe_blackout || !settings::enabled ||
        !std::isfinite(display_size.x) || !std::isfinite(display_size.y) ||
        display_size.x < 320.f || display_size.y < 240.f)
        return;

    const float responsive = std::clamp(std::min(display_size.x / 1280.f, display_size.y / 720.f), .45f, 1.5f);
    const float model_scale = responsive * std::clamp(settings::scale, .7f, 1.35f);
    const float origin_x = std::min(display_size.x * .50f,
                                    std::max(12.f, display_size.x - 540.f * model_scale - 12.f));
    const Canvas canvas{{origin_x, display_size.y - 245.f * model_scale - 12.f}, model_scale};
    const float opacity = std::clamp(settings::opacity, .2f, 1.f);
    const ImU32 color = WithAlpha(cfg::esp::wireframe_color, opacity);
    const ImU32 faint = WithAlpha(cfg::esp::wireframe_color, opacity * .38f);

    const Shape shape = Classify(local.weapon.item_index);
    switch (shape) {
    case Shape::Pistol: DrawPistol(draw, canvas, color, faint); break;
    case Shape::Grenade: DrawGrenade(draw, canvas, color, faint); break;
    case Shape::Utility: DrawHands(draw, canvas, color, faint); break;
    case Shape::Knife: DrawKnife(draw, canvas, color, faint); break;
    case Shape::Bomb: DrawBomb(draw, canvas, color, faint); break;
    default: DrawLongGun(draw, canvas, color, faint, shape); break;
    }

    const ImVec2 label = canvas.point(12.f, -30.f);
    float icon_width = 0.f;
    if (icon_font && local.weapon.icon && local.weapon.icon[0]) {
        const float icon_size = 25.f * canvas.scale;
        draw->AddText(icon_font, icon_size, label, color, local.weapon.icon);
        icon_width = icon_font->CalcTextSizeA(icon_size, FLT_MAX, 0.f, local.weapon.icon).x
                   + 10.f * canvas.scale;
    }
    std::string status = local.weapon.name;
    if (local.is_reloading)
        status += " / RELOADING";
    else if (local.ammo >= 0 && shape != Shape::Grenade && shape != Shape::Knife &&
             shape != Shape::Bomb && shape != Shape::Utility)
        status += std::format(" / {}", local.ammo);
    draw->AddText({label.x + icon_width, label.y + 4.f * canvas.scale}, color, status.c_str());
    Line(draw, canvas, faint, 12, -5, 260, -5, 1.f);
}

} // namespace ViewmodelWireframe
