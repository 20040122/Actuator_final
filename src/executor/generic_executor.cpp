#include "generic_executor.h"
#include "../parser/behavior_parser.h"
#include "../coordinator/distributed_semaphore.h"
#include <iostream>
#include <sstream>
#include <chrono>

namespace executor {

GenericExecutor::GenericExecutor(const ExecutorConfig& config)
    : config_(config)
    , running_(false)
    , var_mgr_()
    , evaluator_(var_mgr_)
    , state_mgr_()
    , sem_mgr_() {
}

GenericExecutor::~GenericExecutor() {
    shutdown();
}

bool GenericExecutor::initialize() {
    if (running_.load()) {
        return true;
    }
    
    running_.store(true);
    
    // 设置状态管理器的执行器ID
    state_mgr_.setExecutorId(config_.executor_id);
    
    if (config_.async_mode && config_.max_concurrent_tasks > 0) {
        for (int i = 0; i < config_.max_concurrent_tasks; ++i) {
            worker_threads_.emplace_back(&GenericExecutor::workerLoop, this);
        }
        log("INFO", "启动 " + std::to_string(config_.max_concurrent_tasks) + " 个工作线程");
    }
    
    log("INFO", "执行器初始化完成: " + config_.executor_id);
    return true;
}

void GenericExecutor::shutdown() {
    if (!running_.load()) {
        return;
    }
    
    log("INFO", "正在关闭执行器: " + config_.executor_id);
    running_.store(false);
    
    {
        std::lock_guard<std::mutex> lock(contexts_mutex_);
        for (auto& pair : active_contexts_) {
            pair.second->cancelled.store(true);
        }
    }
    
    queue_cv_.notify_all();
    
    for (auto& thread : worker_threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    worker_threads_.clear();
    
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        while (!task_queue_.empty()) {
            auto& pending = task_queue_.front();
            if (pending.promise) {
                pending.promise->set_value(ExecutionResult::Failure("执行器已关闭"));
            }
            task_queue_.pop();
        }
    }
    
    log("INFO", "执行器已关闭: " + config_.executor_id);
}

void GenericExecutor::registerHandler(std::shared_ptr<ICommandHandler> handler) {
    if (!handler) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(handlers_mutex_);
    std::string type = handler->getCommandType();
    handlers_[type] = handler;
    log("DEBUG", "注册命令处理器: " + type);
}

void GenericExecutor::unregisterHandler(const std::string& command_type) {
    std::lock_guard<std::mutex> lock(handlers_mutex_);
    handlers_.erase(command_type);
}

bool GenericExecutor::hasHandler(const std::string& command_type) const {
    std::lock_guard<std::mutex> lock(handlers_mutex_);
    return handlers_.find(command_type) != handlers_.end();
}

ExecutionResult GenericExecutor::executeTask(const TaskSegment& task, 
                                              const BehaviorNode& behavior) {
    auto ctx = std::make_shared<ExecutionContext>();
    ctx->task_id = task.task_id;
    ctx->segment_id = task.segment_id;
    ctx->start_time = std::chrono::steady_clock::now();
    
    {
        std::lock_guard<std::mutex> lock(contexts_mutex_);
        active_contexts_[task.segment_id] = ctx;
    }
    
    auto result = executeTaskInternal(task, behavior, ctx);
    
    {
        std::lock_guard<std::mutex> lock(contexts_mutex_);
        active_contexts_.erase(task.segment_id);
    }
    
    return result;
}

std::future<ExecutionResult> GenericExecutor::executeTaskAsync(
    const TaskSegment& task,
    const BehaviorNode& behavior,
    TaskCallback callback) {
    
    auto promise = std::make_shared<std::promise<ExecutionResult>>();
    auto future = promise->get_future();
    
    if (!running_.load()) {
        promise->set_value(ExecutionResult::Failure("执行器未运行"));
        return future;
    }
    
    if (!config_.async_mode) {
        auto result = executeTask(task, behavior);
        promise->set_value(result);
        if (callback) {
            callback(task.segment_id, result);
        }
        return future;
    }
    
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        PendingTask pending;
        pending.task = task;
        pending.behavior = behavior;
        pending.promise = promise;
        pending.callback = callback;
        task_queue_.push(std::move(pending));
    }
    queue_cv_.notify_one();
    
    return future;
}

bool GenericExecutor::cancelTask(const std::string& segment_id) {
    std::lock_guard<std::mutex> lock(contexts_mutex_);
    auto it = active_contexts_.find(segment_id);
    if (it != active_contexts_.end()) {
        it->second->cancelled.store(true);
        log("INFO", "任务取消请求: " + segment_id);
        return true;
    }
    return false;
}

