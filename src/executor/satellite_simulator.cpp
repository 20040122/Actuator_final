#include "satellite_simulator.h"
#include "../core/logger.h"
#include "../third_party/nlohmann/json.hpp"
#include <sstream>
#include <algorithm>

namespace executor {

using json = nlohmann::json;
using namespace coordinator;

namespace {

std::vector<uint8_t> jsonToPayload(const json& j) {
    const std::string s = j.dump();
    return std::vector<uint8_t>(s.begin(), s.end());
}

Message buildMessage(MessageType type, const std::string& source, const std::string& dest,
                     Priority priority, std::vector<uint8_t> payload) {
    Message msg;
    msg.header.magic = MessageHeader::MAGIC_NUMBER;
    msg.header.version = MessageHeader::PROTOCOL_VERSION;
    msg.header.msg_type = type;
    msg.header.source_node_id = source;
    msg.header.dest_node_id = dest;
    msg.header.priority = priority;
    msg.header.timestamp_ms = ::getCurrentTimeMs();
    msg.payload = std::move(payload);
    msg.header.payload_size = static_cast<uint32_t>(msg.payload.size());
    msg.header.checksum = 0;
    return msg;
}

std::string jsonValueToString(const json& value) {
    if (value.is_string()) return value.get<std::string>();
    if (value.is_number_integer()) return std::to_string(value.get<int>());
    if (value.is_number_float()) {
        std::ostringstream oss;
        oss << value.get<double>();
        return oss.str();
    }
    if (value.is_boolean()) return value.get<bool>() ? "true" : "false";
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

TaskSegment taskAssignToSegment(const TaskAssignMessage& task, const std::string& satellite_id) {
    TaskSegment segment;
    segment.segment_id = task.segment_id;
    segment.task_id = task.task_id;
    segment.satellite_id = satellite_id;
    segment.behavior_ref = task.behavior_ref;
    segment.behavior_params = task.behavior_params;
    return segment;
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
    return jsonToPayload(j);
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
    return jsonToPayload(j);
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
    return jsonToPayload(j);
}

} // anonymous namespace

SatelliteSimulator::SatelliteSimulator(const SatelliteSimulatorConfig& config)
    : config_(config) {
}

SatelliteSimulator::~SatelliteSimulator() {
    shutdown();
}

bool SatelliteSimulator::initialize(
    std::shared_ptr<InterSatComm> comm
) {
    comm_ = comm;

    ExecutorConfig exec_config = ExecutorConfig::getDefault(config_.satellite_id);
    exec_config.async_mode = false;
    exec_config.debug_mode = false;
    exec_config.max_concurrent_tasks = 1;

    executor_ = std::make_shared<GenericExecutor>(exec_config);
    if (!executor_->initialize()) {
        LOG_ERROR("[SatelliteSimulator] 执行器初始化失败: " + config_.satellite_id);
        return false;
    }

    if (!config_.schedule_file_path.empty()) {
        try {
            executor_->getVariableManager().loadFromScheduleConfig(
                config_.schedule_file_path,
                config_.satellite_id
            );
        } catch (const std::exception& e) {
            std::ostringstream oss;
            oss << "[SatelliteSimulator] ⚠ 加载卫星状态失败 (" << config_.satellite_id << "): " << e.what();
            LOG_WARN(oss.str());
        }
    }

    LOG("[SatelliteSimulator] ✓ 初始化完成: " + config_.satellite_id);
    return true;
}

void SatelliteSimulator::shutdown() {
    if (executor_) {
        executor_->shutdown();
        executor_.reset();
    }
    comm_.reset();
}

void SatelliteSimulator::handleMessage(const Message& message) {
    if (message.header.msg_type == MessageType::BATCH_TASK_ASSIGN) {
        handleBatchTaskAssign(message);
    } else {
        std::ostringstream oss;
        oss << "[SatelliteSimulator] WARN: 未处理的消息类型: "
            << messageTypeToString(message.header.msg_type)
            << " satellite=" << config_.satellite_id;
        LOG_WARN(oss.str());
    }
}

void SatelliteSimulator::handleBatchTaskAssign(const Message& message) {
    const std::string& sat_id = config_.satellite_id;
    const std::string& coord_id = config_.coordinator_id;

    BatchTaskAssignMessage batch_msg;
    BatchTaskAssignAck ack;
    ack.message_id = "";
    ack.satellite_id = sat_id;
    ack.node_id = sat_id;
    ack.timestamp = ::getCurrentTimeMs();
    ack.accepted = false;

    if (!parseBatchTaskAssignPayload(message.payload, batch_msg)) {
        ack.rejection_reasons["payload"] = "invalid batch task payload";
    } else {
        ack.message_id = batch_msg.message_id;
        int task_total = static_cast<int>(batch_msg.scheduled_tasks.size());
        int completed = 0;
        bool all_success = true;

        for (const auto& task_msg : batch_msg.scheduled_tasks) {
            TaskSegment task = taskAssignToSegment(task_msg, sat_id);

            // Send progress: RUNNING
            TaskProgressMessage progress;
            progress.segment_id = task.segment_id;
            progress.task_id = task.task_id;
            progress.status = TaskStatus::RUNNING;
            progress.progress_percent = static_cast<uint8_t>((completed * 100) / std::max(1, task_total));
            progress.current_action = "execute_task";
            progress.message = "task started";

            comm_->sendMessage(coord_id, buildMessage(
                MessageType::TASK_PROGRESS, sat_id, coord_id,
                Priority::NORMAL, serializeTaskProgressPayload(progress)));

            bool task_ok = executeSingleTask(task);
            completed++;
            all_success = all_success && task_ok;

            // Send completion
            TaskCompleteMessage complete;
            complete.segment_id = task.segment_id;
            complete.task_id = task.task_id;
            complete.success = task_ok;
            complete.actual_start = "";
            complete.actual_end = "";
            complete.actual_profit = 0;
            complete.result_summary = task_ok ? "task completed" : "task failed";

            comm_->sendMessage(coord_id, buildMessage(
                MessageType::TASK_COMPLETE, sat_id, coord_id,
                task_ok ? Priority::NORMAL : Priority::URGENT,
                serializeTaskCompletePayload(complete)));

            if (task_ok) {
                ack.accepted_task_ids.push_back(task.task_id);
            } else {
                ack.rejected_task_ids.push_back(task.task_id);
                ack.rejection_reasons[task.task_id] = "task execution failed";
            }
        }

        ack.accepted = all_success;
    }

    // Send ACK
    comm_->sendMessage(coord_id, buildMessage(
        MessageType::BATCH_TASK_ASSIGN_ACK, sat_id, coord_id,
        Priority::NORMAL, serializeBatchTaskAssignAckPayload(ack)));
}

bool SatelliteSimulator::executeSingleTask(const TaskSegment& task) {
    if (!executor_) {
        LOG_ERROR("[SatelliteSimulator] ERROR: 执行器不可用: " + config_.satellite_id);
        return false;
    }

    BehaviorLibraryParser behavior_parser;
    BehaviorNode behavior;
    bool behavior_loaded = false;

    {
        std::lock_guard<std::mutex> lock(behavior_cache_mutex_);
        auto cache_it = behavior_cache_.find(task.behavior_ref);
        if (cache_it != behavior_cache_.end()) {
            behavior = cache_it->second;
            behavior_loaded = true;
        }
    }

    if (!behavior_loaded) {
        try {
            behavior = behavior_parser.parseBehaviorDefinition(config_.behavior_tree_path, task.behavior_ref);
            std::lock_guard<std::mutex> lock(behavior_cache_mutex_);
            behavior_cache_[task.behavior_ref] = behavior;
        } catch (const std::exception& e) {
            LOG_ERROR(std::string("[SatelliteSimulator] ✗ 行为加载失败: ") + e.what());
            return false;
        }
    }

    auto result = executor_->executeTask(task, behavior);

    {
        std::ostringstream oss;
        if (result.success) {
            oss << "[SatelliteSimulator][" << config_.satellite_id << "] ✓ task=" << task.task_id
                << ", elapsed=" << result.execution_time_ms << "ms";
            if (!result.outputs.empty()) {
                oss << ", outputs={";
                bool first = true;
                for (const auto& out : result.outputs) {
                    if (!first) oss << ", ";
                    oss << out.first << "=" << out.second.asString();
                    first = false;
                }
                oss << "}";
            }
            LOG_DEBUG(oss.str());
        } else {
            oss << "[SatelliteSimulator][" << config_.satellite_id << "] ✗ task=" << task.task_id
                << ": " << result.message;
            if (!result.failed_node.empty()) {
                oss << ", failed_node=" << result.failed_node;
            }
            LOG_ERROR(oss.str());
        }
    }

    return result.success;
}

} // namespace executor
