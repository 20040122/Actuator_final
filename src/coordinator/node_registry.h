#ifndef COORDINATOR_NODE_REGISTRY_H
#define COORDINATOR_NODE_REGISTRY_H

#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <chrono>
#include <cstdint>
#include "message_types.h"
#include "../core/types.h"

namespace coordinator {

struct NodeInfo {
    std::string node_id;
    std::string node_name;
    std::string node_type;
    NodeStatus status;
    uint64_t register_time_ms;
    uint64_t last_heartbeat_ms;
    bool is_online;
    
    NodeInfo() : status(NodeStatus::UNKNOWN), 
                 register_time_ms(0), 
                 last_heartbeat_ms(0), 
                 is_online(false) {}
};

class NodeRegistry {
public:
    explicit NodeRegistry(uint64_t heartbeat_timeout_ms = 30000);
    ~NodeRegistry();
    
    NodeRegistry(const NodeRegistry&) = delete;
    NodeRegistry& operator=(const NodeRegistry&) = delete;
    
    bool initialize();
    void shutdown();
    
    bool registerNode(const NodeRegisterMessage& register_msg, std::string& node_id);
    bool unregisterNode(const std::string& node_id, const std::string& reason = "");
    
    bool getNodeInfo(const std::string& node_id, NodeInfo& info) const;
    std::vector<std::string> getAllNodeIds() const;

private:
    void notifyNodeRegistered(const std::string& node_id);
    void notifyNodeUnregistered(const std::string& node_id, const std::string& reason);
    
    std::map<std::string, NodeInfo> nodes_;
    uint64_t heartbeat_timeout_ms_;
    mutable std::mutex mutex_;
    bool initialized_;
};

}

#endif
