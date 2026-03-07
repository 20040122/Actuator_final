#include "coordinator/coordinator.h"
#include <iostream>
#include <memory>
#include <csignal>
#include <atomic>

using namespace coordinator;

static std::atomic<bool> g_running(true);
static std::shared_ptr<Coordinator> g_coordinator;

void signalHandler(int signal) {
    std::cout << "\n[Main] Received signal " << signal << ", shutting down..." << std::endl;
    g_running.store(false);
    if (g_coordinator) {
        g_coordinator->stop();
    }
}

int main(int argc, char* argv[]) {
    std::cout << "========================================" << std::endl;
    std::cout << "             多星执行系统                " << std::endl;
    std::cout << "========================================" << std::endl;
    
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    
    CoordinatorConfig config = CoordinatorConfig::getDefault();
    config.coordinator_id = "COORD_MAIN";
    config.loop_interval_ms = 100;
    
    g_coordinator = std::make_shared<Coordinator>(config);
    
    if (!g_coordinator->initialize("src/input/global.json", "src/input/schedule.json")) {
        std::cerr << "\n[Main] ERROR: 协调器初始化失败!" << std::endl;
        return -1;
    }

    g_coordinator->run();
    g_coordinator->shutdown();
    
    std::cout << "\n[Main] 系统成功退出。" << std::endl;
    return 0;
}
