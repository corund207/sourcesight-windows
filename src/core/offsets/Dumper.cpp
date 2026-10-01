#include "Dumper.hpp"
#include "PatternScanner.hpp"
#include "core/engine/Engine.hpp"
bool Dumper::Init() {
    auto process=Engine::GetProcess();
    const auto client=Engine::GetClient(),engine=Engine::GetEngine();
    if(!process || !client.base || !engine.base) {
        LOGF(WARNING,"Game modules not ready (client=0x{:X} engine=0x{:X}); continuing best-effort with Windows snapshot {}.",
            client.base,engine.base,offsets::supportedBuild);
        return true;
    }
    LOGF(INFO,"Game modules: client=0x{:X} size=0x{:X} engine=0x{:X} size=0x{:X}",
        client.base,client.size,engine.base,engine.size);
    // Auto-adjust: resolve fresh module offsets via signatures so game updates
    // work without a manual rebuild. Falls back to compiled snapshot on failure.
    PatternScanner::ApplyAutoUpdate();
    if(engine.size<=offsets::buildNumber+sizeof(int)) {
        LOGF(WARNING,"engine2.dll size 0x{:X} is smaller than buildNumber offset 0x{:X}; continuing best-effort with snapshot {}.",
            engine.size,offsets::buildNumber,offsets::supportedBuild);
        return true;
    }
    int build=0;
    if(!process->read_raw(engine.base+offsets::buildNumber,&build,sizeof(build))) {
        LOGF(WARNING,"Could not read CS2 build at engine2.dll+0x{:X}; continuing best-effort with snapshot {}.",
            offsets::buildNumber,offsets::supportedBuild);
        return true;
    }
    if(build!=offsets::supportedBuild) {
        LOGF(WARNING,"CS2 build {} does not match the Windows offset snapshot {}. Offsets may be stale; continuing best-effort. Regenerate with tools/update-offsets.mjs and rebuild.",
            build,offsets::supportedBuild);
        return true;
    }
    LOGF(INFO,"Loaded Windows read layout for CS2 build {}",build);
    return true;
}
bool Dumper::RescanEntityList() {
    // The layout is pinned to a verified build; never substitute Linux signatures.
    // Do not re-check the build number here: Game::UpdateEntityList already
    // re-reads the entity list every tick, and repeating the build warning
    // would spam the log when the game updates.
    return true;
}
