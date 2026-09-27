#include "MapRaytrace.hpp"
#include "common.hpp"
#include "config/Current.hpp"
#include "gui/renderer/WireframeLines.hpp"

#include <fstream>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#include <filesystem>
#include <chrono>
#include <memory>
#include <span>
#include <numeric>
#include <optional>
#include <limits>

namespace {

constexpr float EPSILON = 1e-6f;
constexpr int   KD_LEAF_THRESHOLD = 8;
constexpr int   KD_MAX_DEPTH = 24;

void FreeKDTree(MapRaytrace::KDNode* node);
struct Geometry {
    std::string name;
    std::vector<MapRaytrace::Triangle> triangles;
    std::vector<MapRaytrace::AABB> triangle_bounds;
    MapRaytrace::KDNode* root = nullptr;
    ~Geometry() { FreeKDTree(root); }
};
std::atomic<std::shared_ptr<const Geometry>> g_geometry;
std::mutex g_publish_mutex;
uint64_t g_generation = 0;
std::string g_map_folder = "maps";
std::optional<std::string> g_desired_map;

// ── KD-tree helpers ──────────────────────────────────────────────────

MapRaytrace::AABB ComputeBounds(const MapRaytrace::Triangle& t) {
    MapRaytrace::AABB box;
    box.min.x = std::min({t.p1.x, t.p2.x, t.p3.x});
    box.min.y = std::min({t.p1.y, t.p2.y, t.p3.y});
    box.min.z = std::min({t.p1.z, t.p2.z, t.p3.z});
    box.max.x = std::max({t.p1.x, t.p2.x, t.p3.x});
    box.max.y = std::max({t.p1.y, t.p2.y, t.p3.y});
    box.max.z = std::max({t.p1.z, t.p2.z, t.p3.z});
    return box;
}

MapRaytrace::AABB MergeBounds(const MapRaytrace::AABB& a, const MapRaytrace::AABB& b) {
    MapRaytrace::AABB box;
    box.min.x = std::min(a.min.x, b.min.x);
    box.min.y = std::min(a.min.y, b.min.y);
    box.min.z = std::min(a.min.z, b.min.z);
    box.max.x = std::max(a.max.x, b.max.x);
    box.max.y = std::max(a.max.y, b.max.y);
    box.max.z = std::max(a.max.z, b.max.z);
    return box;
}

MapRaytrace::AABB ComputeBoundsFromIndices(const std::vector<MapRaytrace::Triangle>& tris,
                                            std::span<const uint32_t> indices) {
    MapRaytrace::AABB box = ComputeBounds(tris[indices[0]]);
    for (size_t i = 1; i < indices.size(); ++i) {
        box = MergeBounds(box, ComputeBounds(tris[indices[i]]));
    }
    return box;
}

MapRaytrace::KDNode* BuildKDTree(const std::vector<MapRaytrace::Triangle>& tris,
                                  std::span<uint32_t> indices, int depth) {
    auto owner = std::unique_ptr<MapRaytrace::KDNode, decltype(&FreeKDTree)>(new MapRaytrace::KDNode(), FreeKDTree);
    auto* node = owner.get();
    node->bbox = ComputeBoundsFromIndices(tris, indices);

    if ((int)indices.size() <= KD_LEAF_THRESHOLD || depth >= KD_MAX_DEPTH) {
        node->triangle_indices.assign(indices.begin(), indices.end());
        return owner.release();
    }

    // Split along the longest axis of the bounding box
    MapRaytrace::Vec3 extent;
    extent.x = node->bbox.max.x - node->bbox.min.x;
    extent.y = node->bbox.max.y - node->bbox.min.y;
    extent.z = node->bbox.max.z - node->bbox.min.z;

    int axis = 0;
    if (extent.y > extent.x && extent.y > extent.z) axis = 1;
    else if (extent.z > extent.x && extent.z > extent.y) axis = 2;

    // Sort indices by triangle centroid along the chosen axis
    auto comparator = [&](uint32_t a_idx, uint32_t b_idx) {
        const auto& ta = tris[a_idx];
        const auto& tb = tris[b_idx];
        float ca = (axis == 0) ? (ta.p1.x + ta.p2.x + ta.p3.x) / 3.0f
                 : (axis == 1) ? (ta.p1.y + ta.p2.y + ta.p3.y) / 3.0f
                               : (ta.p1.z + ta.p2.z + ta.p3.z) / 3.0f;
        float cb = (axis == 0) ? (tb.p1.x + tb.p2.x + tb.p3.x) / 3.0f
                 : (axis == 1) ? (tb.p1.y + tb.p2.y + tb.p3.y) / 3.0f
                               : (tb.p1.z + tb.p2.z + tb.p3.z) / 3.0f;
        return ca < cb;
    };

    size_t mid = indices.size() / 2;
    std::nth_element(indices.begin(), indices.begin() + mid, indices.end(), comparator);

    node->left = BuildKDTree(tris, indices.first(mid), depth + 1);
    node->right = BuildKDTree(tris, indices.subspan(mid), depth + 1);
    return owner.release();
}

void FreeKDTree(MapRaytrace::KDNode* node) {
    if (!node) return;
    FreeKDTree(node->left);
    FreeKDTree(node->right);
    delete node;
}

// ── Ray-AABB intersection (slab method) ──────────────────────────────

bool RayAABBIntersect(const MapRaytrace::Vec3& origin, const MapRaytrace::Vec3& inv_dir,
                      const MapRaytrace::AABB& box, float& tmin, float& tmax) {
    tmin = 0;
    tmax = std::numeric_limits<float>::max();
    const float o[] = {origin.x,origin.y,origin.z};
    const float inv[] = {inv_dir.x,inv_dir.y,inv_dir.z};
    const float low[] = {box.min.x,box.min.y,box.min.z};
    const float high[] = {box.max.x,box.max.y,box.max.z};
    for (int i=0;i<3;++i) {
        if (std::abs(inv[i]) >= 1e29f) {
            if (o[i]<low[i] || o[i]>high[i]) return false;
            continue;
        }
        const float a=(low[i]-o[i])*inv[i], b=(high[i]-o[i])*inv[i];
        tmin=std::max(tmin,std::min(a,b));
        tmax=std::min(tmax,std::max(a,b));
        if (tmin>tmax) return false;
    }
    return true;
}

// ── Möller-Trumbore ray-triangle intersection ────────────────────────

bool RayTriangleIntersect(const MapRaytrace::Vec3& origin, const MapRaytrace::Vec3& dir,
                          const MapRaytrace::Triangle& tri, float& t) {
    MapRaytrace::Vec3 edge1, edge2, h, s, q;
    float a, f, u, v;

    edge1.x = tri.p2.x - tri.p1.x;
    edge1.y = tri.p2.y - tri.p1.y;
    edge1.z = tri.p2.z - tri.p1.z;

    edge2.x = tri.p3.x - tri.p1.x;
    edge2.y = tri.p3.y - tri.p1.y;
    edge2.z = tri.p3.z - tri.p1.z;

    // h = dir × edge2
    h.x = dir.y * edge2.z - dir.z * edge2.y;
    h.y = dir.z * edge2.x - dir.x * edge2.z;
    h.z = dir.x * edge2.y - dir.y * edge2.x;

    a = edge1.x * h.x + edge1.y * h.y + edge1.z * h.z;

    if (a > -EPSILON && a < EPSILON)
        return false;

    f = 1.0f / a;

    s.x = origin.x - tri.p1.x;
    s.y = origin.y - tri.p1.y;
    s.z = origin.z - tri.p1.z;

    u = f * (s.x * h.x + s.y * h.y + s.z * h.z);
    if (u < 0.0f || u > 1.0f)
        return false;

    // q = s × edge1
    q.x = s.y * edge1.z - s.z * edge1.y;
    q.y = s.z * edge1.x - s.x * edge1.z;
    q.z = s.x * edge1.y - s.y * edge1.x;

    v = f * (dir.x * q.x + dir.y * q.y + dir.z * q.z);
    if (v < 0.0f || u + v > 1.0f)
        return false;

    t = f * (edge2.x * q.x + edge2.y * q.y + edge2.z * q.z);
    return (t > EPSILON);
}

// ── KD-tree ray traversal ────────────────────────────────────────────

bool TraverseKDTree(const std::vector<MapRaytrace::Triangle>& triangles, MapRaytrace::KDNode* node,
                    const MapRaytrace::Vec3& origin,
                    const MapRaytrace::Vec3& dir,
                    const MapRaytrace::Vec3& inv_dir,
                    float max_dist) {
    if (!node) return false;

    float tmin, tmax;
    if (!RayAABBIntersect(origin, inv_dir, node->bbox, tmin, tmax))
        return false;

    // AABB is behind us or too far
    if (tmax < 0.0f || tmin > max_dist)
        return false;

    // Leaf node: test all triangles
    if (!node->triangle_indices.empty()) {
        for (uint32_t idx : node->triangle_indices) {
            float t;
            if (RayTriangleIntersect(origin, dir, triangles[idx], t)) {
                if (t < max_dist)
                    return true;  // blocked
            }
        }
        return false;
    }

    // Internal node: recurse both children
    // Visit the closer child first for early-out
    if (node->left && node->right) {
        // Determine which child is closer
        // (simple heuristic: just try both, the early-out handles the rest)
        if (TraverseKDTree(triangles, node->left, origin, dir, inv_dir, max_dist))
            return true;
        return TraverseKDTree(triangles, node->right, origin, dir, inv_dir, max_dist);
    }

    if (node->left)  return TraverseKDTree(triangles, node->left, origin, dir, inv_dir, max_dist);
    if (node->right) return TraverseKDTree(triangles, node->right, origin, dir, inv_dir, max_dist);
    return false;
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════
// Public API
// ═══════════════════════════════════════════════════════════════════════

void MapRaytrace::Init(const std::string& map_folder) {
    std::lock_guard lock(g_publish_mutex);
    g_map_folder = map_folder;
}

bool MapRaytrace::LoadMap(const std::string& map_name) {
    if (map_name.empty() || map_name.find("..") != std::string::npos ||
        map_name.find_first_of("/\\\\") != std::string::npos)
        return false;
    uint64_t generation;
    std::string folder;
    {
        std::lock_guard lock(g_publish_mutex);
        if (g_desired_map && *g_desired_map != map_name) return false;
        auto current = g_geometry.load();
        if (current && current->name == map_name) return true;
        generation = ++g_generation;
        g_geometry.store(nullptr);
        folder = g_map_folder;
    }
    auto geometry = std::make_shared<Geometry>();
    geometry->name = map_name;
    auto t0 = std::chrono::steady_clock::now();
    std::vector<std::filesystem::path> candidates = {
        std::filesystem::path(folder) / (map_name + ".tri"),
        std::filesystem::path(folder) / map_name / "world_physics.tri",
        map_name + ".tri"
    };
    wchar_t executable_path[32768]{};
    const DWORD length=GetModuleFileNameW(nullptr,executable_path,std::size(executable_path));
    if(length && length<std::size(executable_path)) {
        const std::filesystem::path executable(executable_path);
        candidates.push_back(executable.parent_path()/folder/(map_name+".tri"));
        candidates.push_back(executable.parent_path().parent_path()/folder/(map_name+".tri"));
    }

    std::ifstream in;
    for (const auto& path : candidates) {
        in.open(path, std::ios::binary | std::ios::ate);
        if (in.is_open()) break;
        in.clear();
    }
    if (!in.is_open()) return false;
    const auto bytes = in.tellg();
    static_assert(sizeof(Triangle) == 36);
    if (bytes <= 0 || bytes > 512LL * 1024 * 1024 || bytes % sizeof(Triangle) != 0) {
        LOGF(WARNING, "[raytrace] invalid mesh size for '{}'", map_name);
        return false;
    }
    geometry->triangles.resize(static_cast<size_t>(bytes) / sizeof(Triangle));
    in.seekg(0);
    if (!in.read(reinterpret_cast<char*>(geometry->triangles.data()), bytes)) return false;
    // Reject corrupt coordinates and discard zero-area triangles before partitioning.
    auto valid = [](const Triangle& t) {
        for (auto p : {t.p1,t.p2,t.p3})
            if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) ||
                std::abs(p.x)>1e7f || std::abs(p.y)>1e7f || std::abs(p.z)>1e7f) return false;
        const Vec3 a{t.p2.x-t.p1.x,t.p2.y-t.p1.y,t.p2.z-t.p1.z};
        const Vec3 b{t.p3.x-t.p1.x,t.p3.y-t.p1.y,t.p3.z-t.p1.z};
        const Vec3 c{a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
        return c.x*c.x+c.y*c.y+c.z*c.z > EPSILON*EPSILON;
    };
    auto& triangles = geometry->triangles;
    std::erase_if(triangles, [&](const Triangle& t){return !valid(t);});
    if (triangles.empty()) return false;
    geometry->triangle_bounds.reserve(triangles.size());
    for (const auto& triangle : triangles) geometry->triangle_bounds.push_back(ComputeBounds(triangle));
    std::vector<uint32_t> indices(triangles.size());
    std::iota(indices.begin(), indices.end(), 0u);
    geometry->root = BuildKDTree(triangles, indices, 0);
    {
        std::lock_guard lock(g_publish_mutex);
        if (generation != g_generation) return false; // cancelled or superseded
        g_geometry.store(geometry);
    }
    LOGF(INFO, "[raytrace] loaded '{}' — {} triangles in {:.1f}ms", map_name, triangles.size(),
        std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t0).count());
    return true;
}

