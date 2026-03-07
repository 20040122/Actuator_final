#include "state_manager.h"
#include "../core/logger.h"
#include <iostream>
#include <sstream>

void CommandStateManager::updateState(const std::string& node_id, NodeState state) {
    NodeState old_state = states_[node_id];
    states_[node_id] = state;
    logTransition(node_id, old_state, state);
}
NodeState CommandStateManager::getState(const std::string& node_id) {
    auto it = states_.find(node_id);
    if (it != states_.end()) {
        return it->second;
    }
    return NodeState::NOT_STARTED;
}
void CommandStateManager::logTransition(const std::string& node_id, 
                                        NodeState from, NodeState to) {
    const char* state_names[] = {"NOT_STARTED", "READY", "RUNNING", "SUCCESS", "FAILED", "BLOCKED"};
    std::ostringstream oss;
    if (!executor_id_.empty()) {
        oss << "[" << executor_id_ << "] ";
    }
    oss << "状态转换 [" << node_id << "]: " 
        << state_names[static_cast<int>(from)] << " -> " 
        << state_names[static_cast<int>(to)];
    LOG(oss.str());
}
void CommandStateManager::reset() {
    states_.clear();
}
