#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

// Deliberately small, copyable health surface for UI and explicit support
// exports.  It contains no process, player, profile, or filesystem details.
namespace Diagnostics {

struct CacheHealth {
    std::string state;
    std::uint64_t generation{};
    std::int64_t age_ms{};
    std::int64_t refresh_ms{};
};

struct ConfigHealth {
    std::string code;
    std::string message;
};

struct ReportSnapshot {
    std::string version;
    CacheHealth cache;
    ConfigHealth config;
    std::optional<double> cpu_ms;
    std::optional<double> gpu_ms;
};

// A coherent, thread-safe copy suitable for a menu/status line.
ReportSnapshot Snapshot();

// Supply measured timing values only.  std::nullopt means unavailable; this
// subsystem never estimates or fabricates timing data.
void SetTimings(std::optional<double> cpu_ms, std::optional<double> gpu_ms);

// Produces a compact sanitized JSON document. No export is performed here.
std::string BuildSanitizedJson();

// User-invoked export only. destination must name a file, not a directory.
// Errors are generic by design and never include the supplied path.
bool ExportSanitizedReport(const std::string& destination, std::string& error);

} // namespace Diagnostics