void MapRaytrace::Unload() {
    std::lock_guard lock(g_publish_mutex);
    ++g_generation;
    g_geometry.store(nullptr);
}

void MapRaytrace::SetDesiredMap(const std::string& map_name) {
    std::lock_guard lock(g_publish_mutex);
    g_desired_map = map_name;
    ++g_generation;
    g_geometry.store(nullptr);
}

bool MapRaytrace::IsVisible(const Vec3& origin, const Vec3& target) {
    const auto geometry = g_geometry.load();
    if (!geometry) return true;
    Vec3 dir{target.x-origin.x,target.y-origin.y,target.z-origin.z};
    float length = std::sqrt(dir.x*dir.x+dir.y*dir.y+dir.z*dir.z);
    if (!std::isfinite(length) || length < EPSILON) return true;
    dir.x/=length; dir.y/=length; dir.z/=length;
    Vec3 inv{std::abs(dir.x)<EPSILON?1e30f:1/dir.x,
             std::abs(dir.y)<EPSILON?1e30f:1/dir.y,
             std::abs(dir.z)<EPSILON?1e30f:1/dir.z};
    return !TraverseKDTree(geometry->triangles, geometry->root, origin, dir, inv, length);
}

void MapRaytrace::ClassifyVisibility(const Vec3& origin, std::span<const Vec3> targets,
                                    std::span<Visibility> results) {
    std::fill(results.begin(), results.end(), Visibility::Unknown);
    const auto geometry = g_geometry.load();
    if (!geometry || !std::isfinite(origin.x) || !std::isfinite(origin.y) || !std::isfinite(origin.z)) return;
    const size_t count = std::min(targets.size(), results.size());
    for (size_t i = 0; i < count; ++i) {
        const auto& target = targets[i];
        Vec3 dir{target.x-origin.x, target.y-origin.y, target.z-origin.z};
        const float length = std::sqrt(dir.x*dir.x + dir.y*dir.y + dir.z*dir.z);
        if (!std::isfinite(length) || length < EPSILON) continue;
        dir.x /= length; dir.y /= length; dir.z /= length;
        const Vec3 inv{std::abs(dir.x)<EPSILON?1e30f:1/dir.x,
                       std::abs(dir.y)<EPSILON?1e30f:1/dir.y,
                       std::abs(dir.z)<EPSILON?1e30f:1/dir.z};
        results[i] = TraverseKDTree(geometry->triangles, geometry->root, origin, dir, inv, length)
            ? Visibility::Blocked : Visibility::Visible;
    }
}