bool GenericExecutor::isTaskRunning(const std::string& segment_id) const {
    std::lock_guard<std::mutex> lock(contexts_mutex_);
    return active_contexts_.find(segment_id) != active_contexts_.end();
}

bool GenericExecutor::isBusy() const {
    std::lock_guard<std::mutex> lock(contexts_mutex_);
    return !active_contexts_.empty();
}

size_t GenericExecutor::getPendingTaskCount() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return task_queue_.size();
}

size_t GenericExecutor::getActiveTaskCount() const {
    std::lock_guard<std::mutex> lock(contexts_mutex_);
    return active_contexts_.size();
}

void GenericExecutor::setDebugMode(bool enabled) {
    config_.debug_mode = enabled;
    evaluator_.enableDebug(enabled);
}

void GenericExecutor::setDistributedSemaphore(std::shared_ptr<coordinator::DistributedSemaphore> sem_mgr) {
    distributed_sem_mgr_ = sem_mgr;
}

void GenericExecutor::printVariableStatus() const {
    std::cout << "\n========== 变量状态 ==========" << std::endl;
    
    auto global_vars = var_mgr_.getAllVariables(Scope::GLOBAL);
    std::cout << "\n[全局变量]" << std::endl;
    for (const auto& pair : global_vars) {
        std::cout << "  " << pair.first << " = " << pair.second.asString() << std::endl;
    }
    
    auto local_vars = var_mgr_.getAllVariables(Scope::LOCAL);
    if (!local_vars.empty()) {
        std::cout << "\n[局部变量]" << std::endl;
        for (const auto& pair : local_vars) {
            std::cout << "  " << pair.first << " = " << pair.second.asString() << std::endl;
        }
    }
    
    auto inter_vars = var_mgr_.getAllVariables(Scope::INTERMEDIATE);
    if (!inter_vars.empty()) {
        std::cout << "\n[中间变量]" << std::endl;
        for (const auto& pair : inter_vars) {
            std::cout << "  " << pair.first << " = " << pair.second.asString() << std::endl;
        }
    }
}

void GenericExecutor::workerLoop() {
    while (running_.load()) {
        PendingTask pending;
        
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this] {
                return !running_.load() || !task_queue_.empty();
            });
            
            if (!running_.load() && task_queue_.empty()) {
                return;
            }
            
            if (task_queue_.empty()) {
                continue;
            }
            
            pending = std::move(task_queue_.front());
            task_queue_.pop();
        }
        
        try {
            auto result = executeTask(pending.task, pending.behavior);
            
            if (pending.promise) {
                pending.promise->set_value(result);
            }
            
            if (pending.callback) {
                pending.callback(pending.task.segment_id, result);
            }
        } catch (const std::exception& e) {
            auto result = ExecutionResult::Failure(std::string("执行异常: ") + e.what());
            
            if (pending.promise) {
                pending.promise->set_value(result);
            }
            
            if (pending.callback) {
                pending.callback(pending.task.segment_id, result);
            }
        }
    }
}

ExecutionResult GenericExecutor::executeTaskInternal(
    const TaskSegment& task,
    const BehaviorNode& behavior,
    std::shared_ptr<ExecutionContext> ctx) {
    
    ExecutionResult result;
    result.success = true;
    
    log("INFO", "开始执行任务: " + task.segment_id);
    
    state_mgr_.reset();
    initializeTaskContext(task);
    ctx->snapshot_id = var_mgr_.createSnapshot("task_" + task.segment_id);
    
    try {
        BehaviorTreeParser parser;
        auto nodes = parser.instantiate(behavior, task.behavior_params);
        
        for (const auto& node : nodes) {
            if (ctx->cancelled.load()) {
                result = ExecutionResult::Failure("任务已取消", node.name);
                break;
            }
            
            auto node_result = executeNode(node, ctx);
            if (!node_result.success) {
                result = node_result;
                
                if (config_.enable_rollback) {
                    log("INFO", "回滚变量状态...");
                    var_mgr_.restoreSnapshot(ctx->snapshot_id);
                }
                break;
            }
        }
        
    } catch (const std::exception& e) {
        result = ExecutionResult::Failure(std::string("执行异常: ") + e.what());
        var_mgr_.restoreSnapshot(ctx->snapshot_id);
    }
    
    var_mgr_.clearLocal();
    
    auto end_time = std::chrono::steady_clock::now();
    result.execution_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - ctx->start_time).count();
    
    log("INFO", "任务执行完成: " + task.segment_id + 
        " [" + (result.success ? "成功" : "失败") + "] " +
        "耗时: " + std::to_string(result.execution_time_ms) + "ms");
    
    return result;
}

