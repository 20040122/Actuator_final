#include "core/types.h"
#include "executor/generic_executor.h"

#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct TestFailure : public std::runtime_error {
    explicit TestFailure(const std::string& message)
        : std::runtime_error(message) {
    }
};

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw TestFailure(message);
    }
}

bool containsText(const std::string& text, const std::string& needle) {
    return text.find(needle) != std::string::npos;
}

executor::ExecutorConfig makeTestConfig() {
    executor::ExecutorConfig config = executor::ExecutorConfig::getDefault("TEST");
    config.async_mode = false;
    config.debug_mode = false;
    config.max_concurrent_tasks = 0;
    return config;
}

TaskSegment makeTask(const std::map<std::string, std::string>& params) {
    TaskSegment task;
    task.segment_id = "TEST_SEGMENT";
    task.task_id = "TEST_TASK";
    task.satellite_id = "TEST_SAT";
    task.behavior_ref = "TestBehavior";
    task.behavior_params = params;
    return task;
}

BehaviorNode makeActionNode(const std::string& name,
                            const std::string& command,
                            const std::map<std::string, std::string>& params =
                                std::map<std::string, std::string>()) {
    BehaviorNode node;
    node.name = name;
    node.type = NodeType::ACTION;
    node.command = command;
    node.params = params;
    return node;
}

BehaviorNode makeConditionNode(const std::string& name, const std::string& expression) {
    BehaviorNode node;
    node.name = name;
    node.type = NodeType::CONDITION;
    node.expression = expression;
    return node;
}

BehaviorNode makeCompositeNode(const std::string& name,
                               NodeType type,
                               const std::vector<BehaviorNode>& children) {
    BehaviorNode node;
    node.name = name;
    node.type = type;
    node.children = children;
    return node;
}

void testConditionFalseStopsSequence() {
    executor::GenericExecutor exec(makeTestConfig());
    require(exec.initialize(), "executor initialize failed");

    BehaviorNode behavior = makeCompositeNode(
        "root_sequence",
        NodeType::SEQUENCE,
        std::vector<BehaviorNode>{
            makeConditionNode("guard_false", "value == 2"),
            makeActionNode("should_not_run", "SETVAR sequence_reached=1")
        }
    );

    executor::ExecutionResult result = exec.executeTask(
        makeTask(std::map<std::string, std::string>{{"value", "1"}}),
        behavior
    );

    require(!result.success, "expected sequence to fail when condition is false");
    require(result.failed_node == "guard_false", "unexpected failed node: " + result.failed_node);
    require(!exec.getVariableManager().exists("sequence_reached"),
            "action after failed condition should not execute");

    exec.shutdown();
}

void testConditionMissingVariableFails() {
    executor::GenericExecutor exec(makeTestConfig());
    require(exec.initialize(), "executor initialize failed");

    BehaviorNode behavior = makeCompositeNode(
        "root_sequence",
        NodeType::SEQUENCE,
        std::vector<BehaviorNode>{
            makeConditionNode("missing_guard", "missing_value == 1"),
            makeActionNode("should_not_run", "SETVAR unreachable=1")
        }
    );

    executor::ExecutionResult result = exec.executeTask(makeTask(std::map<std::string, std::string>()), behavior);

    require(!result.success, "expected missing variable condition to fail");
    require(result.failed_node == "missing_guard", "unexpected failed node: " + result.failed_node);
    require(containsText(result.message, "Variable not found"),
            "expected missing variable error, got: " + result.message);
    require(!exec.getVariableManager().exists("unreachable"),
            "action should not execute after missing variable failure");

    exec.shutdown();
}

void testConditionStringMismatchFails() {
    executor::GenericExecutor exec(makeTestConfig());
    require(exec.initialize(), "executor initialize failed");

    BehaviorNode behavior = makeCompositeNode(
        "root_sequence",
        NodeType::SEQUENCE,
        std::vector<BehaviorNode>{
            makeConditionNode("mode_guard", "mode == 'alpha'"),
            makeActionNode("should_not_run", "SETVAR string_match=1")
        }
    );

    executor::ExecutionResult result = exec.executeTask(
        makeTask(std::map<std::string, std::string>{{"mode", "beta"}}),
        behavior
    );

    require(!result.success, "expected string mismatch condition to fail");
    require(result.failed_node == "mode_guard", "unexpected failed node: " + result.failed_node);
    require(containsText(result.message, "Constraint not satisfied"),
            "expected constraint mismatch message, got: " + result.message);
    require(!exec.getVariableManager().exists("string_match"),
            "action should not execute when string condition is false");

    exec.shutdown();
}

void testSelectorFallsBackAfterConditionFailure() {
    executor::GenericExecutor exec(makeTestConfig());
    require(exec.initialize(), "executor initialize failed");

    BehaviorNode guarded_branch = makeCompositeNode(
        "guarded_branch",
        NodeType::SEQUENCE,
        std::vector<BehaviorNode>{
            makeConditionNode("guard_false", "armed == true"),
            makeActionNode("primary_action", "SETVAR primary_taken=1")
        }
    );

    BehaviorNode fallback_branch = makeActionNode("fallback_action", "SETVAR fallback_taken=1");
    BehaviorNode behavior = makeCompositeNode(
        "root_selector",
        NodeType::SELECTOR,
        std::vector<BehaviorNode>{guarded_branch, fallback_branch}
    );

    executor::ExecutionResult result = exec.executeTask(
        makeTask(std::map<std::string, std::string>{{"armed", "false"}}),
        behavior
    );

    require(result.success, "selector should succeed via fallback branch");
    require(exec.getVariableManager().exists("fallback_taken"),
            "fallback branch should run after guarded branch condition failure");
    require(!exec.getVariableManager().exists("primary_taken"),
            "guarded branch action should not run when condition is false");

    exec.shutdown();
}

} // namespace

int main() {
    struct TestCase {
        const char* name;
        void (*func)();
    };

    const std::vector<TestCase> tests = {
        {"condition_false_stops_sequence", &testConditionFalseStopsSequence},
        {"condition_missing_variable_fails", &testConditionMissingVariableFails},
        {"condition_string_mismatch_fails", &testConditionStringMismatchFails},
        {"selector_falls_back_after_condition_failure", &testSelectorFallsBackAfterConditionFailure}
    };

    int failed = 0;

    for (size_t i = 0; i < tests.size(); ++i) {
        try {
            tests[i].func();
            std::cout << "[PASS] " << tests[i].name << std::endl;
        } catch (const std::exception& e) {
            ++failed;
            std::cerr << "[FAIL] " << tests[i].name << ": " << e.what() << std::endl;
        }
    }

    if (failed != 0) {
        std::ostringstream oss;
        oss << failed << " test(s) failed";
        std::cerr << oss.str() << std::endl;
        return 1;
    }

    std::cout << tests.size() << " test(s) passed" << std::endl;
    return 0;
}
