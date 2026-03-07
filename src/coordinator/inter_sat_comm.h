#ifndef COORDINATOR_INTER_SAT_COMM_H
#define COORDINATOR_INTER_SAT_COMM_H

#include <string>
#include <vector>
#include <map>
#include <queue>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <chrono>

#include "message_types.h"

// 平台相关的 socket 类型定义
#ifdef _WIN32
    #include <winsock2.h>
    typedef SOCKET socket_t;
    #define INVALID_SOCKET_VALUE INVALID_SOCKET
#else
    typedef int socket_t;
    #define INVALID_SOCKET_VALUE (-1)
#endif

namespace coordinator {

class InterSatComm;
class MessageSerializer;

struct CommConfig {
    std::string     node_id;
    std::string     node_name;
    std::string     node_type;
    std::string     bind_address;
    uint16_t        bind_port;
    uint32_t        heartbeat_interval_ms;
    uint32_t        heartbeat_timeout_ms;
    uint32_t        max_missed_heartbeats;
    uint32_t        message_queue_size;
    uint32_t        send_buffer_size;
    uint32_t        recv_buffer_size;
    
    static CommConfig getDefault() {
        CommConfig config;
        config.node_id = "NODE_DEFAULT";
        config.node_name = "Default Node";
        config.node_type = "SATELLITE";
        config.bind_address = "0.0.0.0";
        config.bind_port = 8800;
        config.heartbeat_interval_ms = 5000;
        config.heartbeat_timeout_ms = 15000;
        config.max_missed_heartbeats = 3;
        config.message_queue_size = 1000;
        config.send_buffer_size = 65536;
        config.recv_buffer_size = 65536;
        return config;
    }
};

struct RemoteNode {
    std::string     node_id;
    std::string     node_name;
    std::string     node_type;
    std::string     ip_address;
    uint16_t        port;
    NodeStatus      status;
    uint64_t        last_heartbeat_time_ms;
    uint64_t        messages_sent;
    uint64_t        messages_received;
    uint64_t        bytes_sent;
    uint64_t        bytes_received;
    uint64_t        connect_time_ms;
    socket_t        socket_fd;
    
    RemoteNode() : port(0), status(NodeStatus::UNKNOWN),
                   last_heartbeat_time_ms(0),
                   messages_sent(0), messages_received(0), bytes_sent(0), bytes_received(0),
                   connect_time_ms(0), socket_fd(INVALID_SOCKET_VALUE) {}
};

class MessageQueue {
public:
    explicit MessageQueue(size_t max_size = 1000);
    ~MessageQueue();
    bool push(const Message& message);
    bool pop(Message& message, uint32_t timeout_ms = 0);
    size_t size() const;
    bool empty() const;
    void clear();
    
private:
    struct PriorityMessage {
        Message message;
        int priority;
        uint64_t timestamp;
        
        bool operator<(const PriorityMessage& other) const {
            if (priority != other.priority) return priority > other.priority;
            return timestamp > other.timestamp;
        }
    };
    
    std::priority_queue<PriorityMessage> queue_;
    size_t max_size_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
};

class MessageSerializer {
public:
    static std::vector<uint8_t> serialize(const Message& message);
    static std::vector<uint8_t> serializeHeader(const MessageHeader& header);
    static std::vector<uint8_t> serializeBatchTaskAssign(const BatchTaskAssignMessage& msg);
};

class InterSatComm {
public:
    explicit InterSatComm(const CommConfig& config = CommConfig::getDefault());
    ~InterSatComm();
    InterSatComm(const InterSatComm&) = delete;
    InterSatComm& operator=(const InterSatComm&) = delete;
    
    bool initialize();
    bool start();
    void stop();
    bool isRunning() const { return running_.load(); }
    
    bool connectToNode(const std::string& ip_address, uint16_t port);
    void disconnectNode(const std::string& node_id);
    bool getNodeInfo(const std::string& node_id, RemoteNode& info) const;
    std::vector<std::string> getConnectedNodes() const;
    NodeStatus getNodeStatus(const std::string& node_id) const;
    
    bool sendMessage(const std::string& dest_node_id, const Message& message);
    bool sendBatchTaskAssign(const std::string& dest_node_id, const BatchTaskAssignMessage& batch_tasks);
    
    const CommConfig& getConfig() const { return config_; }
    
private:
    void acceptThreadFunc();
    void receiveThreadFunc();
    void sendThreadFunc();
    void heartbeatThreadFunc();
    void processReceivedMessage(const Message& message);
    void addNode(const RemoteNode& node);
    void removeNode(const std::string& node_id);
    void updateNodeHeartbeat(const std::string& node_id);
    void checkNodeTimeouts();
    Message buildMessage(MessageType type, const std::string& dest_node_id,
                         const std::vector<uint8_t>& payload);
    uint32_t getNextSequenceId();
    uint64_t getCurrentTimeMs() const;
    bool initializeSocket();
    void cleanupSocket();
    bool bindSocket();
    bool acceptConnection(RemoteNode& node);
    bool connectSocket(const std::string& ip, uint16_t port, socket_t& socket_fd);
    bool sendData(socket_t socket_fd, const std::vector<uint8_t>& data);
    bool receiveData(socket_t socket_fd, std::vector<uint8_t>& data);
    
    CommConfig config_;
    std::atomic<bool> running_;
    std::atomic<bool> initialized_;
    std::map<std::string, RemoteNode> nodes_;
    mutable std::mutex nodes_mutex_;
    MessageQueue send_queue_;
    MessageQueue recv_queue_;
    std::atomic<uint32_t> sequence_id_;
    uint64_t start_time_ms_;
    socket_t listen_socket_;
    std::unique_ptr<std::thread> accept_thread_;
    std::unique_ptr<std::thread> receive_thread_;
    std::unique_ptr<std::thread> send_thread_;
    std::unique_ptr<std::thread> heartbeat_thread_;
    NodeStatus local_status_;
    std::mutex local_status_mutex_;
};

} // namespace coordinator

#endif // COORDINATOR_INTER_SAT_COMM_H
