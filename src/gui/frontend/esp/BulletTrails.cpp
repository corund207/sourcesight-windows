#include "BulletTrails.hpp"
#include "core/engine/classes/MapRaytrace.hpp"
#include "SoundEsp.hpp"
#include <limits>
#include <numbers>

namespace {
constexpr float infinity=std::numeric_limits<float>::infinity();
bool Finite(const Vec3_t& p) { return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z); }
bool Firearm(short weapon) {
    switch(weapon) {
        case weapon_deagle: case weapon_elite: case weapon_fiveseven: case weapon_glock:
        case weapon_ak47: case weapon_aug: case weapon_awp: case weapon_famas: case weapon_g3sg1:
        case weapon_galilar: case weapon_m249: case weapon_m4a1: case weapon_mac10: case weapon_p90:
        case weapon_mp5sd: case weapon_ump45: case weapon_xm1014: case weapon_bizon: case weapon_mag7:
        case weapon_negev: case weapon_sawedoff: case weapon_tec9: case weapon_hkp2000: case weapon_mp7:
        case weapon_mp9: case weapon_nova: case weapon_p250: case weapon_scar20: case weapon_sg556:
        case weapon_ssg08: case weapon_m4a1_silencer: case weapon_usp_silencer: case weapon_cz75a:
        case weapon_revolver: return true;
        default: return false;
    }
}
float Sphere(const Vec3_t& origin,const Vec3_t& dir,const Vec3_t& center,float radius) {
    const auto delta=origin-center;
    const float b=delta.dot(dir),c=delta.length_sqr()-radius*radius;
    if(c<=0) return 0;
    const float discriminant=b*b-c;
    if(discriminant<0) return infinity;
    const float t=-b-std::sqrt(discriminant);
    return t>=0?t:infinity;
}
float Capsule(const Vec3_t& origin,const Vec3_t& dir,const Vec3_t& a,const Vec3_t& b,float radius) {
    const auto axis=b-a,offset=origin-a;
    const float length=axis.length();
    if(length<.01f) return Sphere(origin,dir,a,radius);
    const auto unit=axis/length;
    const float along=offset.dot(unit),slope=dir.dot(unit);
    const auto closest=a+unit*std::clamp(along,0.f,length);
    if((origin-closest).length_sqr()<=radius*radius) return 0;
    float result=std::min(Sphere(origin,dir,a,radius),Sphere(origin,dir,b,radius));
    const auto perpendicular=dir-unit*slope,radial=offset-unit*along;
    const float aa=perpendicular.length_sqr(),bb=radial.dot(perpendicular),cc=radial.length_sqr()-radius*radius;
    const float discriminant=bb*bb-aa*cc;
    if(aa>1e-8f && discriminant>=0) {
        for(float t:{(-bb-std::sqrt(discriminant))/aa,(-bb+std::sqrt(discriminant))/aa}) {
            const float height=along+t*slope;
            if(t>=0 && height>=0 && height<=length) result=std::min(result,t);
        }
    }
    return result;
}
float Box(const Vec3_t& origin,const Vec3_t& dir,const Vec3_t& low,const Vec3_t& high) {
    float entry=0,exit=infinity;
    for(int i=0;i<3;++i) {
        if(std::abs(dir[i])<1e-6f) { if(origin[i]<low[i] || origin[i]>high[i]) return infinity; }
        else {
            const float a=(low[i]-origin[i])/dir[i],b=(high[i]-origin[i])/dir[i];
            entry=std::max(entry,std::min(a,b));exit=std::min(exit,std::max(a,b));
            if(entry>exit) return infinity;
        }
    }
    return entry;
}
bool Joint(const Player& player,int index,Vec3_t& point) {
    if(index<0 || size_t(index)>=player.bone_list.size()) return false;
    point=player.bone_list[index].pos;
    return Finite(point)&&!point.zero()&&(point-player.pos).length_sqr()<128.f*128.f;
}
float PlayerHit(const Player& player,const Vec3_t& origin,const Vec3_t& dir,float limit) {
    // Broad phase covers crouches and extended limbs; detailed capsules avoid
    // the old standing box's empty space between arms and legs.
    const Vec3_t extent{128,128,128};
    if(Box(origin,dir,player.pos-extent,player.pos+extent)>limit) return infinity;
    struct Part {int a,b;float radius;};
    constexpr Part parts[]={{pelvis,spine_1,7},{spine_1,neck,8},{neck,head,3},
        {shoulder_L,elbow_L,3.5f},{elbow_L,hand_L,3},{shoulder_R,elbow_R,3.5f},{elbow_R,hand_R,3},
        {pelvis,hip_L,5},{hip_L,knee_L,4.5f},{knee_L,foot_heel_L,3.5f},
        {pelvis,hip_R,5},{hip_R,knee_R,4.5f},{knee_R,foot_heel_R,3.5f}};
    float nearest=infinity;
    bool have_bones=false;
    for(const auto& part:parts) {
        Vec3_t a,b;
        if(!Joint(player,part.a,a)||!Joint(player,part.b,b)) continue;
        if((a-b).length_sqr()>80.f*80.f) continue;
        have_bones=true;
        nearest=std::min(nearest,Capsule(origin,dir,a,b,part.radius));
    }
    Vec3_t top;
    if(Joint(player,head,top)) {have_bones=true;nearest=std::min(nearest,Sphere(origin,dir,top,4.5f));}
    // Missing torso joints can still occur with otherwise valid limb data.
    Vec3_t hips,neck_pos;
    if(Joint(player,pelvis,hips)&&Joint(player,neck,neck_pos)) {
        have_bones=true;nearest=std::min(nearest,Capsule(origin,dir,hips,neck_pos,7.5f));
    }
    if(!have_bones) nearest=Box(origin,dir,player.pos+Vec3_t{-16,-16,0},player.pos+Vec3_t{16,16,72});
    return nearest;
}
Vec3_t Direction(const Vec3_t& angles) {
    const float pitch=angles.x*std::numbers::pi_v<float>/180.f,yaw=angles.y*std::numbers::pi_v<float>/180.f;
    return {std::cos(pitch)*std::cos(yaw),std::cos(pitch)*std::sin(yaw),-std::sin(pitch)};
}