MapRaytrace::RayHit MapRaytrace::TraceSegment(const Vec3& origin, const Vec3& target) {
    RayHit result;
    const auto geometry = g_geometry.load();
    if (!geometry) return result;
    Vec3 dir{target.x-origin.x,target.y-origin.y,target.z-origin.z};
    float nearest=std::sqrt(dir.x*dir.x+dir.y*dir.y+dir.z*dir.z);
    if (!std::isfinite(origin.x) || !std::isfinite(origin.y) || !std::isfinite(origin.z) ||
        !std::isfinite(nearest) || nearest<EPSILON) return result;
    result.ready=true;
    dir.x/=nearest; dir.y/=nearest; dir.z/=nearest;
    const Vec3 inv{std::abs(dir.x)<EPSILON?1e30f:1/dir.x,
                   std::abs(dir.y)<EPSILON?1e30f:1/dir.y,
                   std::abs(dir.z)<EPSILON?1e30f:1/dir.z};
    auto visit=[&](auto&& self,const KDNode* node)->void {
        float entry,exit;
        if(!node || !RayAABBIntersect(origin,inv,node->bbox,entry,exit) || entry>nearest) return;
        for(auto id:node->triangle_indices) {
            float distance;
            if(RayTriangleIntersect(origin,dir,geometry->triangles[id],distance) && distance<=nearest) {
                nearest=distance; result.hit=true;
            }
        }
        auto* first=node->left;auto* second=node->right;
        float left_entry=std::numeric_limits<float>::max(),right_entry=left_entry,unused;
        if(first) RayAABBIntersect(origin,inv,first->bbox,left_entry,unused);
        if(second) RayAABBIntersect(origin,inv,second->bbox,right_entry,unused);
        if(right_entry<left_entry) std::swap(first,second);
        self(self,first);self(self,second);
    };
    visit(visit,geometry->root);
    result.distance=nearest;
    result.point={origin.x+dir.x*nearest,origin.y+dir.y*nearest,origin.z+dir.z*nearest};
    return result;
}

