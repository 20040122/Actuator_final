#include "generic_executor.h"
#include "../parser/behavior_parser.h"
#include <sstream>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdlib>

namespace executor {
namespace {

void mergeOutputs(std::map<std::string, VariableValue>& dst,
                  const std::map<std::string, VariableValue>& src) {
    for (const auto& kv : src) {
        dst[kv.first] = kv.second;
    }
}

std::map<std::string, VariableValue> g_shared_mailbox;
std::mutex g_shared_mailbox_mutex;

std::string trimCopy(const std::string& text) {
    size_t start = 0;
    while (start < text.size() &&
           std::isspace(static_cast<unsigned char>(text[start]))) {
        ++start;
    }

    size_t end = text.size();
    while (end > start &&
           std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    return text.substr(start, end - start);
}

std::string toUpperCopy(const std::string& text) {
    std::string upper = text;
    for (size_t i = 0; i < upper.size(); ++i) {
        upper[i] = static_cast<char>(
            std::toupper(static_cast<unsigned char>(upper[i])));
    }
    return upper;
}

bool tryParseNumberLiteral(const std::string& text, double& out) {
    if (text.empty()) {
        return false;
    }
    char* end_ptr = nullptr;
    out = std::strtod(text.c_str(), &end_ptr);
    return end_ptr != text.c_str() && *end_ptr == '\0';
}

bool containsComparisonOperator(const std::string& expression) {
    return expression.find("==") != std::string::npos ||
           expression.find("!=") != std::string::npos ||
           expression.find(">=") != std::string::npos ||
           expression.find("<=") != std::string::npos ||
           expression.find('>') != std::string::npos ||
           expression.find('<') != std::string::npos ||
           expression.find("&&") != std::string::npos ||
           expression.find("||") != std::string::npos;
}

bool containsArithmeticOperator(const std::string& expression) {
    return expression.find('+') != std::string::npos ||
           expression.find('-') != std::string::npos ||
           expression.find('*') != std::string::npos ||
           expression.find('/') != std::string::npos ||
           expression.find("sqrt") != std::string::npos;
}

std::string observationAlias(const std::string& sensor_key) {
    if (sensor_key == "POS_X") return "x";
    if (sensor_key == "POS_Y") return "y";
    if (sensor_key == "POS_Z") return "z";
    if (sensor_key == "TARGET_X") return "tx";
    if (sensor_key == "TARGET_Y") return "ty";
    if (sensor_key == "TARGET_Z") return "tz";
    if (sensor_key == "REC") return "rec_type";
    if (sensor_key == "REC_TRUE") return "rec_type_true";
    return "";
}

VariableValue defaultObservationValue(const std::string& sensor_key) {
    if (sensor_key.find("REC") != std::string::npos ||
        sensor_key.find("TYPE") != std::string::npos) {
        return VariableValue(std::string(""));
    }
    if (sensor_key.find("IS_") != std::string::npos ||
        sensor_key.find("FLAG") != std::string::npos ||
        sensor_key.find("LIGHT") != std::string::npos) {
        return VariableValue(false);
    }
    return VariableValue(0.0);
}

class NumericExpressionParser {
public:
    NumericExpressionParser(const std::string& expression, VariableManager& var_mgr)
        : expression_(expression), pos_(0), var_mgr_(var_mgr) {}

    double parse() {
        double value = parseExpression();
        skipSpaces();
        if (pos_ != expression_.size()) {
            throw std::runtime_error("numeric expression trailing characters: " +
                                     expression_.substr(pos_));
        }
        return value;
    }

private:
    const std::string expression_;
    size_t pos_;
    VariableManager& var_mgr_;

    void skipSpaces() {
        while (pos_ < expression_.size() &&
               std::isspace(static_cast<unsigned char>(expression_[pos_]))) {
            ++pos_;
        }
    }

    bool consume(char expected) {
        skipSpaces();
        if (pos_ < expression_.size() && expression_[pos_] == expected) {
            ++pos_;
            return true;
        }
        return false;
    }

    std::string parseIdentifier() {
        skipSpaces();
        if (pos_ >= expression_.size() ||
            !(std::isalpha(static_cast<unsigned char>(expression_[pos_])) ||
              expression_[pos_] == '_')) {
            throw std::runtime_error("identifier expected");
        }

        size_t start = pos_;
        ++pos_;
        while (pos_ < expression_.size() &&
               (std::isalnum(static_cast<unsigned char>(expression_[pos_])) ||
                expression_[pos_] == '_')) {
            ++pos_;
        }
        return expression_.substr(start, pos_ - start);
    }

    double parseNumber() {
        skipSpaces();
        const char* start_ptr = expression_.c_str() + pos_;
        char* end_ptr = nullptr;
        double value = std::strtod(start_ptr, &end_ptr);
        if (end_ptr == start_ptr) {
            throw std::runtime_error("number expected");
        }
        pos_ += static_cast<size_t>(end_ptr - start_ptr);
        return value;
    }

    double variableToDouble(const std::string& var_name) {
        if (!var_mgr_.exists(var_name)) {
            throw std::runtime_error("variable not found: " + var_name);
        }
        const VariableValue value = var_mgr_.get(var_name);
        switch (value.getType()) {
            case VariableValue::Type::INT:
                return static_cast<double>(value.asInt());
            case VariableValue::Type::DOUBLE:
                return value.asDouble();
            case VariableValue::Type::BOOL:
                return value.asBool() ? 1.0 : 0.0;
            case VariableValue::Type::STRING: {
                double parsed = 0.0;
                const std::string as_text = trimCopy(value.asString());
                if (!tryParseNumberLiteral(as_text, parsed)) {
                    throw std::runtime_error("variable is not numeric: " + var_name);
                }
                return parsed;
            }
            default:
                break;
        }
        throw std::runtime_error("unsupported variable type: " + var_name);
    }

    double parseFactor() {
        skipSpaces();
        if (consume('+')) {
            return parseFactor();
        }
        if (consume('-')) {
            return -parseFactor();
        }
        if (consume('(')) {
            double value = parseExpression();
            if (!consume(')')) {
                throw std::runtime_error("missing closing ')' in numeric expression");
            }
            return value;
        }

        skipSpaces();
        if (pos_ >= expression_.size()) {
            throw std::runtime_error("unexpected end of numeric expression");
        }

        const char ch = expression_[pos_];
        if (std::isdigit(static_cast<unsigned char>(ch)) || ch == '.') {
            return parseNumber();
        }
        if (std::isalpha(static_cast<unsigned char>(ch)) || ch == '_') {
            const std::string identifier = parseIdentifier();
            if (consume('(')) {
                double arg = parseExpression();
                if (!consume(')')) {
                    throw std::runtime_error("missing closing ')' for function: " + identifier);
                }
                if (identifier == "sqrt") {
                    return std::sqrt(arg);
                }
                throw std::runtime_error("unsupported numeric function: " + identifier);
            }
            return variableToDouble(identifier);
        }

        throw std::runtime_error("unexpected token in numeric expression");
    }

    double parseTerm() {
        double value = parseFactor();
        while (true) {
            if (consume('*')) {
                value *= parseFactor();
            } else if (consume('/')) {
                const double rhs = parseFactor();
                if (std::fabs(rhs) < 1e-12) {
                    throw std::runtime_error("division by zero");
                }
                value /= rhs;
            } else {
                break;
            }
        }
        return value;
    }

    double parseExpression() {
        double value = parseTerm();
        while (true) {
            if (consume('+')) {
                value += parseTerm();
            } else if (consume('-')) {
                value -= parseTerm();
            } else {
                break;
            }
        }
        return value;
    }
};

void appendNodeParamsToOutputs(const BehaviorNode& node,
                               VariableManager& var_mgr,
                               ExecutionResult& result) {
    for (const auto& kv : node.params) {
        auto existing_it = result.outputs.find(kv.first);
        if (existing_it != result.outputs.end()) {
            result.outputs[node.name + "." + kv.first] = existing_it->second;
        } else if (var_mgr.exists(kv.first)) {
            result.outputs[node.name + "." + kv.first] = var_mgr.get(kv.first);
        } else {
            result.outputs[node.name + "." + kv.first] = VariableValue(kv.second);
        }
    }
}

} // namespace

GenericExecutor::GenericExecutor(const ExecutorConfig& config)
    : config_(config)
    , running_(false)
    , var_mgr_()
    , evaluator_(var_mgr_)
    , state_mgr_() {
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
    
    ExecutionResult result;
    {
        std::lock_guard<std::mutex> lock(runtime_mutex_);
        result = executeTaskInternal(task, behavior, ctx);
    }
    
    {
        std::lock_guard<std::mutex> lock(contexts_mutex_);
        active_contexts_.erase(task.segment_id);
    }
    
    return result;
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
            initializeBehaviorDefaults(node);
        }
        var_mgr_.setFromParams(task.behavior_params, Scope::LOCAL);
        
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