float CollisionReach(const Vec3_t& origin) {
    const auto bounds=MapRaytrace::WorldBounds();
    if(!Finite(origin)) return 0;
    const float dx=std::max(std::abs(origin.x-bounds.min.x),std::abs(origin.x-bounds.max.x));
    const float dy=std::max(std::abs(origin.y-bounds.min.y),std::abs(origin.y-bounds.max.y));
    const float dz=std::max(std::abs(origin.z-bounds.min.z),std::abs(origin.z-bounds.max.z));
    const float reach=std::hypot(dx,dy,dz)+1024.f;
    return std::isfinite(reach)?std::clamp(reach,1024.f,1000000.f):0.f;
}

// Finite homogeneous clipping: a visible segment survives even when its
// muzzle is outside the viewport or behind the near plane.
bool Project(const Vec3_t& a,const Vec3_t& b,const view_matrix_t& matrix,const ImVec2& size,ImVec2& sa,ImVec2& sb) {
    const auto& m=matrix.matrix;
    auto clip=[&](const Vec3_t& p){return std::array<float,3>{
        m[0][0]*p.x+m[0][1]*p.y+m[0][2]*p.z+m[0][3],
        m[1][0]*p.x+m[1][1]*p.y+m[1][2]*p.z+m[1][3],
        m[3][0]*p.x+m[3][1]*p.y+m[3][2]*p.z+m[3][3]};};
    auto planes=[](auto p){return std::array<float,5>{p[2]-.01f,p[2]+p[0],p[2]-p[0],p[2]+p[1],p[2]-p[1]};};
    const auto ca=clip(a),cb=clip(b);
    const auto fa=planes(ca),fb=planes(cb);
    float lo=0,hi=1;
    for(int i=0;i<5;++i) {
        if(!std::isfinite(fa[i])||!std::isfinite(fb[i])||(fa[i]<0&&fb[i]<0)) return false;
        if(fa[i]<0) lo=std::max(lo,fa[i]/(fa[i]-fb[i]));
        else if(fb[i]<0) hi=std::min(hi,fa[i]/(fa[i]-fb[i]));
    }
    if(lo>hi) return false;
    auto screen=[&](float t){const float w=ca[2]+(cb[2]-ca[2])*t;return ImVec2{
        (1+(ca[0]+(cb[0]-ca[0])*t)/w)*size.x*.5f,(1-(ca[1]+(cb[1]-ca[1])*t)/w)*size.y*.5f};};
    sa=screen(lo);sb=screen(hi);
    return std::isfinite(sa.x)&&std::isfinite(sa.y)&&std::isfinite(sb.x)&&std::isfinite(sb.y);
}
}

