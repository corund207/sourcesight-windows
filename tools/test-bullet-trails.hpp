#pragma once
#include "gui/frontend/esp/BulletTrails.hpp"

// Called by test-map-geometry.cpp inside its temporary directory and ImGui context.
static void TestBulletTrails(const std::filesystem::path& dir) {
    using namespace BulletTrails;
    auto wall=[](float x){return MapRaytrace::Triangle{{x,-200,-200},{x,200,-200},{x,0,400}};};
    auto load=[&](const char* name,std::vector<MapRaytrace::Triangle> triangles) {
        {std::ofstream file(dir/(std::string(name)+".tri"),std::ios::binary);
         file.write(reinterpret_cast<const char*>(triangles.data()),triangles.size()*sizeof(MapRaytrace::Triangle));}
        require(MapRaytrace::LoadMap(name),"bullet wall fixture load");
    };
    std::vector<MapRaytrace::Triangle> walls;
    for(int i=12;i>=1;--i) walls.push_back(wall(float(i)*25));
    load("bullet-nearest",walls);
    auto world=MapRaytrace::TraceSegment({0,0,36},{500,0,36});
    require(world.ready&&world.hit&&std::abs(world.distance-25)<.001f,"world ray finds nearest triangle across KD nodes");
    world=MapRaytrace::TraceSegment({0,0,36},{20,0,36});
    require(world.ready&&!world.hit&&std::abs(world.distance-20)<.001f,"world ray respects finite distance");
    require(!MapRaytrace::TraceSegment({NAN,0,0},{50,0,0}).ready,"invalid world ray rejected");

    Player target;
    target.index=2;target.alive=true;target.pos={60,0,0};target.team=1;
    target.bone_list.resize(30);
    auto joint=[&](int index,float y,float z){target.bone_list[index].pos={60,y,z};};
    joint(pelvis,0,30);joint(spine_1,0,44);joint(neck,0,58);joint(head,0,65);
    joint(shoulder_L,-12,55);joint(elbow_L,-18,42);joint(hand_L,-20,30);
    joint(shoulder_R,12,55);joint(elbow_R,18,42);joint(hand_R,20,30);
    joint(hip_L,-7,28);joint(knee_L,-7,14);joint(foot_heel_L,-7,2);
    joint(hip_R,7,28);joint(knee_R,7,14);joint(foot_heel_R,7,2);
    std::array<Player,1> targets{target};
    auto hit=Trace({0,0,44},{1,0,0},targets,1);
    require(hit.map_ready&&hit.kind==HitKind::World&&std::abs(hit.end.x-25)<.001f,"wall shields player behind it");
    load("bullet-far",{wall(100)});
    hit=Trace({0,0,44},{1,0,0},targets,1);
    require(hit.kind==HitKind::Player&&hit.player==2&&hit.end.x>50&&hit.end.x<60,"bone capsule stops shot before wall");
    hit=Trace({0,0,44},{1,0,0},targets,2);
    require(hit.kind==HitKind::World,"shooter excluded from player collision");
    hit=Trace({0,0,10},{1,0,0},targets,1);
    require(hit.kind==HitKind::World,"ray through leg gap does not hit a standing hull");
    hit=Trace({60,0,44},{1,0,0},targets,1);
    require(hit.kind==HitKind::Player&&hit.end.x==60,"ray starting inside player stops immediately");
    hit=Trace({0,0,65},{1,0,0},targets,1);
    require(hit.kind==HitKind::Player&&std::abs(hit.end.x-55.5f)<.01f,"head sphere collision");
    targets[0].bone_list.clear();
    hit=Trace({0,0,44},{1,0,0},targets,1);
    require(hit.kind==HitKind::Player&&hit.end.x==44,"missing bones use conservative player hull");
    targets[0]=target;
    for(auto& bone:targets[0].bone_list) bone.pos.z*=.5f;
    hit=Trace({0,0,65},{1,0,0},targets,1);
    require(hit.kind==HitKind::World,"crouching pose does not retain standing head collision");

    std::array<Player,0> no_players{};
    load("bullet-distant",{wall(20000)});
    hit=Trace({0,0,44},{1,0,0},no_players,1);
    require(hit.kind==HitKind::World&&std::abs(hit.end.x-20000)<.01f,
            "automatic collision reach does not stop at the legacy distance cutoff");
    load("bullet-far",{wall(100)});

    namespace settings=cfg::esp::bullet_tracer;
    settings::enabled=true;settings::duration=1.25f;settings::muzzle_offset=20;
    Player shooter;
    shooter.index=1;shooter.localplayer=true;shooter.alive=true;shooter.pos={0,0,0};
    shooter.team=1;shooter.weapon.item_index=7;shooter.ammo=30;shooter.pawn_controller_addr=123;
    std::array<Player,2> players{shooter,target}; // Same team must still block shots.
    System system;
    double time=1;
    std::string active_map="bullet-far";
    auto update=[&] {system.Update(players,players[0],time,active_map,true);time+=.01;};
    update();require(system.Shots().empty(),"first observation does not create a phantom shot");
    players[0].ammo--;update();
    require(system.Shots().size()==1&&system.Shots()[0].hit.kind==HitKind::Player,"local shot hits teammate capsule");
    const auto frozen=system.Shots()[0].hit.end;
    players[1].pos.x=200;players[1].bone_list.clear();update();
    require(system.Shots().size()==1&&system.Shots()[0].hit.end==frozen,"shot endpoint freezes at firing time and is not duplicated");
    players[0].ammo=30;players[0].is_reloading=true;update();
    players[0].ammo=29;update();
    require(system.Shots().size()==1,"reload changes do not create shots");
    players[0].is_reloading=false;players[0].weapon.item_index=9;players[0].ammo=5;update();
    require(system.Shots().size()==1,"weapon switch does not create a shot");
    players[0].pawn_controller_addr++;players[0].ammo=3;update();
    require(system.Shots().size()==1,"reused player identity resets baseline");
    players[0].ammo--;update();
    require(system.Shots().size()==2&&system.Shots().back().hit.kind==HitKind::World,"shot after identity reset collides with environment");
    players[0].weapon.item_index=weapon_hegrenade;players[0].ammo=1;update();players[0].ammo=0;update();
    require(system.Shots().size()==2,"grenades do not emit bullet trails");
    players[0].weapon.item_index=weapon_ak47;players[0].ammo=30;update();

    view_matrix_t matrix{};
    matrix[0][1]=1;matrix[1][2]=1;matrix[1][3]=-50;matrix[3][0]=1;
    auto render=[&](int style) {
        settings::style=style;
        ImGui::NewFrame();auto* draw=ImGui::GetBackgroundDrawList();
        system.Render(matrix,ImGui::GetIO().DisplaySize,draw,time);
        for(const auto& vertex:draw->VtxBuffer)
            require(std::isfinite(vertex.pos.x)&&std::isfinite(vertex.pos.y),"stylized tracer projection is finite");
        const int count=draw->VtxBuffer.Size;
        ImGui::Render();return count;
    };
    require(render(0)>0&&render(1)>0&&render(2)>0,"all three tracer styles render");
    // Move the camera between muzzle and impact: near-plane clipping preserves the visible tail.
    matrix[3][3]=-30;
    require(render(0)>0,"tracer crossing camera near plane remains visible");
    settings::enabled=false;update();require(system.Shots().empty(),"disable clears histories and active trails");
    settings::enabled=true;players[0].ammo=30;update();players[0].ammo--;update();
    require(system.Shots().size()==1,"re-enable seeds a fresh baseline");
    system.Update(players,players[0],time,"new-map",true);
    require(system.Shots().empty(),"map transition clears active trails");
    // The cosmetic muzzle must not leap through a wall immediately in front of the shooter.
    load("bullet-close",{wall(10)});
    active_map="bullet-close";
    system.Clear();time=10;players[0].ammo=30;update();players[0].ammo--;update();
    require(system.Shots().size()==1&&system.Shots()[0].hit.kind==HitKind::World&&
            std::abs(system.Shots()[0].hit.end.x-10)<.001f&&system.Shots()[0].origin.x<=10,"muzzle offset cannot bypass nearby collision");
    settings::duration=10;
    for(int i=0;i<300;++i) {players[0].ammo=30;update();players[0].ammo--;update();}
    require(system.Shots().size()==256,"shot buffer is bounded under automatic fire");
    settings::duration=.1f;
    for(int i=0;i<20;++i) update();
    require(system.Shots().empty(),"old trails expire without another shot");
    settings::duration=1.25f;
    MapRaytrace::Unload();players[0].ammo--;update();
    require(system.Shots().empty(),"missing map data suppresses collision-unknown trails");
    hit=Trace({0,0,44},{1,0,0},targets,1);
    require(!hit.map_ready,"missing map is not presented as a clear world ray");
    settings::style=1;settings::glow=.27f;settings::impact=false;
    require(Config::SaveProfile("tracer-style-test"),"save tracer style settings");
    settings::style=2;settings::glow=1;settings::impact=true;
    require(Config::LoadProfile("tracer-style-test"),"load tracer style settings");
    require(settings::style==1&&std::abs(settings::glow-.27f)<.001f&&!settings::impact,"tracer styles persist");
    settings::glow=.65f;settings::impact=true;
    settings::enabled=false;settings::style=0;
    std::cout<<"PASS: nearest world/player collisions, pose capsules, shooter/reload identity, muzzle guard, tracer styles and lifecycle\n";
}