            mergeOutputs(result.outputs, node_result.outputs);
        }
        
    } catch (const std::exception& e) {
        result = ExecutionResult::Failure(std::string("执行异常: ") + e.what());
        var_mgr_.restoreSnapshot(ctx->snapshot_id);
    }
    
    var_mgr_.clearLocal();
    var_mgr_.clearScope(Scope::INTERMEDIATE);
    
    auto end_time = std::chrono::steady_clock::now();
    result.execution_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - ctx->start_time).count();
    
    log("INFO", "任务执行完成: " + task.segment_id + 
        " [" + (result.success ? "成功" : "失败") + "] " +
        "耗时: " + std::to_string(result.execution_time_ms) + "ms");
    
    return result;
}

void GenericExecutor::initializeTaskContext(const TaskSegment& task) {
    var_mgr_.clearLocal();
    var_mgr_.clearScope(Scope::INTERMEDIATE);

    var_mgr_.set("satellite_id", VariableValue(task.satellite_id), Scope::GLOBAL);
    var_mgr_.set("task_id", VariableValue(task.task_id), Scope::GLOBAL);
    var_mgr_.set("segment_id", VariableValue(task.segment_id), Scope::GLOBAL);
    var_mgr_.setFromParams(task.behavior_params, Scope::LOCAL);
    
    log("DEBUG", "任务上下文初始化完成，参数数量: " + 
        std::to_string(task.behavior_params.size()));
}