static BulletTrails::Hit TraceLimited(const Vec3_t& origin,const Vec3_t& direction,float distance,
                                      std::span<const Player> players,int shooter) {
    BulletTrails::Hit result{origin};
    const float length=direction.length();
    if(!Finite(origin)||!Finite(direction)||!std::isfinite(length)||!std::isfinite(distance)||distance<=0||length<1e-6f) return result;
    const auto dir=direction/length;
    float nearest=std::min(distance,1000000.f);
    const auto target=origin+dir*nearest;
    const auto world=MapRaytrace::TraceSegment({origin.x,origin.y,origin.z},{target.x,target.y,target.z});
    result.map_ready=world.ready;
    if(world.hit) {nearest=world.distance;result.kind=BulletTrails::HitKind::World;}
    for(const auto& player:players) {
        if(!player.alive||player.index==shooter||!Finite(player.pos)) continue;
        const float hit=PlayerHit(player,origin,dir,nearest);
        if(hit<nearest) {nearest=hit;result.kind=BulletTrails::HitKind::Player;result.player=player.index;}
    }
    result.end=origin+dir*nearest;
    return result;
}

BulletTrails::Hit BulletTrails::Trace(const Vec3_t& origin,const Vec3_t& direction,
                                      std::span<const Player> players,int shooter) {
    return TraceLimited(origin,direction,CollisionReach(origin),players,shooter);
}

void BulletTrails::System::Clear() {previous.clear();shots.clear();current_map.clear();last_time=0;}

void BulletTrails::System::Update(std::span<const Player> players,const Player& local,double now,
                                  const std::string& map,bool in_match) {
    namespace settings=cfg::esp::bullet_tracer;
    const std::string map_name=std::filesystem::path(map).stem().string();
    if(!cfg::enabled||!settings::enabled||!in_match||!std::isfinite(now)||
       map_name.empty()||MapRaytrace::CurrentMap()!=map_name) {Clear();return;}
    if(map_name!=current_map||now<last_time||now-last_time>.5) Clear();
    current_map=map_name;last_time=now;
    const float duration=std::clamp(settings::duration,.1f,10.f);
    std::erase_if(shots,[&](const Shot& shot){return now-shot.time>duration;});
    std::array<bool,256> seen{};
    for(const auto& player:players) {
        if(player.index<0||!player.alive||!Firearm(player.weapon.item_index)||player.ammo<0) continue;
        seen[static_cast<unsigned char>(player.index)]=true;
        const auto it=previous.find(player.index);
        const bool fired=it!=previous.end()&&it->second.weapon==player.weapon.item_index&&
            it->second.pawn==player.pawn_controller_addr&&it->second.steam==player.steam_id&&
            !it->second.reloading&&!player.is_reloading&&it->second.ammo>player.ammo;
        previous.insert_or_assign(player.index,Previous{player.ammo,player.weapon.item_index,
            player.pawn_controller_addr,player.steam_id,player.is_reloading});
        if(!fired||!Finite(player.eye_angles)||!Finite(player.pos)) continue;
        const bool teammate=player.team==local.team;
        if(!player.localplayer && ((!cfg::esp::team&&teammate)||
           (cfg::esp::spotted&&!player.spotted)||(!teammate&&cfg::esp::spotted_only&&!player.spotted))) continue;
        const auto dir=Direction(player.eye_angles);
        Vec3_t eye=player.pos+Vec3_t{0,0,64};
        Vec3_t head_pos;
        if(Joint(player,head,head_pos)) eye=head_pos;
        Vec3_t a,b,grip=eye;
        if(Joint(player,hand_L,a)&&Joint(player,hand_R,b)) grip=(a+b)*.5f;
        const float offset=std::clamp(settings::muzzle_offset,0.f,150.f);
        const auto desired_muzzle=grip+dir*offset;
        // Clamp the cosmetic muzzle to the first obstruction from the eye,
        // so a long muzzle offset cannot place a tracer beyond a nearby wall.
        const auto reach=desired_muzzle-eye;
        const auto muzzle=TraceLimited(eye,reach,reach.length(),players,player.index);
        Hit hit;
        Vec3_t origin=eye;
        if(muzzle.kind!=HitKind::None) hit=muzzle;
        else {
            origin=desired_muzzle;
            hit=Trace(origin,dir,players,player.index);
        }
        if(!hit.map_ready || MapRaytrace::CurrentMap()!=current_map) {
            Clear();return; // Geometry changed while the shot was being evaluated.
        }
        if(shots.size()>=256) shots.erase(shots.begin());
        shots.push_back({origin,hit,now,teammate});
        SoundEsp::AddGunshot(origin,!teammate);
    }
    std::erase_if(previous,[&](const auto& item){return !seen[static_cast<unsigned char>(item.first)];});
}

