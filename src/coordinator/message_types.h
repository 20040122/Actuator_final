#ifndef COORDINATOR_MESSAGE_TYPES_H
#define COORDINATOR_MESSAGE_TYPES_H

#include <string>
#include <vector>
#include <map>
#include <cstdint>

namespace coordinator {

enum class MessageType : uint16_t {
 
    HEARTBEAT           = 0x0001,   
    HEARTBEAT_ACK       = 0x0002,  
    NODE_REGISTER       = 0x0003,   
    NODE_REGISTER_ACK   = 0x0004,   
    NODE_UNREGISTER     = 0x0005,   
    NODE_STATUS_UPDATE  = 0x0006,   
    
    TASK_ASSIGN         = 0x0101,   
    TASK_ASSIGN_ACK     = 0x0102,   
    BATCH_TASK_ASSIGN   = 0x0108,   
    BATCH_TASK_ASSIGN_ACK = 0x0109, 
    TASK_START          = 0x0103,   
    TASK_PROGRESS       = 0x0104,   
    TASK_COMPLETE       = 0x0105,   
    TASK_ABORT          = 0x0106,   
    TASK_CANCEL         = 0x0107,   
  
    STATE_SYNC_REQUEST  = 0x0201,   
    STATE_SYNC_RESPONSE = 0x0202,   
    STATE_BROADCAST     = 0x0203,   
    BARRIER_WAIT        = 0x0204,   
    BARRIER_RELEASE     = 0x0205,   

    ERROR_RESPONSE      = 0xFF01,   
    TIMEOUT_NOTIFICATION= 0xFF02,   
    
    UNKNOWN             = 0xFFFF
};

enum class NodeStatus : uint8_t {
    UNKNOWN         = 0,    
    INITIALIZING    = 1,    
    HEALTHY         = 2,    
    DEGRADED        = 3,    
    BUSY            = 4,    
    OFFLINE         = 5,    
    FAULT           = 6,    
    MAINTENANCE     = 7     
};

enum class Priority : uint8_t {
    LOW         = 4,
    NORMAL      = 3,
    URGENT      = 2,
    EMERGENCY   = 1
};

struct MessageHeader {
    uint32_t        magic;              
    uint16_t        version;           
    MessageType     msg_type;           
    uint32_t        sequence_id;        
    uint64_t        timestamp_ms;       
    std::string     source_node_id;     
    std::string     dest_node_id;       
    Priority        priority;           
    uint32_t        payload_size;       
    uint32_t        checksum;           
    
    static constexpr uint32_t MAGIC_NUMBER = 0x53415443;
    static constexpr uint16_t PROTOCOL_VERSION = 0x0100;
};


struct NodeCapability {
    std::string     capability_id;      
    std::string     description;        
    std::map<std::string, std::string> params;  
};

struct NodeRegisterMessage {
    std::string     node_id;            
    std::string     node_name;        
    std::string     node_type;          
    std::string     ip_address;         
    uint16_t        port;             
    std::vector<NodeCapability> capabilities;  
    std::map<std::string, std::string> metadata; 
};


struct TaskTarget {
    double          latitude_deg;       
    double          longitude_deg;      
    std::string     target_name;        
};


struct ResourceRequirement {
    std::string     resource_id;        
    std::string     semaphore_id;       
    std::string     usage_type;       
    std::string     acquire_at;        
    int             hold_duration_s;    
};

struct TaskStaticConstraint {
    std::string     name;               
    bool            hard;               
    std::string     deadline;           
};

struct TaskDynamicConstraint {
    std::string     name;               
    bool            hard;               
};

struct TaskConstraints {
    std::vector<TaskStaticConstraint> static_constraints;
    std::vector<TaskDynamicConstraint> dynamic_constraints;
};

struct TaskAssignMessage {
    std::string     segment_id;         
    std::string     task_id;            
    std::string     task_name;          
    Priority        priority;           
    int             profit;             
    TaskTarget      target;             
    std::string     behavior_ref;       
    std::map<std::string, std::string> behavior_params;  
    std::vector<ResourceRequirement> resource_requirements; 
    TaskConstraints constraints;        
};


struct BatchTaskAssignMessage {
    std::string     message_id;         
    std::string     satellite_id;       
    std::string     node_id;            
    std::string     plan_id;            
    uint64_t        timestamp;          
    std::vector<TaskAssignMessage> scheduled_tasks;  
    bool            require_ack;        
    uint64_t        ack_timeout_ms;     
};

struct BatchTaskAssignAck {
    std::string     message_id;         
    std::string     satellite_id;       
    std::string     node_id;            
    uint64_t        timestamp;          
    bool            accepted;           
    std::vector<std::string> accepted_task_ids;  
    std::vector<std::string> rejected_task_ids;  
    std::map<std::string, std::string> rejection_reasons; 
};

enum class TaskStatus : uint8_t {
    PENDING     = 0,    
    QUEUED      = 1,    
    RUNNING     = 2,    
    COMPLETED   = 3,    
    FAILED      = 4,    
    ABORTED     = 5,    
    CANCELLED   = 6     
};

struct TaskProgressMessage {
    std::string     segment_id;         
    std::string     task_id;            
    TaskStatus      status;             
    uint8_t         progress_percent;   
    std::string     current_action;     
    std::string     message;            
    std::map<std::string, std::string> output_vars;  
};

struct TaskCompleteMessage {
    std::string     segment_id;         
    std::string     task_id;            
    bool            success;            
    std::string     actual_start;       
    std::string     actual_end;         
    int             actual_profit;      
    std::string     result_summary;     
    std::map<std::string, std::string> output_data;  
};


struct Message {
    MessageHeader   header;           

