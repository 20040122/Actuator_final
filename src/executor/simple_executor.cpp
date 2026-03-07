#include <iostream>
#include <sstream>
#include "../core/types.h"
#include "../parser/behavior_parser.h"
#include "../constraint/evaluator.h"
#include "state_manager.h"
#include "variable_manager.h"
#include "semaphore_manager.h"

class SimpleExecutor {
public:
    SimpleExecutor() 
        : var_mgr_(), 
          evaluator_(var_mgr_),
          sem_mgr_(),
          debug_mode_(false) {
    }   
    void executeTask(const TaskSegment& task, 
                     const BehaviorNode& behavior_def) {
        state_mgr_.reset();
        initializeTaskContext(task);
        std::string snapshot_id = var_mgr_.createSnapshot("task_start_" + task.segment_id);
        try {
            BehaviorTreeParser parser;
            auto nodes = parser.instantiate(behavior_def, task.behavior_params);
            for (const auto& node : nodes) {
                if (!executeNode(node)) {
                    std::cout << "\n[错误] 节点执行失败: " << node.name << std::endl;
                    std::cout << "任务终止" << std::endl;
                    if (shouldRollbackOnFailure()) {
                        std::cout << "回滚变量状态..." << std::endl;
                        var_mgr_.restoreSnapshot(snapshot_id);
                    }
                    break;
                }
            }          
            std::cout << "\n任务段执行完成: " << task.segment_id << std::endl;    
        } catch (const std::exception& e) {
            std::cout << "\n[异常] 执行过程中发生错误: " << e.what() << std::endl;
            var_mgr_.restoreSnapshot(snapshot_id);
        }
        var_mgr_.clearLocal();
    }

    void setDebugMode(bool enabled) {
        debug_mode_ = enabled;
        evaluator_.enableDebug(enabled);
    }
    