void GenericExecutor::initializeTaskContext(const TaskSegment& task) {
    var_mgr_.set("satellite_id", VariableValue(task.satellite_id), Scope::GLOBAL);
    var_mgr_.set("task_id", VariableValue(task.task_id), Scope::GLOBAL);
    var_mgr_.set("segment_id", VariableValue(task.segment_id), Scope::GLOBAL);
    
    if (!task.execution.planned_start.empty()) {
        var_mgr_.set("current_time", VariableValue(task.execution.planned_start), Scope::GLOBAL);
    }
    
    if (!task.window.start.empty()) {
        var_mgr_.set("window_start", VariableValue(task.window.start), Scope::GLOBAL);
    }
    if (!task.window.end.empty()) {
        var_mgr_.set("window_end", VariableValue(task.window.end), Scope::GLOBAL);
    }
    if (!task.window.window_id.empty()) {
        var_mgr_.set("window_id", VariableValue(task.window.window_id), Scope::GLOBAL);
    }
    
    var_mgr_.setFromParams(task.behavior_params, Scope::LOCAL);
    
    if (!var_mgr_.exists("observation_duration_s", Scope::LOCAL) && 
        task.execution.duration_s > 0) {
        var_mgr_.set("observation_duration_s", 
                     VariableValue(task.execution.duration_s), Scope::LOCAL);
    }
    
    log("DEBUG", "任务上下文初始化完成，参数数量: " + 
        std::to_string(task.behavior_params.size()));
}

ExecutionResult GenericExecutor::executeNode(
    const BehaviorNode& node,
    std::shared_ptr<ExecutionContext> ctx) {
    
    if (ctx && ctx->cancelled.load()) {
        return ExecutionResult::Failure("任务已取消", node.name);
    }
    
    log("DEBUG", "执行节点: " + node.name + " [" + getNodeTypeName(node.type) + "]");
    
    if (!node.expression.empty()) {
        log("DEBUG", "评估前置条件: " + node.expression);
        
        ConstraintResult constraint_result = evaluator_.evaluateDetailed(node.expression);
        if (!constraint_result.satisfied) {
            log("DEBUG", "前置条件不满足: " + constraint_result.message);
            state_mgr_.updateState(node.name, NodeState::BLOCKED);
            return ExecutionResult::Failure("前置条件不满足: " + constraint_result.message, node.name);
        }
        log("DEBUG", "前置条件通过");
    }
    
    state_mgr_.updateState(node.name, NodeState::RUNNING);
    
    ExecutionResult result;
    
    switch (node.type) {
        case NodeType::ACTION:
            result = executeAction(node, ctx);
            break;
            
        case NodeType::CONDITION:
            result = executeCondition(node);
            break;
            
        case NodeType::SEQUENCE:
            result = executeSequence(node, ctx);
            break;
            
        case NodeType::SELECTOR:
            result = executeSelector(node, ctx);
            break;
            
        case NodeType::PARALLEL:
            result = executeParallel(node, ctx);
            break;
            
        default:
            result = executeCommand(node.command, node.params);
    }
    
    state_mgr_.updateState(node.name, result.success ? NodeState::SUCCESS : NodeState::FAILED);
    log("DEBUG", "节点执行结果: " + node.name + " -> " + (result.success ? "成功" : "失败"));
    
    return result;
}

ExecutionResult GenericExecutor::executeAction(const BehaviorNode& node, 
                                                std::shared_ptr<ExecutionContext> ctx) {
    log("DEBUG", "执行动作: " + node.command);
    
    for (const auto& param : node.params) {
        var_mgr_.set(param.first, VariableValue(param.second), Scope::INTERMEDIATE);
    }
    
    for (const auto& var_name : node.variables) {
        var_mgr_.set(var_name, VariableValue(100), Scope::INTERMEDIATE);
    }
    
    return executeCommand(node.command, node.params);
}

ExecutionResult GenericExecutor::executeCondition(const BehaviorNode& node) {
    if (node.expression.empty()) {
        return ExecutionResult::Failure("条件节点缺少表达式", node.name);
    }
    
    log("DEBUG", "评估条件: " + node.expression);
    bool result = evaluator_.evaluate(node.expression);
    
    if (result) {
        return ExecutionResult::Success("条件为真");
    } else {
        return ExecutionResult::Failure("条件为假", node.name);
    }
}

ExecutionResult GenericExecutor::executeSequence(const BehaviorNode& node,
                                                  std::shared_ptr<ExecutionContext> ctx) {
    for (const auto& child : node.children) {
        auto result = executeNode(child, ctx);
        if (!result.success) {
            log("DEBUG", "顺序节点中断: 子节点失败");
            return result;
        }
    }
    return ExecutionResult::Success();
}

