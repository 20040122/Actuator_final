#include "coordinator.h"
#include "../core/logger.h"
#include "../executor/satellite_simulator.h"
#include "../third_party/nlohmann/json.hpp"
#include <iostream>
#include <chrono>
#include <thread>
#include <algorithm>
#include <sstream>

namespace coordinator {

using json = nlohmann::json;

namespace {

std::string jsonValueToString(const json& value) {
    if (value.is_string()) {
        return value.get<std::string>();
    }
    if (value.is_number_integer()) {
        return std::to_string(value.get<int>());
    }
    if (value.is_number_float()) {
        std::ostringstream oss;
        oss << value.get<double>();
        return oss.str();
    }
    if (value.is_boolean()) {
        return value.get<bool>() ? "true" : "false";
    }
    return value.dump();
}

bool parseBatchTaskAssignAckPayload(const std::vector<uint8_t>& payload, BatchTaskAssignAck& ack) {
    try {
        const std::string payload_str(payload.begin(), payload.end());
        json j = json::parse(payload_str);
        ack.message_id = j.value("message_id", "");
        ack.satellite_id = j.value("satellite_id", "");
        ack.node_id = j.value("node_id", "");
        ack.timestamp = j.value("timestamp", static_cast<uint64_t>(0));
        ack.accepted = j.value("accepted", false);
        ack.accepted_task_ids.clear();
        ack.rejected_task_ids.clear();
        ack.rejection_reasons.clear();
        if (j.contains("accepted_task_ids")) {
            for (const auto& id : j["accepted_task_ids"]) {
                ack.accepted_task_ids.push_back(id.get<std::string>());
            }
        }
        if (j.contains("rejected_task_ids")) {
            for (const auto& id : j["rejected_task_ids"]) {
                ack.rejected_task_ids.push_back(id.get<std::string>());
            }
        }
        if (j.contains("rejection_reasons")) {
            for (auto it = j["rejection_reasons"].begin(); it != j["rejection_reasons"].end(); ++it) {
                ack.rejection_reasons[it.key()] = jsonValueToString(it.value());
            }
        }
    } catch (const std::exception&) {
        return false;
    }
    return true;
}

bool parseTaskProgressPayload(const std::vector<uint8_t>& payload, TaskProgressMessage& progress) {
    try {
        const std::string payload_str(payload.begin(), payload.end());
        json j = json::parse(payload_str);
        progress.segment_id = j.value("segment_id", "");
        progress.task_id = j.value("task_id", "");
        progress.status = static_cast<TaskStatus>(j.value("status", static_cast<int>(TaskStatus::PENDING)));
        progress.progress_percent = static_cast<uint8_t>(j.value("progress_percent", 0));
        progress.current_action = j.value("current_action", "");
        progress.message = j.value("message", "");
        progress.output_vars.clear();
        if (j.contains("output_vars")) {
            for (auto it = j["output_vars"].begin(); it != j["output_vars"].end(); ++it) {
                progress.output_vars[it.key()] = jsonValueToString(it.value());
            }
        }
    } catch (const std::exception&) {
        return false;
    }
    return true;
}

bool parseTaskCompletePayload(const std::vector<uint8_t>& payload, TaskCompleteMessage& complete) {
    try {
        const std::string payload_str(payload.begin(), payload.end());
        json j = json::parse(payload_str);
        complete.segment_id = j.value("segment_id", "");
        complete.task_id = j.value("task_id", "");
        complete.success = j.value("success", false);
        complete.actual_start = j.value("actual_start", "");
        complete.actual_end = j.value("actual_end", "");
        complete.actual_profit = j.value("actual_profit", 0);
        complete.result_summary = j.value("result_summary", "");
        complete.output_data.clear();
        if (j.contains("output_data")) {
            for (auto it = j["output_data"].begin(); it != j["output_data"].end(); ++it) {
                complete.output_data[it.key()] = jsonValueToString(it.value());
            }
        }
    } catch (const std::exception&) {
        return false;
    }
    return true;
}

} // namespace

Coordinator::Coordinator(const CoordinatorConfig& config)
    : config_(config),
      running_(false),
      initialized_(false) {
}

Coordinator::~Coordinator() {
    shutdown();
}

bool Coordinator::initialize(const std::string& schedule_file) {
    if (initialized_.load()) {
        return true;
    }
    
    std::cout << "\n[Coordinator] ===== 初始化阶段 =====" << std::endl;
    
    // Step 1: 加载调度任务
    std::cout << "[Coordinator] Step 1: 加载调度任务" << std::endl;
    schedule_file_path_ = schedule_file;
    if (!loadSchedule(schedule_file)) {
        std::cerr << "[Coordinator] ERROR: Failed to load schedule!" << std::endl;
        return false;
    }
    std::cout << "[Coordinator] ✓ 调度任务加载完成 (" << schedule_.satellite_ids.size() << " 颗卫星)" << std::endl;
    
    // Step 2: 初始化节点注册表
    std::cout << "[Coordinator] Step 2: 初始化节点注册表" << std::endl;
    node_registry_ = std::make_shared<NodeRegistry>();
    if (!node_registry_->initialize()) {
        std::cerr << "[Coordinator] ERROR: Node registry initialization failed!" << std::endl;
        return false;
    }
    std::cout << "[Coordinator] ✓ 节点注册表初始化完成" << std::endl;
    
    // Step 3: 注册卫星节点
    std::cout << "[Coordinator] Step 3: 注册卫星节点" << std::endl;
    if (!registerSatelliteNodes(schedule_)) {
        std::cerr << "[Coordinator] ERROR: Failed to register satellite nodes!" << std::endl;
        return false;
    }
    
    // Step 4: 初始化通信模块
    std::cout << "[Coordinator] Step 4: 初始化通信模块" << std::endl;
    CommConfig comm_config = CommConfig::getDefault();
    comm_config.node_id = config_.coordinator_id;
    comm_config.node_name = "Main Coordinator";
    comm_config.node_type = "COORDINATOR";
    comm_config.bind_address = "0.0.0.0";
    comm_config.bind_port = 50000;
    
    comm_ = std::make_shared<InterSatComm>(comm_config);
    if (!comm_->initialize()) {
        std::cerr << "[Coordinator] ERROR: Communication module initialization failed!" << std::endl;
        return false;
    }
    std::cout << "[Coordinator] ✓ 通信模块初始化完成" << std::endl;
    
    std::cout << "[Coordinator] Step 5: 初始化卫星模拟器" << std::endl;
    initializeSimulators();
    std::cout << "[Coordinator] ✓ 卫星模拟器初始化完成 (" << simulators_.size() << " 个模拟器)" << std::endl;

    initializeMessageRouters();
    registerLocalMessageHandlers();
    
    std::cout << "\n[Coordinator] 已注册节点摘要:" << std::endl;
    auto node_ids = node_registry_->getAllNodeIds();
    for (const auto& node_id : node_ids) {
        NodeInfo info;
        if (node_registry_->getNodeInfo(node_id, info)) {
            std::cout << "  - " << node_id << " (" << info.node_name << ") - 状态: " 
                      << (info.is_online ? "在线" : "离线") << std::endl;
        }
    }
    
    std::cout << "\n[Coordinator] ===== 初始化完成 =====" << std::endl;
    
    initialized_.store(true);
    return true;
}

void Coordinator::shutdown() {
    if (!initialized_.load()) {
        return;
    }
    
    std::cout << "\n[Coordinator] ===== 关闭阶段 =====" << std::endl;
    
    stop();
    
    if (comm_) {
        unregisterLocalMessageHandlers();
        std::cout << "[Coordinator] 停止通信模块" << std::endl;
        comm_->stop();
        std::cout << "[Coordinator] ✓ 通信模块已停止" << std::endl;
    }
    
    if (node_registry_) {
        std::cout << "[Coordinator] 注销所有卫星节点" << std::endl;
        auto all_nodes = node_registry_->getAllNodeIds();
        for (const auto& node_id : all_nodes) {
            node_registry_->unregisterNode(node_id, "系统关闭");
        }
        node_registry_->shutdown();
        std::cout << "[Coordinator] ✓ 节点注册表已关闭" << std::endl;
    }
    
    for (auto& pair : simulators_) {
        if (pair.second) {
            pair.second->shutdown();
        }
    }
    simulators_.clear();
    std::cout << "[Coordinator] ✓ 所有卫星模拟器已关闭" << std::endl;
    
    initialized_.store(false);
    std::cout << "[Coordinator] ✓ 协调器已关闭" << std::endl;
}

bool Coordinator::loadSchedule(const std::string& schedule_file) {
    ScheduleParser parser;
    schedule_ = parser.parseAllSatellites(schedule_file);
    
    if (schedule_.satellite_ids.empty()) {
        return false;
    }
    
    return true;
}

void Coordinator::run() {
    if (!initialized_.load()) {
        LOG_ERROR("[Coordinator] ERROR: Not initialized!");
        return;
    }
    
    if (!comm_->start()) {
        LOG_ERROR("[Coordinator] ERROR: Failed to start communication module!");
        return;
    }

    running_.store(true);
    
    LOG("\n[Coordinator] Step 7: 执行任务");

    {
        std::lock_guard<std::mutex> lock(task_status_mutex_);
        satellite_task_results_.clear();
        satellite_task_finished_.clear();
        satellite_task_expected_counts_.clear();
        for (const auto& sat_id : schedule_.satellite_ids) {
            auto it = schedule_.satellite_tasks.find(sat_id);
            if (it == schedule_.satellite_tasks.end() || it->second.empty()) {
                continue;
            }
            satellite_task_results_[sat_id] = false;
            satellite_task_finished_[sat_id] = false;
            satellite_task_expected_counts_[sat_id] = static_cast<int>(it->second.size());
        }
    }

    if (!distributeAllTasks(schedule_)) {
        LOG_ERROR("[Coordinator] ERROR: 任务分发过程中存在失败");
    }

    std::unique_lock<std::mutex> lock(task_status_mutex_);
    bool finished = task_status_cv_.wait_for(
        lock,
        std::chrono::seconds(60),
        [this]() {
            if (satellite_task_finished_.empty()) {
                return true;
            }
            for (const auto& pair : satellite_task_finished_) {
                if (!pair.second) {
                    return false;
                }
            }
            return true;
        }
    );

    if (!finished) {
        LOG_ERROR("[Coordinator] WARN: 等待任务ACK超时，未完成的卫星将标记为失败");
        for (auto& pair : satellite_task_finished_) {
            if (!pair.second) {
                pair.second = true;
                satellite_task_results_[pair.first] = false;
            }
        }
    }

    int total_satellites = static_cast<int>(satellite_task_finished_.size());
    int success_satellites = 0;
    for (const auto& pair : satellite_task_results_) {
        if (pair.second) {
            success_satellites++;
        }
    }

    lock.unlock();

    {
        std::ostringstream oss;
        oss << "[Coordinator] ✓ 任务执行完成 (" << success_satellites << "/" << total_satellites << " 卫星)";
        LOG(oss.str());
    }

    running_.store(false);
    LOG("\n[Coordinator] 所有任务完成，退出运行。");
}

void Coordinator::stop() {
    running_.store(false);
}

void Coordinator::initializeMessageRouters() {
    coordinator_message_router_.clear();

    coordinator_message_router_[MessageType::BATCH_TASK_ASSIGN_ACK] = [this](const Message& message) {
        onCoordinatorBatchAck(message);
    };
    coordinator_message_router_[MessageType::TASK_PROGRESS] = [this](const Message& message) {
        onCoordinatorTaskProgress(message);
    };
    coordinator_message_router_[MessageType::TASK_COMPLETE] = [this](const Message& message) {
        onCoordinatorTaskComplete(message);
    };
}

void Coordinator::registerLocalMessageHandlers() {
    if (!comm_) {
        return;
    }

    comm_->registerLocalHandler(config_.coordinator_id, [this](const Message& message) {
        handleCoordinatorMessage(message);
    });

    for (const auto& sat_id : schedule_.satellite_ids) {
        auto sim_it = simulators_.find(sat_id);
        if (sim_it != simulators_.end()) {
            auto simulator = sim_it->second;
            comm_->registerLocalHandler(sat_id, [simulator](const Message& message) {
                simulator->handleMessage(message);
            });
        }
    }
}

void Coordinator::unregisterLocalMessageHandlers() {
    if (!comm_) {
        return;
    }

    comm_->unregisterLocalHandler(config_.coordinator_id);
    for (const auto& sat_id : schedule_.satellite_ids) {
        comm_->unregisterLocalHandler(sat_id);
    }
}

void Coordinator::handleCoordinatorMessage(const Message& message) {
    auto it = coordinator_message_router_.find(message.header.msg_type);
    if (it == coordinator_message_router_.end()) {
        std::ostringstream oss;
        oss << "[Coordinator] WARN: 未注册的协调器消息类型: "
            << messageTypeToString(message.header.msg_type);
        LOG(oss.str());
        return;
    }
    it->second(message);
}

void Coordinator::onCoordinatorBatchAck(const Message& message) {
    BatchTaskAssignAck ack;
    if (!parseBatchTaskAssignAckPayload(message.payload, ack)) {
        LOG_ERROR("[Coordinator] ERROR: 无法解析 BATCH_TASK_ASSIGN_ACK payload");
        return;
    }

    bool tracked_satellite = false;
    bool satellite_success = ack.accepted;
    int expected_task_count = 0;
    {
        std::lock_guard<std::mutex> lock(task_status_mutex_);
        auto expected_it = satellite_task_expected_counts_.find(ack.satellite_id);
        if (expected_it != satellite_task_expected_counts_.end()) {
            tracked_satellite = true;
            expected_task_count = expected_it->second;
            const int processed_task_count =
                static_cast<int>(ack.accepted_task_ids.size() + ack.rejected_task_ids.size());
            if (expected_task_count > 0 &&
                processed_task_count > 0 &&
                processed_task_count != expected_task_count) {
                satellite_success = false;
            }
            satellite_task_finished_[ack.satellite_id] = true;
            satellite_task_results_[ack.satellite_id] = satellite_success;
        }
    }

    if (!tracked_satellite) {
        LOG_WARN("[Coordinator] 收到未跟踪卫星的ACK: " + ack.satellite_id);
        return;
    }
    task_status_cv_.notify_all();

    {
        std::ostringstream oss;
        oss << "[Coordinator] 收到ACK: " << ack.satellite_id
            << " -> " << (satellite_success ? "SUCCESS" : "FAILED")
            << " (accepted=" << ack.accepted_task_ids.size()
            << ", rejected=" << ack.rejected_task_ids.size()
            << ", expected=" << expected_task_count << ")";
        LOG(oss.str());
    }
}

void Coordinator::onCoordinatorTaskProgress(const Message& message) {
    TaskProgressMessage progress;
    if (!parseTaskProgressPayload(message.payload, progress)) {
        LOG_ERROR("[Coordinator] ERROR: 无法解析 TASK_PROGRESS payload");
        return;
    }
}

void Coordinator::onCoordinatorTaskComplete(const Message& message) {
    TaskCompleteMessage complete;
    if (!parseTaskCompletePayload(message.payload, complete)) {
        LOG_ERROR("[Coordinator] ERROR: 无法解析 TASK_COMPLETE payload");
        return;
    }
}

bool Coordinator::registerSatelliteNodes(const ScheduleParser::MultiSatSchedule& schedule) {
    int registered_count = 0;
    for (const auto& sat_info : schedule.satellites) {
        const std::string comm_node_id = sat_info.node_id.empty()
            ? sat_info.satellite_id
            : sat_info.node_id;

        NodeRegisterMessage register_msg;
        register_msg.node_id = comm_node_id;
        register_msg.node_name = sat_info.name.empty() ? sat_info.satellite_id : sat_info.name;
        register_msg.node_type = "SATELLITE";
        register_msg.ip_address = "";
        register_msg.port = 0;
        
        NodeCapability imaging_cap;
        imaging_cap.capability_id = "imaging";
        imaging_cap.description = "Imaging payload capability";
        register_msg.capabilities.push_back(imaging_cap);
        
        register_msg.metadata["satellite_id"] = sat_info.satellite_id;
        register_msg.metadata["node_id"] = comm_node_id;
        
        std::string assigned_id;
        if (node_registry_->registerNode(register_msg, assigned_id)) {
            std::cout << "[Coordinator]   ✓ 注册卫星: " << sat_info.satellite_id 
                      << " (" << sat_info.name << ")" << std::endl;
            registered_count++;
        } else {
            std::cerr << "[Coordinator]   ✗ Failed to register satellite: " << sat_info.satellite_id << std::endl;
        }
    }
    std::cout << "[Coordinator] ✓ Satellite node registration complete (" 
              << registered_count << "/" << schedule.satellites.size() << " nodes)" << std::endl;
    
    return registered_count > 0;
}

bool Coordinator::distributeAllTasks(const ScheduleParser::MultiSatSchedule& schedule) {
    if (!comm_ || !node_registry_) {
        return false;
    }
    
    int total_tasks = 0;
    int success_count = 0;
    
    for (const auto& sat_id : schedule.satellite_ids) {
        auto it = schedule.satellite_tasks.find(sat_id);
        if (it == schedule.satellite_tasks.end() || it->second.empty()) {
            continue;
        }
        
        const auto& tasks = it->second;
        total_tasks += tasks.size();
        
        auto node_it = std::find_if(schedule.satellites.begin(), schedule.satellites.end(),
            [&sat_id](const SatelliteInfo& info) { return info.satellite_id == sat_id; });
        
        if (node_it == schedule.satellites.end()) {
            continue;
        }
        
        {
            std::ostringstream oss;
            oss << "[Coordinator] 分发任务 -> " << sat_id
                << " (" << node_it->name << "), tasks=" << tasks.size();
            LOG(oss.str());
        }
        
        BatchTaskAssignMessage msg;
        msg.message_id = "BATCH_" + sat_id + "_" + std::to_string(::getCurrentTimeMs());
        msg.satellite_id = sat_id;
        msg.node_id = node_it->node_id;
        msg.plan_id = "";
        msg.timestamp = ::getCurrentTimeMs();
        msg.require_ack = true;
        msg.ack_timeout_ms = 60000;
        
        int task_num = 0;
        for (const auto& task : tasks) {
            task_num++;
            TaskAssignMessage task_msg;
            task_msg.segment_id = task.segment_id;
            task_msg.task_id = task.task_id;
            task_msg.task_name = task.task_id;
            task_msg.behavior_ref = task.behavior_ref;
            task_msg.behavior_params = task.behavior_params;
            task_msg.priority = Priority::NORMAL;
            
            msg.scheduled_tasks.push_back(task_msg);

            std::ostringstream task_debug;
            task_debug << "[Coordinator][Dispatch] [" << task_num << "/" << tasks.size() << "] "
                       << "task=" << task.task_id
                       << ", segment=" << task.segment_id
                       << ", behavior=" << task.behavior_ref;
            if (!task.behavior_params.empty()) {
                task_debug << ", params={";
                bool first = true;
                for (const auto& param : task.behavior_params) {
                    if (!first) {
                        task_debug << ", ";
                    }
                    task_debug << param.first << "=" << param.second;
                    first = false;
                }
                task_debug << "}";
            }
            LOG_DEBUG(task_debug.str());
        }
        
        Message message;
        message.header.msg_type = MessageType::BATCH_TASK_ASSIGN;
        message.header.source_node_id = config_.coordinator_id;
        message.header.dest_node_id = sat_id;
        message.header.priority = Priority::NORMAL;
        message.header.timestamp_ms = ::getCurrentTimeMs();
        message.payload = MessageSerializer::serializeBatchTaskAssign(msg);
        
        if (comm_->sendMessage(sat_id, message)) {
            LOG("[Coordinator] 任务分发成功: " + sat_id + ", count=" + std::to_string(tasks.size()));
            success_count += tasks.size();
        } else {
            LOG_ERROR("[Coordinator] 任务分发失败: " + sat_id);
        }
    }
    
    return success_count == total_tasks;
}

void Coordinator::initializeSimulators() {
    for (const auto& sat_info : schedule_.satellites) {
        executor::SatelliteSimulatorConfig sim_config;
        sim_config.satellite_id = sat_info.satellite_id;
        sim_config.coordinator_id = config_.coordinator_id;
        sim_config.schedule_file_path = schedule_file_path_;

        auto simulator = std::make_shared<executor::SatelliteSimulator>(sim_config);
        if (simulator->initialize(comm_)) {
            simulators_[sat_info.satellite_id] = simulator;
            std::cout << "[Coordinator]   ✓ 卫星模拟器已创建: " << sat_info.satellite_id << std::endl;
        } else {
            std::cerr << "[Coordinator]   ✗ 卫星模拟器初始化失败: " << sat_info.satellite_id << std::endl;
        }
    }
}

}