    VariableManager& getVariableManager() { return var_mgr_; }
    ConstraintEvaluator& getConstraintEvaluator() { return evaluator_; }
    SemaphoreManager& getSemaphoreManager() { return sem_mgr_; }
    void printVariableStatus() const {
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
    
private:
    VariableManager var_mgr_;         
    ConstraintEvaluator evaluator_;    
    CommandStateManager state_mgr_;
    SemaphoreManager sem_mgr_;
    bool debug_mode_;                   
    void initializeTaskContext(const TaskSegment& task) {
        var_mgr_.set("satellite_id", VariableValue(task.satellite_id), Scope::GLOBAL);
        var_mgr_.set("task_id", VariableValue(task.task_id), Scope::GLOBAL);
        var_mgr_.set("segment_id", VariableValue(task.segment_id), Scope::GLOBAL);
        if (!task.execution.planned_start.empty()) {
            var_mgr_.set("current_time", VariableValue(task.execution.planned_start), Scope::GLOBAL);
            std::cout << "设置模拟时间: current_time = " << task.execution.planned_start << std::endl;
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
        if (!var_mgr_.exists("observation_duration_s", Scope::LOCAL) && task.execution.duration_s > 0) {
            var_mgr_.set("observation_duration_s", VariableValue(task.execution.duration_s), Scope::LOCAL);
        }
        std::cout << "任务上下文初始化完成，参数数量: " << task.behavior_params.size() << std::endl;
    }
    bool executeNode(const BehaviorNode& node) {
        std::cout << "\n------------------------------------------" << std::endl;
        std::cout << "执行节点: " << node.name << std::endl;
        std::cout << "类型: " << getNodeTypeName(node.type) << std::endl;
        if (!node.expression.empty()) {
            std::cout << "评估前置条件: " << node.expression << std::endl;
            ConstraintResult result = evaluator_.evaluateDetailed(node.expression); 
            if (!result.satisfied) {
                std::cout << "  [失败] " << result.message << std::endl;
                state_mgr_.updateState(node.name, NodeState::BLOCKED);
                return false;
            }
            std::cout << "  [通过] " << result.message << std::endl;
        }
        state_mgr_.updateState(node.name, NodeState::RUNNING);
        bool success = false;
        switch (node.type) {
            case NodeType::ACTION:
                success = executeAction(node);
                break;
            case NodeType::CONDITION:
                success = executeCondition(node);
                break;
            case NodeType::SEQUENCE:
                success = executeSequence(node);
                break;
            case NodeType::SELECTOR:
                success = executeSelector(node);
                break;
            case NodeType::PARALLEL:
                success = executeParallel(node);
                break;
            default:
                success = executeGeneric(node);
        }
        state_mgr_.updateState(node.name, 
            success ? NodeState::SUCCESS : NodeState::FAILED);
        std::cout << "节点执行结果: " << (success ? "成功" : "失败") << std::endl;
        std::cout << "------------------------------------------" << std::endl;
        return success;
    }
    bool executeAction(const BehaviorNode& node) {
        std::cout << "执行动作: " << node.command << std::endl;
        if (!node.params.empty()) {
            std::cout << "参数:" << std::endl;
            for (const auto& param : node.params) {
                std::cout << "  " << param.first << " = " << param.second << std::endl;
                var_mgr_.set(param.first, VariableValue(param.second), Scope::INTERMEDIATE);
            }
        }
        if (!node.variables.empty()) {
            std::cout << "输出变量:" << std::endl;
            for (const auto& var_name : node.variables) {
                var_mgr_.set(var_name, VariableValue(100), Scope::INTERMEDIATE);
                std::cout << "  " << var_name << " = 100 (模拟值)" << std::endl;
            }
        }
        return simulateCommandExecution(node.command);
    }
    bool executeCondition(const BehaviorNode& node) {
        if (node.expression.empty()) {
            std::cout << "警告: 条件节点缺少表达式" << std::endl;
            return false;
        }
        std::cout << "评估条件: " << node.expression << std::endl;
        bool result = evaluator_.evaluate(node.expression);
        std::cout << "条件结果: " << (result ? "真" : "假") << std::endl;
        return result;
    }
    bool executeSequence(const BehaviorNode& node) {
        for (const auto& child : node.children) {
            if (!executeNode(child)) {
                std::cout << "顺序节点中断: 子节点失败" << std::endl;
                return false;
            }
        }
        return true;
    }
    bool executeSelector(const BehaviorNode& node) {
        std::cout << "选择执行（尝试 " << node.children.size() << " 个选项）" << std::endl; 
        for (const auto& child : node.children) {
            if (executeNode(child)) {
                std::cout << "选择节点成功: 找到可用分支" << std::endl;
                return true;
            }
        }
        std::cout << "选择节点失败: 所有分支均失败" << std::endl;
        return false;
    }
    bool executeParallel(const BehaviorNode& node) {
        std::cout << "并行执行 " << node.children.size() << " 个子节点" << std::endl;
        bool all_success = true;
        for (const auto& child : node.children) {
            if (!executeNode(child)) {
                all_success = false;
            }
        }
        return all_success;
    }
    bool executeGeneric(const BehaviorNode& node) {
        std::cout << "执行命令: " << node.command << std::endl;
        return simulateCommandExecution(node.command);
    }
    bool simulateCommandExecution(const std::string& command) {
        if (command.empty()) {
            return true;
        }
        std::cout << "  → 执行: " << command << std::endl;
        if (command == "SEMAPHORE_ACQUIRE") {
            std::string sem_id = var_mgr_.get("semaphore_id").asString();
            int timeout = 30;
            if (var_mgr_.exists("timeout_s")) {
                timeout = var_mgr_.get("timeout_s").asInt();
            }
            return sem_mgr_.acquire(sem_id, timeout);
        }
        if (command == "SEMAPHORE_RELEASE") {
            std::string sem_id = var_mgr_.get("semaphore_id").asString();
            sem_mgr_.release(sem_id);
            return true;
        }
        if (command.find("setvar") != std::string::npos) {
            size_t eq_pos = command.find('=');
            if (eq_pos != std::string::npos) {
                std::string var_name = command.substr(7, eq_pos - 7);
                std::string var_value = command.substr(eq_pos + 1);
                var_mgr_.set(var_name, VariableValue(std::stoi(var_value)), Scope::GLOBAL);
                std::cout << "  设置变量: " << var_name << " = " << var_value << std::endl;
            }
        }
        return true;
    }
    std::string getNodeTypeName(NodeType type) const {
        switch (type) {
            case NodeType::ACTION: return "动作";
            case NodeType::CONDITION: return "条件";
            case NodeType::SEQUENCE: return "顺序";
            case NodeType::SELECTOR: return "选择";
            case NodeType::PARALLEL: return "并行";
            case NodeType::SUBTREE: return "子树";
            default: return "未知";
        }
    }
    bool shouldRollbackOnFailure() const {
        return false;
    }
};