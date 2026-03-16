#ifndef EXECUTOR_GENERIC_EXECUTOR_H
#define EXECUTOR_GENERIC_EXECUTOR_H

#include <memory>
#include <string>
#include <functional>
#include <unordered_map>
#include <map>
#include <thread>
#include <atomic>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <future>
#include <chrono>
#include <vector>

#include "../core/types.h"
#include "../core/logger.h"
#include "../constraint/evaluator.h"
#include "variable_manager.h"

namespace executor {

struct ExecutionResult {
    bool success;
    std::string message;
    std::string failed_node;
    std::map<std::string, VariableValue> outputs;
    int64_t execution_time_ms;
    
    ExecutionResult() 
        : success(true), execution_time_ms(0) {}
    
    static ExecutionResult Success(const std::string& msg = "") {
        ExecutionResult r;
        r.success = true;
        r.message = msg;
        return r;
    }
    
    static ExecutionResult Failure(const std::string& msg, const std::string& node = "") {
        ExecutionResult r;
        r.success = false;
        r.message = msg;
        r.failed_node = node;
        return r;
    }
};

struct ExecutorConfig {
    std::string executor_id;
    std::string satellite_id;
    bool async_mode;
    int max_concurrent_tasks;
    int command_timeout_ms;
    bool enable_rollback;
    bool debug_mode;
    
    ExecutorConfig()
        : async_mode(true)
        , max_concurrent_tasks(4)
        , command_timeout_ms(30000)
        , enable_rollback(true)
        , debug_mode(false) {}
    
    static ExecutorConfig getDefault(const std::string& sat_id = "") {
        ExecutorConfig cfg;
        cfg.satellite_id = sat_id;
        cfg.executor_id = sat_id.empty() ? "EXEC_DEFAULT" : ("EXEC_" + sat_id);
        return cfg;
    }
};

struct ExecutionContext {
    std::string task_id;
    std::string segment_id;
    std::string snapshot_id;
    std::chrono::steady_clock::time_point start_time;
    std::atomic<bool> cancelled;
    
    ExecutionContext() : cancelled(false) {}
    ExecutionContext(const ExecutionContext&) = delete;
    ExecutionContext& operator=(const ExecutionContext&) = delete;
    ExecutionContext(ExecutionContext&& other) noexcept
        : task_id(std::move(other.task_id))
        , segment_id(std::move(other.segment_id))
        , snapshot_id(std::move(other.snapshot_id))
        , start_time(other.start_time)
        , cancelled(other.cancelled.load()) {}
};

class GenericExecutor {
public:
    using TaskCallback = std::function<void(const std::string& segment_id, 
                                            const ExecutionResult& result)>;
    
    explicit GenericExecutor(const ExecutorConfig& config = ExecutorConfig());
    ~GenericExecutor();
    
    GenericExecutor(const GenericExecutor&) = delete;
    GenericExecutor& operator=(const GenericExecutor&) = delete;
    
    bool initialize();
    void shutdown();
    
    ExecutionResult executeTask(const TaskSegment& task, const BehaviorNode& behavior);
    
    bool isRunning() const { return running_.load(); }
    
    VariableManager& getVariableManager() { return var_mgr_; }
    const VariableManager& getVariableManager() const { return var_mgr_; }

private:
    ExecutorConfig config_;
    std::atomic<bool> running_;
    
    VariableManager var_mgr_;
    ConstraintEvaluator evaluator_;
    
    struct PendingTask {
        TaskSegment task;
        BehaviorNode behavior;
        std::shared_ptr<std::promise<ExecutionResult>> promise;
        TaskCallback callback;
    };
    std::queue<PendingTask> task_queue_;
    mutable std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    
    std::vector<std::thread> worker_threads_;
    
    std::unordered_map<std::string, std::shared_ptr<ExecutionContext>> active_contexts_;
    mutable std::mutex contexts_mutex_;
    mutable std::mutex runtime_mutex_;
    
    void workerLoop();
    
    ExecutionResult executeTaskInternal(
        const TaskSegment& task,
        const BehaviorNode& behavior,
        std::shared_ptr<ExecutionContext> ctx
    );
    
    void initializeTaskContext(const TaskSegment& task);
    void initializeBehaviorDefaults(const BehaviorNode& node);
    void applyObservationValues(const BehaviorNode& node);
    VariableValue evaluateValueExpression(const std::string& expression);
    
    ExecutionResult executeNode(
        const BehaviorNode& node,
        std::shared_ptr<ExecutionContext> ctx
    );
    
    ExecutionResult executeAction(const BehaviorNode& node);
    ExecutionResult executeCondition(const BehaviorNode& node);
    ExecutionResult executeSequence(const BehaviorNode& node, std::shared_ptr<ExecutionContext> ctx);
    ExecutionResult executeSelector(const BehaviorNode& node, std::shared_ptr<ExecutionContext> ctx);
    ExecutionResult executeParallel(const BehaviorNode& node, std::shared_ptr<ExecutionContext> ctx);
    
    ExecutionResult executeCommand(
        const std::string& command,
        const std::map<std::string, std::string>& params
    );
    
    ExecutionResult executeBuiltinCommand(
        const std::string& command,
        const std::map<std::string, std::string>& params
    );
    
    std::string getNodeTypeName(NodeType type) const;
    void log(const std::string& level, const std::string& message) const;
};

} // namespace executor

#endif
