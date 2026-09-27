#include <iostream>
#include "common.hpp"
#include "core/memory/Memory.hpp"
#include "core/platform/FileIO.hpp"
#include "config/Config.hpp"
#include <stdexcept>

static void require(bool value,const char* message) { if(!value)throw std::runtime_error(message); }
int RunChecks() {
    LogHelper::Init();
    std::filesystem::remove("unicode-\u03A9.txt");
    std::filesystem::remove("replacement.txt");
    pProcess process;
    require(!process.AttachProcess("sourcesight-missing-process.exe"),"missing process is rejected");
    require(process.AttachProcess("test-windows.exe"),"attach to own test process");
    const std::uint64_t fixture=0xF123456789ABCDEFull;
    require(process.read<std::uint64_t>(reinterpret_cast<uintptr_t>(&fixture))==fixture,"64-bit read");
    require(process.read<std::uint64_t>(1)==0,"invalid read returns a zero value");
    require(process.GetModule("test-windows.exe").base!=0,"first module is enumerated");
    require(process.GetModule("not-loaded.dll").base==0,"missing module returns empty");
    process.Close();process.Close();
    require(!process.handle_ && !process.pid_,"idempotent close resets attachment");
    require(FileIO::WriteNew("unicode-\u03A9.txt","first"),"create Unicode path");
    require(!FileIO::WriteNew("unicode-\u03A9.txt","second"),"exclusive create refuses overwrite");
    require(FileIO::WriteNew("replacement.txt","replacement"),"create replacement");
    std::error_code error;FileIO::Replace("replacement.txt","unicode-\u03A9.txt",error);
    require(!error,"atomically replace existing file on Windows");
    auto legacy=nlohmann::json{{"aim",{{"enabled",true}}},{"triggerbot",{{"enabled",true}}},
        {"spinbot",{{"enabled",true}}},{"macro",{{"awp_quickswitch",true}}},
        {"bypass",{{"timing_jitter",true}}},{"audio",{{"lock_sound",true}}}};
    std::filesystem::create_directories("configs");
    { std::ofstream out("configs/linux-profile.json");out<<legacy; }
    require(Config::LoadProfile("linux-profile"),"legacy profile imports without automation");
    require(Config::SaveProfile("linux-profile"),"save imported visual profile");
    require(!Config::SaveProfile("CON"),"reject reserved Windows filename");
    require(!Config::SaveProfile("LPT1.json"),"reject reserved device with extension");
    nlohmann::json saved;{ std::ifstream in("configs/linux-profile.json");in>>saved; }
    for(const char* key:{"aim","triggerbot","spinbot","macro","bypass","audio"})
        require(!saved.contains(key),"retired automation sections are stripped");
    LogHelper::Destroy();return 0;
}

int main() {
    try { return RunChecks(); }
    catch(const std::exception& error) { std::cerr << error.what() << std::endl; LogHelper::Destroy(); return 1; }
}
