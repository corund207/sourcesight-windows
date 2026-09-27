#include "common.hpp"
#include "config/Config.hpp"
#include "core/engine/classes/MapRaytrace.hpp"
#include "gui/renderer/FullMapRenderer.hpp"
#include <imgui/backends/imgui_impl_opengl3.h>
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <fstream>
#include <iostream>

static void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

int RunChecks() {
    LogHelper::Init();
    require(glfwInit(),"initialize GLFW");
    glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
    glfwWindowHint(GLFW_ALPHA_BITS,8);glfwWindowHint(GLFW_DEPTH_BITS,24);
    auto window=glfwCreateWindow(128,128,"Map framebuffer tests",nullptr,nullptr);
    require(window!=nullptr,"create RGBA/depth context");glfwMakeContextCurrent(window);
    glewExperimental=GL_TRUE;require(glewInit()==GLEW_OK,"load OpenGL");while(glGetError()!=GL_NO_ERROR) {}
    std::cout << "OpenGL renderer: " << glGetString(GL_RENDERER) << '\n';
    glViewport(0, 0, 128, 128);

    using MapRaytrace::Triangle;
    MapRaytrace::Init(std::filesystem::current_path().string());
    unsigned fixture_id=0;
    auto load = [&](const std::vector<Triangle>& triangles) {
        // Different names force map replacement instead of the same-map cache.
        const auto name="fixture-"+std::to_string(++fixture_id);
        { std::ofstream file(name+".tri", std::ios::binary);
          file.write(reinterpret_cast<const char*>(triangles.data()), triangles.size()*sizeof(Triangle)); }
        require(MapRaytrace::LoadMap(name), "load synthetic map");
    };
    const Triangle front_a{{-9,-9,10},{9,-9,10},{9,9,10}};
    const Triangle front_b{{-9,-9,10},{9,9,10},{-9,9,10}};
    const Triangle rear{{-8,-8,20},{8,-8,20},{0,8,20}};
    std::vector<Triangle> triangles{rear, front_a, front_b, rear, front_a, front_b};
    load(triangles);
    view_matrix_t matrix{};
    matrix[0][0]=1; matrix[1][1]=1; matrix[3][2]=1;
    cfg::esp::wireframe_panel_opacity=.10f;
    cfg::esp::wireframe_opacity=0.f;
    cfg::esp::wireframe_color={1,0,0,1};
    cfg::esp::wireframe_full_xray=false;

    using Pixels = std::array<unsigned char,128*128*4>;
    auto read = [] {
        Pixels pixels{};
        glReadPixels(0,0,128,128,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
        require(glGetError()==GL_NO_ERROR,"no OpenGL errors");
        return pixels;
    };
    auto render = [&] {
        glDisable(GL_SCISSOR_TEST);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
        glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT);
        require(FullMapRenderer::Render(matrix),"render map");
        return read();
    };
    auto check_panel = [](const Pixels& pixels, int alpha) {
        const int i=(60*128+48)*4;
        require(std::abs(int(pixels[i+3])-alpha)<=1,"panel respects requested opacity, not forced opaque");
        require(pixels[i]<=9 && pixels[i+1]<=10 && pixels[i+2]<=11,
                "panel is premultiplied graphite, independent of red wire color");
        require(pixels[3]==0,"outside mesh stays transparent");
    };
    auto panels=render();
    check_panel(panels,26);
    for(size_t i=3;i<panels.size();i+=4)
        require(panels[i]<=26,"rear and coplanar duplicate panels never stack opacity");
    std::reverse(triangles.begin(),triangles.end());load(triangles);
    require(render()==panels,"panel visibility is independent of triangle draw order");
    cfg::esp::wireframe_full_xray=true;
    require(render()==panels,"X-ray cannot accumulate rear panel fills");
    cfg::esp::wireframe_panel_opacity=.35f;
    check_panel(render(),89);
    cfg::esp::wireframe_panel_opacity=0.f;
    require(render()==Pixels{},"zero fill and line opacity produce a transparent frame");
    cfg::esp::wireframe_panel_opacity=.10f;
    cfg::esp::wireframe_opacity=.8f;
    cfg::esp::wireframe_full_xray=false;
    auto visible=render();
    check_panel(visible,26);
    auto red_count=[](const Pixels& pixels) {
        int count=0;
        for(size_t i=0;i<pixels.size();i+=4) count+=pixels[i]>100 && pixels[i+1]<20;
        return count;
    };
    require(red_count(visible)>100,"front wire edges render in the configured color");
    cfg::esp::wireframe_full_xray=true;
    auto xray=render();
    require(red_count(xray)>red_count(visible)+50,"rear edges show only in X-ray mode");
    check_panel(xray,26);
    matrix[3][2]=-1;
    require(render()==Pixels{},"camera turn clips geometry behind the viewer");
    matrix[3][2]=1;
    matrix[0][3]=-100;
    require(render()==Pixels{},"camera translation immediately updates projection");
    matrix[0][3]=0;

    // Non-default caller state must survive the map pass, ready for UI rendering.
    glEnable(GL_SCISSOR_TEST);glScissor(0,0,1,1);
    glEnable(GL_CULL_FACE);glEnable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);glEnable(GL_DEPTH_TEST);glDepthFunc(GL_GREATER);
    glDepthMask(GL_FALSE);glColorMask(GL_FALSE,GL_TRUE,GL_FALSE,GL_TRUE);
    glClearDepth(.25);glDepthRange(.2,.8);
    require(FullMapRenderer::Render(matrix),"render with caller state");
    GLboolean mask[4],depth_mask;GLint func;GLdouble clear_depth,range[2];
    glGetBooleanv(GL_COLOR_WRITEMASK,mask);glGetBooleanv(GL_DEPTH_WRITEMASK,&depth_mask);
    glGetIntegerv(GL_DEPTH_FUNC,&func);glGetDoublev(GL_DEPTH_CLEAR_VALUE,&clear_depth);
    glGetDoublev(GL_DEPTH_RANGE,range);
    require(glIsEnabled(GL_SCISSOR_TEST) && glIsEnabled(GL_CULL_FACE) && glIsEnabled(GL_STENCIL_TEST) &&
            !glIsEnabled(GL_BLEND) && glIsEnabled(GL_DEPTH_TEST) && func==GL_GREATER && !depth_mask &&
            !mask[0] && mask[1] && !mask[2] && mask[3] && clear_depth==.25 &&
            std::abs(range[0]-.2)<.0001 && std::abs(range[1]-.8)<.0001,"restore caller GL state");
    glDisable(GL_STENCIL_TEST);glDisable(GL_CULL_FACE);glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);glDepthRange(0,1);
    render();
    ImGui::CreateContext();
    auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={128,128};io.DeltaTime=1.f/60;
    require(ImGui_ImplOpenGL3_Init("#version 130"),"initialize UI renderer");
    ImGui_ImplOpenGL3_NewFrame();ImGui::NewFrame();
    ImGui::GetForegroundDrawList()->AddRectFilled({40,40},{60,60},IM_COL32(0,0,255,255));
    ImGui::Render();ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    const auto ui=read();const int ui_pixel=(78*128+50)*4;
    require(ui[ui_pixel]==0 && ui[ui_pixel+2]==255 && ui[ui_pixel+3]==255,"UI renders above the map");
    ImGui_ImplOpenGL3_Shutdown();ImGui::DestroyContext();

    require(Config::SaveProfile("legacy"),"save test profile");
    nlohmann::json legacy;
    { std::ifstream file("configs/legacy.json");file>>legacy; }
    legacy["esp"]["wireframe_occlude_game"]=true;
    legacy["esp"].erase("wireframe_panel_opacity");
    { std::ofstream file("configs/legacy.json");file<<legacy; }
    cfg::esp::wireframe_panel_opacity=.35f;
    require(Config::LoadProfile("legacy"),"load legacy opaque profile");
    require(cfg::esp::wireframe_panel_opacity==.10f,"legacy profile gets transparent default fill");
    check_panel(render(),26);
    require(Config::SaveProfile("legacy"),"save migrated profile");
    { std::ifstream file("configs/legacy.json");file>>legacy; }
    require(!legacy["esp"].contains("wireframe_occlude_game"),"retire legacy opaque setting");

    MapRaytrace::Unload();
    require(!FullMapRenderer::Render(matrix),"unloaded map releases renderer without drawing");
    require(glGetError()==GL_NO_ERROR,"clean renderer teardown");
    glfwDestroyWindow(window);glfwTerminate();
    LogHelper::Destroy();
    std::cout << "Full-map framebuffer regression tests passed.\n";
    return 0;
}

int main() {
    try { return RunChecks(); }
    catch(const std::exception& error) { std::cerr << error.what() << std::endl; LogHelper::Destroy(); return 1; }
}
