#include "Dumper.hpp"
#include "core/engine/Engine.hpp"
bool Dumper::Init() {
    auto process=Engine::GetProcess();
    const auto client=Engine::GetClient(),engine=Engine::GetEngine();
    if(!process || !client.base || !engine.base || engine.size<=offsets::buildNumber+sizeof(int))return false;
    int build=0;
    if(!process->read_raw(engine.base+offsets::buildNumber,&build,sizeof(build)))return false;
    if(build!=offsets::supportedBuild) {
        LOGF(FATAL,"CS2 build {} does not match the Windows offset snapshot {}. Regenerate offsets with tools/update-offsets.mjs and rebuild.",build,offsets::supportedBuild);
        return false;
    }
    LOGF(INFO,"Loaded Windows read layout for CS2 build {}",build);
    return true;
}
bool Dumper::RescanEntityList() {
    // The layout is pinned to a verified build; never substitute Linux signatures.
    return Dumper::Init();
}
