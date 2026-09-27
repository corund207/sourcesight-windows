#include "PlayerWireframe.hpp"
#include "core/engine/classes/Player.hpp"
#include "core/engine/classes/MapRaytrace.hpp"
#include "gui/renderer/WireframeLines.hpp"
#include <numbers>

namespace {
bool Finite(const Vec3_t& p) {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}

// A bone-driven approximation, not the game's model topology or hitboxes.
enum class Shape { Limb, Torso, Head, Hand, Foot };
struct Part { int a, b; float radius; Shape shape = Shape::Limb; };
constexpr std::array<Part, 19> parts{{
    {pelvis, neck, 8.f, Shape::Torso}, {neck, head, 4.5f, Shape::Head},
    {neck, head, 2.f},
    {neck, shoulder_L, 3.5f}, {shoulder_L, elbow_L, 3.f}, {elbow_L, hand_L, 2.5f},
    {neck, shoulder_R, 3.5f}, {shoulder_R, elbow_R, 3.f}, {elbow_R, hand_R, 2.5f},
    {pelvis, hip_L, 4.5f}, {hip_L, knee_L, 4.f}, {knee_L, foot_heel_L, 3.f},
    {pelvis, hip_R, 4.5f}, {hip_R, knee_R, 4.f}, {knee_R, foot_heel_R, 3.f},
    {elbow_L, hand_L, 2.8f, Shape::Hand}, {elbow_R, hand_R, 2.8f, Shape::Hand},
    {knee_L, foot_heel_L, 3.f, Shape::Foot}, {knee_R, foot_heel_R, 3.f, Shape::Foot}
}};
constexpr int max_sides=16, max_rings=7;
constexpr size_t max_edges = parts.size()*max_sides*(3*max_rings-2);
constexpr int visibility_sides=8, visibility_rows=3;
constexpr size_t max_samples = parts.size()*visibility_sides*visibility_rows;
struct Edge { ImVec2 a, b; };
struct Clip { float x, y, w; };

float Profile(Shape shape, float t) {
    // Radius profiles give the torso a waist/chest and close head/hands/feet
    // toward their poles instead of drawing open, constant-radius tubes.
    const std::array<float,5> profile = shape==Shape::Torso ? std::array<float,5>{1.f,.85f,1.2f,1.25f,.5f} :
        shape==Shape::Head ? std::array<float,5>{.15f,.8f,1.f,.9f,.12f} :
        (shape==Shape::Hand || shape==Shape::Foot) ? std::array<float,5>{.6f,.95f,1.f,.8f,.12f} :
        std::array<float,5>{.75f,1.f,.95f,.8f,.55f};
    const float position=t*4;
    const int index=std::min(3,int(position));
    return profile[index]+(profile[index+1]-profile[index])*(position-index);
}

// Clip before dividing by W, so off-screen joints and near-plane crossings do
// not create huge lines or disappear simply because a player box is off screen.
bool ProjectEdge(const Vec3_t& a, const Vec3_t& b, const view_matrix_t& matrix,
                 const ImVec2& display, Edge& edge, MapRaytrace::Vec3& sample) {
    const auto& m = matrix.matrix;
    auto project = [&](const Vec3_t& p) {
        return Clip{m[0][0]*p.x+m[0][1]*p.y+m[0][2]*p.z+m[0][3],
                    m[1][0]*p.x+m[1][1]*p.y+m[1][2]*p.z+m[1][3],
                    m[3][0]*p.x+m[3][1]*p.y+m[3][2]*p.z+m[3][3]};
    };
    auto planes = [](Clip p) { return std::array<float,5>{p.w-.01f, p.w+p.x, p.w-p.x, p.w+p.y, p.w-p.y}; };
    const auto ca = project(a), cb = project(b);
    const auto fa = planes(ca), fb = planes(cb);
    float lo = 0, hi = 1;
    for (int i = 0; i < 5; ++i) {
        if (!std::isfinite(fa[i]) || !std::isfinite(fb[i]) || (fa[i]<0 && fb[i]<0)) return false;
        if (fa[i]<0) lo = std::max(lo, fa[i]/(fa[i]-fb[i]));
        else if (fb[i]<0) hi = std::min(hi, fa[i]/(fa[i]-fb[i]));
    }
    if (lo>hi) return false;
    auto screen = [&](float t) {
        const float w = ca.w+(cb.w-ca.w)*t;
        return ImVec2((1+(ca.x+(cb.x-ca.x)*t)/w)*display.x*.5f,
                      (1-(ca.y+(cb.y-ca.y)*t)/w)*display.y*.5f);
    };
    edge = {screen(lo), screen(hi)};
    if (!std::isfinite(edge.a.x) || !std::isfinite(edge.a.y) ||
        !std::isfinite(edge.b.x) || !std::isfinite(edge.b.y)) return false;
    const float dx = edge.a.x-edge.b.x, dy = edge.a.y-edge.b.y;
    if (dx*dx+dy*dy < .25f) return false; // Skip subpixel edges and their rays.
    const auto midpoint = a+(b-a)*((lo+hi)*.5f);
    sample = {midpoint.x, midpoint.y, midpoint.z};
    return true;
}
}

