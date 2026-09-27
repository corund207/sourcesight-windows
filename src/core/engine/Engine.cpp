#include "Engine.hpp"

#include "core/offsets/Dumper.hpp"
#include "core/engine/cache/Cache.hpp"
#include "core/engine/classes/MapRaytrace.hpp"
#include "core/engine/classes/MapExtractor.hpp"

bool Engine::Init() {
    return GetInstance().InitImpl();
}

void Engine::Stop() {
    auto& worker=GetInstance().worker;
    worker.request_stop();
    if(worker.joinable()) worker.join();
    Cache::StopBackgroundWork();
}

ProcessModule Engine::GetClient() {
    return GetInstance().client;
}

ProcessModule Engine::GetEngine() {
    return GetInstance().engine;
}

std::shared_ptr<pProcess> Engine::GetProcess() {
    return GetInstance().process;
}

bool Engine::InitImpl() {
    process = std::make_shared<pProcess>();

    if (!this->AwaitProcess()) {
        LOGF(FATAL, "Could not find process, please make sure the game is open");
        return false;
    }

    if (!this->AwaitModules()) {
        LOGF(FATAL, "Game took too long to load, please open me again once its fully loaded");
        return false;
    }

    if (!Dumper::Init()) {
        LOGF(FATAL, "Failed to dump game offsets");
        return false;
    }	if (!Config::Read())
		LOGF(WARNING, "Failed to parse config, using default values");

	MapRaytrace::Init();
	MapExtractor::Init();



#ifdef _DEBUG
    if (!cfg::dev::console)
        LogHelper::Free();
#endif

    worker=std::jthread([this](std::stop_token stop) { Thread(stop); });

    LOGF(INFO, "Successfully initialized engine...");
    return true;
}

void Engine::Thread(std::stop_token stop) {
    while (!stop.stop_requested()) {
        auto start = steady_clock::now();

        if(!Cache::Refresh()) {
            // Never drive features using a failed/stale refresh, or busy-spin
            // after a disconnect. Cache publishes the failure for the UI.
            std::this_thread::sleep_until(start + 20ms);
            continue;
        }
        std::this_thread::sleep_until(start + (cfg::settings::free_cpu ? 1000us : 250us));
    }
}

bool Engine::AwaitProcess() {
    if (!process || process->handle_) // Process not initialized, or already attached
        return false;

    do {
        if (process->AttachProcess("cs2.exe"))
            break;

        if (process->pid_ && !process->handle_) {
            LOGF(FATAL, "Windows denied read access to cs2.exe. Run at the same privilege level as the game.");
            return false;
        }

        static int attempts = 0;

        if (!attempts)
            LOGF(INFO, "Waiting 50s for the game to open...");

        if (attempts > 10)
            return false;
        attempts++;

        std::this_thread::sleep_for(5s);
    } while (true);

    return true;
}

bool Engine::AwaitModules() {
    if (!process || !process->handle_) // Process not initialized, or not attached
        return false;

    LOGF(INFO, "Waiting for the game to open...");

    do {
		this->client = process->GetModule("client.dll");
		this->engine = process->GetModule("engine2.dll");

        if (this->client.base && this->engine.base)
            break;

        static int attempts = 0;
        if (attempts > 10)
            return false;
        attempts++;

        std::this_thread::sleep_for(5s);
    } while (true);

    return true;
}
