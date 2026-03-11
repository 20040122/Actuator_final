#include "coordinator.h"
#include "../core/logger.h"
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

bool parseBatchTaskAssignPayload(const std::vector<uint8_t>& payload, BatchTaskAssignMessage& out_msg) {
    try {
        const std::string payload_str(payload.begin(), payload.end());
        json j = json::parse(payload_str);
        out_msg.message_id = j.value("message_id", "");
        out_msg.satellite_id = j.value("satellite_id", "");
        out_msg.node_id = j.value("node_id", "");
        out_msg.plan_id = j.value("plan_id", "");
        out_msg.timestamp = j.value("timestamp", static_cast<uint64_t>(0));
        out_msg.require_ack = j.value("require_ack", false);
        out_msg.ack_timeout_ms = j.value("ack_timeout_ms", static_cast<uint64_t>(0));

        out_msg.scheduled_tasks.clear();
        if (j.contains("scheduled_tasks")) {
            for (const auto& task_j : j["scheduled_tasks"]) {
                TaskAssignMessage task;
                task.segment_id = task_j.value("segment_id", "");
                task.task_id = task_j.value("task_id", "");
                task.task_name = task_j.value("task_name", "");
                task.profit = task_j.value("profit", 0);
                task.behavior_ref = task_j.value("behavior_ref", "");

                if (task_j.contains("window")) {
                    const auto& win = task_j["window"];
                    task.window.window_id = win.value("window_id", "");
                    task.window.window_seq = win.value("window_seq", 0);
                    task.window.start = win.value("start", "");
                    task.window.end = win.value("end", "");
                }

                if (task_j.contains("execution")) {
                    const auto& exec = task_j["execution"];
                    task.execution.planned_start = exec.value("planned_start", "");
                    task.execution.planned_end = exec.value("planned_end", "");
                    task.execution.duration_s = exec.value("duration_s", 0);
                }

                if (task_j.contains("behavior_params")) {
                    for (auto it = task_j["behavior_params"].begin(); it != task_j["behavior_params"].end(); ++it) {
                        task.behavior_params[it.key()] = jsonValueToString(it.value());
                    }
                }

                out_msg.scheduled_tasks.push_back(task);
            }
        }
    } catch (const std::exception&) {
        return false;
    }
    return true;
}