bool PlayerWireframe::CameraPosition(const view_matrix_t& matrix, Vec3_t& camera) {
    for (const auto& row : matrix.matrix)
        for (float v : row) if (!std::isfinite(v)) return false;
    const auto& m = matrix.matrix;
    const Vec3_t a{m[0][0],m[0][1],m[0][2]}, b{m[1][0],m[1][1],m[1][2]}, c{m[3][0],m[3][1],m[3][2]};
    const float determinant = a.dot(b.cross(c));
    if (!std::isfinite(determinant) || std::abs(determinant)<1e-8f) return false;
    camera = (b.cross(c)*(-m[0][3])+c.cross(a)*(-m[1][3])+a.cross(b)*(-m[3][3]))/determinant;
    return Finite(camera);
}

void PlayerWireframe::Render(const Player& player, const view_matrix_t& matrix,
                             const ImVec2& display, ImDrawList* draw) {
    namespace settings = cfg::esp::player_wireframe;
    if (!settings::enabled || !draw || !player.alive || !Finite(player.pos) ||
        !std::isfinite(display.x) || !std::isfinite(display.y) || display.x<=0 || display.y<=0) return;
    const float opacity = std::clamp(settings::opacity, 0.f, 1.f);
    const float distance = std::clamp(settings::max_distance, 100.f, 10000.f);
    const float thickness = std::clamp(settings::thickness, 1.f, 3.f);
    if (!std::isfinite(opacity) || opacity<=0 || !std::isfinite(distance) || !std::isfinite(thickness)) return;
    Vec3_t camera;
    if (!CameraPosition(matrix, camera) || (player.pos-camera).length_sqr()>distance*distance) return;

    // Fixed storage; each small surface cell shares one visibility sample
    // across its ring, longitudinal and diagonal edges. No per-edge ray burst.
    std::array<Edge, max_edges> edges;
    std::array<uint16_t,max_edges> sample_ids;
    std::array<MapRaytrace::Vec3, max_samples> samples;
    std::array<MapRaytrace::Visibility, max_samples> states;
    size_t count = 0, sample_count=0;
    int detail=std::clamp(settings::detail,0,2);
    const auto& m=matrix.matrix;
    const float w=m[3][0]*player.pos.x+m[3][1]*player.pos.y+m[3][2]*player.pos.z+m[3][3];
    const float projected_height=80.f*std::sqrt(m[1][0]*m[1][0]+m[1][1]*m[1][1]+m[1][2]*m[1][2])*display.y*.5f/std::max(.01f,std::abs(w));
    if(projected_height<80) detail=0;
    else if(projected_height<160) detail=std::min(detail,1);
    const int sides=8+detail*4, rings=3+detail*2;
    // Trigonometry is shared by every body part, rather than every ring.
    std::array<ImVec2,max_sides> circle;
    for(int i=0;i<sides;++i) {
        const float angle=2.f*std::numbers::pi_v<float>*i/sides;
        circle[i]={std::cos(angle),std::sin(angle)};
    }
    std::array<ImVec2,visibility_sides> sample_circle;
    for(int i=0;i<visibility_sides;++i) {
        const float angle=2.f*std::numbers::pi_v<float>*(i+.5f)/visibility_sides;
        sample_circle[i]={std::cos(angle),std::sin(angle)};
    }
    auto joint = [&](int index, Vec3_t& p) {
        if (index<0 || static_cast<size_t>(index)>=player.bone_list.size()) return false;
        p = player.bone_list[index].pos;
        return Finite(p) && !p.zero() && (p-player.pos).length_sqr()<128.f*128.f;
    };
    uint16_t current_sample=0;
    auto edge = [&](const Vec3_t& a, const Vec3_t& b) {
        MapRaytrace::Vec3 unused;
        if (count<max_edges && ProjectEdge(a,b,matrix,display,edges[count],unused)) {
            sample_ids[count]=current_sample;
            ++count;
        }
    };
    Vec3_t left,right,hips,neck_pos;
    const Vec3_t body_right=joint(shoulder_L,left)&&joint(shoulder_R,right) ? (right-left).normalized() : Vec3_t{1,0,0};
    const Vec3_t body_up=joint(pelvis,hips)&&joint(neck,neck_pos) ? (neck_pos-hips).normalized() : Vec3_t{0,0,1};
    const auto body_forward=body_right.cross(body_up).normalized();
    for (const auto& part : parts) {
        Vec3_t a, b;
        if (!joint(part.a,a) || !joint(part.b,b)) continue;
        const auto delta = b-a;
        const float length = delta.length();
        if (length<.1f || length>80.f) continue;
        auto axis = delta/length;
        if(part.shape==Shape::Head) { a=b-axis*5.f; b+=axis*5.f; }
        if(part.shape==Shape::Hand) { a=b-axis; b+=axis*6.f; }
        if(part.shape==Shape::Foot) {
            axis=body_forward;
            if(axis.length_sqr()<.5f) continue;
            a=b-axis*2.f; b+=axis*8.f;
        }
        auto width=body_right-axis*axis.dot(body_right);
        if(width.length_sqr()<.01f) {
            const auto reference=std::abs(axis.z)<.9f ? Vec3_t{0,0,1} : Vec3_t{0,1,0};
            width=axis.cross(reference);
        }
        const auto u=width.normalized()*part.radius;
        const float depth=part.shape==Shape::Torso ? .65f :
            (part.shape==Shape::Hand || part.shape==Shape::Foot) ? .5f : .85f;
        const auto v=axis.cross(u)*depth;
        std::array<std::array<Vec3_t,max_sides>,max_rings> mesh;
        for(int r=0;r<rings;++r) {
            const float t=float(r)/(rings-1), radius=Profile(part.shape,t);
            for(int s=0;s<sides;++s)
                mesh[r][s]=a+(b-a)*t+(u*circle[s].x+v*circle[s].y)*radius;
        }
        // Keep visibility resolution bounded independently of visual detail:
        // 24 surface patches per part, shared by all tessellation edges.
        std::array<std::array<int,visibility_sides>,visibility_rows> patch_ids;
        for(auto& row:patch_ids) row.fill(-1);
        for(int r=0;r<rings;++r) for(int s=0;s<sides;++s) {
            const int next=(s+1)%sides;
            const int row=std::min(visibility_rows-1,int((r+.5f)/(rings-1)*visibility_rows));
            const int sector=std::min(visibility_sides-1,int((s+.5f)/sides*visibility_sides));
            auto& id=patch_ids[row][sector];
            current_sample=static_cast<uint16_t>(id<0 ? sample_count : size_t(id));
            const size_t before=count;
            edge(mesh[r][s],mesh[r][next]);
            if(r+1<rings) {
                edge(mesh[r][s],mesh[r+1][s]);
                if(detail>0) edge(mesh[r][s],mesh[r+1][next]);
            }
            if(count!=before && id<0) {
                id=static_cast<int>(sample_count);
                const float t=(row+.5f)/visibility_rows;
                const auto midpoint=a+(b-a)*t+(u*sample_circle[sector].x+v*sample_circle[sector].y)*Profile(part.shape,t);
                samples[sample_count++]={midpoint.x,midpoint.y,midpoint.z};
            }
        }
    }
    if (!count) return;
    MapRaytrace::ClassifyVisibility({camera.x,camera.y,camera.z},
        std::span(samples).first(sample_count), std::span(states).first(sample_count));
    auto tint = [&](color_t color) {
        ImVec4 c = color;
        // RGB controls plus one explicit opacity control.
        c.w = opacity;
        return ImGui::ColorConvertFloat4ToU32(c);
    };
    const auto visible = tint(settings::visible), blocked = tint(settings::blocked), unknown = tint(settings::unknown);
    draw->PushClipRect({0,0},display,true);
    std::array<WireframeLines::Line,max_edges> lines;
    size_t line_count=0;
    for (size_t i=0; i<count; ++i) {
        const auto state = states[sample_ids[i]];
        if (settings::visible_only && state != MapRaytrace::Visibility::Visible) continue;
        const auto color = state == MapRaytrace::Visibility::Visible ? visible :
            state == MapRaytrace::Visibility::Blocked ? blocked : unknown;
        lines[line_count++]={edges[i].a,edges[i].b,color};
    }
    WireframeLines::Draw(draw,std::span(lines).first(line_count),thickness);
    draw->PopClipRect();
}
