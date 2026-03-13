#include "state_manager.h"
#include "../core/logger.h"
#include <sstream>

void CommandStateManager::setExecutorId(const std::string& executor_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    executor_id_ = executor_id;
}

std::string CommandStateManager::getExecutorId() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return executor_id_;
}

void CommandStateManager::updateState(const std::string& node_id, NodeState state) {
    NodeState old_state = NodeState::NOT_STARTED;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = states_.find(node_id);
        if (it != states_.end()) {
            old_state = it->second;
        }
        states_[node_id] = state;
    }
    logTransition(node_id, old_state, state);
}

NodeState CommandStateManager::getState(const std::string& node_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = states_.find(node_id);
    if (it != states_.end()) {
        return it->second;
    }
    return NodeState::NOT_STARTED;
}

void CommandStateManager::logTransition(const std::string& node_id, 
                                        NodeState from, NodeState to) const {
    const char* state_names[] = {"NOT_STARTED", "READY", "RUNNING", "SUCCESS", "FAILED", "BLOCKED"};
    std::string executor_id;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        executor_id = executor_id_;
    }
    std::ostringstream oss;
    if (!executor_id.empty()) {
        oss << "[" << executor_id << "] ";
    }
    oss << "状态转换 [" << node_id << "]: " 
        << state_names[static_cast<int>(from)] << " -> " 
        << state_names[static_cast<int>(to)];
    LOG_DEBUG(oss.str());
}

void CommandStateManager::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    states_.clear();
}
