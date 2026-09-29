#pragma once
#include <cstddef>
#include <cstdint>

// Windows snapshot: a2x/cs2-dumper e5ab60eebd30d9b45692c8ad3daf72c358df46b7
// Generated 2026-09-29T08:54:33.699279+00:00; rerun tools/update-offsets.mjs after game updates.
namespace offsets {
    inline constexpr int supportedBuild=14186;
    inline constexpr std::uintptr_t entityList=0x27151E8;
    inline constexpr std::uintptr_t viewMatrix=0x2565A20;
    inline constexpr std::uintptr_t localPlayerController=0x2537628;
    inline constexpr std::uintptr_t globalVars=0x222BF88;
    inline constexpr std::uintptr_t plantedC4=0x24C9290;
    inline constexpr std::uintptr_t weaponC4=0x24C4550;
    inline constexpr std::uintptr_t buildNumber=0x61D1E8;
    namespace controller {
        inline constexpr std::ptrdiff_t m_iPing=0x838; // CCSPlayerController
        inline constexpr std::ptrdiff_t m_hPawn=0x6BC; // CBasePlayerController
        inline constexpr std::ptrdiff_t m_steamID=0x788; // CBasePlayerController
        inline constexpr std::ptrdiff_t m_iszPlayerName=0x6FC; // CBasePlayerController
        inline constexpr std::ptrdiff_t m_bIsLocalPlayerController=0x790; // CBasePlayerController
        inline constexpr std::ptrdiff_t m_pInGameMoneyServices=0x818; // CCSPlayerController
        inline constexpr std::ptrdiff_t m_iAccount=0x40; // CCSPlayerController_InGameMoneyServices
    }
    namespace pawn {
        inline constexpr std::ptrdiff_t m_vOldOrigin=0x14A4; // C_BasePlayerPawn
        inline constexpr std::ptrdiff_t m_iHealth=0x34C; // C_BaseEntity
        inline constexpr std::ptrdiff_t m_iTeamNum=0x3E7; // C_BaseEntity
        inline constexpr std::ptrdiff_t m_bIsScoped=0x1EA0; // C_CSPlayerPawn
        inline constexpr std::ptrdiff_t m_ArmorValue=0x1ECC; // C_CSPlayerPawn
        inline constexpr std::ptrdiff_t m_bIsDefusing=0x1EA2; // C_CSPlayerPawn
        inline constexpr std::ptrdiff_t m_vecAbsVelocity=0x3F8; // C_BaseEntity
        inline constexpr std::ptrdiff_t m_pGameSceneNode=0x330; // C_BaseEntity
        inline constexpr std::ptrdiff_t m_angEyeAngles=0x35F0; // C_CSPlayerPawn
        inline constexpr std::ptrdiff_t m_entitySpottedState=0x1E88; // C_CSPlayerPawn
        inline constexpr std::ptrdiff_t m_bSpotted=0x8; // EntitySpottedState_t
        inline constexpr std::ptrdiff_t m_bSpottedByMask=0xC; // EntitySpottedState_t
        inline constexpr std::ptrdiff_t m_flFlashOverlayAlpha=0x1504; // C_CSPlayerPawnBase
        inline constexpr std::ptrdiff_t m_pWeaponServices=0x12F0; // C_BasePlayerPawn
        inline constexpr std::ptrdiff_t m_hActiveWeapon=0x60; // CPlayer_WeaponServices
        inline constexpr std::ptrdiff_t m_hMyWeapons=0x48; // CPlayer_WeaponServices
        inline constexpr std::ptrdiff_t m_AttributeManager=0x1290; // C_EconEntity
        inline constexpr std::ptrdiff_t m_Item=0x50; // C_AttributeContainer
        inline constexpr std::ptrdiff_t m_iItemDefinitionIndex=0x1BA; // C_EconItemView
        inline constexpr std::ptrdiff_t m_iClip1=0x1928; // C_BasePlayerWeapon
        inline constexpr std::ptrdiff_t m_bInReload=0x1A3C; // C_CSWeaponBase
        inline constexpr std::ptrdiff_t m_pObserverServices=0x1308; // C_BasePlayerPawn
        inline constexpr std::ptrdiff_t m_hOwnerEntity=0x520; // C_BaseEntity
        inline constexpr auto m_WeaponCount=m_hMyWeapons;
    }
    namespace bomb {
        inline constexpr std::ptrdiff_t m_bC4Activated=0x12D0; // C_PlantedC4
        inline constexpr std::ptrdiff_t m_nBombSite=0x128C; // C_PlantedC4
        inline constexpr std::ptrdiff_t m_vecAbsOrigin=0xC8; // CGameSceneNode
    }
    namespace bone {
        inline constexpr std::ptrdiff_t m_modelState=0x140; // CSkeletonInstance
    }
    namespace observerServices {
        inline constexpr std::ptrdiff_t m_iObserverMode=0x48; // CPlayer_ObserverServices
        inline constexpr std::ptrdiff_t m_hObserverTarget=0x4C; // CPlayer_ObserverServices
    }
    namespace entity {
        inline constexpr std::ptrdiff_t buckets=0x10;
        inline constexpr std::ptrdiff_t stride=0x70;
    }
    namespace global {
        // CGlobalVarsBase is not part of the schema dump. Map text is optional and validated.
        inline constexpr std::ptrdiff_t currentMapName=0x188;
        inline constexpr std::ptrdiff_t currentTime=0x30;
        inline constexpr std::uintptr_t networkClient=0x91B1C0;
        inline constexpr std::ptrdiff_t maxClients=0x240;
    }
}
