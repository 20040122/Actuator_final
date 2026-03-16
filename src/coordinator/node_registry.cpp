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
    alias_to_node_id_.clear();
}

bool NodeRegistry::registerNode(const NodeRegisterMessage& register_msg,
                                std::string& node_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!initialized_) {
        return false;
    }
    
    std::string comm_node_id;
    auto comm_it = register_msg.metadata.find("node_id");
    if (comm_it != register_msg.metadata.end()) {
        comm_node_id = comm_it->second;
    }
    if (comm_node_id.empty()) {
        comm_node_id = register_msg.node_id;
    }

    std::string satellite_id;
    auto sat_it = register_msg.metadata.find("satellite_id");
    if (sat_it != register_msg.metadata.end()) {
        satellite_id = sat_it->second;
    }
    if (satellite_id.empty()) {
        satellite_id = register_msg.node_id;
    }

    if (comm_node_id.empty() && satellite_id.empty()) {
        return false;
    }
    if (comm_node_id.empty()) {
        comm_node_id = satellite_id;
    }
    if (satellite_id.empty()) {
        satellite_id = comm_node_id;
    }

    auto existing_sat = alias_to_node_id_.find(satellite_id);
    if (existing_sat != alias_to_node_id_.end() && existing_sat->second != comm_node_id) {
        nodes_.erase(existing_sat->second);
        eraseAliasesLocked(existing_sat->second);
    }

    node_id = comm_node_id;
    NodeInfo info;
    info.node_id = comm_node_id;
    info.satellite_id = satellite_id;
    info.node_name = register_msg.node_name;
    info.node_type = register_msg.node_type;
    info.status = NodeStatus::HEALTHY;
    info.register_time_ms = ::getCurrentTimeMs();
    info.last_heartbeat_ms = info.register_time_ms;
    info.is_online = true;
    
    nodes_[comm_node_id] = info;
    eraseAliasesLocked(comm_node_id);
    alias_to_node_id_[comm_node_id] = comm_node_id;
    alias_to_node_id_[satellite_id] = comm_node_id;
    if (!register_msg.node_id.empty()) {
        alias_to_node_id_[register_msg.node_id] = comm_node_id;
    }
    
    return true;
}

bool NodeRegistry::unregisterNode(const std::string& node_id, const std::string& reason) {
    std::lock_guard<std::mutex> lock(mutex_);

    const std::string resolved_id = resolveNodeIdLocked(node_id);
    auto it = nodes_.find(resolved_id);
    if (it == nodes_.end()) {
        return false;
    }
    
    nodes_.erase(it);
    eraseAliasesLocked(resolved_id);
    
    return true;
}

bool NodeRegistry::getNodeInfo(const std::string& node_id, NodeInfo& info) const {
    std::lock_guard<std::mutex> lock(mutex_);

    const std::string resolved_id = resolveNodeIdLocked(node_id);
    auto it = nodes_.find(resolved_id);
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

std::string NodeRegistry::resolveNodeIdLocked(const std::string& node_id) const {
    auto it = alias_to_node_id_.find(node_id);
    if (it != alias_to_node_id_.end()) {
        return it->second;
    }
    return node_id;
}

void NodeRegistry::eraseAliasesLocked(const std::string& canonical_node_id) {
    for (auto it = alias_to_node_id_.begin(); it != alias_to_node_id_.end();) {
        if (it->second == canonical_node_id) {
            it = alias_to_node_id_.erase(it);
        } else {
            ++it;
        }
    }
}

}
