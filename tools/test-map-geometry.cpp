#include "common.hpp"
#include "core/engine/classes/MapRaytrace.hpp"
#include "config/Current.hpp"
#include "config/Config.hpp"
#include "core/engine/classes/Player.hpp"
#include "gui/frontend/esp/PlayerWireframe.hpp"
#include "gui/frontend/esp/ViewmodelWireframe.hpp"
#include "gui/renderer/WireframeLines.hpp"
#include <fstream>
#include <thread>
#include <limits>
#include <iostream>

static void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

#include "test-bullet-trails.hpp"

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    LogHelper::Init();
    const auto dir = std::filesystem::current_path();
    using namespace MapRaytrace;
    Init(dir.string());
    auto write = [&](const char* name, std::vector<Triangle> tris) {
        std::ofstream file(dir / (std::string(name)+".tri"), std::ios::binary);
        file.write(reinterpret_cast<const char*>(tris.data()), tris.size()*sizeof(Triangle));
    };
    const Triangle front{{-30,-5,10},{30,-5,10},{0,30,10}};
    write("front",{front});
    require(LoadMap("front"),"load fixture");
    const auto bounds=WorldBounds();
    require(bounds.min.x<=-30&&bounds.max.x>=30&&bounds.min.z<=10&&bounds.max.z>=10,"world bounds are published with mesh");
    require(!IsVisible({0,0,0},{0,0,20}),"wall must block ray");
    require(IsVisible({0,0,0},{0,0,5}),"wall beyond endpoint must not block");
    require(IsVisible({100,100,0},{100,100,20}),"miss must remain visible");
    require(!LoadMap("../front"),"reject path traversal");
    std::array<Vec3,4> targets{{{0,0,20},{0,0,5},{0,0,0},{NAN,0,20}}};
    std::array<Visibility,5> visibility;
    ClassifyVisibility({0,0,0},targets,visibility);
    require(visibility[0]==Visibility::Blocked && visibility[1]==Visibility::Visible &&
            visibility[2]==Visibility::Unknown && visibility[3]==Visibility::Unknown &&
            visibility[4]==Visibility::Unknown,"batch visibility classifies invalid and unmatched samples as unknown");
    ClassifyVisibility({NAN,0,0},targets,visibility);
    require(visibility[0]==Visibility::Unknown,"invalid camera is unknown");

    ImGui::CreateContext();
    auto& io=ImGui::GetIO();
    io.IniFilename=nullptr;io.DisplaySize=ImVec2(800,600);io.DeltaTime=1.f/60;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    io.Fonts->AddFontDefault();
    unsigned char* pixels;int w,h;
    io.Fonts->GetTexDataAsRGBA32(&pixels,&w,&h);io.Fonts->SetTexID(1);
    require(ViewmodelWireframe::Classify(weapon_ak47)==ViewmodelWireframe::Shape::Rifle &&
            ViewmodelWireframe::Classify(weapon_deagle)==ViewmodelWireframe::Shape::Pistol &&
            ViewmodelWireframe::Classify(weapon_awp)==ViewmodelWireframe::Shape::Sniper &&
            ViewmodelWireframe::Classify(weapon_hegrenade)==ViewmodelWireframe::Shape::Grenade &&
            ViewmodelWireframe::Classify(weapon_knife)==ViewmodelWireframe::Shape::Knife &&
            ViewmodelWireframe::Classify(weapon_c4)==ViewmodelWireframe::Shape::Bomb,
            "active equipment selects the expected viewmodel geometry");
    Player local_viewmodel;
    local_viewmodel.alive=true;
    local_viewmodel.weapon.item_index=weapon_ak47;
    local_viewmodel.weapon.name="AK-47";
    local_viewmodel.weapon.icon="A";
    local_viewmodel.ammo=30;
    cfg::esp::wireframe=true;
    cfg::esp::wireframe_blackout=true;
    cfg::esp::viewmodel_wireframe::enabled=true;
    auto render_viewmodel=[&] {
        ImGui::NewFrame();
        auto* draw=ImGui::GetBackgroundDrawList();
        ViewmodelWireframe::Render(local_viewmodel,io.DisplaySize,draw,io.Fonts->Fonts[0]);
        for(const auto& vertex:draw->VtxBuffer)
            require(std::isfinite(vertex.pos.x)&&std::isfinite(vertex.pos.y),"finite viewmodel vertex");
        const int count=draw->VtxBuffer.Size;
        ImGui::Render();
        return count;
    };
    require(render_viewmodel()>100,"active rifle draws weapon and hand wireframes");
    local_viewmodel.weapon.item_index=weapon_smokegrenade;
    local_viewmodel.weapon.name="Smoke Grenade";
    require(render_viewmodel()>100,"selected grenade draws equipment and hand wireframes");
    cfg::esp::wireframe_blackout=false;
    require(render_viewmodel()==0,"viewmodel stays hidden outside dark map mode");
    cfg::esp::wireframe_blackout=true;
    // Compare batched AA positions/UVs against ImGui's regular line renderer.
    const std::array<WireframeLines::Line,2> line_fixture{{{{20,30},{150,70},IM_COL32_WHITE},
                                                        {{40,100},{80,10},IM_COL32(80,160,90,180)}}};
    for(float thickness:{1.f,1.5f,2.f}) {
        ImGui::NewFrame();
        auto* draw=ImGui::GetBackgroundDrawList();
        for(const auto& line:line_fixture) draw->AddLine(line.a,line.b,line.color,thickness);
        const std::vector<ImDrawVert> reference(draw->VtxBuffer.begin(),draw->VtxBuffer.end());
        const int first=draw->VtxBuffer.Size;
        WireframeLines::Draw(draw,line_fixture,thickness);
        require(draw->VtxBuffer.Size==first*2,"batch preserves AA vertex count, including fractional fallback");
        for(const auto& vertex:reference) {
            bool found=false;
            for(int i=first;i<draw->VtxBuffer.Size;++i) {
                const auto& actual=draw->VtxBuffer[i];
                found |= std::abs(actual.pos.x-vertex.pos.x)<.001f && std::abs(actual.pos.y-vertex.pos.y)<.001f &&
                         std::abs(actual.uv.x-vertex.uv.x)<.0001f && std::abs(actual.uv.y-vertex.uv.y)<.0001f && actual.col==vertex.col;
            }
            require(found,"batch preserves AA line positions, texture coordinates and color");
        }
        ImGui::Render();
    }
    ImGui::NewFrame();
    auto* large_draw=ImGui::GetBackgroundDrawList();
    std::vector<WireframeLines::Line> many_lines(20000,line_fixture[0]);
    WireframeLines::Draw(large_draw,many_lines);
    require(large_draw->VtxBuffer.Size==80000,"large line batch uses vertex offsets");
    bool offset_used=false;
    for(const auto& command:large_draw->CmdBuffer) {
        offset_used |= command.VtxOffset>0;
        for(unsigned i=0;i<command.ElemCount;++i)
            require(large_draw->IdxBuffer[command.IdxOffset+i]+command.VtxOffset<unsigned(large_draw->VtxBuffer.Size),"batch indices stay in range");
    }
    require(offset_used,"large batch splits 16-bit indices safely");
    ImGui::Render();
    view_matrix_t m{};
    m[0][0]=1;m[1][1]=1;m[3][2]=1; // Row 2 deliberately zero.
    cfg::esp::wireframe_max_dist=3000;
    auto render=[&](Vec3_t eye=Vec3_t(0,0,0)) {
        ImGui::NewFrame();
        auto* d=ImGui::GetBackgroundDrawList();
        RenderWireframe(m,io,d,eye);
        for (const auto& v:d->VtxBuffer)
            require(std::isfinite(v.pos.x)&&std::isfinite(v.pos.y),"finite clipped vertex");
        int count=d->VtxBuffer.Size;
        require(count<=32000,"wireframe draw budget");
        ImGui::Render();
        return count;
    };
    require(render()>0,"off-screen endpoints must produce clipped visible edges");
    m[3][2]=-1;
    require(render()==0,"map clipping tracks camera turns immediately");
    m[3][2]=1;
    cfg::esp::wireframe_opacity=0;
    require(render()==0,"zero opacity must skip rendering");
    cfg::esp::wireframe_opacity=.65f;
    write("behind",{{{-3,-5,-10},{3,-5,-10},{0,3,-10}}});
    require(LoadMap("behind"),"load behind fixture");
    require(render()==0,"behind-camera mesh must be culled");
    write("crossing",{{{-2,-1,-1},{2,-1,10},{0,2,10}}});
    require(LoadMap("crossing"),"load near-plane fixture");
    require(render()>0,"near-plane crossing must remain visible");

    // Player cages use the same collision fixture and the perspective camera.
    namespace pw = cfg::esp::player_wireframe;
    Vec3_t camera;
    require(PlayerWireframe::CameraPosition(m,camera) && camera.length()<.001f,"camera at origin");
    auto translated=m;
    translated[0][3]=-20;translated[1][3]=30;translated[3][3]=-40;
    require(PlayerWireframe::CameraPosition(translated,camera) &&
            (camera-Vec3_t(20,-30,40)).length()<.001f,"translated spectator camera");
    auto rotated=m;
    rotated[0][0]=0;rotated[0][2]=2;rotated[0][3]=-80;
    rotated[1][1]=3;rotated[1][3]=90;
    rotated[3][0]=1;rotated[3][2]=0;rotated[3][3]=-20;
    require(PlayerWireframe::CameraPosition(rotated,camera) &&
            (camera-Vec3_t(20,-30,40)).length()<.001f,"rotated camera and changed FOV");
    view_matrix_t singular{};
    require(!PlayerWireframe::CameraPosition(singular,camera),"reject singular camera");
    Player player;
    player.alive=true;player.pos={0,0,100};player.bone_list.resize(30);
    auto bone=[&](int i,float x,float y) {player.bone_list[i].pos={x,y,100};};
    bone(pelvis,0,-10);bone(neck,0,20);bone(head,0,30);
    bone(shoulder_L,-10,20);bone(elbow_L,-16,5);bone(hand_L,-18,-8);
    bone(shoulder_R,10,20);bone(elbow_R,16,5);bone(hand_R,18,-8);
    bone(hip_L,-5,-12);bone(knee_L,-6,-30);bone(foot_heel_L,-6,-48);
    bone(hip_R,5,-12);bone(knee_R,6,-30);bone(foot_heel_R,6,-48);
    pw::enabled=true;
    auto player_render=[&](ImU32 expected_rgb=0) {
        ImGui::NewFrame();
        auto* d=ImGui::GetBackgroundDrawList();
        PlayerWireframe::Render(player,m,io.DisplaySize,d);
        require(d->VtxBuffer.Size<=19*16*19*8,"bounded player draw list");
        for (const auto& vertex:d->VtxBuffer) {
            require(std::isfinite(vertex.pos.x)&&std::isfinite(vertex.pos.y),"finite player vertices");
            if(expected_rgb) require((vertex.col&~IM_COL32_A_MASK)==expected_rgb,"correct visibility color");
        }
        const int count=d->VtxBuffer.Size;
        ImGui::Render();
        return count;
    };
    auto rgb=[](color_t color){return ImGui::ColorConvertFloat4ToU32(color)&~IM_COL32_A_MASK;};
    require(LoadMap("front"),"restore blocking wall");
    require(player_render(rgb(pw::blocked))>0,"occluded body cage is blocked color");
    pw::visible_only=true;
    require(player_render()==0,"visible-only hides blocked body");
    Unload();
    require(player_render()==0,"visible-only hides unknown body");
    pw::visible_only=false;
    require(player_render(rgb(pw::unknown))>0,"missing map shows unknown body");
    ClassifyVisibility({0,0,0},targets,visibility);
    require(visibility[0]==Visibility::Unknown && visibility[1]==Visibility::Unknown,"missing map returns unknown batch");
    require(LoadMap("behind"),"wall behind camera");
    pw::visible_only=true;
    pw::detail=0;
    const int standard=player_render(rgb(pw::visible));
    require(standard>0,"clear body cage is visible color");
    write("half-wall",{{{0,-100,50},{100,-100,50},{0,100,50}}});
    require(LoadMap("half-wall"),"load partial cover");
    pw::visible_only=false;
    ImGui::NewFrame();
    auto* partial_draw=ImGui::GetBackgroundDrawList();
    PlayerWireframe::Render(player,m,io.DisplaySize,partial_draw);
    bool has_visible=false,has_blocked=false;
    for(const auto& vertex:partial_draw->VtxBuffer) {
        has_visible |= (vertex.col&~IM_COL32_A_MASK)==rgb(pw::visible);
        has_blocked |= (vertex.col&~IM_COL32_A_MASK)==rgb(pw::blocked);
    }
    require(has_visible&&has_blocked,"partial cover produces both visibility colors on one player");
    ImGui::Render();
    require(LoadMap("behind"),"restore clear view");
    pw::visible_only=true;
    pw::detail=1;
    const int detailed=player_render();
    require(detailed>standard*2,"detailed body adds subdivisions and triangulation");
    pw::detail=2;
    require(player_render()>detailed,"ultra adds more body rings and sides");
    pw::detail=1;
    pw::opacity=0;
    require(player_render()==0,"zero player opacity skips drawing");
    pw::opacity=.8f;
    const auto original=player;
    for(auto& b:player.bone_list)b.pos.z=800;
    player.pos.z=800;
    pw::detail=0;
    const int distant_standard=player_render();
    pw::detail=2;
    require(distant_standard>0 && player_render()==distant_standard,"distant ultra uses adaptive LOD");
    pw::detail=1;
    player=original;
    for(auto& b:player.bone_list)b.pos.z=-100;
    player.pos.z=-100;
    require(player_render()==0,"behind-camera players are culled");
    player=original;
    for(auto& b:player.bone_list)b.pos.x+=100;
    player.pos.x+=100;
    require(player_render()>0,"partially offscreen player edges survive clipping");
    player=original;
    for(auto& b:player.bone_list)b.pos.z=1;
    player.pos.z=1;
    require(player_render()>0,"body cage crossing near plane remains drawable");
    player=original;
    for(auto& b:player.bone_list)b.pos.z+=4000;
    player.pos.z+=4000;
    require(player_render()==0,"distant body cages are culled before raycasting");
    player=original;
    for(auto& b:player.bone_list)b.pos.x=NAN;
    require(player_render()==0,"corrupt bones skipped");
    player.bone_list.clear();
    require(player_render()==0,"missing bones skipped");
    player=original;
    pw::enabled=false;
    require(player_render()==0,"disabled mode does not draw");
    pw::enabled=true;
    pw::opacity=.37f;pw::thickness=2.f;pw::max_distance=1700;
    pw::visible={.2f,.4f,.6f,1};
    require(Config::SaveProfile("wireframe-test"),"save player settings");
    pw::enabled=false;pw::visible_only=false;pw::detail=0;pw::opacity=.8f;pw::thickness=1;pw::max_distance=3000;
    require(Config::LoadProfile("wireframe-test"),"load player settings");
    require(pw::enabled&&pw::visible_only&&pw::detail==1&&std::abs(pw::opacity-.37f)<.001f&&
            pw::thickness==2.f&&pw::max_distance==1700&&std::abs(pw::visible.r-.2f)<.001f,"player settings round trip");
    {
        const auto file=dir/"configs"/"wireframe-test.json";
        nlohmann::json legacy;
        {std::ifstream input(file);input>>legacy;}
        legacy["esp"].erase("player_wireframe");
        {std::ofstream output(file);output<<legacy;}
        require(Config::LoadProfile("wireframe-test"),"load legacy profile");
        require(!pw::enabled&&!pw::visible_only&&pw::detail==1&&pw::opacity==.8f&&
                pw::max_distance==3000,"legacy profiles restore safe defaults");
    }
    pw::enabled=false;
    TestBulletTrails(dir);
    auto invalid=front;invalid.p1.x=std::numeric_limits<float>::quiet_NaN();
    write("mixed",{invalid,front,{{0,0,0},{0,0,0},{0,0,0}}});
    require(LoadMap("mixed")&&TriangleCount()==1,"filter corrupt and degenerate geometry");
    {std::ofstream f(dir/"broken.tri");f<<"broken";}
    require(!LoadMap("broken")&&!IsReady()&&CurrentMap().empty(),"bad mesh clears old state");
    SetDesiredMap("front");
    require(!LoadMap("behind"),"obsolete map cannot publish");
    require(LoadMap("front"),"desired map can publish");
    std::thread worker([&] {for(int i=0;i<30;++i){Unload();LoadMap("front");}});
    for(int i=0;i<100;++i) {
        IsVisible({0,0,0},{0,0,20});render();
        const std::array<Vec3,2> equal_targets{{{0,0,20},{0,0,20}}};
        std::array<Visibility,2> states;
        ClassifyVisibility({0,0,0},equal_targets,states);
        require(states[0]==states[1],"batch holds one map snapshot during unload/reload");
    }
    worker.join();
    if(std::string(argv[1])=="--synthetic") {
        SetDesiredMap("benchmark");write("benchmark",{front});
        require(LoadMap("benchmark"),"synthetic benchmark map");
    } else {
        SetDesiredMap("de_dust2");Init(argv[1]);
        require(LoadMap("de_dust2"),"local map load");
    }
    cfg::esp::wireframe_budget=500;
    require(render()<=2000,"low detail must enforce its edge budget");
    cfg::esp::wireframe_budget=6000;
    auto start=std::chrono::steady_clock::now();
    int vertices=0;
    for(int i=0;i<100;++i) vertices=render();
    std::cout<<"PASS: player visibility colors, clipping, camera/FOV, settings migration; map ray queries, invalid files, cancellation and concurrent snapshots\n";
    std::cout<<TriangleCount()<<" triangles; "<<vertices<<" wireframe vertices; "
             <<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/100
             <<" ms/frame (synthetic camera)\n";
    pw::enabled=true;pw::visible_only=false;pw::detail=1;
    start=std::chrono::steady_clock::now();
    for(int frame=0;frame<100;++frame) {
        ImGui::NewFrame();
        auto* d=ImGui::GetBackgroundDrawList();
        for(int i=0;i<10;++i) {
            player=original;
            const Vec3_t offset{float(i-5)*10,0,float(i)*25};
            player.pos+=offset;
            for(auto& bone:player.bone_list)bone.pos+=offset;
            PlayerWireframe::Render(player,m,io.DisplaySize,d);
        }
        ImGui::Render();
    }
    std::cout<<"10 detailed player cages against current fixture: "
             <<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/100
             <<" ms/frame (synthetic poses/camera)\n";
    Unload();ImGui::DestroyContext();LogHelper::Destroy();
}
