#include "PatternScanner.hpp"
#include "Offsets.hpp"
#include "core/engine/Engine.hpp"
#include <cstring>
#include <cmath>

namespace PatternScanner {
namespace {
struct Sig {
    const char* name;
    Pattern bytes;
    std::size_t disp_off;
    std::size_t instr_len;
};
// Signatures mirror a2x/cs2-dumper src/analysis/offsets.rs so they survive updates.
// dwWeaponC4 uses the full 29-byte sequence: the short 12-byte prefix occurs
// multiple times in client.dll and mis-resolves (e.g. 0x21BBFA0 far from planted C4).
const Sig kClientSigs[] = {
    {"dwEntityList", {0x48,0x89,0x0D,-1,-1,-1,-1,0xE9,-1,-1,-1,-1,0xCC}, 3, 7},
    {"dwViewMatrix", {0x48,0x8D,0x0D,-1,-1,-1,-1,0x48,0xC1,0xE0,0x06}, 3, 7},
    {"dwLocalPlayerController", {0x48,0x8B,0x05,-1,-1,-1,-1,0x41,0x89,0xBE}, 3, 7},
    {"dwGlobalVars", {0x48,0x89,0x15,-1,-1,-1,-1,0x48,0x89,0x42}, 3, 7},
    {"dwPlantedC4", {0x48,0x8B,0x1D,-1,-1,-1,-1,0x45,0x32,0xF6}, 3, 7},
    {"dwWeaponC4", {0x48,0x8B,0x15,-1,-1,-1,-1,0x48,0x8B,0x5C,0x24,-1,0xFF,0xC0,0x89,0x05,-1,-1,-1,-1,0x48,0x8B,0xC6,0x48,0x89,0x34,0xEA,0x80,0xBE}, 3, 7},
};
const Sig kEngineSigs[] = {
    {"dwBuildNumber", {0x89,0x05,-1,-1,-1,-1,0x48,0x8D,0x0D,-1,-1,-1,-1,0xFF,0x15,-1,-1,-1,-1,0x48,0x8B,0x0D}, 2, 6},
    {"dwNetworkGameClient", {0x48,0x89,0x3D,-1,-1,-1,-1,0xFF,0x87}, 3, 7},
};

bool ParseHeaders(const std::uint8_t* base, std::size_t have,
                  std::vector<std::pair<std::uintptr_t,std::size_t>>& exec_ranges) {
    exec_ranges.clear();
    if (have < 0x1000 || !base) return false;
    if (base[0] != 0x4D || base[1] != 0x5A) return false; // MZ
    std::uint32_t e_lfanew = 0;
    std::memcpy(&e_lfanew, base + 0x3C, sizeof(e_lfanew));
    if (e_lfanew + 6 > have) return false;
    if (base[e_lfanew] != 0x50 || base[e_lfanew+1] != 0x45 ||
        base[e_lfanew+2] != 0x00 || base[e_lfanew+3] != 0x00) return false; // PE
    std::uint16_t num_sections = 0, opt_size = 0;
    std::memcpy(&num_sections, base + e_lfanew + 4 + 2, sizeof(num_sections));
    std::memcpy(&opt_size, base + e_lfanew + 4 + 16, sizeof(opt_size));
    if (num_sections == 0 || num_sections > 64) return false;
    std::size_t section_table = static_cast<std::size_t>(e_lfanew) + 4 + 20 + opt_size;
    if (section_table + num_sections * 40 > have) return false;
    for (std::uint16_t i = 0; i < num_sections; ++i) {
        const std::uint8_t* sec = base + section_table + i * 40;
        std::uint32_t vsize = 0, vaddr = 0, characteristics = 0;
        std::memcpy(&vsize, sec + 8, 4);
        std::memcpy(&vaddr, sec + 12, 4);
        std::memcpy(&characteristics, sec + 36, 4);
        constexpr std::uint32_t kExecute = 0x20000000u;
        if ((characteristics & kExecute) == 0) continue;
        if (vaddr == 0 || vsize == 0) continue;
        exec_ranges.emplace_back(static_cast<std::uintptr_t>(vaddr),
                                 static_cast<std::size_t>(vsize));
    }
    return !exec_ranges.empty();
}

bool ScanOne(const std::uint8_t* data, std::size_t size, std::uintptr_t section_va,
             const Sig& sig, std::uintptr_t& out_rva) {
    std::size_t match = 0;
    if (!FindPattern(data, size, sig.bytes, match)) return false;
    return ResolveRip(data, size, match, sig.disp_off, sig.instr_len, section_va, out_rva);
}
} // namespace

bool FindPattern(const std::uint8_t* data, std::size_t size,
                 const Pattern& pat, std::size_t& match_off) {
    if (!data || pat.empty() || size < pat.size()) return false;
    const int first = pat[0];
    const std::size_t n = pat.size();
    for (std::size_t i = 0; i + n <= size; ++i) {
        if (first >= 0 && data[i] != static_cast<std::uint8_t>(first)) continue;
        bool ok = true;
        for (std::size_t j = 1; j < n; ++j) {
            const int want = pat[j];
            if (want >= 0 && data[i + j] != static_cast<std::uint8_t>(want)) { ok = false; break; }
        }
        if (ok) { match_off = i; return true; }
    }
    return false;
}

bool ResolveRip(const std::uint8_t* section_data, std::size_t section_size,
                std::size_t match_off, std::size_t disp_off, std::size_t instr_len,
                std::uintptr_t section_va, std::uintptr_t& out_rva) {
    if (!section_data || match_off + disp_off + 4 > section_size) return false;
    std::int32_t disp = 0;
    std::memcpy(&disp, section_data + match_off + disp_off, sizeof(disp));
    const std::int64_t target = static_cast<std::int64_t>(section_va + match_off + instr_len) +
                                static_cast<std::int64_t>(disp);
    if (target <= 0 || target > static_cast<std::int64_t>(0xFFFFFFFFu)) return false;
    out_rva = static_cast<std::uintptr_t>(target);
    return true;
}

bool TryResolve(const pProcess& proc, const ProcessModule& client,
                const ProcessModule& engine, Resolved& out) {
    out = Resolved{};
    if (!client.base || !engine.base || client.size < 0x10000 || engine.size < 0x10000)
        return false;
    // Read headers to locate executable sections.
    std::uint8_t client_head[0x1000]{}, engine_head[0x1000]{};
    if (!proc.read_raw(client.base, client_head, sizeof(client_head))) return false;
    if (!proc.read_raw(engine.base, engine_head, sizeof(engine_head))) return false;
    std::vector<std::pair<std::uintptr_t,std::size_t>> client_exec, engine_exec;
    if (!ParseHeaders(client_head, sizeof(client_head), client_exec)) return false;
    if (!ParseHeaders(engine_head, sizeof(engine_head), engine_exec)) return false;

    auto scan_module = [&](const ProcessModule& mod,
                           const std::vector<std::pair<std::uintptr_t,std::size_t>>& exec,
                           const Sig* sigs, std::size_t count,
                           std::uintptr_t* results) {
        for (std::size_t s = 0; s < count; ++s) results[s] = 0;
        for (const auto& [va, vs] : exec) {
            if (va + vs > mod.size) continue;
            std::vector<std::uint8_t> buf(vs);
            if (!proc.read_raw(mod.base + va, buf.data(), buf.size())) continue;
            for (std::size_t s = 0; s < count; ++s) {
                if (results[s] != 0) continue;
                std::uintptr_t rva = 0;
                if (ScanOne(buf.data(), buf.size(), va, sigs[s], rva)) results[s] = rva;
            }
        }
        for (std::size_t s = 0; s < count; ++s)
            if (results[s] == 0) return false;
        return true;
    };

    std::uintptr_t c_res[6]{}, e_res[2]{};
    if (!scan_module(client, client_exec, kClientSigs, 6, c_res)) return false;
    if (!scan_module(engine, engine_exec, kEngineSigs, 2, e_res)) return false;
    // Bounds check against module sizes.
    for (int i = 0; i < 6; ++i)
        if (c_res[i] + 8 > client.size) return false;
    for (int i = 0; i < 2; ++i)
        if (e_res[i] + 8 > engine.size) return false;

    // Validate build number plausibility before accepting.
    int build = 0;
    if (!proc.read_raw(engine.base + e_res[0], &build, sizeof(build))) return false;
    if (build < 14000 || build > 30000) return false;
    // Validate key pointers are readable when present; zero is allowed in menus
    // but a present pointer must be readable. View matrix must be finite floats.
    {
        const std::uintptr_t el = proc.read<std::uintptr_t>(client.base + c_res[0]);
        if (el) {
            std::uint8_t probe[8]{};
            if (!proc.read_raw(el, probe, sizeof(probe))) return false;
            if (!proc.read_raw(el + 0x10, probe, sizeof(probe))) return false;
        }
        float vm[16]{};
        if (!proc.read_raw(client.base + c_res[1], vm, sizeof(vm))) return false;
        for (float v : vm)
            if (!std::isfinite(v)) return false;
        const std::uintptr_t gv = proc.read<std::uintptr_t>(client.base + c_res[3]);
        if (gv) {
            std::uint8_t probe[8]{};
            if (!proc.read_raw(gv, probe, sizeof(probe))) return false;
        }
    }
    out.entityList = c_res[0];
    out.viewMatrix = c_res[1];
    out.localPlayerController = c_res[2];
    out.globalVars = c_res[3];
    out.plantedC4 = c_res[4];
    out.weaponC4 = c_res[5];
    out.buildNumber = e_res[0];
    out.networkClient = e_res[1];
    // planted and weapon C4 live in the same data region; a 3MB split means
    // the short weapon prefix hit a false positive. Reject that one match.
    {
        const std::uintptr_t a = out.plantedC4 > out.weaponC4
            ? out.plantedC4 - out.weaponC4 : out.weaponC4 - out.plantedC4;
        if (a > 0x100000) return false;
    }
    out.build = build;
    out.ok = true;
    return true;
}

bool ApplyAutoUpdate() {
    auto proc = Engine::GetProcess();
    if (!proc) return false;
    const auto client = Engine::GetClient();
    const auto engine = Engine::GetEngine();
    Resolved r;
    if (!TryResolve(*proc, client, engine, r) || !r.ok) {
        LOGF(WARNING, "[offsets] pattern scan found no complete layout; using compiled snapshot {}.",
             offsets::supportedBuild);
        return false;
    }
    const bool changed = (r.entityList != offsets::entityList ||
                          r.viewMatrix != offsets::viewMatrix ||
                          r.localPlayerController != offsets::localPlayerController ||
                          r.globalVars != offsets::globalVars ||
                          r.plantedC4 != offsets::plantedC4 ||
                          r.weaponC4 != offsets::weaponC4 ||
                          r.buildNumber != offsets::buildNumber ||
                          r.networkClient != offsets::global::networkClient);
    offsets::entityList = r.entityList;
    offsets::viewMatrix = r.viewMatrix;
    offsets::localPlayerController = r.localPlayerController;
    offsets::globalVars = r.globalVars;
    offsets::plantedC4 = r.plantedC4;
    offsets::weaponC4 = r.weaponC4;
    offsets::buildNumber = r.buildNumber;
    offsets::global::networkClient = r.networkClient;
    offsets::supportedBuild = r.build;
    if (changed) {
        LOGF(INFO, "[offsets] auto-resolved for CS2 build {}: entity=0x{:X} matrix=0x{:X} lpc=0x{:X} globals=0x{:X} c4=0x{:X}/0x{:X} build=0x{:X} net=0x{:X}",
             r.build, r.entityList, r.viewMatrix, r.localPlayerController, r.globalVars,
             r.plantedC4, r.weaponC4, r.buildNumber, r.networkClient);
    } else {
        LOGF(INFO, "[offsets] pattern scan confirms compiled snapshot for CS2 build {}", r.build);
    }
    return true;
}
} // namespace PatternScanner
