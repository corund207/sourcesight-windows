#include "Globals.hpp"
#include "core/engine/Engine.hpp"
#include "core/offsets/Dumper.hpp"
#include <cstring>
#include <cctype>
bool Globals::Update() {
    auto process=Engine::GetProcess();
    if(!process)return false;
    const auto network=process->read<uintptr_t>(Engine::GetEngine().base+offsets::global::networkClient);
    if(!network || !process->read_raw(network+offsets::global::maxClients,&max_clients,sizeof(max_clients)))return false;
    in_match=max_clients>1;
    address=process->read<uintptr_t>(Engine::GetClient().base+offsets::globalVars);
    current_time=address?process->read<float>(address+offsets::global::currentTime):0.f;
    std::memset(map_name,0,sizeof(map_name));
    // Unknown globals must not suppress otherwise valid player data.
    if(address) {
        const auto name=process->read<uintptr_t>(address+offsets::global::currentMapName);
        char candidate[sizeof(map_name)]{};
        if(name && process->read_raw(name,candidate,sizeof(candidate))) {
            const size_t length=strnlen(candidate,sizeof(candidate));
            bool valid=length>0 && length<sizeof(candidate);
            for(size_t i=0;i<length && valid;++i)
                valid=std::isalnum(static_cast<unsigned char>(candidate[i])) || candidate[i]=='_';
            if(valid)std::memcpy(map_name,candidate,length);
        }
    }
    return true;
}
