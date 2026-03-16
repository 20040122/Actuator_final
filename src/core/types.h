#ifndef CORE_TYPES_H
#define CORE_TYPES_H

#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <cstdint>

inline uint64_t getCurrentTimeMs() {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

enum class NodeState {
    NOT_STARTED,
    READY,
    RUNNING,
    SUCCESS,
    FAILED,
    BLOCKED
};


enum class NodeType {
    SEQUENCE,
    SELECTOR,
    PARALLEL,
    ACTION,
    CONDITION,
    SUBTREE
};


struct BehaviorNode {
    std::string name;
    NodeType type;
    std::string description;
    std::map<std::string, std::string> params;
    std::vector<BehaviorNode> children;
    std::string expression;
    std::vector<std::string> variables;
    std::string command;
    std::map<std::string, std::string> constants;       // 常量名 -> 值
    std::map<std::string, std::string> variable_inits;  // 变量名 -> 初始值
    std::map<std::string, std::string> observation;     // 变量名 -> 传感器名
};


struct TaskSegment {
    std::string segment_id;
    std::string task_id;
    std::string satellite_id;
    std::string behavior_ref; 
    std::map<std::string, std::string> behavior_params;
};


struct CommandResult {
    bool success;
    std::string message;
    std::map<std::string, std::string> output_vars;
};

#endif