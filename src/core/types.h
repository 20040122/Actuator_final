#ifndef CORE_TYPES_H
#define CORE_TYPES_H

#include <string>
#include <vector>
#include <map>

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
};


struct TaskSegment {
    std::string segment_id;
    std::string task_id;
    std::string satellite_id;
    std::string behavior_ref; 
    std::map<std::string, std::string> behavior_params;
    struct {
        std::string planned_start;
        std::string planned_end;
        int duration_s;
    } execution;
    struct {
        std::string window_id;
        int window_seq;
        std::string start; 
        std::string end;   
    } window;
};


struct CommandResult {
    bool success;
    std::string message;
    std::map<std::string, std::string> output_vars;
};

#endif