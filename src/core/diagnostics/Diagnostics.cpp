#include "Diagnostics.hpp"

#include "config/Config.hpp"
#include "core/engine/cache/Cache.hpp"

#include <cmath>
#include <cerrno>
#include <filesystem>
#include <mutex>
#include "core/platform/FileIO.hpp"

#include <nlohmann/json.hpp>

namespace {

std::mutex& TimingMutex() {
    static std::mutex mutex;
    return mutex;
}

std::optional<double>& CpuTiming() {
    static std::optional<double> value;
    return value;
}

std::optional<double>& GpuTiming() {
    static std::optional<double> value;
    return value;
}

std::string CacheStateName(CacheStatus state) {
    switch (state) {
    case CacheStatus::Unavailable: return "unavailable";
    case CacheStatus::Refreshing: return "refreshing";
    case CacheStatus::Ready: return "ready";
    case CacheStatus::ProcessUnavailable: return "process_unavailable";
    case CacheStatus::GameReadFailed: return "game_read_failed";
    case CacheStatus::EntityListUnavailable: return "entity_list_unavailable";
    case CacheStatus::GlobalsReadFailed: return "globals_read_failed";
    case CacheStatus::NoMatch: return "no_match";
    case CacheStatus::MissingLocal: return "missing_local";
    }
    return "unavailable";
}

std::optional<double> SafeTiming(std::optional<double> value) {
    return value && std::isfinite(*value) && *value >= 0.0 ? value : std::nullopt;
}

} // namespace

namespace Diagnostics {

ReportSnapshot Snapshot() {
    const auto cache = Cache::Status();
    const auto config = Config::LastError();
    std::lock_guard lock(TimingMutex());
    return {
#ifdef SOURCESIGHT_VERSION
        SOURCESIGHT_VERSION,
#else
        "development",
#endif
        { (cache.state == CacheStatus::Ready || cache.state == CacheStatus::MissingLocal) && !cache.ready()
              ? "stale" : CacheStateName(cache.state),
          cache.generation, cache.age.count(), cache.refresh_duration.count() },
        { Config::ErrorCodeName(config.code), config.message },
        SafeTiming(CpuTiming()),
        SafeTiming(GpuTiming())
    };
}

void SetTimings(std::optional<double> cpu_ms, std::optional<double> gpu_ms) {
    std::lock_guard lock(TimingMutex());
    CpuTiming() = SafeTiming(cpu_ms);
    GpuTiming() = SafeTiming(gpu_ms);
}

std::string BuildSanitizedJson() {
    const auto snapshot = Snapshot();
    nlohmann::json report = {
        { "format", "sourcesight-diagnostics-v1" },
        { "version", snapshot.version },
        { "cache", {
            { "state", snapshot.cache.state },
            { "generation", snapshot.cache.generation },
            { "age_ms", snapshot.cache.age_ms },
            { "refresh_ms", snapshot.cache.refresh_ms }
        } },
        { "config", {
            { "code", snapshot.config.code },
            { "message", snapshot.config.message }
        } },
        { "timings", {
            { "cpu_ms", snapshot.cpu_ms ? nlohmann::json(*snapshot.cpu_ms) : nlohmann::json(nullptr) },
            { "gpu_ms", snapshot.gpu_ms ? nlohmann::json(*snapshot.gpu_ms) : nlohmann::json(nullptr) }
        } }
    };
    return report.dump(2) + "\n";
}

bool ExportSanitizedReport(const std::string& destination, std::string& error) {
    error.clear();
    const std::filesystem::path target(destination);
    std::error_code ec;
    if (destination.empty() || target.filename().empty() || std::filesystem::is_directory(target, ec)) {
        error = "Choose a report filename.";
        return false;
    }

    if (!FileIO::WriteNew(target,BuildSanitizedJson())) {
        const auto code=GetLastError();
        error=(code==ERROR_FILE_EXISTS || code==ERROR_ALREADY_EXISTS)
            ? "A diagnostic report already exists at that destination."
            : "Unable to create or write the diagnostic report.";
        return false;
    }
    return true;
}
} // namespace Diagnostics
