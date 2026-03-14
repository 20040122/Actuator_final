#ifndef EXECUTOR_STATE_MANAGER_H
#define EXECUTOR_STATE_MANAGER_H

#include <map>
#include <string>
#include <mutex>
#include "../core/types.h"

class CommandStateManager {
public:
    void setExecutorId(const std::string& executor_id);
    
    void updateState(const std::string& node_id, NodeState state);
    void reset();
    
private:
    void logTransition(const std::string& node_id, 
                       NodeState from, NodeState to) const;
    std::string executor_id_;
    std::map<std::string, NodeState> states_;
    mutable std::mutex mutex_;
};

#endif
