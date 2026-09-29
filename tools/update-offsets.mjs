// Generates the Windows-only read layout from a pinned a2x/cs2-dumper snapshot.
// Run: node tools/update-offsets.mjs [40-character commit SHA]
import fs from 'node:fs/promises';
const revision=process.argv[2]??'e5ab60eebd30d9b45692c8ad3daf72c358df46b7';
if(!/^[a-f0-9]{40}$/.test(revision))throw new Error('Supply a full commit SHA.');
const base=`https://raw.githubusercontent.com/a2x/cs2-dumper/${revision}/output/`;
const [schema,modules,info]=await Promise.all(['client_dll.json','offsets.json','info.json'].map(async file=>{
    const response=await fetch(base+file);if(!response.ok)throw new Error(`${file}: ${response.status}`);
    return response.json();
}));
const classes=schema['client.dll'].classes;
const field=(cls,name)=>{
    const value=classes[cls]?.fields[name];
    if(!Number.isSafeInteger(value)||value<0)throw new Error(`Missing ${cls}::${name}`);
    return value;
};
const groups={
    controller:{
        m_iPing:'CCSPlayerController',m_hPawn:'CBasePlayerController',m_steamID:'CBasePlayerController',
        m_iszPlayerName:'CBasePlayerController',m_bIsLocalPlayerController:'CBasePlayerController',
        m_pInGameMoneyServices:'CCSPlayerController',m_iAccount:'CCSPlayerController_InGameMoneyServices'},
    pawn:{
        m_vOldOrigin:'C_BasePlayerPawn',m_iHealth:'C_BaseEntity',m_iTeamNum:'C_BaseEntity',
        m_bIsScoped:'C_CSPlayerPawn',m_ArmorValue:'C_CSPlayerPawn',m_bIsDefusing:'C_CSPlayerPawn',
        m_vecAbsVelocity:'C_BaseEntity',m_pGameSceneNode:'C_BaseEntity',m_angEyeAngles:'C_CSPlayerPawn',
        m_entitySpottedState:'C_CSPlayerPawn',m_bSpotted:'EntitySpottedState_t',m_bSpottedByMask:'EntitySpottedState_t',
        m_flFlashOverlayAlpha:'C_CSPlayerPawnBase',m_pWeaponServices:'C_BasePlayerPawn',
        m_hActiveWeapon:'CPlayer_WeaponServices',m_hMyWeapons:'CPlayer_WeaponServices',
        m_AttributeManager:'C_EconEntity',m_Item:'C_AttributeContainer',m_iItemDefinitionIndex:'C_EconItemView',
        m_iClip1:'C_BasePlayerWeapon',m_bInReload:'C_CSWeaponBase',m_pObserverServices:'C_BasePlayerPawn',
        m_hOwnerEntity:'C_BaseEntity'},
    bomb:{m_bC4Activated:'C_PlantedC4',m_nBombSite:'C_PlantedC4',m_vecAbsOrigin:'CGameSceneNode'},
    bone:{m_modelState:'CSkeletonInstance'},
    observerServices:{m_iObserverMode:'CPlayer_ObserverServices',m_hObserverTarget:'CPlayer_ObserverServices'}
};
const hex=value=>'0x'+value.toString(16).toUpperCase();
let text=`#pragma once\n#include <cstddef>\n#include <cstdint>\n\n// Windows snapshot: a2x/cs2-dumper ${revision}\n// Generated ${info.timestamp}; rerun tools/update-offsets.mjs after game updates.\nnamespace offsets {\n    inline constexpr int supportedBuild=${info.build_number};\n`;
for(const [name,key] of Object.entries({entityList:'dwEntityList',viewMatrix:'dwViewMatrix',localPlayerController:'dwLocalPlayerController',globalVars:'dwGlobalVars',plantedC4:'dwPlantedC4',weaponC4:'dwWeaponC4'}))
    text+=`    inline constexpr std::uintptr_t ${name}=${hex(modules['client.dll'][key])};\n`;
text+=`    inline constexpr std::uintptr_t buildNumber=${hex(modules['engine2.dll'].dwBuildNumber)};\n`;
for(const [group,fields] of Object.entries(groups)){
    text+=`    namespace ${group} {\n`;
    for(const [name,cls] of Object.entries(fields))text+=`        inline constexpr std::ptrdiff_t ${name}=${hex(field(cls,name))}; // ${cls}\n`;
    if(group==='pawn')text+=`        inline constexpr auto m_WeaponCount=m_hMyWeapons;\n`;
    text+='    }\n';
}
text+=`    namespace entity {\n        inline constexpr std::ptrdiff_t buckets=0x10;\n        inline constexpr std::ptrdiff_t stride=0x70;\n    }\n    namespace global {\n        // CGlobalVarsBase is not part of the schema dump. Map text is optional and validated.\n        inline constexpr std::ptrdiff_t currentMapName=0x188;\n        inline constexpr std::ptrdiff_t currentTime=0x30;\n        inline constexpr std::uintptr_t networkClient=${hex(modules['engine2.dll'].dwNetworkGameClient)};\n        inline constexpr std::ptrdiff_t maxClients=${hex(modules['engine2.dll'].dwNetworkGameClient_maxClients)};\n    }\n}\n`;
await fs.writeFile(new URL('../src/core/offsets/Offsets.hpp',import.meta.url),text);
console.log(`Generated Windows offsets for build ${info.build_number} from ${revision}.`);
