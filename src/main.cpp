#include "common.hpp"
#include "core/engine/Engine.hpp"
#include "gui/renderer/Renderer.hpp"
#include "gui/frontend/menu/Menu.hpp"
#include "gui/renderer/window/Window.hpp"
#include "external/exception.hpp"
#include <charconv>
#include <cstring>
int main(int argc,char** argv) {
    bool preview=false;int frames=0;
    for(int i=1;i<argc;++i) {
        if(std::strcmp(argv[i],"--preview")==0)preview=true;
        else if(std::strcmp(argv[i],"--screenshot")==0 && i+1<argc)
            Window::preview_screenshot=argv[++i];
        else if(std::strcmp(argv[i],"--frames")==0 && i+1<argc) {
            const char* value=argv[++i];const auto end=value+std::strlen(value);
            const auto result=std::from_chars(value,end,frames);
            if(result.ec!=std::errc{} || result.ptr!=end || frames<=0)return 2;
        } else { std::cout<<"Usage: sourcesight.exe [--preview] [--frames positive-count] [--screenshot preview.bmp]\n";return 2; }
    }
    if(frames && !preview)return 2;
    if(!Window::preview_screenshot.empty() && (!preview || frames<12))return 2;
    LogHelper::Init();c_exception_handler::setup();
    LOGF(INFO,"SourceSight Windows {}",SOURCESIGHT_VERSION);
    Menu::SetPreviewMode(preview);
    if(!preview && !Engine::Init()) { Engine::Stop();LogHelper::Destroy();return 1; }
    if(!Renderer::Init()) { Engine::Stop();LogHelper::Destroy();return 1; }
    Renderer::Thread(frames);
    Engine::Stop();Renderer::Destroy();LogHelper::Destroy();return 0;
}
