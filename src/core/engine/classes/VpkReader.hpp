#pragma once
#include <filesystem>
#include <string>
namespace VpkReader {
// Exact path lookup; validates directory bounds and handles preload/chunks.
bool Extract(const std::filesystem::path& archive,const std::string& entry,
             const std::filesystem::path& output);
}
