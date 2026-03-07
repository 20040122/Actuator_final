#ifndef COORDINATOR_COORDINATOR_H
#define COORDINATOR_COORDINATOR_H

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include <atomic>
#include <functional>
#include <cstdint>

#include "inter_sat_comm.h"
#include "message_types.h"
#include "distributed_semaphore.h"
#include "node_registry.h"
#include "../input/json_parser.h"
#include "../executor/generic_executor.h"
#include "../parser/behavior_parser.h"

namespace coordinator {

struct CoordinatorConfig {
    std::string coordinator_id;
    uint32_t loop_interval_ms;
    
    static CoordinatorConfig getDefault() {
        CoordinatorConfig cfg;
        cfg.coordinator_id = "COORDINATOR";
        cfg.loop_interval_ms = 100;
        return cfg;
    }
};

class Coordinator {
public:
    explicit Coordinator(const CoordinatorConfig& config = CoordinatorConfig::getDefault());
    ~Coordinator();
    
    Coordinator(const Coordinator&) = delete;
    Coordinator& operator=(const Coordinator&) = delete;
    
    bool initialize(const std::string& global_config_file, const std::string& schedule_file);
    void shutdown();
    
    void run();
    void stop();

private:
    bool loadGlobalConfig(const std::string& config_file);
    bool loadSchedule(const std::string& schedule_file);
    bool registerSatelliteNodes(const ScheduleParser::MultiSatSchedule& schedule);
    bool distributeAllTasks(const ScheduleParser::MultiSatSchedule& schedule);
    void initializeSemaphores();
    void initializeExecutors();
    bool executeTasksForSatellite(const std::string& satellite_id, const std::vector<TaskSegment>& tasks);
    uint64_t getCurrentTimeMs() const;

private:
    CoordinatorConfig config_;
    std::shared_ptr<InterSatComm> comm_;
    std::shared_ptr<NodeRegistry> node_registry_;
    std::shared_ptr<DistributedSemaphore> semaphore_mgr_;
    
    GlobalConfigParser::GlobalConfig global_config_;
    ScheduleParser::MultiSatSchedule schedule_;
    std::string schedule_file_path_;
    
    std::map<std::string, std::shared_ptr<executor::GenericExecutor>> executors_;
    std::map<std::string, BehaviorNode> behavior_cache_;
    std::mutex executors_mutex_;
    std::mutex console_mutex_;  // 用于控制台输出的互斥锁
    
    std::atomic<bool> running_;
    std::atomic<bool> initialized_;
};

}

#endif
