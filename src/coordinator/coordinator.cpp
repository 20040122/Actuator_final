#include "coordinator.h"
#include "../core/logger.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <algorithm>
#include <sstream>

namespace coordinator {

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
    
    LOG("\n[Coordinator] Step 8: 执行任务");
    std::vector<std::thread> worker_threads;
    std::mutex result_mutex;
    int total_satellites = 0;
    int success_satellites = 0;
    
    for (const auto& sat_id : schedule_.satellite_ids) {
        auto it = schedule_.satellite_tasks.find(sat_id);
        if (it == schedule_.satellite_tasks.end() || it->second.empty()) {
            continue;
        }
        
        total_satellites++;
        
        // 获取任务列表的引用（在创建线程前捕获）
        const auto& tasks = it->second;
        
        // 为每个卫星创建一个线程（值捕获 sat_id，引用捕获 tasks）
        worker_threads.emplace_back([this, sat_id, &tasks, &success_satellites, &result_mutex]() {
            bool success = executeTasksForSatellite(sat_id, tasks);
            if (success) {
                std::lock_guard<std::mutex> lock(result_mutex);
                success_satellites++;
            }
        });
    }
    
    // 等待所有卫星任务完成
    for (auto& thread : worker_threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    
    {
        std::ostringstream oss;
        oss << "[Coordinator] ✓ 任务执行完成 (" << success_satellites << "/" << total_satellites << " 卫星)";
        LOG(oss.str());
    }
    
    LOG("\n[Coordinator] 所有任务完成，退出运行。");
}

void Coordinator::stop() {
    running_.store(false);
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
        uint64_t session_token;
        if (node_registry_->registerNode(register_msg, assigned_id, session_token)) {
            std::cout << "[Coordinator]   ✓ 注册卫星: " << sat_info.satellite_id 
                      << " (" << sat_info.name << ") - 会话: " << session_token << std::endl;
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