void BulletTrails::System::Render(view_matrix_t& matrix,const ImVec2& display,ImDrawList* draw,double now) const {
    namespace settings=cfg::esp::bullet_tracer;
    if(!settings::enabled||!draw||!std::isfinite(now)||!std::isfinite(display.x)||!std::isfinite(display.y)||display.x<=0||display.y<=0) return;
    for(const auto& row:matrix.matrix) for(float value:row) if(!std::isfinite(value)) return;
    const float duration=std::clamp(settings::duration,.1f,10.f),width=std::clamp(settings::thickness,1.f,4.f);
    const float glow=std::clamp(settings::glow,0.f,1.f);
    if(!std::isfinite(duration)||!std::isfinite(width)||!std::isfinite(glow)) return;
    draw->PushClipRect({0,0},display,true);
    for(const auto& shot:shots) {
        const float age=static_cast<float>(now-shot.time),progress=age/duration;
        if(progress<0||progress>=1) continue;
        const float fade=(1-progress)*(1-progress);
        const color_t base=shot.teammate?settings::team:settings::enemy;
        auto color=[&](float alpha,bool core=false){return ImGui::ColorConvertFloat4ToU32({
            core?base.r*.3f+.7f:base.r,core?base.g*.3f+.7f:base.g,core?base.b*.3f+.7f:base.b,
            std::clamp(base.a*fade*alpha,0.f,1.f)});};
        const auto delta=shot.hit.end-shot.origin;
        const int segments=settings::style==2?1:8;
        for(int i=0;i<segments;++i) {
            const float t0=float(i)/segments,t1=float(i+1)/segments;
            ImVec2 a,b;
            if(!Project(shot.origin+delta*t0,shot.origin+delta*t1,matrix,display,a,b)) continue;
            const float strength=settings::style==2?1.f:.25f+.75f*t1;
            if(settings::style==0&&glow>0) draw->AddLine(a,b,color(.16f*glow*strength),width+5.f);
            draw->AddLine(a,b,color(strength),width);
            if(settings::style!=2) draw->AddLine(a,b,color(.85f*strength,true),1.f);
        }
        // A short launch pulse never advances beyond the frozen collision end.
        if(settings::style!=2&&age<.12f) {
            const auto point=shot.origin+delta*std::clamp(age/.08f,0.f,1.f);
            Vec2_t screen;
            if(matrix.wts(point,display,screen)) draw->AddCircleFilled(screen,2.f,color(1,true),10);
        }
        if(settings::impact&&shot.hit.kind!=HitKind::None) {
            Vec2_t impact;
            if(matrix.wts(shot.hit.end,display,impact)) {
                const float radius=3.f+std::min(age/.3f,1.f)*5.f;
                draw->AddCircle(impact,radius,color(.75f),20,1.f);
                if(shot.hit.kind==HitKind::Player) {
                    draw->AddLine(impact+Vec2_t{-3,-3},impact+Vec2_t{3,3},color(1,true),1.f);
                    draw->AddLine(impact+Vec2_t{-3,3},impact+Vec2_t{3,-3},color(1,true),1.f);
                }
                else draw->AddCircleFilled(impact,1.5f,color(1,true),8);
            }
        }
    }
    draw->PopClipRect();
}
