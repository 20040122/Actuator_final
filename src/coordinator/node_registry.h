#ifndef COORDINATOR_NODE_REGISTRY_H
#define COORDINATOR_NODE_REGISTRY_H

#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <chrono>
#include <cstdint>
#include <set>
#include "message_types.h"

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

struct NodeQueryCriteria {
    std::set<std::string> node_types;
    std::set<NodeStatus> statuses;
    bool only_online;
    
    NodeQueryCriteria() : only_online(false) {}
};

class INodeRegistryListener {
public:
    virtual ~INodeRegistryListener() = default;
    virtual void onNodeRegistered(const std::string& node_id) = 0;
    virtual void onNodeUnregistered(const std::string& node_id, const std::string& reason) = 0;
    virtual void onNodeStatusChanged(const std::string& node_id, NodeStatus old_status, NodeStatus new_status) = 0;
    virtual void onNodeTimeout(const std::string& node_id) = 0;
    virtual void onNodeRecovered(const std::string& node_id) = 0;
};

class NodeRegistry {
public:
    explicit NodeRegistry(uint64_t heartbeat_timeout_ms = 30000);
    ~NodeRegistry();
    
    NodeRegistry(const NodeRegistry&) = delete;
    NodeRegistry& operator=(const NodeRegistry&) = delete;
    
    bool initialize();
    void shutdown();
    
    bool registerNode(const NodeRegisterMessage& register_msg, std::string& node_id, uint64_t& session_token);
    bool unregisterNode(const std::string& node_id, const std::string& reason = "");
    
    bool hasNode(const std::string& node_id) const;
    bool getNodeInfo(const std::string& node_id, NodeInfo& info) const;
    std::vector<std::string> getAllNodeIds() const;
    std::vector<std::string> queryNodes(const NodeQueryCriteria& criteria) const;
    
    bool updateNodeStatus(const std::string& node_id, NodeStatus status);
    bool updateHeartbeat(const std::string& node_id, const HeartbeatMessage& heartbeat);
    
    void registerListener(INodeRegistryListener* listener);
    void unregisterListener(INodeRegistryListener* listener);

private:
    uint64_t getCurrentTimeMs() const;
    void notifyNodeRegistered(const std::string& node_id);
    void notifyNodeUnregistered(const std::string& node_id, const std::string& reason);
    void notifyNodeStatusChanged(const std::string& node_id, NodeStatus old_status, NodeStatus new_status);
    void notifyNodeTimeout(const std::string& node_id);
    void notifyNodeRecovered(const std::string& node_id);
    
    std::map<std::string, NodeInfo> nodes_;
    std::vector<INodeRegistryListener*> listeners_;
    uint64_t heartbeat_timeout_ms_;
    mutable std::mutex mutex_;
    bool initialized_;
};

}

#endif
