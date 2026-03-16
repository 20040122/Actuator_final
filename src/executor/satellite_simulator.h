#ifndef EXECUTOR_SATELLITE_SIMULATOR_H
#define EXECUTOR_SATELLITE_SIMULATOR_H

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <mutex>

#include "../core/types.h"
#include "../coordinator/inter_sat_comm.h"
#include "../coordinator/message_types.h"
#include "../parser/json_parser.h"
#include "../parser/behavior_parser.h"
#include "generic_executor.h"

namespace executor {

struct SatelliteSimulatorConfig {
    std::string satellite_id;
    std::string coordinator_id;
    std::string schedule_file_path;
    std::string behavior_tree_path;

    SatelliteSimulatorConfig()
        : behavior_tree_path("src/input/behaviorTree.json") {}
};

class SatelliteSimulator {
public:
    explicit SatelliteSimulator(const SatelliteSimulatorConfig& config);
    ~SatelliteSimulator();

    SatelliteSimulator(const SatelliteSimulator&) = delete;
    SatelliteSimulator& operator=(const SatelliteSimulator&) = delete;

    bool initialize(
        std::shared_ptr<coordinator::InterSatComm> comm
    );
    void shutdown();

    void handleMessage(const coordinator::Message& message);

    const std::string& satelliteId() const { return config_.satellite_id; }

private:
    void handleBatchTaskAssign(const coordinator::Message& message);
    bool executeSingleTask(const TaskSegment& task);

    SatelliteSimulatorConfig config_;
    std::shared_ptr<coordinator::InterSatComm> comm_;
    std::shared_ptr<GenericExecutor> executor_;

    std::map<std::string, BehaviorNode> behavior_cache_;
    std::mutex behavior_cache_mutex_;
};

} // namespace executor

#endif
