#include "inter_sat_comm.h"
#include "../third_party/nlohmann/json.hpp"
#include "../core/logger.h"
#include <iostream>
#include <sstream>
#include <cstring>
#include <cerrno>
#include <algorithm>
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    typedef int socklen_t;
    #ifndef INVALID_SOCKET_VALUE
        #define INVALID_SOCKET_VALUE INVALID_SOCKET
    #endif
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    #ifndef INVALID_SOCKET_VALUE
        #define INVALID_SOCKET_VALUE (-1)
    #endif
    #define SOCKET_ERROR -1
    #define closesocket close
#endif
using json = nlohmann::json;

namespace coordinator {

// MessageQueue 实现
MessageQueue::MessageQueue(size_t max_size) : max_size_(max_size) {}
MessageQueue::~MessageQueue() {
    clear();
}
bool MessageQueue::push(const Message& message) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (queue_.size() >= max_size_) {
        return false;
    }
    PriorityMessage pm;
    pm.message = message;
    pm.priority = static_cast<int>(message.header.priority);
    pm.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    queue_.push(pm);
    cv_.notify_one();
    return true;
}
bool MessageQueue::pop(Message& message, uint32_t timeout_ms) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (timeout_ms == 0) {
        cv_.wait(lock, [this] { return !queue_.empty(); });
    } else {
        if (!cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                         [this] { return !queue_.empty(); })) {
            return false;
        }
    }
    if (queue_.empty()) {
        return false;
    }
    message = queue_.top().message;
    queue_.pop();
    return true;
}
size_t MessageQueue::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
}
bool MessageQueue::empty() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.empty();
}
void MessageQueue::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    while (!queue_.empty()) {
        queue_.pop();
    }
}

// MessageSerializer 实现
std::vector<uint8_t> MessageSerializer::serialize(const Message& message) {
    std::vector<uint8_t> result;
    auto header_data = serializeHeader(message.header);
    result.insert(result.end(), header_data.begin(), header_data.end());
    result.insert(result.end(), message.payload.begin(), message.payload.end());
    return result;
}
std::vector<uint8_t> MessageSerializer::serializeHeader(const MessageHeader& header) {
    std::vector<uint8_t> data;
    data.push_back((header.magic >> 24) & 0xFF);
    data.push_back((header.magic >> 16) & 0xFF);
    data.push_back((header.magic >> 8) & 0xFF);
    data.push_back(header.magic & 0xFF);
    data.push_back((header.version >> 8) & 0xFF);
    data.push_back(header.version & 0xFF);
    uint16_t msg_type = static_cast<uint16_t>(header.msg_type);
    data.push_back((msg_type >> 8) & 0xFF);
    data.push_back(msg_type & 0xFF);
    data.push_back((header.sequence_id >> 24) & 0xFF);
    data.push_back((header.sequence_id >> 16) & 0xFF);
    data.push_back((header.sequence_id >> 8) & 0xFF);
    data.push_back(header.sequence_id & 0xFF);
    data.push_back((header.timestamp_ms >> 56) & 0xFF);
    data.push_back((header.timestamp_ms >> 48) & 0xFF);
    data.push_back((header.timestamp_ms >> 40) & 0xFF);
    data.push_back((header.timestamp_ms >> 32) & 0xFF);
    data.push_back((header.timestamp_ms >> 24) & 0xFF);
    data.push_back((header.timestamp_ms >> 16) & 0xFF);
    data.push_back((header.timestamp_ms >> 8) & 0xFF);
    data.push_back(header.timestamp_ms & 0xFF);
    uint8_t src_len = static_cast<uint8_t>(header.source_node_id.length());
    data.push_back(src_len);
    data.insert(data.end(), header.source_node_id.begin(), header.source_node_id.end());
    uint8_t dst_len = static_cast<uint8_t>(header.dest_node_id.length());
    data.push_back(dst_len);
    data.insert(data.end(), header.dest_node_id.begin(), header.dest_node_id.end());
    data.push_back(static_cast<uint8_t>(header.priority));
    data.push_back((header.payload_size >> 24) & 0xFF);
    data.push_back((header.payload_size >> 16) & 0xFF);
    data.push_back((header.payload_size >> 8) & 0xFF);
    data.push_back(header.payload_size & 0xFF);
    data.push_back((header.checksum >> 24) & 0xFF);
    data.push_back((header.checksum >> 16) & 0xFF);
    data.push_back((header.checksum >> 8) & 0xFF);
    data.push_back(header.checksum & 0xFF);
    return data;
}

