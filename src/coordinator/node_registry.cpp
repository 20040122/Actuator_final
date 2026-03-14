#include "node_registry.h"
#include <iostream>

namespace coordinator {

NodeRegistry::NodeRegistry()
    : initialized_(false) {
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
}

bool NodeRegistry::registerNode(const NodeRegisterMessage& register_msg,
                                std::string& node_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!initialized_) {
        return false;
    }
    
    node_id = register_msg.node_id;
    NodeInfo info;
    info.node_id = node_id;
    info.node_name = register_msg.node_name;
    info.node_type = register_msg.node_type;
    info.status = NodeStatus::HEALTHY;
    info.register_time_ms = ::getCurrentTimeMs();
    info.last_heartbeat_ms = info.register_time_ms;
    info.is_online = true;
    
    nodes_[node_id] = info;
    
    return true;
}

bool NodeRegistry::unregisterNode(const std::string& node_id, const std::string& reason) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = nodes_.find(node_id);
    if (it == nodes_.end()) {
        return false;
    }
    
    nodes_.erase(it);
    
    return true;
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

}
