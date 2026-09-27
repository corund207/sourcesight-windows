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

// VisCheckCS2 parser for .vphys files
#include "Parser.h"
#include "OptimizedGeometry.h"
#include "Math.hpp"

namespace MapExtractor {

namespace {
    std::string g_cs2_install_path;
    std::string g_maps_dir = "maps";
    bool g_initialized = false;
    std::vector<std::string> g_vpk_files;
}

// VPK file format structures (VPK v2)
#pragma pack(push, 1)
struct VPKHeader {
    uint32_t signature;        // 0x55aa1234
    uint32_t version;          // 2 for VPK v2
    uint32_t tree_size;        // Size of directory tree in bytes
    uint32_t file_data_section_size; // v2 only
    uint32_t archive_md5_section_size; // v2 only
    uint32_t other_md5_section_size;   // v2 only
    uint32_t signature_section_size;   // v2 only
};
#pragma pack(pop)

// Simple VPK extractor for specific files
bool ExtractFileFromVPK(const std::string& vpk_path, const std::string& file_to_find, const std::string& output_path) {
    std::ifstream vpk(vpk_path, std::ios::binary);
    if (!vpk) {
        LOGF(WARNING, "[vpk] Failed to open {}", vpk_path);
        return false;
    }

    VPKHeader header;
    vpk.read(reinterpret_cast<char*>(&header), sizeof(header));

    if (header.signature != 0x55aa1234 || header.version != 2) {
        LOGF(WARNING, "[vpk] Invalid VPK header: sig=0x{:X} ver={}", header.signature, header.version);
        return false;
    }

    LOGF(INFO, "[vpk] Reading VPK: tree_size={} bytes", header.tree_size);

    // Read directory tree
    std::vector<char> tree_data(header.tree_size);
    vpk.read(tree_data.data(), header.tree_size);

    // Parse directory tree to find our file
    size_t pos = 0;
    int file_count = 0;
    while (pos < tree_data.size()) {
        // Read extension
        std::string ext(&tree_data[pos]);
        pos += ext.size() + 1;
        if (ext.empty()) break; // End of tree

        while (pos < tree_data.size()) {
            // Read path
            std::string path(&tree_data[pos]);
            pos += path.size() + 1;
            if (path.empty()) break; // Next extension

            while (pos < tree_data.size()) {
                // Read filename
                std::string filename(&tree_data[pos]);
                pos += filename.size() + 1;
                if (filename.empty()) break; // Next path

                // Read file entry metadata
                if (pos + 20 > tree_data.size()) return false;

                uint32_t crc32 = *reinterpret_cast<uint32_t*>(&tree_data[pos]); pos += 4;
                uint16_t preload_bytes = *reinterpret_cast<uint16_t*>(&tree_data[pos]); pos += 2;
                uint16_t archive_index = *reinterpret_cast<uint16_t*>(&tree_data[pos]); pos += 2;
                uint32_t entry_offset = *reinterpret_cast<uint32_t*>(&tree_data[pos]); pos += 4;
                uint32_t entry_length = *reinterpret_cast<uint32_t*>(&tree_data[pos]); pos += 4;
                uint16_t terminator = *reinterpret_cast<uint16_t*>(&tree_data[pos]); pos += 2;

                file_count++;

                // Check if this is our file
                std::string full_path = path + "/" + filename + "." + ext;
                if (full_path == file_to_find || filename + "." + ext == file_to_find) {
                    LOGF(INFO, "[vpk] Found file: {} (archive_idx={}, offset={}, len={})",
                         full_path, archive_index, entry_offset, entry_length);

                    // Found it! Read the file data
                    size_t data_start = sizeof(VPKHeader) + header.tree_size + entry_offset;
                    vpk.seekg(data_start);

                    std::vector<char> file_data(entry_length);
                    vpk.read(file_data.data(), entry_length);

                    if (vpk.gcount() == static_cast<std::streamsize>(entry_length)) {
                        std::ofstream out(output_path, std::ios::binary);
                        out.write(file_data.data(), entry_length);
                        LOGF(INFO, "[vpk] Successfully extracted to {}", output_path);
                        return true;
                    }

                    LOGF(WARNING, "[vpk] Failed to read full file data (got {} of {} bytes)", vpk.gcount(), entry_length);

                    // If not found in this VPK, try chunk files
                    if (archive_index != 0x7FFF) {
                        std::string chunk_path = vpk_path;
                        size_t dot_pos = chunk_path.rfind(".vpk");
                        if (dot_pos != std::string::npos) {
                            chunk_path = chunk_path.substr(0, dot_pos) + "_" +
                                std::to_string(archive_index).substr(0, 3) + ".vpk";
                        }

                        LOGF(INFO, "[vpk] Trying chunk file: {}", chunk_path);
                        std::ifstream chunk_vpk(chunk_path, std::ios::binary);
                        if (chunk_vpk) {
                            chunk_vpk.seekg(entry_offset);
                            std::vector<char> chunk_data(entry_length);
                            chunk_vpk.read(chunk_data.data(), entry_length);
                            if (chunk_vpk.gcount() == static_cast<std::streamsize>(entry_length)) {
                                std::ofstream out(output_path, std::ios::binary);
                                out.write(chunk_data.data(), entry_length);
                                LOGF(INFO, "[vpk] Successfully extracted from chunk: {}", output_path);
                                return true;
                            }
                        }
                    }
                }

                // Skip preload data if any
                if (preload_bytes > 0) {
                    pos += preload_bytes;
                }
            }
        }
    }
    LOGF(INFO, "[vpk] File '{}' not found in VPK (searched {} files)", file_to_find, file_count);
    return false;
}

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