std::vector<uint8_t> MessageSerializer::serializeBatchTaskAssign(const BatchTaskAssignMessage& msg) {
    json j;
    j["message_id"] = msg.message_id;
    j["satellite_id"] = msg.satellite_id;
    j["node_id"] = msg.node_id;
    j["plan_id"] = msg.plan_id;
    j["timestamp"] = msg.timestamp;
    j["require_ack"] = msg.require_ack;
    j["ack_timeout_ms"] = msg.ack_timeout_ms;
    
    json tasks = json::array();
    for (const auto& task : msg.scheduled_tasks) {
        json task_j;
        task_j["segment_id"] = task.segment_id;
        task_j["task_id"] = task.task_id;
        task_j["task_name"] = task.task_name;
        task_j["profit"] = task.profit;
        
        task_j["target"]["latitude_deg"] = task.target.latitude_deg;
        task_j["target"]["longitude_deg"] = task.target.longitude_deg;
        
        task_j["window"]["window_id"] = task.window.window_id;
        task_j["window"]["window_seq"] = task.window.window_seq;
        task_j["window"]["start"] = task.window.start;
        task_j["window"]["end"] = task.window.end;
        
        task_j["execution"]["planned_start"] = task.execution.planned_start;
        task_j["execution"]["planned_end"] = task.execution.planned_end;
        task_j["execution"]["duration_s"] = task.execution.duration_s;
        
        task_j["behavior_ref"] = task.behavior_ref;
        task_j["behavior_params"] = task.behavior_params;
        
        json resources = json::array();
        for (const auto& res : task.resource_requirements) {
            json res_j;
            res_j["resource_id"] = res.resource_id;
            res_j["usage_type"] = res.usage_type;
            res_j["acquire_at"] = res.acquire_at;
            res_j["hold_duration_s"] = res.hold_duration_s;
            resources.push_back(res_j);
        }
        task_j["resource_requirements"] = resources;
        
        if (!task.constraints.static_constraints.empty() || !task.constraints.dynamic_constraints.empty()) {
            json static_cons = json::array();
            for (const auto& sc : task.constraints.static_constraints) {
                json c;
                c["name"] = sc.name;
                c["hard"] = sc.hard;
                if (!sc.deadline.empty()) {
                    c["deadline"] = sc.deadline;
                }
                static_cons.push_back(c);
            }
            task_j["constraints"]["static"] = static_cons;
            
            json dynamic_cons = json::array();
            for (const auto& dc : task.constraints.dynamic_constraints) {
                json c;
                c["name"] = dc.name;
                c["hard"] = dc.hard;
                dynamic_cons.push_back(c);
            }
            task_j["constraints"]["dynamic"] = dynamic_cons;
        }
        
        tasks.push_back(task_j);
    }
    j["scheduled_tasks"] = tasks;
    
    std::string str = j.dump();
    return std::vector<uint8_t>(str.begin(), str.end());
}

// InterSatComm 实现
InterSatComm::InterSatComm(const CommConfig& config)
    : config_(config)
    , running_(false)
    , initialized_(false)
    , send_queue_(config.message_queue_size)
    , recv_queue_(config.message_queue_size)
    , sequence_id_(0)
    , start_time_ms_(0)
    , listen_socket_(INVALID_SOCKET_VALUE)
    , local_status_(NodeStatus::INITIALIZING) {
}

