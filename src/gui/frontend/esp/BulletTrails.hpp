#pragma once

#include "core/engine/classes/Player.hpp"
#include <span>
#include <unordered_map>

namespace BulletTrails {
enum class HitKind { None, World, Player };
struct Hit {
    Vec3_t end;
    HitKind kind=HitKind::None;
    int player=-1;
    bool map_ready=false;
};
// Player capsules follow sampled bones. The reach is derived from the loaded
// map bounds; there is no user distance cutoff. No penetration or ricochet.
Hit Trace(const Vec3_t& origin,const Vec3_t& direction,
          std::span<const Player> players,int shooter);
struct Shot {
    Vec3_t origin;
    Hit hit;
    double time=0;
    bool teammate=false;
};
class System {
public:
    void Clear();
    void Update(std::span<const Player> players,const Player& local,double now,
                const std::string& map,bool in_match);
    void Render(view_matrix_t& matrix,const ImVec2& display,ImDrawList* draw,double now) const;
    const std::vector<Shot>& Shots() const { return shots; }
private:
    struct Previous {
        int ammo;
        short weapon;
        uint32_t pawn;
        uint64_t steam;
        bool reloading;
    };
    std::unordered_map<int,Previous> previous;
    std::vector<Shot> shots;
    std::string current_map;
    double last_time=0;
};
}
