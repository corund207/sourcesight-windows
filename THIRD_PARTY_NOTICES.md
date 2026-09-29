# Third-party and historical rights

The proprietary license applies only to material owned by Jonah Chang.
It does not replace these components' licenses or copyright notices.

| Component | Terms and location |
| --- | --- |
| Dear ImGui | MIT — src/external/imgui/LICENSE.txt |
| nlohmann/json | MIT — src/external/json/LICENSE.MIT |
| AsyncLogger (Yimura) | MIT — src/external/AsyncLogger/LICENSE |
| VisCheckCS2 (Read1dno/BLOOM) | MIT — src/external/VisCheckCS2/LICENSE |
| GLFW | zlib/libpng — src/external/glfw/LICENSE.md |
| GLEW | BSD/MIT — fetched glew-src/LICENSE.txt (included in binary packages) |
| Source 2 Viewer CLI 20.0 | ValveResourceFormat contributors, MIT — licenses/ValveResourceFormat.txt; https://github.com/ValveResourceFormat/ValveResourceFormat/releases/tag/20.0 |
| Windows offset data | a2x/cs2-dumper — MIT; snapshot and source link in src/core/offsets/Offsets.hpp and README.md |
| Interface icon font | Existing Scarlab attribution, MIT — assets/README.md |

The binary archive includes available upstream license texts under licenses/.
GLFW, GLEW, Dear ImGui, AsyncLogger and VisCheckCS2 are statically linked.
Windows and OpenGL system DLLs are not redistributed. MinGW builds may also
statically link the GCC/libstdc++ runtimes, covered by the GCC Runtime Library
Exception; Visual Studio builds statically link Microsoft's C++ runtime.
The Windows repository and binary package contain no input driver.

CS2, map geometry, weapon artwork and other game-derived content belong to
their respective owners. Availability in this repository does not grant
ownership or commercial redistribution rights. Map meshes are not included
in the binary release. Existing game/icon assets retain their original rights.

Historical versions carried GPL, Apache and Creative Commons terms. This
release does not revoke rights validly granted in earlier versions.