InterSatComm::~InterSatComm() {
    stop();
}

bool InterSatComm::initialize() {
    if (initialized_.load()) {
        return true;
    }
    
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        return false;
    }
#endif
    
    if (!initializeSocket()) {
        return false;
    }
    
    initialized_.store(true);
    return true;
}

bool InterSatComm::start() {
    if (running_.load()) {
        return true;
    }
    
    if (!initialized_.load()) {
        if (!initialize()) {
            return false;
        }
    }
    
    running_.store(true);
    start_time_ms_ = getCurrentTimeMs();
    
    accept_thread_ = std::unique_ptr<std::thread>(
        new std::thread(&InterSatComm::acceptThreadFunc, this));
    receive_thread_ = std::unique_ptr<std::thread>(
        new std::thread(&InterSatComm::receiveThreadFunc, this));
    send_thread_ = std::unique_ptr<std::thread>(
        new std::thread(&InterSatComm::sendThreadFunc, this));
    heartbeat_thread_ = std::unique_ptr<std::thread>(
        new std::thread(&InterSatComm::heartbeatThreadFunc, this));
    
    return true;
}

void InterSatComm::stop() {
    if (!running_.load()) {
        return;
    }
    
    LOG("[InterSatComm] Stopping communication module...");
    
    running_.store(false);
    
    LOG("[InterSatComm] Closing listen socket...");
    if (listen_socket_ != INVALID_SOCKET_VALUE) {
        closesocket(listen_socket_);
        listen_socket_ = INVALID_SOCKET_VALUE;
    }
    
    LOG("[InterSatComm] Clearing message queues...");
    send_queue_.clear();
    recv_queue_.clear();
    
    LOG("[InterSatComm] Waiting for threads to finish...");
    if (accept_thread_ && accept_thread_->joinable()) {
        accept_thread_->join();
    }
    if (receive_thread_ && receive_thread_->joinable()) {
        receive_thread_->join();
    }
    if (send_thread_ && send_thread_->joinable()) {
        send_thread_->join();
    }
    if (heartbeat_thread_ && heartbeat_thread_->joinable()) {
        heartbeat_thread_->join();
    }
    
    LOG("[InterSatComm] All threads stopped.");
    
    cleanupSocket();
    
    {
        std::lock_guard<std::mutex> lock(nodes_mutex_);
        nodes_.clear();
    }
    
    send_queue_.clear();
    recv_queue_.clear();
    
#ifdef _WIN32
    WSACleanup();
#endif
    
    initialized_.store(false);
}

bool InterSatComm::connectToNode(const std::string& ip_address, uint16_t port) {
    RemoteNode node;
    node.ip_address = ip_address;
    node.port = port;
    node.status = NodeStatus::INITIALIZING;
    node.connect_time_ms = getCurrentTimeMs();
    
    if (!connectSocket(ip_address, port, node.socket_fd)) {
        return false;
    }
    
    addNode(node);
    return true;
}

void InterSatComm::disconnectNode(const std::string& node_id) {
    removeNode(node_id);
}

bool InterSatComm::getNodeInfo(const std::string& node_id, RemoteNode& info) const {
    std::lock_guard<std::mutex> lock(nodes_mutex_);
    auto it = nodes_.find(node_id);
    if (it != nodes_.end()) {
        info = it->second;
        return true;
    }
    return false;
}

std::vector<std::string> InterSatComm::getConnectedNodes() const {
    std::lock_guard<std::mutex> lock(nodes_mutex_);
    std::vector<std::string> result;
    for (const auto& pair : nodes_) {
        result.push_back(pair.first);
    }
    return result;
}

NodeStatus InterSatComm::getNodeStatus(const std::string& node_id) const {
    std::lock_guard<std::mutex> lock(nodes_mutex_);
    auto it = nodes_.find(node_id);
    if (it != nodes_.end()) {
        return it->second.status;
    }
    return NodeStatus::UNKNOWN;
}