    if (!g_initialized)
        Init();

    // Check if already extracted
    if (HasMapData(map_name, output_dir)) {
        result.success = true;
        result.tri_path = output_dir + "/" + map_name + ".tri";
        return result;
    }

    // Try to find and extract world_physics.vphys from VPK
    // Look for map VPK files (de_dust2.vpk, pak01_dir.vpk, etc.)
    std::string vphys_path;
    std::string vphys_filename = "maps/" + map_name + "/world_physics.vphys";

    // Priority 1: Check map-specific VPK (de_dust2.vpk)
    for (const auto& vpk_file : g_vpk_files) {
        std::string vpk_name = std::filesystem::path(vpk_file).stem().string();
        if (vpk_name == map_name) {
            LOGF(INFO, "[map_extractor] Trying to extract {} from {}", vphys_filename, vpk_file);
            if (ExtractFileFromVPK(vpk_file, vphys_filename, output_dir + "/" + map_name + ".vphys")) {
                vphys_path = output_dir + "/" + map_name + ".vphys";
                LOGF(INFO, "[map_extractor] Successfully extracted .vphys from {}", vpk_file);
                break;
            }
        }
    }

    LOGF(INFO, "[map_extractor] After priority 1, vphys_path empty: {}", vphys_path.empty());

    // Priority 2: Check pak01_dir.vpk (main package)
    if (vphys_path.empty()) {
        LOGF(INFO, "[map_extractor] Trying pak01_dir.vpk... (total VPKs: {})", g_vpk_files.size());
        for (const auto& vpk_file : g_vpk_files) {
            std::string vpk_name = std::filesystem::path(vpk_file).stem().string();
            LOGF(INFO, "[map_extractor] Checking VPK: {} (stem: {})", vpk_file, vpk_name);
            if (vpk_name == "pak01_dir") {
                LOGF(INFO, "[map_extractor] Trying to extract {} from {}", vphys_filename, vpk_file);
                if (ExtractFileFromVPK(vpk_file, vphys_filename, output_dir + "/" + map_name + ".vphys")) {
                    vphys_path = output_dir + "/" + map_name + ".vphys";
                    LOGF(INFO, "[map_extractor] Successfully extracted .vphys from {}", vpk_file);
                    break;
                }
            }
        }
    }

