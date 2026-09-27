#pragma once

#include <string>
#include <filesystem>
#include <optional>

namespace MapExtractor {

// Result of extraction attempt
struct ExtractResult {
    bool success = false;
    std::string tri_path;
    std::string error;
};

// Initialize the extractor (find CS2 install, VPK files, etc.)
// Call once at startup
bool Init();

// Extract collision mesh for a map and save as .tri
// map_name: e.g. "de_mirage"
// output_dir: where to save .tri file (default: "maps/")
// Returns path to .tri file on success
ExtractResult ExtractMap(const std::string& map_name, const std::string& output_dir = "maps");

// Check if .tri file exists for map
bool HasMapData(const std::string& map_name, const std::string& maps_dir = "maps");

// Get path to .tri file if it exists
std::optional<std::string> GetMapTriPath(const std::string& map_name, const std::string& maps_dir = "maps");

// Auto-extract on map change if missing (call from Cache/Engine when map changes)
// Returns true if map is ready for raycasting
bool EnsureMapLoaded(const std::string& map_name);

// Find CS2 installation directory automatically
std::optional<std::string> FindCS2InstallPath();

// List all available maps in CS2 VPKs
std::vector<std::string> ListAvailableMaps();

} // namespace MapExtractor