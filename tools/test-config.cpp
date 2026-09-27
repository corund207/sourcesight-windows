#include "common.hpp"
#include "config/Config.hpp"
#include "config/Current.hpp"
#include "config/AutoCalibration.hpp"
#include "core/diagnostics/Diagnostics.hpp"
#include "core/engine/cache/Cache.hpp"

#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

nlohmann::json read_json(const std::filesystem::path& path) {
    std::ifstream input(path);
    nlohmann::json result;
    input >> result;
    return result;
}

void write_json(const std::filesystem::path& path, const nlohmann::json& value) {
    std::ofstream output(path, std::ios::trunc);
    output << value.dump(2) << '\n';
}
} // namespace

int RunChecks() {
    LogHelper::Init();
    const auto root = std::filesystem::current_path();
    const auto profiles = root / "configs";

    cfg::enabled = false;
    cfg::esp::wireframe_blackout = true;
    cfg::esp::viewmodel_wireframe::enabled = true;
    cfg::esp::viewmodel_wireframe::opacity = .72f;
    cfg::esp::viewmodel_wireframe::scale = 1.18f;
    cfg::settings::advanced_controls = true;
    cfg::world::radar::calibration_height = 1024.f;
    require(Config::SaveProfile("roundtrip"), "initial save");
    const auto roundtrip = profiles / "roundtrip.json";
    auto saved = read_json(roundtrip);
    require(saved.value("schema_version", -1) == Config::SchemaVersion(), "schema version written");
    require(saved["utils"]["advanced_controls"].get<bool>(), "advanced control mode persists");
    saved["esp"]["bullet_tracer"]["length"] = 2048.f;
    saved["unknown_extension"] = { {"preserved", true} };
    write_json(roundtrip, saved);
    cfg::enabled = true;
    cfg::esp::wireframe_blackout = false;
    cfg::esp::viewmodel_wireframe::enabled = false;
    cfg::esp::viewmodel_wireframe::opacity = .9f;
    cfg::esp::viewmodel_wireframe::scale = 1.f;
    cfg::world::radar::calibration_height = 0.f;
    require(Config::LoadProfile("roundtrip"), "round trip load");
    require(cfg::world::radar::calibration_height == 1024.f, "radar resolution calibration persists");
    require(cfg::esp::wireframe_blackout, "dark map setting persists");
    require(cfg::esp::viewmodel_wireframe::enabled &&
            std::abs(cfg::esp::viewmodel_wireframe::opacity - .72f) < .001f &&
            std::abs(cfg::esp::viewmodel_wireframe::scale - 1.18f) < .001f,
            "dark map viewmodel settings persist");
    require(!cfg::enabled, "round trip applies stored setting");
    require(Config::Write(), "round trip rewrite");
    const auto rewritten = read_json(roundtrip);
    require(rewritten["unknown_extension"]["preserved"].get<bool>(), "unknown field preserved");
    require(!rewritten["esp"]["bullet_tracer"].contains("length"), "legacy tracer distance removed on rewrite");
    require(std::filesystem::exists(roundtrip.string() + ".bak"), "previous-good profile backup created");

    const auto corrupt = profiles / "corrupt.json";
    write_json(corrupt, read_json(roundtrip));
    const std::string before_corruption = [] (const std::filesystem::path& path) {
        std::ifstream input(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(input), {});
    }(corrupt);
    { std::ofstream output(corrupt, std::ios::trunc); output << "{ truncated"; }
    cfg::enabled = true;
    require(!Config::LoadProfile("corrupt"), "corrupt profile rejected");
    require(cfg::enabled, "corrupt profile does not partially apply globals");
    require(Config::LastError().code == Config::ErrorCode::ParseFailed, "corrupt profile error exposed");
    require(std::filesystem::file_size(corrupt) < before_corruption.size(), "corrupt profile is not overwritten");

    auto missing = read_json(roundtrip);
    missing.erase("world");
    missing.erase("macro");
    write_json(profiles / "missing.json", missing);
    require(Config::LoadProfile("missing"), "missing optional sections migrate");
    require(Config::GetActiveProfile() == "missing", "active profile only changes after load success");

    auto future = read_json(roundtrip);
    future["schema_version"] = Config::SchemaVersion() + 1;
    write_json(profiles / "future.json", future);
    cfg::enabled = true;
    require(!Config::LoadProfile("future"), "future profile rejected");
    require(cfg::enabled, "future profile leaves globals untouched");
    require(Config::LastError().code == Config::ErrorCode::FutureSchema, "future schema error exposed");

    future["schema_version"] = 4294967296ULL;
    write_json(profiles / "future-overflow.json", future);
    require(!Config::LoadProfile("future-overflow"), "oversized future schema rejected");
    require(Config::LastError().code == Config::ErrorCode::FutureSchema,
            "oversized schema cannot wrap into a legacy migration");

    auto bounded = read_json(roundtrip);
    bounded["esp"]["wireframe_budget"] = 999999;
    write_json(profiles / "bounded.json", bounded);
    require(Config::LoadProfile("bounded"), "bounded profile loads");
    require(cfg::esp::wireframe_budget == 8000, "numeric values are clamped to supported boundaries");

    cfg::settings::advanced_controls = false;
    cfg::world::radar::calibration_height = 1000.f;
    cfg::world::radar::pos = {100.f, 50.f};
    cfg::world::radar::size = {200.f, 200.f};
    AutoCalibration::ApplySimple(800.f, 500.f);
    require(cfg::world::radar::calibration_height == 500.f &&
            std::abs(cfg::world::radar::pos.x - 50.f) < .001f &&
            cfg::esp::wireframe_budget == 3000,
            "simple mode tracks viewport scale and selects a bounded render budget");
    cfg::settings::advanced_controls = true;
    cfg::esp::wireframe_budget = 4321;
    AutoCalibration::ApplySimple(1920.f, 1080.f);
    require(cfg::esp::wireframe_budget == 4321 && cfg::world::radar::calibration_height == 500.f,
            "advanced mode preserves granular values");

    require(Config::SaveProfile("write-failure"), "write failure fixture");
    const auto write_failure = profiles / "write-failure.json";
    std::ifstream original_file(write_failure, std::ios::binary);
    const std::string original(std::istreambuf_iterator<char>(original_file), {});
    Config::SetWriteFailureForTesting(true);
    require(!Config::SaveProfile("write-failure"), "injected write failure reported");
    Config::SetWriteFailureForTesting(false);
    std::ifstream after_file(write_failure, std::ios::binary);
    require(std::string(std::istreambuf_iterator<char>(after_file), {}) == original,
            "failed write preserves old profile");

    require(Cache::PublishForTesting({}), "fresh cache test frame ready");
    require(Cache::Status().ready(), "fresh cache status ready");
    require(!Cache::PublishForTesting({.age = 251ms}), "stale cache test frame unusable");
    require(Diagnostics::Snapshot().cache.state == "stale", "diagnostics reports stale state");
    require(!Cache::PublishForTesting({.in_match = false}), "no match unusable");
    require(Diagnostics::Snapshot().cache.state == "no_match", "diagnostics reports no match");
    require(Cache::PublishForTesting({.local_present = false}), "spectator missing-local frame remains usable");
    require(Diagnostics::Snapshot().cache.state == "missing_local", "diagnostics reports missing local");
    require(!Cache::PublishForTesting({.local_present = false, .age = 251ms}), "stale missing-local frame unusable");
    require(Diagnostics::Snapshot().cache.state == "stale", "diagnostics reports stale missing local");
    require(!Cache::PublishForTesting({.process_available = false}), "disconnect unusable");
    require(Diagnostics::Snapshot().cache.state == "process_unavailable", "diagnostics reports disconnect");

    Diagnostics::SetTimings(1.25, std::nullopt);
    const auto report = Diagnostics::BuildSanitizedJson();
    require(report.find("player") == std::string::npos && report.find("configs") == std::string::npos,
            "diagnostic report is redacted");
    std::string export_error;
    const auto report_path = root / "diagnostics.json";
    std::filesystem::remove(report_path);
    require(Diagnostics::ExportSanitizedReport(report_path.string(), export_error), "explicit diagnostic export");
    require(!Diagnostics::ExportSanitizedReport(report_path.string(), export_error), "diagnostic export never overwrites");
    LogHelper::Destroy();
    std::cout << "PASS: profile validation, backups, write failures, lifecycle and diagnostic redaction\n";
    return 0;
}

int main() {
    try { return RunChecks(); }
    catch(const std::exception& error) { std::cerr << error.what() << std::endl; LogHelper::Destroy(); return 1; }
}