std::vector<uint8_t> serializeBatchTaskAssignAckPayload(const BatchTaskAssignAck& ack) {
    json j;
    j["message_id"] = ack.message_id;
    j["satellite_id"] = ack.satellite_id;
    j["node_id"] = ack.node_id;
    j["timestamp"] = ack.timestamp;
    j["accepted"] = ack.accepted;
    j["accepted_task_ids"] = ack.accepted_task_ids;
    j["rejected_task_ids"] = ack.rejected_task_ids;
    j["rejection_reasons"] = ack.rejection_reasons;
    const std::string payload_str = j.dump();
    return std::vector<uint8_t>(payload_str.begin(), payload_str.end());
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

std::vector<uint8_t> serializeTaskProgressPayload(const TaskProgressMessage& progress) {
    json j;
    j["segment_id"] = progress.segment_id;
    j["task_id"] = progress.task_id;
    j["status"] = static_cast<int>(progress.status);
    j["progress_percent"] = progress.progress_percent;
    j["current_action"] = progress.current_action;
    j["message"] = progress.message;
    j["output_vars"] = progress.output_vars;
    const std::string payload_str = j.dump();
    return std::vector<uint8_t>(payload_str.begin(), payload_str.end());
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

std::vector<uint8_t> serializeTaskCompletePayload(const TaskCompleteMessage& complete) {
    json j;
    j["segment_id"] = complete.segment_id;
    j["task_id"] = complete.task_id;
    j["success"] = complete.success;
    j["actual_start"] = complete.actual_start;
    j["actual_end"] = complete.actual_end;
    j["actual_profit"] = complete.actual_profit;
    j["result_summary"] = complete.result_summary;
    j["output_data"] = complete.output_data;
    const std::string payload_str = j.dump();
    return std::vector<uint8_t>(payload_str.begin(), payload_str.end());
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

TaskSegment taskAssignToSegment(const TaskAssignMessage& task, const std::string& satellite_id) {
    TaskSegment segment;
    segment.segment_id = task.segment_id;
    segment.task_id = task.task_id;
    segment.satellite_id = satellite_id;
    segment.behavior_ref = task.behavior_ref;
    segment.behavior_params = task.behavior_params;
    segment.execution.planned_start = task.execution.planned_start;
    segment.execution.planned_end = task.execution.planned_end;
    segment.execution.duration_s = task.execution.duration_s;
    segment.window.window_id = task.window.window_id;
    segment.window.window_seq = task.window.window_seq;
    segment.window.start = task.window.start;
    segment.window.end = task.window.end;
    return segment;
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

bool Coordinator::initialize(const std::string& global_config_file, const std::string& schedule_file) {
    if (initialized_.load()) {
        return true;
    }
    
    std::cout << "\n[Coordinator] ===== 初始化阶段 =====" << std::endl;
    
    // Step 1: 加载全局配置
    std::cout << "[Coordinator] Step 1: 加载全局配置..." << std::endl;
    if (!loadGlobalConfig(global_config_file)) {
        std::cerr << "[Coordinator] ERROR: Failed to load global configuration!" << std::endl;
        return false;
    }
    std::cout << "[Coordinator] ✓ 全局配置加载完成 (计划ID: " << global_config_.plan_id << ")" << std::endl;
    
    // Step 2: 加载调度任务
    std::cout << "[Coordinator] Step 2: 加载调度任务" << std::endl;
    schedule_file_path_ = schedule_file;
    if (!loadSchedule(schedule_file)) {
        std::cerr << "[Coordinator] ERROR: Failed to load schedule!" << std::endl;
        return false;
    }
    std::cout << "[Coordinator] ✓ 调度任务加载完成 (" << schedule_.satellite_ids.size() << " 颗卫星)" << std::endl;
    
    // Step 3: 初始化节点注册表
    std::cout << "[Coordinator] Step 3: 初始化节点注册表" << std::endl;
    node_registry_ = std::make_shared<NodeRegistry>(30000);
    if (!node_registry_->initialize()) {
        std::cerr << "[Coordinator] ERROR: Node registry initialization failed!" << std::endl;
        return false;
    }
    std::cout << "[Coordinator] ✓ 节点注册表初始化完成" << std::endl;
    
    // Step 4: 注册卫星节点
    std::cout << "[Coordinator] Step 4: 注册卫星节点" << std::endl;
    if (!registerSatelliteNodes(schedule_)) {
        std::cerr << "[Coordinator] ERROR: Failed to register satellite nodes!" << std::endl;
        return false;
    }
    
    // Step 5: 初始化通信模块
    std::cout << "[Coordinator] Step 5: 初始化通信模块" << std::endl;
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
    
    // Step 6: 初始化信号量管理器
    std::cout << "[Coordinator] Step 6: 初始化信号量管理器" << std::endl;
    semaphore_mgr_ = std::make_shared<DistributedSemaphore>();
    if (!semaphore_mgr_->initialize(comm_, config_.coordinator_id)) {
        std::cerr << "[Coordinator] ERROR: Semaphore manager initialization failed!" << std::endl;
        return false;
    }
    initializeSemaphores();
    std::cout << "[Coordinator] ✓ 信号量管理器初始化完成" << std::endl;
    
    std::cout << "[Coordinator] Step 7: 初始化执行器" << std::endl;
    initializeExecutors();
    std::cout << "[Coordinator] ✓ 执行器初始化完成 (" << executors_.size() << " 个执行器)" << std::endl;

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
    
    if (semaphore_mgr_) {
        semaphore_mgr_->clear();
    }
    
    {
        std::lock_guard<std::mutex> lock(executors_mutex_);
        for (auto& pair : executors_) {
            if (pair.second) {
                pair.second->shutdown();
            }
        }
        executors_.clear();
        std::cout << "[Coordinator] ✓ 所有执行器已关闭" << std::endl;
    }
    
    initialized_.store(false);
    std::cout << "[Coordinator] ✓ 协调器已关闭" << std::endl;
}

bool Coordinator::loadGlobalConfig(const std::string& config_file) {
    GlobalConfigParser parser;
    global_config_ = parser.parse(config_file);
    
    if (global_config_.plan_id.empty()) {
        return false;
    }
    
    return true;
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
    
    LOG("\n[Coordinator] Step 8: 执行任务");

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
    satellite_message_router_.clear();

    coordinator_message_router_[MessageType::BATCH_TASK_ASSIGN_ACK] = [this](const Message& message) {
        onCoordinatorBatchAck(message);
    };
    coordinator_message_router_[MessageType::TASK_PROGRESS] = [this](const Message& message) {
        onCoordinatorTaskProgress(message);
    };
    coordinator_message_router_[MessageType::TASK_COMPLETE] = [this](const Message& message) {
        onCoordinatorTaskComplete(message);
    };

    satellite_message_router_[MessageType::BATCH_TASK_ASSIGN] =
        [this](const std::string& satellite_id, const Message& message) {
            onSatelliteBatchTaskAssign(satellite_id, message);
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
        comm_->registerLocalHandler(sat_id, [this, sat_id](const Message& message) {
            handleSatelliteMessage(sat_id, message);
        });
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

    {
        std::lock_guard<std::mutex> lock(task_status_mutex_);
        satellite_task_finished_[ack.satellite_id] = true;
        satellite_task_results_[ack.satellite_id] = ack.accepted;
    }
    task_status_cv_.notify_all();

    {
        std::ostringstream oss;
        oss << "[Coordinator] 收到ACK: " << ack.satellite_id
            << " -> " << (ack.accepted ? "SUCCESS" : "FAILED");
        LOG(oss.str());
    }
}

void Coordinator::onCoordinatorTaskProgress(const Message& message) {
    TaskProgressMessage progress;
    if (!parseTaskProgressPayload(message.payload, progress)) {
        LOG_ERROR("[Coordinator] ERROR: 无法解析 TASK_PROGRESS payload");
        return;
    }

    std::ostringstream oss;
    oss << "[Coordinator] 进度上报: sat=" << message.header.source_node_id
        << ", task=" << progress.task_id
        << ", segment=" << progress.segment_id
        << ", status=" << taskStatusToString(progress.status)
        << ", progress=" << static_cast<int>(progress.progress_percent) << "%";
    if (!progress.current_action.empty()) {
        oss << ", action=" << progress.current_action;
    }
    if (!progress.message.empty()) {
        oss << ", msg=" << progress.message;
    }
    LOG(oss.str());
}

void Coordinator::onCoordinatorTaskComplete(const Message& message) {
    TaskCompleteMessage complete;
    if (!parseTaskCompletePayload(message.payload, complete)) {
        LOG_ERROR("[Coordinator] ERROR: 无法解析 TASK_COMPLETE payload");
        return;
    }

    std::ostringstream oss;
    oss << "[Coordinator] 完成上报: sat=" << message.header.source_node_id
        << ", task=" << complete.task_id
        << ", segment=" << complete.segment_id
        << ", result=" << (complete.success ? "SUCCESS" : "FAILED");
    if (!complete.result_summary.empty()) {
        oss << ", summary=" << complete.result_summary;
    }
    LOG(oss.str());
}

void Coordinator::handleSatelliteMessage(const std::string& satellite_id, const Message& message) {
    auto it = satellite_message_router_.find(message.header.msg_type);
    if (it == satellite_message_router_.end()) {
        std::ostringstream oss;
        oss << "[Coordinator] WARN: 未注册的卫星消息类型: "
            << messageTypeToString(message.header.msg_type)
            << " satellite=" << satellite_id;
        LOG(oss.str());
        return;
    }
    it->second(satellite_id, message);
}

void Coordinator::onSatelliteBatchTaskAssign(const std::string& satellite_id, const Message& message) {
    BatchTaskAssignMessage batch_msg;
    BatchTaskAssignAck ack;
    ack.message_id = "";
    ack.satellite_id = satellite_id;
    ack.node_id = satellite_id;
    ack.timestamp = getCurrentTimeMs();
    ack.accepted = false;

    if (!parseBatchTaskAssignPayload(message.payload, batch_msg)) {
        ack.rejection_reasons["payload"] = "invalid batch task payload";
    } else {
        ack.message_id = batch_msg.message_id;
        int task_total = static_cast<int>(batch_msg.scheduled_tasks.size());
        int completed = 0;
        bool all_success = true;

        for (const auto& task_msg : batch_msg.scheduled_tasks) {
            TaskSegment task = taskAssignToSegment(task_msg, satellite_id);

            TaskProgressMessage progress;
            progress.segment_id = task.segment_id;
            progress.task_id = task.task_id;
            progress.status = TaskStatus::RUNNING;
            progress.progress_percent = static_cast<uint8_t>((completed * 100) / std::max(1, task_total));
            progress.current_action = "execute_task";
            progress.message = "task started";

            Message progress_msg;
            progress_msg.header.magic = MessageHeader::MAGIC_NUMBER;
            progress_msg.header.version = MessageHeader::PROTOCOL_VERSION;
            progress_msg.header.msg_type = MessageType::TASK_PROGRESS;
            progress_msg.header.source_node_id = satellite_id;
            progress_msg.header.dest_node_id = config_.coordinator_id;
            progress_msg.header.priority = Priority::NORMAL;
            progress_msg.header.timestamp_ms = getCurrentTimeMs();
            progress_msg.payload = serializeTaskProgressPayload(progress);
            progress_msg.header.payload_size = static_cast<uint32_t>(progress_msg.payload.size());
            progress_msg.header.checksum = 0;
            comm_->sendMessage(config_.coordinator_id, progress_msg);

            bool task_ok = executeTasksForSatellite(satellite_id, std::vector<TaskSegment>{task});
            completed++;
            all_success = all_success && task_ok;

            TaskCompleteMessage complete;
            complete.segment_id = task.segment_id;
            complete.task_id = task.task_id;
            complete.success = task_ok;
            complete.actual_start = task.execution.planned_start;
            complete.actual_end = task.execution.planned_end;
            complete.actual_profit = 0;
            complete.result_summary = task_ok ? "task completed" : "task failed";

            Message complete_msg;
            complete_msg.header.magic = MessageHeader::MAGIC_NUMBER;
            complete_msg.header.version = MessageHeader::PROTOCOL_VERSION;
            complete_msg.header.msg_type = MessageType::TASK_COMPLETE;
            complete_msg.header.source_node_id = satellite_id;
            complete_msg.header.dest_node_id = config_.coordinator_id;
            complete_msg.header.priority = task_ok ? Priority::NORMAL : Priority::URGENT;
            complete_msg.header.timestamp_ms = getCurrentTimeMs();
            complete_msg.payload = serializeTaskCompletePayload(complete);
            complete_msg.header.payload_size = static_cast<uint32_t>(complete_msg.payload.size());
            complete_msg.header.checksum = 0;
            comm_->sendMessage(config_.coordinator_id, complete_msg);

            if (task_ok) {
                ack.accepted_task_ids.push_back(task.task_id);
            } else {
                ack.rejected_task_ids.push_back(task.task_id);
                ack.rejection_reasons[task.task_id] = "task execution failed";
            }
        }

        ack.accepted = all_success;
    }

    Message ack_message;
    ack_message.header.magic = MessageHeader::MAGIC_NUMBER;
    ack_message.header.version = MessageHeader::PROTOCOL_VERSION;
    ack_message.header.msg_type = MessageType::BATCH_TASK_ASSIGN_ACK;
    ack_message.header.source_node_id = satellite_id;
    ack_message.header.dest_node_id = config_.coordinator_id;
    ack_message.header.priority = Priority::NORMAL;
    ack_message.header.timestamp_ms = getCurrentTimeMs();
    ack_message.payload = serializeBatchTaskAssignAckPayload(ack);
    ack_message.header.payload_size = static_cast<uint32_t>(ack_message.payload.size());
    ack_message.header.checksum = 0;

    comm_->sendMessage(config_.coordinator_id, ack_message);
}

bool Coordinator::registerSatelliteNodes(const ScheduleParser::MultiSatSchedule& schedule) {
    int registered_count = 0;
    for (const auto& sat_info : schedule.satellites) {
        NodeRegisterMessage register_msg;
        register_msg.node_id = sat_info.satellite_id;
        register_msg.node_name = sat_info.name;
        register_msg.node_type = "SATELLITE";
        register_msg.ip_address = "";
        register_msg.port = 0;
        
        NodeCapability imaging_cap;
        imaging_cap.capability_id = "imaging";
        imaging_cap.description = "Imaging payload capability";
        imaging_cap.params["payload_status"] = sat_info.payload_status;
        register_msg.capabilities.push_back(imaging_cap);
        
        register_msg.metadata["node_id"] = sat_info.node_id;
        register_msg.metadata["status"] = sat_info.status;
        register_msg.metadata["battery_percent"] = std::to_string(sat_info.battery_percent);
        register_msg.metadata["storage_available_mb"] = std::to_string(sat_info.storage_available_mb);
        register_msg.metadata["thermal_status"] = sat_info.thermal_status;
        
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
        
        std::cout << "\n[Coordinator] === 卫星 " << sat_id << " (" << node_it->name << ") ===" << std::endl;
        std::cout << "[Coordinator]   节点ID: " << node_it->node_id << std::endl;
        std::cout << "[Coordinator]   任务数量: " << tasks.size() << std::endl;
        std::cout << "[Coordinator]   载荷状态: " << node_it->payload_status << std::endl;
        std::cout << "[Coordinator]   电池电量: " << node_it->battery_percent << "%" << std::endl;
        std::cout << "[Coordinator]   可用存储: " << node_it->storage_available_mb << " MB" << std::endl;
        
        BatchTaskAssignMessage msg;
        msg.message_id = "BATCH_" + sat_id + "_" + std::to_string(getCurrentTimeMs());
        msg.satellite_id = sat_id;
        msg.node_id = node_it->node_id;
        msg.plan_id = schedule.plan_id;
        msg.timestamp = getCurrentTimeMs();
        
        std::cout << "[Coordinator]   调度任务列表:" << std::endl;
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
            
            task_msg.execution.planned_start = task.execution.planned_start;
            task_msg.execution.planned_end = task.execution.planned_end;
            task_msg.execution.duration_s = task.execution.duration_s;
            
            task_msg.window.window_id = task.window.window_id;
            task_msg.window.window_seq = task.window.window_seq;
            task_msg.window.start = task.window.start;
            task_msg.window.end = task.window.end;
            
            msg.scheduled_tasks.push_back(task_msg);
            
            // 打印详细任务信息
            std::cout << "[Coordinator]     [" << task_num << "] 任务ID: " << task.task_id << std::endl;
            std::cout << "[Coordinator]         段ID: " << task.segment_id << std::endl;
            std::cout << "[Coordinator]         行为: " << task.behavior_ref << std::endl;
            std::cout << "[Coordinator]         时间窗: " << task.window.start << " ~ " << task.window.end << std::endl;
            std::cout << "[Coordinator]         执行: " << task.execution.planned_start << " ~ " 
                      << task.execution.planned_end << " (时长: " << task.execution.duration_s << "s)" << std::endl;
            
            // 打印行为参数
            if (!task.behavior_params.empty()) {
                std::cout << "[Coordinator]         参数: ";
                bool first = true;
                for (const auto& param : task.behavior_params) {
                    if (!first) std::cout << ", ";
                    std::cout << param.first << "=" << param.second;
                    first = false;
                }
                std::cout << std::endl;
            }
        }
        
        Message message;
        message.header.msg_type = MessageType::BATCH_TASK_ASSIGN;
        message.header.source_node_id = config_.coordinator_id;
        message.header.dest_node_id = sat_id;
        message.header.priority = Priority::NORMAL;
        message.header.timestamp_ms = getCurrentTimeMs();
        message.payload = MessageSerializer::serializeBatchTaskAssign(msg);
        
        if (comm_->sendMessage(sat_id, message)) {
            std::cout << "[Coordinator]   ✓ 任务分发成功: " << tasks.size() << " 个任务已发送到 " << sat_id << std::endl;
            success_count += tasks.size();
        } else {
            std::cerr << "[Coordinator]   ✗ 任务分发失败: " << sat_id << std::endl;
        }
    }
    
    return success_count == total_tasks;
}

void Coordinator::initializeSemaphores() {
    if (!semaphore_mgr_) {
        return;
    }
    
    for (const auto& sem_cfg : global_config_.semaphores) {
        DistributedSemaphoreConfig config;
        config.semaphore_id = sem_cfg.semaphore_id;
        config.resource_name = sem_cfg.resource_name;
        config.max_permits = sem_cfg.max_permits;
        config.available_permits = sem_cfg.available_permits;
        config.default_timeout_s = sem_cfg.timeout_s;
        config.priority_enabled = sem_cfg.priority_enabled;
        
        if (sem_cfg.resource_type == "GROUND_STATION") {
            config.resource_type = ResourceType::GROUND_STATION;
        } else if (sem_cfg.resource_type == "RELAY_SATELLITE") {
            config.resource_type = ResourceType::RELAY_SATELLITE;
        } else if (sem_cfg.resource_type == "OBSERVATION_TARGET") {
            config.resource_type = ResourceType::OBSERVATION_TARGET;
        } else {
            config.resource_type = ResourceType::UNKNOWN;
        }
        
        if (sem_cfg.queue_policy == "FIFO") {
            config.queue_policy = SemaphoreQueuePolicy::FIFO;
        } else if (sem_cfg.queue_policy == "PRIORITY") {
            config.queue_policy = SemaphoreQueuePolicy::PRIORITY;
        } else if (sem_cfg.queue_policy == "DEADLINE") {
            config.queue_policy = SemaphoreQueuePolicy::DEADLINE;
        } else {
            config.queue_policy = SemaphoreQueuePolicy::FIFO;
        }
        
        semaphore_mgr_->registerSemaphore(config);
    }
    
    if (!global_config_.semaphores.empty()) {
        std::cout << "[Coordinator] ✓ 注册 " << global_config_.semaphores.size() << " 个信号量" << std::endl;
    }
}

void Coordinator::initializeExecutors() {
    std::lock_guard<std::mutex> lock(executors_mutex_);
    
    for (const auto& sat_info : schedule_.satellites) {
        executor::ExecutorConfig exec_config = executor::ExecutorConfig::getDefault(sat_info.satellite_id);
        exec_config.async_mode = false;
        exec_config.debug_mode = false;
        exec_config.max_concurrent_tasks = 1;
        
        auto executor = std::make_shared<executor::GenericExecutor>(exec_config);
        if (executor->initialize()) {
            executor->setDistributedSemaphore(semaphore_mgr_);
            try {
                executor->getVariableManager().loadFromScheduleConfig(
                    schedule_file_path_, 
                    sat_info.satellite_id
                );
            } catch (const std::exception& e) {
                std::cerr << "[Coordinator]   ⚠ 加载卫星状态失败: " << e.what() << std::endl;
            }
            
            executors_[sat_info.satellite_id] = executor;
            std::cout << "[Coordinator]   ✓ 执行器已创建: " << sat_info.satellite_id << std::endl;
        } else {
            std::cerr << "[Coordinator]   ✗ 执行器初始化失败: " << sat_info.satellite_id << std::endl;
        }
    }
}

bool Coordinator::executeTasksForSatellite(const std::string& satellite_id, const std::vector<TaskSegment>& tasks) {
    std::shared_ptr<executor::GenericExecutor> executor;
    {
        std::lock_guard<std::mutex> lock(executors_mutex_);
        auto it = executors_.find(satellite_id);
        if (it == executors_.end()) {
            LOG_ERROR("[Coordinator] ERROR: 未找到执行器: " + satellite_id);
            return false;
        }
        executor = it->second;
    }
    
    auto node_it = std::find_if(schedule_.satellites.begin(), schedule_.satellites.end(),
        [&satellite_id](const SatelliteInfo& info) { return info.satellite_id == satellite_id; });
    
    if (node_it == schedule_.satellites.end()) {
        LOG_ERROR("[Coordinator] ERROR: 未找到卫星信息: " + satellite_id);
        return false;
    }
    
    {
        std::ostringstream oss;
        oss << "\n[Coordinator] === 执行卫星任务: " << satellite_id << " (" << node_it->name << ") ===";
        oss << "\n[Coordinator]   任务数量: " << tasks.size();
        oss << "\n[Coordinator]   载荷状态: " << node_it->payload_status;
        oss << "\n[Coordinator]   电池电量: " << node_it->battery_percent << "%";
        LOG(oss.str());
    }
    
    BehaviorLibraryParser behavior_parser;
    int success_count = 0;
    int task_num = 0;
    
    for (const auto& task : tasks) {
        task_num++;
        {
            std::ostringstream oss;
            oss << "\n[Coordinator]   [" << task_num << "/" << tasks.size() << "] 执行任务: " << task.task_id;
            oss << "\n[Coordinator]       段ID: " << task.segment_id;
            oss << "\n[Coordinator]       行为: " << task.behavior_ref;
            oss << "\n[Coordinator]       时间窗: " << task.window.start << " ~ " << task.window.end;
            LOG(oss.str());
        }
        
        BehaviorNode behavior;
        auto cache_it = behavior_cache_.find(task.behavior_ref);
        if (cache_it != behavior_cache_.end()) {
            behavior = cache_it->second;
        } else {
            try {
                behavior = behavior_parser.parseBehaviorDefinition("src/input/behaviorTree.json", task.behavior_ref);
                behavior_cache_[task.behavior_ref] = behavior;
            } catch (const std::exception& e) {
                LOG_ERROR(std::string("[Coordinator]       ✗ 行为加载失败: ") + e.what());
                continue;
            }
        }
        
        auto result = executor->executeTask(task, behavior);
        
        {
            std::ostringstream oss;
            if (result.success) {
                oss << "[Coordinator]       ✓ 任务执行成功 (耗时: " << result.execution_time_ms << "ms)";
                if (!result.outputs.empty()) {
                    oss << "\n[Coordinator]       输出参数:";
                    for (const auto& out : result.outputs) {
                        oss << "\n[Coordinator]         " << out.first << " = " << out.second.asString();
                    }
                }
                LOG(oss.str());
                success_count++;
            } else {
                oss << "[Coordinator]       ✗ 任务执行失败: " << result.message;
                if (!result.failed_node.empty()) {
                    oss << "\n[Coordinator]       失败节点: " << result.failed_node;
                }
                LOG_ERROR(oss.str());
            }
        }
    }
    
    {
        std::ostringstream oss;
        oss << "[Coordinator]   卫星任务执行完成: " << success_count << "/" << tasks.size() << " 成功";
        LOG(oss.str());
    }
    return success_count == tasks.size();
}

uint64_t Coordinator::getCurrentTimeMs() const {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

}
