#include "common.hpp"
#include "core/memory/Memory.hpp"
#include "core/offsets/Offsets.hpp"
#include "core/engine/classes/MapExtractor.hpp"
#include "core/engine/classes/MapRaytrace.hpp"
#include "gui/renderer/FullMapRenderer.hpp"
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <cctype>
int main(int argc,char** argv) {
    LogHelper::Init();
    Logger::AddSink([](LogMessagePtr msg){std::cout<<msg->Message()<<std::endl;});
    pProcess process;
    if(process.AttachProcess("cs2.exe")) {
        const auto client=process.GetModule("client.dll"),engine=process.GetModule("engine2.dll");
        std::cout<<"build="<<process.read<int>(engine.base+offsets::buildNumber)<<std::endl;
        const auto globals=process.read<uintptr_t>(client.base+offsets::globalVars);
        // Read only the documented map pointer and neighboring global fields
        // for diagnosing layout drift. Never scan unrelated process memory.
        for(size_t offset=0x170;offset<=0x190;offset+=8) {
            const auto pointer=process.read<uintptr_t>(globals+offset);
            char name[64]{};
            if(process.read_raw(pointer,name,sizeof(name))) {
                const auto n=strnlen(name,sizeof(name));bool printable=n>0 && n<sizeof(name);
                for(size_t i=0;i<n;++i)printable &= std::isprint(static_cast<unsigned char>(name[i]))!=0;
                if(printable)std::cout<<"globals+"<<std::hex<<offset<<std::dec<<" text="<<name<<std::endl;
            }
        }
    } else std::cout<<"No readable CS2 process"<<std::endl;
    bool success=true;
    if(argc>=2) {
        const std::string map=argv[1];
        if(map.empty() || map.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos)return 2;
        const std::string output=argc>=3?argv[2]:"map-diagnostics";
        const auto result=MapExtractor::ExtractMap(map,output);
        success=result.success;
        std::cout<<"extraction success="<<result.success<<" error="<<result.error<<std::endl;
        if(result.success) {
            MapRaytrace::Init(output);
            std::cout<<"load="<<MapRaytrace::LoadMap(map)<<" triangles="<<MapRaytrace::TriangleCount()<<std::endl;
            if(argc>=4 && std::string_view(argv[3])=="--render") {
                if(!glfwInit())return 1;
                glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);glfwWindowHint(GLFW_DEPTH_BITS,24);
                glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
                auto window=glfwCreateWindow(640,640,"Actual map verification",nullptr,nullptr);
                if(!window)return 1;
                glfwMakeContextCurrent(window);glewExperimental=GL_TRUE;
                if(glewInit()!=GLEW_OK)return 1;
                while(glGetError()!=GL_NO_ERROR){}
                const auto bounds=MapRaytrace::WorldBounds();
                view_matrix_t view{};view[0][0]=view[1][1]=1;
                view[0][3]=-(bounds.min.x+bounds.max.x)*.5f;
                view[1][3]=-(bounds.min.y+bounds.max.y)*.5f;
                view[3][2]=-1;
                view[3][3]=bounds.max.z+std::max(bounds.max.x-bounds.min.x,bounds.max.y-bounds.min.y)*.7f;
                cfg::esp::wireframe_opacity=.7f;cfg::esp::wireframe_panel_opacity=.1f;
                glViewport(0,0,640,640);glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT);
                success=FullMapRenderer::Render(view);
                std::vector<unsigned char> pixels(640*640*4);
                glReadPixels(0,0,640,640,GL_BGRA,GL_UNSIGNED_BYTE,pixels.data());
                size_t drawn=0;for(size_t i=3;i<pixels.size();i+=4)drawn+=pixels[i]!=0;
                success &= glGetError()==GL_NO_ERROR && drawn>1000;
                BITMAPFILEHEADER file{};file.bfType=0x4d42;file.bfOffBits=sizeof(file)+sizeof(BITMAPINFOHEADER);
                file.bfSize=file.bfOffBits+DWORD(pixels.size());
                BITMAPINFOHEADER info{};info.biSize=sizeof(info);info.biWidth=info.biHeight=640;info.biPlanes=1;info.biBitCount=32;
                std::ofstream image(std::filesystem::path(output)/(map+"-verified.bmp"),std::ios::binary);
                image.write(reinterpret_cast<const char*>(&file),sizeof(file));image.write(reinterpret_cast<const char*>(&info),sizeof(info));
                image.write(reinterpret_cast<const char*>(pixels.data()),pixels.size());
                std::cout<<"actual map framebuffer: drawn_pixels="<<drawn<<" success="<<success<<std::endl;
                FullMapRenderer::Destroy();glfwDestroyWindow(window);glfwTerminate();
            }
        }
    }
    LogHelper::Destroy();
    return success?0:1;
}
