#include "RadarConvars.hpp"
#include "core/memory/Memory.hpp"
#include "common.hpp"

#include <chrono>
#include <cmath>
#include <cstring>
#include <string_view>
#include <vector>

namespace {
using namespace std::chrono_literals;

// Offsets from the slot that holds the convar-name pointer.
constexpr size_t kValue = 0x58, kMin = 0x60, kMax = 0x68, kDefault = 0x70;
constexpr size_t kBlock = 0x80;
constexpr size_t kChunk = 4u << 20;
constexpr size_t kMaxRegion = 512u << 20;

struct Convar {
    const char* name;
    std::atomic<float>* out;
    std::uintptr_t object = 0;
};

std::atomic<float> radar_scale{0.f}, hud_radar_scale{0.f};
std::jthread worker;

bool Finite(float v) { return std::isfinite(v) && std::abs(v) < 1e6f; }

// Reads and validates the float slots of a candidate convar object.
bool ReadValue(const pProcess& p, std::uintptr_t object, float* value) {
    unsigned char block[kBlock];
    if (!p.read_raw(object, block, sizeof(block))) return false;
    float v, lo, hi, def;
    std::memcpy(&v, block + kValue, 4);
    std::memcpy(&lo, block + kMin, 4);
    std::memcpy(&hi, block + kMax, 4);
    std::memcpy(&def, block + kDefault, 4);
    if (!Finite(v) || !Finite(lo) || !Finite(hi) || !Finite(def)) return false;
    const float eps = 1e-4f;
    if (!(lo < hi) || v < lo - eps || v > hi + eps || def < lo - eps || def > hi + eps) return false;
    *value = v;
    return true;
}

template <class Fn> void ForEachRegion(const pProcess& p, Fn&& fn) {
    MEMORY_BASIC_INFORMATION mbi{};
    for (std::uintptr_t a = 0; VirtualQueryEx(p.handle_, reinterpret_cast<void*>(a), &mbi, sizeof(mbi));
         a = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress) + mbi.RegionSize) {
        if (mbi.State != MEM_COMMIT || mbi.Type != MEM_PRIVATE || mbi.Protect != PAGE_READWRITE) continue;
        if (mbi.RegionSize > kMaxRegion) continue;
        if (!fn(reinterpret_cast<std::uintptr_t>(mbi.BaseAddress), mbi.RegionSize)) return;
    }
}

// Reads a region in chunks, overlapping by `overlap` bytes so matches that
// straddle a chunk boundary are not lost.
template <class Fn> void ScanRegion(const pProcess& p, std::uintptr_t base, size_t size, size_t overlap,
                                     std::vector<unsigned char>& buffer, Fn&& fn) {
    for (size_t off = 0; off < size; off += kChunk - overlap) {
        const size_t want = std::min(kChunk, size - off);
        buffer.resize(want);
        if (!p.read_raw(base + off, buffer.data(), want)) continue;
        fn(base + off, buffer.data(), want);
        if (off + want >= size) break;
    }
}

bool Locate(const pProcess& p, std::vector<Convar>& convars) {
    std::vector<unsigned char> buffer;
    // Pass 1: addresses of the NUL-terminated name strings.
    std::vector<std::vector<std::uintptr_t>> strings(convars.size());
    ForEachRegion(p, [&](std::uintptr_t base, size_t size) {
        ScanRegion(p, base, size, 64, buffer, [&](std::uintptr_t at, const unsigned char* data, size_t n) {
            for (size_t c = 0; c < convars.size(); ++c) {
                const std::string_view name(convars[c].name);
                const size_t len = name.size() + 1;
                for (size_t i = 1; i + len <= n; ++i)
                    if (data[i] == static_cast<unsigned char>(name[0]) && data[i - 1] == 0 &&
                        std::memcmp(data + i, name.data(), len) == 0)
                        strings[c].push_back(at + i);
            }
        });
        return true;
    });
    // Pass 2: 8-byte-aligned slots holding one of those addresses, then validate.
    size_t found = 0;
    ForEachRegion(p, [&](std::uintptr_t base, size_t size) {
        ScanRegion(p, base, size, kBlock, buffer, [&](std::uintptr_t at, const unsigned char* data, size_t n) {
            for (size_t i = 0; i + 8 <= n; i += 8) {
                std::uintptr_t value;
                std::memcpy(&value, data + i, 8);
                if (value < 0x10000) continue;
                for (size_t c = 0; c < convars.size(); ++c) {
                    if (convars[c].object) continue;
                    for (auto s : strings[c]) {
                        float v;
                        if (value == s && ReadValue(p, at + i, &v)) { convars[c].object = at + i; ++found; break; }
                    }
                }
            }
        });
        return found < convars.size();
    });
    return found == convars.size();
}

void Run(std::stop_token stop, std::shared_ptr<pProcess> process) {
    std::vector<Convar> convars{{"cl_radar_scale", &radar_scale}, {"cl_hud_radar_scale", &hud_radar_scale}};
    auto retry = 2s;
    while (!stop.stop_requested()) {
        for (auto& c : convars) { c.object = 0; c.out->store(0.f); }
        if (Locate(*process, convars)) {
            LOGF(INFO, "Radar convars found: cl_radar_scale={:.2f} cl_hud_radar_scale={:.2f}",
                 [&]{ float v{}; ReadValue(*process, convars[0].object, &v); return v; }(),
                 [&]{ float v{}; ReadValue(*process, convars[1].object, &v); return v; }());
            retry = 2s;
            bool valid = true;
            while (valid && !stop.stop_requested()) {
                for (auto& c : convars) {
                    float v;
                    if (ReadValue(*process, c.object, &v)) c.out->store(v);
                    else valid = false;
                }
                std::this_thread::sleep_for(250ms);
            }
        } else {
            LOGF(WARNING, "Could not locate CS2 radar convars; using manual radar zoom");
            for (auto s = 0ms; s < retry && !stop.stop_requested(); s += 250ms) std::this_thread::sleep_for(250ms);
            retry = std::min<std::chrono::seconds>(retry * 2, 30s);
        }
    }
}
} // namespace

namespace RadarConvars {
void Start(std::shared_ptr<pProcess> process) {
    Stop();
    if (!process) return;
    worker = std::jthread([process](std::stop_token stop) { Run(stop, process); });
}

void Stop() {
    worker.request_stop();
    if (worker.joinable()) worker.join();
    radar_scale.store(0.f);
    hud_radar_scale.store(0.f);
}

Values Get() { return {radar_scale.load(), hud_radar_scale.load()}; }
} // namespace RadarConvars
