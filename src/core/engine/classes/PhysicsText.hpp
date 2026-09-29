#pragma once
#include "MapRaytrace.hpp"
#include <istream>
namespace PhysicsText {
// Reads the pinned Source 2 Viewer PHYS text dump in Source world coordinates.
// Throws on malformed topology instead of publishing an incomplete/corrupt mesh.
std::vector<MapRaytrace::Triangle> Read(std::istream& input);
}
