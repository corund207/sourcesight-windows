#include "MapExtractor.hpp"
#include "core/platform/FileIO.hpp"
#include "MapRaytrace.hpp"

#include "core/engine/Engine.hpp"
#include "core/logger/LogHelper.hpp"

#include <fstream>
#include <vector>
#include <algorithm>
#include <filesystem>
#include <cstdlib>
#include <cstdint>
#include <unordered_map>

// Source 2 collision decoding and checked triangle conversion.
#include "PhysicsText.hpp"
#include "PhysicsDecoder.hpp"
#include "VpkReader.hpp"
#include <mutex>

namespace MapExtractor {

namespace {
    std::string g_cs2_install_path;
    std::string g_maps_dir = "maps";
    bool g_initialized = false;
    std::vector<std::string> g_vpk_files;
    std::mutex g_status_mutex;
    std::string g_status="Waiting for a map.";
    void SetStatus(std::string value) {std::lock_guard lock(g_status_mutex);g_status=std::move(value);}
}
std::string StatusText() {std::lock_guard lock(g_status_mutex);return g_status;}

bool Init() {
    if (g_initialized)
        return true;

    // Find CS2 install path
    auto cs2_path = FindCS2InstallPath();
    if (!cs2_path) {
        LOGF(WARNING, "[map_extractor] Could not find CS2 installation");
        return false;
    }
    g_cs2_install_path = *cs2_path;

    // Find all VPK files in cs2/maps/ and csgo/ (for pak01_dir.vpk and chunks)
    std::vector<std::string> vpk_dirs = {
        g_cs2_install_path + "/game/csgo/maps",
        g_cs2_install_path + "/game/csgo",
        g_cs2_install_path + "/csgo/maps",
        g_cs2_install_path + "/csgo",
    };

    std::error_code ec;
    for (const auto& vpk_dir : vpk_dirs) {
        if (!std::filesystem::exists(vpk_dir)) continue;
        for (const auto& entry : std::filesystem::directory_iterator(vpk_dir, ec)) {
            if (entry.is_regular_file() && entry.path().extension() == ".vpk") {
                g_vpk_files.push_back(entry.path().string());
            }
        }
    }

    if (g_vpk_files.empty()) {
        LOGF(WARNING, "[map_extractor] No VPK files found in scanned directories");
    } else {
        LOGF(INFO, "[map_extractor] Found {} VPK files", g_vpk_files.size());
    }

    // Create maps output directory
    std::filesystem::create_directories(g_maps_dir, ec);

    g_initialized = true;
    return true;
}

std::optional<std::string> FindCS2InstallPath() {
    std::vector<std::string> steam_roots={"C:/Program Files (x86)/Steam","C:/Program Files/Steam"};
    wchar_t registry_path[32768]{};DWORD size=sizeof(registry_path);
    if(RegGetValueW(HKEY_CURRENT_USER,L"Software\\Valve\\Steam",L"SteamPath",RRF_RT_REG_SZ,
                   nullptr,registry_path,&size)==ERROR_SUCCESS)
        steam_roots.insert(steam_roots.begin(),std::filesystem::path(registry_path).string());
    std::vector<std::string> common_paths,steam_configs;
    for(const auto& root:steam_roots) {
        common_paths.push_back(root+"/steamapps/common/Counter-Strike Global Offensive");
        common_paths.push_back(root+"/steamapps/common/Counter-Strike 2");
        steam_configs.push_back(root+"/steamapps/libraryfolders.vdf");
    }
    for (const auto& config : steam_configs) {
        if (std::filesystem::exists(config)) {
            // Parse libraryfolders.vdf for additional paths
            // Simple string search for "path" entries
            std::ifstream in(config);
            std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

            size_t pos = 0;
            while ((pos = content.find("\"path\"", pos)) != std::string::npos) {
                size_t quote1 = content.find('"', pos + 6);
                size_t quote2 = content.find('"', quote1 + 1);
                if (quote1 != std::string::npos && quote2 != std::string::npos) {
                    std::string lib_path = content.substr(quote1 + 1, quote2 - quote1 - 1);
                    // Fix escaped backslashes
                    std::replace(lib_path.begin(), lib_path.end(), '\\', '/');
                    std::string cs2_path = lib_path + "/steamapps/common/Counter-Strike 2";
                    common_paths.push_back(cs2_path);
                    common_paths.push_back(lib_path+"/steamapps/common/Counter-Strike Global Offensive");
                }
                if(quote2==std::string::npos)break;
                pos = quote2+1;
            }
        }
    }

    for (const auto& path : common_paths) {
        if (std::filesystem::exists(path)) {
            LOGF(INFO, "[map_extractor] Found CS2 at: {}", path);
            return path;
        }
    }

    return std::nullopt;
}

bool HasMapData(const std::string& map_name, const std::string& maps_dir) {
    std::string tri_path = maps_dir + "/" + map_name + ".tri";
    return std::filesystem::exists(tri_path);
}

std::optional<std::string> GetMapTriPath(const std::string& map_name, const std::string& maps_dir) {
    std::string tri_path = maps_dir + "/" + map_name + ".tri";
    if (std::filesystem::exists(tri_path))
        return tri_path;
    return std::nullopt;
}

ExtractResult ExtractMap(const std::string& map_name, const std::string& output_dir) {
    ExtractResult result;
    if(map_name.empty() || map_name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos) {
        result.error="Invalid map name";return result;
    }
    try {
        const auto directory=std::filesystem::absolute(output_dir);
        std::filesystem::create_directories(directory);
        const auto tri=directory/(map_name+".tri");
        if(std::filesystem::is_regular_file(tri)) {
            const auto size=std::filesystem::file_size(tri);
            if(size>0 && size%sizeof(MapRaytrace::Triangle)==0) {
                result.success=true;result.tri_path=tri.string();return result;
            }
        }
        const auto text=directory/(map_name+".vphys");
        auto text_input=text;
        if(!std::filesystem::is_regular_file(text)) {
            if(!Init())throw std::runtime_error("CS2 installation not found");
            std::vector<std::string> candidates;
            for(const auto& path:g_vpk_files)
                if(std::filesystem::path(path).stem()==map_name)candidates.push_back(path);
            for(const auto& path:g_vpk_files)
                if(std::filesystem::path(path).stem()=="pak01_dir")candidates.push_back(path);
            bool found=false;
            for(const auto& archive:candidates) {
                for(const auto* ext:{"vmdl_c","vphys_c","vphys"}) {
                    const auto resource=directory/(map_name+"."+ext);
                    const auto entry="maps/"+map_name+"/world_physics."+ext;
                    if(!VpkReader::Extract(archive,entry,resource))continue;
                    LOGF(INFO,"[map_extractor] Decoding {} from {}",entry,archive);
                    if(std::string_view(ext)!="vphys") {
                        const auto temporary=std::filesystem::path(text.wstring()+L".tmp");
                        PhysicsDecoder::Decode(resource,temporary,std::string_view(ext)=="vmdl_c"?"PHYS":"DATA");
                        // A failed/unsupported decode must not become a persistent
                        // cache hit. Only the validated .tri is published below.
                        text_input=temporary;
                    }
                    found=true;break;
                }
                if(found)break;
            }
            if(!found)throw std::runtime_error("Map package has no supported world_physics collision resource");
        }
        std::ifstream input(text_input,std::ios::binary);
        const auto triangles=PhysicsText::Read(input);
        const auto temporary=std::filesystem::path(tri.wstring()+L".tmp");
        std::ofstream output(temporary,std::ios::binary|std::ios::trunc);
        output.write(reinterpret_cast<const char*>(triangles.data()),triangles.size()*sizeof(MapRaytrace::Triangle));
        output.close();if(!output)throw std::runtime_error("Cannot write extracted map triangles");
        std::error_code error;FileIO::Replace(temporary,tri,error);
        if(error)throw std::system_error(error);
        LOGF(INFO,"[map_extractor] Saved {} world triangles to {}",triangles.size(),tri.string());
        result.success=true;result.tri_path=tri.string();
    } catch(const std::exception& error) {result.error=error.what();}
    return result;
}
bool EnsureMapLoaded(const std::string& map_name) {
    if (map_name.empty())
        return false;

    // Check if already loaded in MapRaytrace
    if (MapRaytrace::CurrentMap() == map_name && MapRaytrace::IsReady())
        return true;

    // Check if .tri exists locally
    if (MapRaytrace::LoadMap(map_name))
        return true;

    SetStatus("Loading collision geometry for "+map_name+"...");
    // New caches live beside the executable, regardless of the launcher's CWD.
    wchar_t executable[32768]{};
    const auto length=GetModuleFileNameW(nullptr,executable,std::size(executable));
    const auto directory=length && length<std::size(executable)
        ? std::filesystem::path(executable).parent_path()/"maps" : std::filesystem::path("maps");
    auto result = ExtractMap(map_name,directory.string());
    if (result.success) {
        return MapRaytrace::LoadMap(map_name);
    }

    LOGF(WARNING, "[map_extractor] No collision data for map '{}': {}", map_name, result.error);
    SetStatus(map_name+": "+result.error);
    return false;
}

std::vector<std::string> ListAvailableMaps() {
    std::vector<std::string> maps;

    if (!g_initialized)
        Init();

    // Known official maps
    static const std::vector<std::string> official_maps = {
        "de_dust2", "de_mirage", "de_inferno", "de_nuke", "de_overpass",
        "de_vertigo", "de_ancient", "de_anubis", "de_train", "de_cache",
        "de_cbble", "de_season", "de_aztec", "de_dust", "de_italy",
        "de_office", "de_agency", "de_basalt", "de_thera", "de_mills",
        "de_crown", "de_lake", "de_vostok", "de_klim", "de_mooncamp",
        "de_shattered", "de_canals", "de_shortdust", "de_shortnuke",
        "de_safehouse", "de_olddust2", "de_vietnam", "de_stmarc",
        "de_museum", "de_breach", "de_river", "de_frost", "de_tulip",
        "de_havana", "de_primetime", "de_rubicon", "de_bikini", "de_grotto"
    };

    for (const auto& map : official_maps) {
        if (HasMapData(map))
            maps.push_back(map);
    }

    return maps;
}

} // namespace MapExtractor
