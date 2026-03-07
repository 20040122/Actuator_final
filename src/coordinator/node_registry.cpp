#include "node_registry.h"
#include <algorithm>
#include <iostream>

namespace coordinator {

NodeRegistry::NodeRegistry(uint64_t heartbeat_timeout_ms)
    : heartbeat_timeout_ms_(heartbeat_timeout_ms),
      initialized_(false) {
}

NodeRegistry::~NodeRegistry() {
    shutdown();
}

bool NodeRegistry::initialize() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_) {
        return true;
    }
    initialized_ = true;
    return true;
}

void NodeRegistry::shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_) {
        return;
    }
    initialized_ = false;
    nodes_.clear();
    listeners_.clear();
}

bool NodeRegistry::registerNode(const NodeRegisterMessage& register_msg,
                                std::string& node_id,
                                uint64_t& session_token) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!initialized_) {
        return false;
    }
    
    node_id = register_msg.node_id;
    session_token = getCurrentTimeMs();
    
    NodeInfo info;
    info.node_id = node_id;
    info.node_name = register_msg.node_name;
    info.node_type = register_msg.node_type;
    info.status = NodeStatus::HEALTHY;
    info.register_time_ms = getCurrentTimeMs();
    info.last_heartbeat_ms = info.register_time_ms;
    info.is_online = true;
    
    nodes_[node_id] = info;
    notifyNodeRegistered(node_id);
    
    return true;
}

bool NodeRegistry::unregisterNode(const std::string& node_id, const std::string& reason) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = nodes_.find(node_id);
    if (it == nodes_.end()) {
        return false;
    }
    
    nodes_.erase(it);
    notifyNodeUnregistered(node_id, reason);
    
    return true;
}

bool NodeRegistry::hasNode(const std::string& node_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return nodes_.find(node_id) != nodes_.end();
}

bool NodeRegistry::getNodeInfo(const std::string& node_id, NodeInfo& info) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = nodes_.find(node_id);
    if (it == nodes_.end()) {
        return false;
    }
    
    info = it->second;
    return true;
}

std::vector<std::string> NodeRegistry::getAllNodeIds() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<std::string> ids;
    ids.reserve(nodes_.size());
    
    for (const auto& pair : nodes_) {
        ids.push_back(pair.first);
    }
    
    return ids;
}

std::vector<std::string> NodeRegistry::queryNodes(const NodeQueryCriteria& criteria) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<std::string> result;
    
    for (const auto& pair : nodes_) {
        const NodeInfo& info = pair.second;
        
        if (!criteria.node_types.empty() && 
            criteria.node_types.find(info.node_type) == criteria.node_types.end()) {
            continue;
        }
        
        if (!criteria.statuses.empty() && 
            criteria.statuses.find(info.status) == criteria.statuses.end()) {
            continue;
        }
        
        if (criteria.only_online && !info.is_online) {
            continue;
        }
        
        result.push_back(pair.first);
    }
    
    return result;
}

bool NodeRegistry::updateNodeStatus(const std::string& node_id, NodeStatus status) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = nodes_.find(node_id);
    if (it == nodes_.end()) {
        return false;
    }
    
    NodeStatus old_status = it->second.status;
    if (old_status != status) {
        it->second.status = status;
        notifyNodeStatusChanged(node_id, old_status, status);
    }
    
    return true;
}

bool NodeRegistry::updateHeartbeat(const std::string& node_id, const HeartbeatMessage& heartbeat) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = nodes_.find(node_id);
    if (it == nodes_.end()) {
        return false;
    }
    
    uint64_t current_time = getCurrentTimeMs();
    bool was_offline = !it->second.is_online;
    
    it->second.last_heartbeat_ms = current_time;
    it->second.status = heartbeat.status;
    it->second.is_online = true;
    
    if (was_offline) {
        notifyNodeRecovered(node_id);
    }
    
    return true;
}

void NodeRegistry::registerListener(INodeRegistryListener* listener) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (listener) {
        listeners_.push_back(listener);
    }
}

void NodeRegistry::unregisterListener(INodeRegistryListener* listener) {
    std::lock_guard<std::mutex> lock(mutex_);
    listeners_.erase(std::remove(listeners_.begin(), listeners_.end(), listener), listeners_.end());
}

uint64_t NodeRegistry::getCurrentTimeMs() const {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

void NodeRegistry::notifyNodeRegistered(const std::string& node_id) {
    for (auto* listener : listeners_) {
        if (listener) {
            listener->onNodeRegistered(node_id);
        }
    }
}

void NodeRegistry::notifyNodeUnregistered(const std::string& node_id, const std::string& reason) {
    for (auto* listener : listeners_) {
        if (listener) {
            listener->onNodeUnregistered(node_id, reason);
        }
    }
}

void NodeRegistry::notifyNodeStatusChanged(const std::string& node_id, 
                                          NodeStatus old_status, 
                                          NodeStatus new_status) {
    for (auto* listener : listeners_) {
        if (listener) {
            listener->onNodeStatusChanged(node_id, old_status, new_status);
        }
    }
}

void NodeRegistry::notifyNodeTimeout(const std::string& node_id) {
    for (auto* listener : listeners_) {
        if (listener) {
            listener->onNodeTimeout(node_id);
        }
    }
}

void NodeRegistry::notifyNodeRecovered(const std::string& node_id) {
    for (auto* listener : listeners_) {
        if (listener) {
            listener->onNodeRecovered(node_id);
        }
    }
}

}