void GenericExecutor::initializeBehaviorDefaults(const BehaviorNode& node) {
    if (!node.variable_inits.empty()) {
        var_mgr_.setFromParams(node.variable_inits, Scope::LOCAL);
    }
    if (!node.constants.empty()) {
        var_mgr_.setFromParams(node.constants, Scope::LOCAL);
    }
    for (const auto& child : node.children) {
        initializeBehaviorDefaults(child);
    }
}

void GenericExecutor::applyObservationValues(const BehaviorNode& node) {
    if (node.observation.empty()) {
        return;
    }

    for (const auto& mapping : node.observation) {
        const std::string& var_name = mapping.first;
        const std::string& sensor_key = mapping.second;

        VariableValue observed_value;
        bool resolved = false;

        if (var_mgr_.exists(sensor_key)) {
            observed_value = var_mgr_.get(sensor_key);
            resolved = true;
        }

        if (!resolved) {
            const std::string alias = observationAlias(sensor_key);
            if (!alias.empty() && var_mgr_.exists(alias)) {
                observed_value = var_mgr_.get(alias);
                resolved = true;
            }
        }

        if (!resolved && var_mgr_.exists(var_name)) {
            observed_value = var_mgr_.get(var_name);
            resolved = true;
        }

        if (!resolved) {
            observed_value = defaultObservationValue(sensor_key);
        }

        var_mgr_.set(var_name, observed_value, Scope::LOCAL);
    }
}

