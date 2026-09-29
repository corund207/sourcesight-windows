#pragma once
#include <filesystem>
namespace PhysicsDecoder {
void Decode(const std::filesystem::path& resource,const std::filesystem::path& text,const char* block);
}
