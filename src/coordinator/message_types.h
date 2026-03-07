#ifndef COORDINATOR_MESSAGE_TYPES_H
#define COORDINATOR_MESSAGE_TYPES_H

#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include <chrono>

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

    SEM_ACQUIRE_REQUEST = 0x0301,  
    SEM_ACQUIRE_GRANTED = 0x0302,   
    SEM_ACQUIRE_DENIED  = 0x0303,   
    SEM_ACQUIRE_QUEUED  = 0x0304,   
    SEM_RELEASE         = 0x0305,   
    SEM_RELEASE_ACK     = 0x0306,   
    SEM_STATUS_QUERY    = 0x0307,   
    SEM_STATUS_RESPONSE = 0x0308,  
    
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

struct HeartbeatMessage {
    std::string     node_id;           
    uint64_t        uptime_ms;         
    NodeStatus      status;             
    uint8_t         cpu_usage_percent;  
    uint8_t         memory_usage_percent; 
    int8_t          battery_percent;    
    uint32_t        active_tasks;       
    uint32_t        pending_messages;   
};

struct HeartbeatAck {
    std::string     node_id;            
    uint64_t        server_time_ms;     
    bool            sync_required;      
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

struct NodeRegisterAck {
    bool            success;            
    std::string     assigned_node_id;   
    std::string     coordinator_id;     
    uint64_t        session_token;      
    uint32_t        heartbeat_interval_ms;  
    std::string     error_message;     
};

struct TaskTarget {
    double          latitude_deg;       
    double          longitude_deg;      
    std::string     target_name;        
};

struct TaskWindow {
    std::string     window_id;          
    int             window_seq;         
    std::string     start;              
    std::string     end;                
};

struct TaskExecution {
    std::string     planned_start;     
    std::string     planned_end;        
    int             duration_s;         
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
    TaskWindow      window;             
    TaskExecution   execution;          
    std::string     behavior_ref;       
    std::map<std::string, std::string> behavior_params;  
    std::vector<ResourceRequirement> resource_requirements; 
    TaskConstraints constraints;        
};

struct TaskAssignAck {
    std::string     segment_id;         
    std::string     task_id;            
    bool            accepted;           
    std::string     reject_reason;      
    std::string     estimated_start;    
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

struct TaskAbortMessage {
    std::string     segment_id;         
    std::string     task_id;            
    std::string     abort_reason;       
    bool            recoverable;        
};

struct StateSyncRequest {
    std::string     requester_id;       
    std::vector<std::string> sync_items; 
    bool            full_sync;          
    uint64_t        last_sync_time_ms;  
};

struct NodeStateData {
    std::string     node_id;           
    NodeStatus      status;             
    int             battery_percent;    
    int             storage_available_mb; 
    std::string     payload_status;     
    std::vector<std::string> active_tasks;  
    uint64_t        last_update_ms;     
};

struct StateSyncResponse {
    std::string     responder_id;       
    std::vector<NodeStateData> node_states;  
    std::map<std::string, int> semaphore_status; 
    uint64_t        sync_time_ms;       
};

struct BarrierWaitMessage {
    std::string     sync_id;            
    std::string     node_id;            
    std::string     barrier_type;       
    NodeStatus      current_status;     
    uint64_t        arrival_time_ms;    
};

struct BarrierReleaseMessage {
    std::string     sync_id;            
    std::vector<std::string> participants;  
    std::vector<std::string> missing_nodes; 
    bool            all_arrived;        
    std::string     release_reason;     
};

struct SemaphoreAcquireRequest {
    std::string     request_id;         
    std::string     node_id;           
    std::string     semaphore_id;       
    std::string     task_id;            
    Priority        priority;           
    int             timeout_ms;         
    bool            blocking;           
};

struct SemaphoreAcquireResponse {
    std::string     request_id;         
    std::string     semaphore_id;       
    bool            granted;            
    bool            queued;             
    int             queue_position;     
    int             estimated_wait_ms;  
    uint64_t        grant_token;        
    std::string     deny_reason;        
};

struct SemaphoreReleaseMessage {
    std::string     node_id;            
    std::string     semaphore_id;       
    uint64_t        grant_token;        
    std::string     task_id;            
};

struct SemaphoreReleaseAck {
    std::string     semaphore_id;       
    bool            success;           
    int             available_permits;  
};

struct SemaphoreStatusQuery {
    std::string     requester_id;       
    std::vector<std::string> semaphore_ids;  
};

struct SemaphoreStatus {
    std::string     semaphore_id;       
    std::string     resource_name;      
    int             max_permits;        
    int             available_permits;  
    std::vector<std::string> holders;   
    int             queue_length;       
};

struct SemaphoreStatusResponse {
    std::vector<SemaphoreStatus> statuses;  
    uint64_t        query_time_ms;      
};

enum class ErrorCode : uint16_t {
    SUCCESS             = 0,
    UNKNOWN_ERROR       = 1,
    INVALID_MESSAGE     = 2,
    TIMEOUT             = 3,
    NODE_NOT_FOUND      = 4,
    SEMAPHORE_NOT_FOUND = 5,
    TASK_NOT_FOUND      = 6,
    PERMISSION_DENIED   = 7,
    RESOURCE_UNAVAILABLE= 8,
    NETWORK_ERROR       = 9,
    PROTOCOL_ERROR      = 10,
    INTERNAL_ERROR      = 11
};

struct ErrorResponse {
    ErrorCode       error_code;         
    std::string     error_message;      
    std::string     original_msg_type;  
    uint32_t        original_seq_id;    
    std::map<std::string, std::string> details; 
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
        case MessageType::SEM_ACQUIRE_REQUEST:  return "SEM_ACQUIRE_REQUEST";
        case MessageType::SEM_ACQUIRE_GRANTED:  return "SEM_ACQUIRE_GRANTED";
        case MessageType::SEM_ACQUIRE_DENIED:   return "SEM_ACQUIRE_DENIED";
        case MessageType::SEM_ACQUIRE_QUEUED:   return "SEM_ACQUIRE_QUEUED";
        case MessageType::SEM_RELEASE:          return "SEM_RELEASE";
        case MessageType::SEM_RELEASE_ACK:      return "SEM_RELEASE_ACK";
        case MessageType::SEM_STATUS_QUERY:     return "SEM_STATUS_QUERY";
        case MessageType::SEM_STATUS_RESPONSE:  return "SEM_STATUS_RESPONSE";
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