VariableValue GenericExecutor::evaluateValueExpression(const std::string& expression) {
    const std::string trimmed = trimCopy(expression);
    if (trimmed.empty()) {
        return VariableValue(std::string(""));
    }

    if (trimmed.size() >= 2 &&
        ((trimmed.front() == '"' && trimmed.back() == '"') ||
         (trimmed.front() == '\'' && trimmed.back() == '\''))) {
        return VariableValue(trimmed.substr(1, trimmed.size() - 2));
    }

    if (trimmed == "true" || trimmed == "TRUE" || trimmed == "True") {
        return VariableValue(true);
    }
    if (trimmed == "false" || trimmed == "FALSE" || trimmed == "False") {
        return VariableValue(false);
    }

    if (var_mgr_.exists(trimmed)) {
        return var_mgr_.get(trimmed);
    }

    if (containsComparisonOperator(trimmed)) {
        return VariableValue(evaluator_.evaluate(trimmed));
    }

    double numeric_literal = 0.0;
    if (tryParseNumberLiteral(trimmed, numeric_literal)) {
        if (trimmed.find('.') == std::string::npos &&
            trimmed.find('e') == std::string::npos &&
            trimmed.find('E') == std::string::npos) {
            return VariableValue(static_cast<int>(numeric_literal));
        }
        return VariableValue(numeric_literal);
    }

    if (containsArithmeticOperator(trimmed)) {
        NumericExpressionParser parser(trimmed, var_mgr_);
        const double numeric_value = parser.parse();
        if (std::fabs(numeric_value - std::round(numeric_value)) < 1e-9) {
            return VariableValue(static_cast<int>(std::llround(numeric_value)));
        }
        return VariableValue(numeric_value);
    }

    return VariableValue(trimmed);
}

ExecutionResult GenericExecutor::executeNode(
    const BehaviorNode& node,
    std::shared_ptr<ExecutionContext> ctx) {
    
    if (ctx && ctx->cancelled.load()) {
        return ExecutionResult::Failure("任务已取消", node.name);
    }
    
    log("DEBUG", "执行节点: " + node.name + " [" + getNodeTypeName(node.type) + "]");

    applyObservationValues(node);
    
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
            result = executeAction(node);
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

    if (result.success && !node.params.empty()) {
        appendNodeParamsToOutputs(node, var_mgr_, result);
        for (const auto& param : node.params) {
            result.outputs.erase(param.first);
        }
    }
    
    state_mgr_.updateState(node.name, result.success ? NodeState::SUCCESS : NodeState::FAILED);
    log("DEBUG", "节点执行结果: " + node.name + " -> " + (result.success ? "成功" : "失败"));
    
    return result;
}

