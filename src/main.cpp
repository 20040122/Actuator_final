#include "coordinator/coordinator.h"
#include "core/logger.h"
#include <iostream>
#include <memory>
#include <csignal>
#include <atomic>
#ifdef _WIN32
#include <windows.h>
#endif

using namespace coordinator;

static std::atomic<bool> g_running(true);
static std::shared_ptr<Coordinator> g_coordinator;

void signalHandler(int signal) {
    (void)signal;
    g_running.store(false);
    if (g_coordinator) {
        g_coordinator->stop();
    }
}

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    LOG("===== 多星执行系统 ===== ");
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    
    CoordinatorConfig config = CoordinatorConfig::getDefault();
    config.coordinator_id = "COORD_MAIN";
    config.loop_interval_ms = 100;
    
    g_coordinator = std::make_shared<Coordinator>(config);
    
    if (!g_coordinator->initialize("src/input/global.json", "src/input/schedule.json")) {
        LOG_ERROR("[Main] 协调器初始化失败!");
        return -1;
    }

    g_coordinator->run();
    g_coordinator->shutdown();
    
    LOG("[Main] 系统成功退出。");
    return 0;
}
