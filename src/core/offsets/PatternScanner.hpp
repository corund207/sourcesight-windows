#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
#include <utility>
#include "core/memory/Memory.hpp"

// Runtime pattern scanner for CS2 module offsets.
// Mirrors a2x/cs2-dumper signatures so game updates resolve without a rebuild.
// Scans live process memory (executable sections only) and computes RIP-relative
// RVAs. Compiled Offsets.hpp values remain the fallback.
namespace PatternScanner {
    // Pattern byte: 0-255 exact, -1 wildcard.
    using Pattern = std::vector<int>;

    // Pure helper for unit tests: find first occurrence of pat in data.
    bool FindPattern(const std::uint8_t* data, std::size_t size,
                     const Pattern& pat, std::size_t& match_off);

    // Resolve RIP-relative RVA: target = section_va + match_off + instr_len + disp.
    // disp_off is the offset of the int32 disp within the match.
    bool ResolveRip(const std::uint8_t* section_data, std::size_t section_size,
                    std::size_t match_off, std::size_t disp_off, std::size_t instr_len,
                    std::uintptr_t section_va, std::uintptr_t& out_rva);

    struct Resolved {
        bool ok = false;
        std::uintptr_t entityList = 0;
        std::uintptr_t viewMatrix = 0;
        std::uintptr_t localPlayerController = 0;
        std::uintptr_t globalVars = 0;
        std::uintptr_t plantedC4 = 0;
        std::uintptr_t weaponC4 = 0;
        std::uintptr_t buildNumber = 0;
        std::uintptr_t networkClient = 0;
        int build = 0;
    };

    // Scan client.dll + engine2.dll via live reads and validate.
    // Returns true and fills out when all 8 critical offsets resolve and the
    // build number at the scanned dwBuildNumber looks plausible.
    bool TryResolve(const pProcess& proc, const ProcessModule& client,
                    const ProcessModule& engine, Resolved& out);

    // Resolve via TryResolve and overwrite offsets:: vars + supportedBuild.
    // Always safe to call: on any failure it keeps compiled defaults.
    // Returns true if auto-adjust applied.
    bool ApplyAutoUpdate();
}