    std::vector<uint8_t> payload;       

    bool isValid() const {
        return header.magic == MessageHeader::MAGIC_NUMBER;
    }
    
    bool isRequest() const {
        return (static_cast<uint16_t>(header.msg_type) & 0x01) != 0;
    }
    
    bool isBroadcast() const {
        return header.dest_node_id.empty();
    }
};

inline const char* messageTypeToString(MessageType type) {
    switch (type) {
        case MessageType::HEARTBEAT:            return "HEARTBEAT";
        case MessageType::HEARTBEAT_ACK:        return "HEARTBEAT_ACK";
        case MessageType::NODE_REGISTER:        return "NODE_REGISTER";
        case MessageType::NODE_REGISTER_ACK:    return "NODE_REGISTER_ACK";
        case MessageType::NODE_UNREGISTER:      return "NODE_UNREGISTER";
        case MessageType::NODE_STATUS_UPDATE:   return "NODE_STATUS_UPDATE";
        case MessageType::TASK_ASSIGN:          return "TASK_ASSIGN";
        case MessageType::TASK_ASSIGN_ACK:      return "TASK_ASSIGN_ACK";
        case MessageType::BATCH_TASK_ASSIGN:    return "BATCH_TASK_ASSIGN";
        case MessageType::BATCH_TASK_ASSIGN_ACK:return "BATCH_TASK_ASSIGN_ACK";
        case MessageType::TASK_START:           return "TASK_START";
        case MessageType::TASK_PROGRESS:        return "TASK_PROGRESS";
        case MessageType::TASK_COMPLETE:        return "TASK_COMPLETE";
        case MessageType::TASK_ABORT:           return "TASK_ABORT";
        case MessageType::TASK_CANCEL:          return "TASK_CANCEL";
        case MessageType::STATE_SYNC_REQUEST:   return "STATE_SYNC_REQUEST";
        case MessageType::STATE_SYNC_RESPONSE:  return "STATE_SYNC_RESPONSE";
        case MessageType::STATE_BROADCAST:      return "STATE_BROADCAST";
        case MessageType::BARRIER_WAIT:         return "BARRIER_WAIT";
        case MessageType::BARRIER_RELEASE:      return "BARRIER_RELEASE";
        case MessageType::ERROR_RESPONSE:       return "ERROR_RESPONSE";
        case MessageType::TIMEOUT_NOTIFICATION: return "TIMEOUT_NOTIFICATION";
        default:                                return "UNKNOWN";
    }
}

inline const char* nodeStatusToString(NodeStatus status) {
    switch (status) {
        case NodeStatus::UNKNOWN:       return "UNKNOWN";
        case NodeStatus::INITIALIZING:  return "INITIALIZING";
        case NodeStatus::HEALTHY:       return "HEALTHY";
        case NodeStatus::DEGRADED:      return "DEGRADED";
        case NodeStatus::BUSY:          return "BUSY";
        case NodeStatus::OFFLINE:       return "OFFLINE";
        case NodeStatus::FAULT:         return "FAULT";
        case NodeStatus::MAINTENANCE:   return "MAINTENANCE";
        default:                        return "UNKNOWN";
    }
}

inline const char* taskStatusToString(TaskStatus status) {
    switch (status) {
        case TaskStatus::PENDING:   return "PENDING";
        case TaskStatus::QUEUED:    return "QUEUED";
        case TaskStatus::RUNNING:   return "RUNNING";
        case TaskStatus::COMPLETED: return "COMPLETED";
        case TaskStatus::FAILED:    return "FAILED";
        case TaskStatus::ABORTED:   return "ABORTED";
        case TaskStatus::CANCELLED: return "CANCELLED";
        default:                    return "UNKNOWN";
    }
}

inline const char* priorityToString(Priority priority) {
    switch (priority) {
        case Priority::LOW:         return "LOW";
        case Priority::NORMAL:      return "NORMAL";
        case Priority::URGENT:      return "URGENT";
        case Priority::EMERGENCY:   return "EMERGENCY";
        default:                    return "UNKNOWN";
    }
}

} 

#endif