ExecutionResult GenericExecutor::executeAction(const BehaviorNode& node) {
    log("DEBUG", "执行动作: " + node.command);

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
    ExecutionResult seq_result = ExecutionResult::Success();
    for (const auto& child : node.children) {
        auto result = executeNode(child, ctx);
        if (!result.success) {
            log("DEBUG", "顺序节点中断: 子节点失败");
            return result;
        }
        mergeOutputs(seq_result.outputs, result.outputs);
    }
    return seq_result;
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
    ExecutionResult parallel_result = ExecutionResult::Success();
    
    for (const auto& child : node.children) {
        auto result = executeNode(child, ctx);
        if (!result.success && all_success) {
            all_success = false;
            first_failed_node = result.failed_node.empty() ? child.name : result.failed_node;
        }
        if (result.success) {
            mergeOutputs(parallel_result.outputs, result.outputs);
        }
    }
    
    if (all_success) {
        return parallel_result;
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

    return executeBuiltinCommand(command, params);
}

ExecutionResult GenericExecutor::executeBuiltinCommand(
    const std::string& command,
    const std::map<std::string, std::string>& params) {
    
    log("DEBUG", "执行命令: " + command);

    ExecutionResult result = ExecutionResult::Success();
    const std::string command_upper = toUpperCopy(trimCopy(command));

    if (command_upper == "NO_OP") {
        return ExecutionResult::Success("NO_OP");
    }

    if (command_upper == "ASSIGN") {
        for (const auto& item : params) {
            const VariableValue evaluated = evaluateValueExpression(item.second);
            var_mgr_.set(item.first, evaluated, Scope::LOCAL);
            result.outputs[item.first] = evaluated;
        }
        return result;
    }

    if (command_upper == "MOVE_TO") {
        static const std::pair<const char*, const char*> kPositionKeys[] = {
            {"x", "POS_X"},
            {"y", "POS_Y"},
            {"z", "POS_Z"}
        };

        for (size_t i = 0; i < sizeof(kPositionKeys) / sizeof(kPositionKeys[0]); ++i) {
            const std::string axis = kPositionKeys[i].first;
            const std::string sensor = kPositionKeys[i].second;
            auto it = params.find(axis);
            if (it == params.end()) {
                continue;
            }
            const VariableValue evaluated = evaluateValueExpression(it->second);
            var_mgr_.set(axis, evaluated, Scope::LOCAL);
            var_mgr_.set(sensor, evaluated, Scope::GLOBAL);
            result.outputs[axis] = evaluated;
        }
        return result;
    }

    if (command_upper == "SEND") {
        std::map<std::string, VariableValue> packet;
        for (const auto& item : params) {
            const VariableValue evaluated = evaluateValueExpression(item.second);
            packet[item.first] = evaluated;
            result.outputs[item.first] = evaluated;
        }

        {
            std::lock_guard<std::mutex> lock(g_shared_mailbox_mutex);
            g_shared_mailbox = packet;
        }
        return result;
    }

    if (command_upper == "RECEIVE") {
        std::map<std::string, VariableValue> packet;
        {
            std::lock_guard<std::mutex> lock(g_shared_mailbox_mutex);
            packet = g_shared_mailbox;
        }

        std::string result_to_var;
        auto result_to_it = params.find("result_to");
        if (result_to_it != params.end()) {
            result_to_var = trimCopy(result_to_it->second);
        }

        for (const auto& item : params) {
            if (item.first == "result_to") {
                continue;
            }

            VariableValue value;
            auto packet_it = packet.find(item.first);
            if (packet_it != packet.end()) {
                value = packet_it->second;
            } else {
                value = evaluateValueExpression(item.second);
            }

            var_mgr_.set(item.first, value, Scope::LOCAL);
            result.outputs[item.first] = value;
        }

        if (!result_to_var.empty()) {
            const bool has_packet = !packet.empty();
            var_mgr_.set(result_to_var, VariableValue(has_packet), Scope::LOCAL);
            result.outputs[result_to_var] = VariableValue(has_packet);
        }
        return result;
    }

    if (command_upper == "EMIT_COMMAND") {
        for (const auto& item : params) {
            const VariableValue evaluated = evaluateValueExpression(item.second);
            var_mgr_.set(item.first, evaluated, Scope::INTERMEDIATE);
            result.outputs[item.first] = evaluated;
        }
        return result;
    }

    if (command_upper == "NEXT_TASK") {
        var_mgr_.set("next_task", VariableValue(true), Scope::LOCAL);
        return ExecutionResult::Success("NEXT_TASK");
    }

    if (command_upper == "REPORT_TARGET") {
        var_mgr_.set("target_reported", VariableValue(true), Scope::GLOBAL);
        for (const auto& item : params) {
            const VariableValue evaluated = evaluateValueExpression(item.second);
            var_mgr_.set("reported_" + item.first, evaluated, Scope::GLOBAL);
            result.outputs[item.first] = evaluated;
        }
        return result;
    }

    if (command_upper == "EXIT") {
        var_mgr_.set("mission_exit", VariableValue(true), Scope::GLOBAL);
        return ExecutionResult::Success("EXIT");
    }

    if (command_upper.find("SETVAR") != std::string::npos) {
        size_t eq_pos = command.find('=');
        if (eq_pos != std::string::npos && eq_pos > 6) {
            std::string var_name = trimCopy(command.substr(6, eq_pos - 6));
            std::string var_expr = trimCopy(command.substr(eq_pos + 1));
            if (var_name.empty()) {
                return ExecutionResult::Failure("setvar 缺少变量名");
            }
            const VariableValue evaluated = evaluateValueExpression(var_expr);
            var_mgr_.set(var_name, evaluated, Scope::GLOBAL);
            result.outputs[var_name] = evaluated;
            return result;
        }
        return ExecutionResult::Failure("setvar 命令格式错误: " + command);
    }

    return ExecutionResult::Failure("不支持的命令: " + command);
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
    oss << "[" << config_.executor_id << "]";
    if (level != "INFO") {
        oss << "[" << level << "]";
    }
    oss << " " << message;
    LOG(oss.str());
}

} // namespace executor
