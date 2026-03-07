#ifndef EXECUTOR_STATE_MANAGER_H
#define EXECUTOR_STATE_MANAGER_H

#include <map>
#include <string>
#include "../core/types.h"

class CommandStateManager {
public:
    void setExecutorId(const std::string& executor_id) { executor_id_ = executor_id; }
    const std::string& getExecutorId() const { return executor_id_; }
    
    void updateState(const std::string& node_id, NodeState state);
    NodeState getState(const std::string& node_id);
    void logTransition(const std::string& node_id, 
                       NodeState from, NodeState to);
    void reset();
    
private:
    std::string executor_id_;
    std::map<std::string, NodeState> states_;
};

#endif