ExecutionResult GenericExecutor::executeSelector(const BehaviorNode& node,
                                                  std::shared_ptr<ExecutionContext> ctx) {
    log("DEBUG", "选择执行（尝试 " + std::to_string(node.children.size()) + " 个选项）");
    
    for (const auto& child : node.children) {
        auto result = executeNode(child, ctx);
        if (result.success) {
            log("DEBUG", "选择节点成功: 找到可用分支");
            return result;
        }
    }
    
    return ExecutionResult::Failure("所有分支均失败", node.name);
}

ExecutionResult GenericExecutor::executeParallel(const BehaviorNode& node,
                                                  std::shared_ptr<ExecutionContext> ctx) {
    log("DEBUG", "并行执行 " + std::to_string(node.children.size()) + " 个子节点");
    
    bool all_success = true;
    std::string first_failed_node;
    
    for (const auto& child : node.children) {
        auto result = executeNode(child, ctx);
        if (!result.success && all_success) {
            all_success = false;
            first_failed_node = result.failed_node.empty() ? child.name : result.failed_node;
        }
    }
    
    if (all_success) {
        return ExecutionResult::Success();
    } else {
        return ExecutionResult::Failure("部分子节点失败", first_failed_node);
    }
}

ExecutionResult GenericExecutor::executeCommand(
    const std::string& command,
    const std::map<std::string, std::string>& params) {
    
    if (command.empty()) {
        return ExecutionResult::Success();
    }
    
    {
        std::lock_guard<std::mutex> lock(handlers_mutex_);
        auto it = handlers_.find(command);
        if (it != handlers_.end()) {
            log("DEBUG", "使用注册处理器执行: " + command);
            return it->second->execute(command, params, var_mgr_);
        }
    }
    
    return executeBuiltinCommand(command, params);
}

ExecutionResult GenericExecutor::executeBuiltinCommand(
    const std::string& command,
    const std::map<std::string, std::string>& params) {
    
    log("DEBUG", "执行命令: " + command);
    
    if (command == "SEMAPHORE_ACQUIRE") {
        std::string sem_id;
        if (var_mgr_.exists("semaphore_id")) {
            sem_id = var_mgr_.get("semaphore_id").asString();
        }
        int timeout = 30;
        if (var_mgr_.exists("timeout_s")) {
            timeout = var_mgr_.get("timeout_s").asInt();
        }
        
        bool success = false;
        if (distributed_sem_mgr_) {
            std::string task_id = var_mgr_.exists("task_id") ? var_mgr_.get("task_id").asString() : "";
            success = distributed_sem_mgr_->acquire(sem_id, 1, 5, timeout, task_id);
        } else {
            success = sem_mgr_.acquire(sem_id, timeout);
        }
        
        if (success) {
            return ExecutionResult::Success("信号量获取成功: " + sem_id);
        } else {
            return ExecutionResult::Failure("信号量获取超时: " + sem_id);
        }
    }
    
    if (command == "SEMAPHORE_RELEASE") {
        std::string sem_id;
        if (var_mgr_.exists("semaphore_id")) {
            sem_id = var_mgr_.get("semaphore_id").asString();
        }
        
        if (distributed_sem_mgr_) {
            distributed_sem_mgr_->release(sem_id, 0, config_.executor_id);
        } else {
            sem_mgr_.release(sem_id);
        }
        return ExecutionResult::Success("信号量已释放: " + sem_id);
    }
    
    if (command.find("setvar") != std::string::npos) {
        size_t eq_pos = command.find('=');
        if (eq_pos != std::string::npos && eq_pos > 6) {
            std::string var_name = command.substr(6, eq_pos - 6);
            std::string var_value = command.substr(eq_pos + 1);
            
            try {
                int int_val = std::stoi(var_value);
                var_mgr_.set(var_name, VariableValue(int_val), Scope::GLOBAL);
            } catch (...) {
                var_mgr_.set(var_name, VariableValue(var_value), Scope::GLOBAL);
            }
            
            log("DEBUG", "设置变量: " + var_name + " = " + var_value);
            return ExecutionResult::Success("变量已设置: " + var_name);
        }
    }
    
    log("DEBUG", "模拟执行命令: " + command);
    return ExecutionResult::Success();
}

std::string GenericExecutor::getNodeTypeName(NodeType type) const {
    switch (type) {
        case NodeType::ACTION:    return "动作";
        case NodeType::CONDITION: return "条件";
        case NodeType::SEQUENCE:  return "顺序";
        case NodeType::SELECTOR:  return "选择";
        case NodeType::PARALLEL:  return "并行";
        case NodeType::SUBTREE:   return "子树";
        default:                  return "未知";
    }
}

void GenericExecutor::log(const std::string& level, const std::string& message) const {
    if (level == "DEBUG" && !config_.debug_mode) {
        return;
    }
    std::ostringstream oss;
    oss << "[" << config_.executor_id << "][" << level << "] " << message;
    LOG(oss.str());
}

} // namespace executor