std::string MapRaytrace::CurrentMap() {
    const auto geometry = g_geometry.load();
    return geometry ? geometry->name : "";
}

bool MapRaytrace::IsReady() { return bool(g_geometry.load()); }

size_t MapRaytrace::TriangleCount() {
    const auto geometry = g_geometry.load();
    return geometry ? geometry->triangles.size() : 0;
}

MapRaytrace::AABB MapRaytrace::WorldBounds() {
    const auto geometry=g_geometry.load();
    return geometry&&geometry->root ? geometry->root->bbox : AABB{{0,0,0},{0,0,0}};
}

std::shared_ptr<const std::vector<MapRaytrace::Triangle>> MapRaytrace::MeshSnapshot() {
    const auto geometry=g_geometry.load();
    return geometry ? std::shared_ptr<const std::vector<Triangle>>(geometry,&geometry->triangles) : nullptr;
}

void MapRaytrace::RenderWireframe(view_matrix_t& matrix, const ImGuiIO& io, ImDrawList* d, const Vec3_t& camera_pos) {
    const auto geometry = g_geometry.load();
    if (!geometry || !d || !std::isfinite(io.DisplaySize.x) || !std::isfinite(io.DisplaySize.y) ||
        io.DisplaySize.x <= 0 || io.DisplaySize.y <= 0 || !std::isfinite(camera_pos.x) ||
        !std::isfinite(camera_pos.y) || !std::isfinite(camera_pos.z)) return;
    for (auto& row : matrix.matrix)
        for (float value : row) if (!std::isfinite(value)) return;
    const float radius = std::clamp(cfg::esp::wireframe_max_dist,100.0f,10000.0f);
    if (!std::isfinite(radius)) return;
    const auto distance2 = [&](const AABB& box) {
        const float x=camera_pos.x-std::clamp(camera_pos.x,box.min.x,box.max.x);
        const float y=camera_pos.y-std::clamp(camera_pos.y,box.min.y,box.max.y);
        const float z=camera_pos.z-std::clamp(camera_pos.z,box.min.z,box.max.z);
        return x*x+y*y+z*z;
    };
    // Homogeneous clip coordinates use matrix row 3 for perspective W.
    struct Clip { float x,y,w; };
    auto project = [&](Vec3 p) {
        return Clip{matrix[0][0]*p.x+matrix[0][1]*p.y+matrix[0][2]*p.z+matrix[0][3],
                    matrix[1][0]*p.x+matrix[1][1]*p.y+matrix[1][2]*p.z+matrix[1][3],
                    matrix[3][0]*p.x+matrix[3][1]*p.y+matrix[3][2]*p.z+matrix[3][3]};
    };
    auto planes = [](Clip p) { return std::array<float,5>{p.w-.01f,p.w+p.x,p.w-p.x,p.w+p.y,p.w-p.y}; };
    // Extract five world-space clip planes once, then test each box's support
    // vertex instead of projecting all eight corners for every visited node.
    std::array<std::array<float,4>,5> frustum;
    for(int j=0;j<4;++j) {
        frustum[0][j]=matrix[3][j];
        frustum[1][j]=matrix[3][j]+matrix[0][j];
        frustum[2][j]=matrix[3][j]-matrix[0][j];
        frustum[3][j]=matrix[3][j]+matrix[1][j];
        frustum[4][j]=matrix[3][j]-matrix[1][j];
    }
    frustum[0][3]-=.01f;
    auto outside = [&](const AABB& box) {
        for(const auto& p:frustum)
            if(p[0]*(p[0]>=0?box.max.x:box.min.x)+p[1]*(p[1]>=0?box.max.y:box.min.y)+
               p[2]*(p[2]>=0?box.max.z:box.min.z)+p[3]<0) return true;
        return false;
    };
    // Bound draw-list growth and prioritize nearby nodes when the budget is reached.
    const float opacity = std::clamp(cfg::esp::wireframe_opacity, 0.f, 1.f);
    if (!std::isfinite(opacity) || opacity <= 0) return;
    ImVec4 tint = cfg::esp::wireframe_color;
    tint.w = 1;
    const ImU32 rgb = ImGui::ColorConvertFloat4ToU32(tint) & ~IM_COL32_A_MASK;
    int remaining=std::clamp(cfg::esp::wireframe_budget, 500, 8000);
    int candidates_left=remaining*8;
    std::array<WireframeLines::Line,8000> lines;
    size_t line_count=0;
    auto edge = [&](Clip a, Clip b, ImU32 color) {
        if (!remaining || !(color & IM_COL32_A_MASK)) return;
        float lo=0,hi=1;
        auto fa=planes(a),fb=planes(b);
        for(int i=0;i<5;++i) {
            if(fa[i]<0 && fb[i]<0) return;
            if(fa[i]<0) lo=std::max(lo,fa[i]/(fa[i]-fb[i]));
            else if(fb[i]<0) hi=std::min(hi,fa[i]/(fa[i]-fb[i]));
        }
        if(lo>hi) return;
        auto screen = [&](float t) {
            float w=a.w+(b.w-a.w)*t;
            return ImVec2((1+(a.x+(b.x-a.x)*t)/w)*io.DisplaySize.x*.5f,
                          (1-(a.y+(b.y-a.y)*t)/w)*io.DisplaySize.y*.5f);
        };
        lines[line_count++]={screen(lo),screen(hi),color};
        --remaining;
    };
    auto visit = [&](auto&& self, const KDNode* node) -> void {
        if(!node || !remaining || !candidates_left || distance2(node->bbox)>radius*radius || outside(node->bbox)) return;
        if(!node->triangle_indices.empty()) {
            for(auto id:node->triangle_indices) {
                if(!candidates_left--) { candidates_left=0; break; }
                const auto& t=geometry->triangles[id];
                const float dist2=distance2(geometry->triangle_bounds[id]);
                if(dist2>radius*radius) continue;
                const int alpha=int(255*opacity*std::clamp(1-std::sqrt(dist2)/radius,.18f,1.f));
                const auto a=project(t.p1),b=project(t.p2),c=project(t.p3);
                const ImU32 color=rgb | (ImU32(alpha) << IM_COL32_A_SHIFT);
                edge(a,b,color);edge(b,c,color);edge(c,a,color);
                if(!remaining) break;
            }
            return;
        }
        auto* first=node->left;auto* second=node->right;
        if(first && second && distance2(first->bbox)>distance2(second->bbox)) std::swap(first,second);
        self(self,first);self(self,second);
    };
    d->PushClipRect(ImVec2(0,0),io.DisplaySize,true);
    visit(visit,geometry->root);
    WireframeLines::Draw(d,std::span(lines).first(line_count));
    d->PopClipRect();
}
