#include "PhysicsDecoder.hpp"
#include <windows.h>
#include <stdexcept>
#include <string>
namespace {
struct Handle {HANDLE value=nullptr;~Handle(){if(value && value!=INVALID_HANDLE_VALUE)CloseHandle(value);}};
}
void PhysicsDecoder::Decode(const std::filesystem::path& resource,const std::filesystem::path& text,const char* block) {
    wchar_t executable[32768]{};
    const auto count=GetModuleFileNameW(nullptr,executable,std::size(executable));
    if(!count || count==std::size(executable))throw std::runtime_error("Cannot locate map decoder");
    const auto decoder=std::filesystem::path(executable).parent_path()/"tools/source2viewer/Source2Viewer-CLI.exe";
    if(!std::filesystem::is_regular_file(decoder))
        throw std::runtime_error("Map decoder missing. Run scripts/setup-map-decoder.ps1 or use the complete release package.");
    SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};
    Handle output{CreateFileW(text.c_str(),GENERIC_WRITE,FILE_SHARE_READ,&security,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr)};
    const auto log=text.wstring()+L".log";
    Handle errors{CreateFileW(log.c_str(),GENERIC_WRITE,FILE_SHARE_READ,&security,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr)};
    Handle input{CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&security,OPEN_EXISTING,0,nullptr)};
    if(output.value==INVALID_HANDLE_VALUE || errors.value==INVALID_HANDLE_VALUE || input.value==INVALID_HANDLE_VALUE)
        throw std::runtime_error("Cannot create map decoder output files");
    std::wstring command=L"\""+decoder.wstring()+L"\" -i \""+std::filesystem::absolute(resource).wstring()+L"\" -b ";
    command+=std::string_view(block)=="PHYS"?L"PHYS":L"DATA";
    STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESTDHANDLES;
    startup.hStdInput=input.value;startup.hStdOutput=output.value;startup.hStdError=errors.value;
    PROCESS_INFORMATION process{};
    if(!CreateProcessW(decoder.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process))
        throw std::runtime_error("Unable to start map decoder");
    Handle process_handle{process.hProcess},thread_handle{process.hThread};
    if(WaitForSingleObject(process.hProcess,120000)!=WAIT_OBJECT_0) {
        TerminateProcess(process.hProcess,1);WaitForSingleObject(process.hProcess,5000);
        throw std::runtime_error("Map decoding exceeded two minutes");
    }
    DWORD code=1;GetExitCodeProcess(process.hProcess,&code);
    if(code!=0)throw std::runtime_error("Map decoder failed; see the .vphys.log file");
}