    // Priority 3: Check for already extracted .vphys in maps folder
    if (vphys_path.empty()) {
        std::string local_vphys = output_dir + "/" + map_name + ".vphys";
        if (std::filesystem::exists(local_vphys)) {
            vphys_path = local_vphys;
        }
    }

    // Priority 4: Check for .vphys in CS2 install (expanded map)
    if (vphys_path.empty()) {
        std::string cs2_vphys = g_cs2_install_path + "/game/csgo/maps/" + map_name + "/world_physics.vphys";
        if (std::filesystem::exists(cs2_vphys)) {
            vphys_path = cs2_vphys;
        }
    }

    // Priority 5: Check for .vphys_c (compressed) - needs decompression via Source 2 Viewer
    if (vphys_path.empty()) {
        std::string cs2_vphys_c = g_cs2_install_path + "/game/csgo/maps/" + map_name + "/world_physics.vphys_c";
        if (std::filesystem::exists(cs2_vphys_c)) {
            LOGF(INFO, "[map_extractor] Found compressed .vphys_c for {}", map_name);
            result.success = false;
            result.error = "Found compressed .vphys_c - use Source 2 Viewer (ValveResourceFormat) to extract: "
                           "Open pak01_dir.vpk in Source 2 Viewer, find maps/" + map_name + "/world_physics.vphys_c, "
                           "extract and decompress to .vphys, then convert to .tri with VPhysToOpt.";
            return result;
        }
    }

    if (!vphys_path.empty()) {
        LOGF(INFO, "[map_extractor] Parsing .vphys for {}: {}", map_name, vphys_path);

        try {
            // Use VisCheckCS2 parser to parse .vphys
            Parser parser(vphys_path);
            auto combined = parser.GetCombinedList();

            // Save as .tri format (compatible with MapRaytrace - just array of Triangle {Vec3 p1,p2,p3})
            std::string tri_path = output_dir + "/" + map_name + ".tri";
            const std::string temporary_path = tri_path + ".tmp";
            std::ofstream out(temporary_path, std::ios::binary);
            if (!out) {
                result.success = false;
                result.error = "Failed to open output .tri file";
                return result;
            }

            // MapRaytrace expects: array of Triangle { Vec3 p1, p2, p3 } (36 bytes each)
            // No header, just raw triangles
            size_t total_tris = 0;
            for (const auto& mesh : combined) {
                total_tris += mesh.size();
            }

            static_assert(sizeof(TriangleCombined) == 36);
            for (const auto& mesh : combined)
                out.write(reinterpret_cast<const char*>(mesh.data()), mesh.size() * sizeof(TriangleCombined));
            out.close();
            if (!out || total_tris == 0) {
                result.error = "Empty geometry or incomplete mesh write";
                return result;
            }
            // Readers see only a complete mesh; a failed write leaves a .tmp
            // diagnostic file rather than poisoning the persistent map cache.
            std::error_code rename_error;
            FileIO::Replace(temporary_path,tri_path,rename_error);
            if(rename_error)throw std::system_error(rename_error);

            LOGF(INFO, "[map_extractor] Saved {} triangles to {}", total_tris, tri_path);
            result.success = true;
            result.tri_path = tri_path;
            return result;
        }
        catch (const std::exception& e) {
            result.success = false;
            result.error = std::string("Parser error: ") + e.what();
            return result;
        }
    }

    result.success = false;
    result.error = "No .vphys file found for map. Extract using Source 2 Viewer or cs2-phys-extractor.";
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

    // Try to extract
    auto result = ExtractMap(map_name);
    if (result.success) {
        return MapRaytrace::LoadMap(map_name);
    }

    LOGF(WARNING, "[map_extractor] No collision data for map '{}': {}", map_name, result.error);
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