bool InterSatComm::sendMessage(const std::string& dest_node_id, const Message& message) {
    return send_queue_.push(message);
}

bool InterSatComm::sendBatchTaskAssign(const std::string& dest_node_id, 
                                       const BatchTaskAssignMessage& batch_tasks) {
    auto payload = MessageSerializer::serializeBatchTaskAssign(batch_tasks);
    Message msg = buildMessage(MessageType::BATCH_TASK_ASSIGN, dest_node_id, payload);
    return sendMessage(dest_node_id, msg);
}

void InterSatComm::acceptThreadFunc() {
    while (running_.load()) {
        RemoteNode node;
        if (acceptConnection(node)) {
            addNode(node);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void InterSatComm::receiveThreadFunc() {
    while (running_.load()) {
        Message message;
        if (recv_queue_.pop(message, 100)) {
            processReceivedMessage(message);
        }
    }
}

void InterSatComm::sendThreadFunc() {
    while (running_.load()) {
        Message message;
        if (send_queue_.pop(message, 100)) {
            std::lock_guard<std::mutex> lock(nodes_mutex_);
            auto it = nodes_.find(message.header.dest_node_id);
            if (it != nodes_.end()) {
                auto data = MessageSerializer::serialize(message);
                if (sendData(it->second.socket_fd, data)) {
                    it->second.messages_sent++;
                    it->second.bytes_sent += data.size();
                }
            }
        }
    }
}

void InterSatComm::heartbeatThreadFunc() {
    while (running_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(config_.heartbeat_interval_ms));
        if (!running_.load()) break;
        checkNodeTimeouts();
    }
}

void InterSatComm::processReceivedMessage(const Message& message) {
    // 简化的消息处理
}

void InterSatComm::addNode(const RemoteNode& node) {
    std::lock_guard<std::mutex> lock(nodes_mutex_);
    nodes_[node.node_id] = node;
}

void InterSatComm::removeNode(const std::string& node_id) {
    std::lock_guard<std::mutex> lock(nodes_mutex_);
    auto it = nodes_.find(node_id);
    if (it != nodes_.end()) {
        closesocket(it->second.socket_fd);
        nodes_.erase(it);
    }
}

void InterSatComm::updateNodeHeartbeat(const std::string& node_id) {
    std::lock_guard<std::mutex> lock(nodes_mutex_);
    auto it = nodes_.find(node_id);
    if (it != nodes_.end()) {
        it->second.last_heartbeat_time_ms = getCurrentTimeMs();
    }
}

void InterSatComm::checkNodeTimeouts() {
    uint64_t now = getCurrentTimeMs();
    std::vector<std::string> timed_out_nodes;
    
    {
        std::lock_guard<std::mutex> lock(nodes_mutex_);
        for (auto& pair : nodes_) {
            if (now - pair.second.last_heartbeat_time_ms > config_.heartbeat_timeout_ms) {
                timed_out_nodes.push_back(pair.first);
            }
        }
    }
    
    for (const auto& node_id : timed_out_nodes) {
        removeNode(node_id);
    }
}

Message InterSatComm::buildMessage(MessageType type, 
                                   const std::string& dest_node_id,
                                   const std::vector<uint8_t>& payload) {
    Message msg;
    msg.header.magic = MessageHeader::MAGIC_NUMBER;
    msg.header.version = MessageHeader::PROTOCOL_VERSION;
    msg.header.msg_type = type;
    msg.header.sequence_id = getNextSequenceId();
    msg.header.timestamp_ms = getCurrentTimeMs();
    msg.header.source_node_id = config_.node_id;
    msg.header.dest_node_id = dest_node_id;
    msg.header.priority = Priority::NORMAL;
    msg.header.payload_size = static_cast<uint32_t>(payload.size());
    msg.payload = payload;
    msg.header.checksum = 0;
    
    return msg;
}

uint32_t InterSatComm::getNextSequenceId() {
    return sequence_id_.fetch_add(1);
}

uint64_t InterSatComm::getCurrentTimeMs() const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

bool InterSatComm::initializeSocket() {
    listen_socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_socket_ == INVALID_SOCKET_VALUE) {
        LOG_ERROR("[InterSatComm] socket() failed, errno=" + std::to_string(errno) + " (" + std::string(std::strerror(errno)) + ")");
        return false;
    }
    
    int opt = 1;
    setsockopt(listen_socket_, SOL_SOCKET, SO_REUSEADDR, 
               reinterpret_cast<char*>(&opt), sizeof(opt));
    
    return bindSocket();
}

void InterSatComm::cleanupSocket() {
    if (listen_socket_ != INVALID_SOCKET_VALUE) {
        closesocket(listen_socket_);
        listen_socket_ = INVALID_SOCKET_VALUE;
    }
}

bool InterSatComm::bindSocket() {
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(config_.bind_port);
    if (config_.bind_address.empty() || config_.bind_address == "0.0.0.0") {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        in_addr bind_addr;
        if (inet_pton(AF_INET, config_.bind_address.c_str(), &bind_addr) != 1) {
            LOG_ERROR("[InterSatComm] invalid bind address: " + config_.bind_address);
            return false;
        }
        addr.sin_addr = bind_addr;
    }
    
    if (bind(listen_socket_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        LOG_ERROR("[InterSatComm] bind() failed on " + config_.bind_address + ":" + std::to_string(config_.bind_port) + ", errno=" + std::to_string(errno) + " (" + std::string(std::strerror(errno)) + ")");
        return false;
    }
    
    if (listen(listen_socket_, SOMAXCONN) == SOCKET_ERROR) {
        LOG_ERROR("[InterSatComm] listen() failed, errno=" + std::to_string(errno) + " (" + std::string(std::strerror(errno)) + ")");
        return false;
    }
    
    return true;
}

bool InterSatComm::acceptConnection(RemoteNode& node) {
    sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    
    socket_t client_socket = accept(listen_socket_, 
                               reinterpret_cast<sockaddr*>(&client_addr), 
                               &addr_len);
    
    if (client_socket == INVALID_SOCKET_VALUE) {
        return false;
    }
    
    node.socket_fd = client_socket;
    node.ip_address = inet_ntoa(client_addr.sin_addr);
    node.port = ntohs(client_addr.sin_port);
    node.connect_time_ms = getCurrentTimeMs();
    node.status = NodeStatus::INITIALIZING;
    
    return true;
}

bool InterSatComm::connectSocket(const std::string& ip, uint16_t port, socket_t& socket_fd) {
    socket_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket_fd == INVALID_SOCKET_VALUE) {
        return false;
    }
    
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(ip.c_str());
    
    if (connect(socket_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        closesocket(socket_fd);
        socket_fd = INVALID_SOCKET_VALUE;
        return false;
    }
    
    return true;
}

bool InterSatComm::sendData(socket_t socket_fd, const std::vector<uint8_t>& data) {
    size_t total_sent = 0;
    while (total_sent < data.size()) {
        int sent = send(socket_fd, 
                       reinterpret_cast<const char*>(data.data() + total_sent),
                       static_cast<int>(data.size() - total_sent), 
                       0);
        
        if (sent == SOCKET_ERROR) {
            return false;
        }
        
        total_sent += sent;
    }
    
    return true;
}

bool InterSatComm::receiveData(socket_t socket_fd, std::vector<uint8_t>& data) {
    uint8_t buffer[4096];
    int received = recv(socket_fd, reinterpret_cast<char*>(buffer), sizeof(buffer), 0);
    
    if (received <= 0) {
        return false;
    }
    
    data.assign(buffer, buffer + received);
    return true;
}

} // namespace coordinator
