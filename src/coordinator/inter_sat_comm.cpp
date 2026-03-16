#include "inter_sat_comm.h"
#include "../third_party/nlohmann/json.hpp"
#include "../core/logger.h"
#include <iostream>
#include <sstream>
#include <cstring>
#include <cerrno>
#include <algorithm>
#include <utility>
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

namespace {

constexpr uint32_t kMaxFrameSizeBytes = 8 * 1024 * 1024;

std::string makeEndpointNodeId(const std::string& ip, uint16_t port) {
    std::ostringstream oss;
    oss << ip << ":" << port;
    return oss.str();
}

bool readUint16BE(const std::vector<uint8_t>& data, size_t& pos, uint16_t& out) {
    if (pos + 2 > data.size()) {
        return false;
    }
    out = static_cast<uint16_t>(data[pos] << 8) |
          static_cast<uint16_t>(data[pos + 1]);
    pos += 2;
    return true;
}

bool readUint32BE(const std::vector<uint8_t>& data, size_t& pos, uint32_t& out) {
    if (pos + 4 > data.size()) {
        return false;
    }
    out = (static_cast<uint32_t>(data[pos]) << 24) |
          (static_cast<uint32_t>(data[pos + 1]) << 16) |
          (static_cast<uint32_t>(data[pos + 2]) << 8) |
          static_cast<uint32_t>(data[pos + 3]);
    pos += 4;
    return true;
}

bool readUint64BE(const std::vector<uint8_t>& data, size_t& pos, uint64_t& out) {
    if (pos + 8 > data.size()) {
        return false;
    }
    out = (static_cast<uint64_t>(data[pos]) << 56) |
          (static_cast<uint64_t>(data[pos + 1]) << 48) |
          (static_cast<uint64_t>(data[pos + 2]) << 40) |
          (static_cast<uint64_t>(data[pos + 3]) << 32) |
          (static_cast<uint64_t>(data[pos + 4]) << 24) |
          (static_cast<uint64_t>(data[pos + 5]) << 16) |
          (static_cast<uint64_t>(data[pos + 6]) << 8) |
          static_cast<uint64_t>(data[pos + 7]);
    pos += 8;
    return true;
}

bool readString(const std::vector<uint8_t>& data, size_t& pos, uint8_t length, std::string& out) {
    if (pos + length > data.size()) {
        return false;
    }
    out.assign(reinterpret_cast<const char*>(&data[pos]), length);
    pos += length;
    return true;
}

bool deserializeMessage(const std::vector<uint8_t>& data, Message& out) {
    out = Message();
    size_t pos = 0;

    if (!readUint32BE(data, pos, out.header.magic)) {
        return false;
    }
    if (!readUint16BE(data, pos, out.header.version)) {
        return false;
    }

    uint16_t msg_type = 0;
    if (!readUint16BE(data, pos, msg_type)) {
        return false;
    }
    out.header.msg_type = static_cast<MessageType>(msg_type);

    if (!readUint32BE(data, pos, out.header.sequence_id)) {
        return false;
    }
    if (!readUint64BE(data, pos, out.header.timestamp_ms)) {
        return false;
    }

    if (pos >= data.size()) {
        return false;
    }
    const uint8_t src_len = data[pos++];
    if (!readString(data, pos, src_len, out.header.source_node_id)) {
        return false;
    }

    if (pos >= data.size()) {
        return false;
    }
    const uint8_t dst_len = data[pos++];
    if (!readString(data, pos, dst_len, out.header.dest_node_id)) {
        return false;
    }

    if (pos >= data.size()) {
        return false;
    }
    const uint8_t priority_raw = data[pos++];
    if (priority_raw < static_cast<uint8_t>(Priority::EMERGENCY) ||
        priority_raw > static_cast<uint8_t>(Priority::LOW)) {
        out.header.priority = Priority::NORMAL;
    } else {
        out.header.priority = static_cast<Priority>(priority_raw);
    }

    if (!readUint32BE(data, pos, out.header.payload_size)) {
        return false;
    }
    if (!readUint32BE(data, pos, out.header.checksum)) {
        return false;
    }

    if (out.header.payload_size > data.size() - pos) {
        return false;
    }

    out.payload.assign(data.begin() + pos, data.begin() + pos + out.header.payload_size);
    return true;
}

bool recvAll(socket_t socket_fd, uint8_t* buffer, size_t length) {
    size_t total_received = 0;
    while (total_received < length) {
        int received = recv(
            socket_fd,
            reinterpret_cast<char*>(buffer + total_received),
            static_cast<int>(length - total_received),
            0
        );
        if (received <= 0) {
            return false;
        }
        total_received += static_cast<size_t>(received);
    }
    return true;
}

} // namespace

// MessageQueue ʵ��
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

// MessageSerializer ʵ��
std::vector<uint8_t> MessageSerializer::serialize(const Message& message) {
    auto header_data = serializeHeader(message.header);
    std::vector<uint8_t> result;
    result.reserve(header_data.size() + message.payload.size());
    result.insert(result.end(), header_data.begin(), header_data.end());
    result.insert(result.end(), message.payload.begin(), message.payload.end());
    return result;
}
std::vector<uint8_t> MessageSerializer::serializeHeader(const MessageHeader& header) {
    std::vector<uint8_t> data;
    const size_t header_size =
        4 + 2 + 2 + 4 + 8 +
        1 + header.source_node_id.size() +
        1 + header.dest_node_id.size() +
        1 + 4 + 4;
    data.reserve(header_size);
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

// InterSatComm ʵ��
InterSatComm::InterSatComm(const CommConfig& config)
    : config_(config)
    , running_(false)
    , initialized_(false)
    , send_queue_(config.message_queue_size)
    , recv_queue_(config.message_queue_size)
    , sequence_id_(0)
    , listen_socket_(INVALID_SOCKET_VALUE) {
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

    {
        std::lock_guard<std::mutex> lock(nodes_mutex_);
        for (auto& pair : nodes_) {
            if (pair.second.socket_fd != INVALID_SOCKET_VALUE) {
                closesocket(pair.second.socket_fd);
                pair.second.socket_fd = INVALID_SOCKET_VALUE;
            }
        }
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
    {
        std::lock_guard<std::mutex> lock(local_handlers_mutex_);
        local_handlers_.clear();
    }

    send_queue_.clear();
    recv_queue_.clear();
    
#ifdef _WIN32
    WSACleanup();
#endif
    
    initialized_.store(false);
}

bool InterSatComm::sendMessage(const std::string& dest_node_id, const Message& message) {
    Message outbound = message;
    const std::string target_node = dest_node_id.empty() ? outbound.header.dest_node_id : dest_node_id;
    if (target_node.empty()) {
        LOG_ERROR("[InterSatComm] sendMessage failed: empty destination");
        return false;
    }

    outbound.header.magic = MessageHeader::MAGIC_NUMBER;
    outbound.header.version = MessageHeader::PROTOCOL_VERSION;
    outbound.header.dest_node_id = target_node;
    if (outbound.header.source_node_id.empty()) {
        outbound.header.source_node_id = config_.node_id;
    }
    if (outbound.header.priority != Priority::LOW &&
        outbound.header.priority != Priority::NORMAL &&
        outbound.header.priority != Priority::URGENT &&
        outbound.header.priority != Priority::EMERGENCY) {
        outbound.header.priority = Priority::NORMAL;
    }
    if (outbound.header.timestamp_ms == 0) {
        outbound.header.timestamp_ms = ::getCurrentTimeMs();
    }
    if (outbound.header.sequence_id == 0) {
        outbound.header.sequence_id = getNextSequenceId();
    }
    outbound.header.payload_size = static_cast<uint32_t>(outbound.payload.size());
    outbound.header.checksum = 0;

    MessageHandler handler;
    {
        std::lock_guard<std::mutex> lock(local_handlers_mutex_);
        auto it = local_handlers_.find(target_node);
        if (it != local_handlers_.end()) {
            handler = it->second;
        }
    }

    if (handler) {
        std::thread(handler, outbound).detach();
        return true;
    }

    return send_queue_.push(outbound);
}

void InterSatComm::registerLocalHandler(const std::string& node_id, MessageHandler handler) {
    if (node_id.empty() || !handler) {
        return;
    }
    std::lock_guard<std::mutex> lock(local_handlers_mutex_);
    local_handlers_[node_id] = handler;
}

void InterSatComm::unregisterLocalHandler(const std::string& node_id) {
    std::lock_guard<std::mutex> lock(local_handlers_mutex_);
    local_handlers_.erase(node_id);
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
        std::vector<std::pair<std::string, socket_t>> node_sockets;
        {
            std::lock_guard<std::mutex> lock(nodes_mutex_);
            node_sockets.reserve(nodes_.size());
            for (const auto& pair : nodes_) {
                node_sockets.push_back(std::make_pair(pair.first, pair.second.socket_fd));
            }
        }

        if (node_sockets.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        fd_set read_fds;
        FD_ZERO(&read_fds);
        bool has_valid_socket = false;
        socket_t max_fd = 0;
        for (const auto& pair : node_sockets) {
            if (pair.second == INVALID_SOCKET_VALUE) {
                continue;
            }
            has_valid_socket = true;
            FD_SET(pair.second, &read_fds);
#ifndef _WIN32
            if (pair.second > max_fd) {
                max_fd = pair.second;
            }
#endif
        }

        if (!has_valid_socket) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000;

#ifdef _WIN32
        const int ready = select(0, &read_fds, nullptr, nullptr, &timeout);
#else
        const int ready = select(static_cast<int>(max_fd + 1), &read_fds, nullptr, nullptr, &timeout);
#endif
        if (ready <= 0) {
            continue;
        }

        std::vector<std::string> failed_nodes;
        for (const auto& pair : node_sockets) {
            if (pair.second == INVALID_SOCKET_VALUE || !FD_ISSET(pair.second, &read_fds)) {
                continue;
            }

            std::vector<uint8_t> raw_message;
            if (!receiveData(pair.second, raw_message)) {
                failed_nodes.push_back(pair.first);
                continue;
            }

            Message decoded;
            if (!deserializeMessage(raw_message, decoded)) {
                LOG_ERROR("[InterSatComm] Invalid incoming message frame");
                continue;
            }

            {
                std::lock_guard<std::mutex> lock(nodes_mutex_);
                auto current_it = nodes_.find(pair.first);
                if (current_it != nodes_.end()) {
                    current_it->second.messages_received++;
                    current_it->second.bytes_received += raw_message.size();
                    current_it->second.last_heartbeat_time_ms = ::getCurrentTimeMs();

                    if (!decoded.header.source_node_id.empty() &&
                        decoded.header.source_node_id != pair.first) {
                        RemoteNode updated = current_it->second;
                        updated.node_id = decoded.header.source_node_id;
                        nodes_.erase(current_it);
                        nodes_[updated.node_id] = updated;
                    }
                }
            }

            processReceivedMessage(decoded);
        }

        for (const auto& node_id : failed_nodes) {
            removeNode(node_id);
        }
    }
}

void InterSatComm::sendThreadFunc() {
    while (running_.load()) {
        Message message;
        if (send_queue_.pop(message, 100)) {
            socket_t target_socket = INVALID_SOCKET_VALUE;
            {
                std::lock_guard<std::mutex> lock(nodes_mutex_);
                auto it = nodes_.find(message.header.dest_node_id);
                if (it != nodes_.end()) {
                    target_socket = it->second.socket_fd;
                }
            }

            if (target_socket == INVALID_SOCKET_VALUE) {
                LOG_WARN("[InterSatComm] Destination node not connected: " + message.header.dest_node_id);
                continue;
            }

            auto data = MessageSerializer::serialize(message);
            if (!sendData(target_socket, data)) {
                removeNode(message.header.dest_node_id);
                continue;
            }

            std::lock_guard<std::mutex> lock(nodes_mutex_);
            auto it = nodes_.find(message.header.dest_node_id);
            if (it != nodes_.end() && it->second.socket_fd == target_socket) {
                it->second.messages_sent++;
                it->second.bytes_sent += data.size();
            }
        }
    }
}

void InterSatComm::heartbeatThreadFunc() {
    while (running_.load()) {
        const uint32_t interval_ms = std::max<uint32_t>(config_.heartbeat_interval_ms, 100);
        uint32_t waited_ms = 0;
        while (running_.load() && waited_ms < interval_ms) {
            const uint32_t step_ms = std::min<uint32_t>(100, interval_ms - waited_ms);
            std::this_thread::sleep_for(std::chrono::milliseconds(step_ms));
            waited_ms += step_ms;
        }
        if (!running_.load()) {
            break;
        }
        checkNodeTimeouts();
    }
}

void InterSatComm::processReceivedMessage(const Message& message) {
    if (!message.isValid()) {
        LOG_ERROR("[InterSatComm] Dropping invalid message frame");
        return;
    }

    std::vector<MessageHandler> handlers;
    {
        std::lock_guard<std::mutex> lock(local_handlers_mutex_);
        if (message.isBroadcast()) {
            for (const auto& pair : local_handlers_) {
                handlers.push_back(pair.second);
            }
        } else {
            auto it = local_handlers_.find(message.header.dest_node_id);
            if (it != local_handlers_.end()) {
                handlers.push_back(it->second);
            }
        }
    }

    for (const auto& handler : handlers) {
        if (handler) {
            handler(message);
        }
    }
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

void InterSatComm::checkNodeTimeouts() {
    uint64_t now = ::getCurrentTimeMs();
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

uint32_t InterSatComm::getNextSequenceId() {
    return sequence_id_.fetch_add(1);
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
    node.node_id = makeEndpointNodeId(node.ip_address, node.port);
    node.connect_time_ms = ::getCurrentTimeMs();
    node.last_heartbeat_time_ms = node.connect_time_ms;
    node.status = NodeStatus::INITIALIZING;
    
    return true;
}

bool InterSatComm::sendData(socket_t socket_fd, const std::vector<uint8_t>& data) {
    if (data.empty() || data.size() > kMaxFrameSizeBytes) {
        return false;
    }

    const uint32_t frame_size = static_cast<uint32_t>(data.size());
    uint8_t frame_header[4];
    frame_header[0] = static_cast<uint8_t>((frame_size >> 24) & 0xFF);
    frame_header[1] = static_cast<uint8_t>((frame_size >> 16) & 0xFF);
    frame_header[2] = static_cast<uint8_t>((frame_size >> 8) & 0xFF);
    frame_header[3] = static_cast<uint8_t>(frame_size & 0xFF);

    auto send_all = [socket_fd](const uint8_t* buffer, size_t length) -> bool {
        size_t total_sent = 0;
        while (total_sent < length) {
            int sent = send(
                socket_fd,
                reinterpret_cast<const char*>(buffer + total_sent),
                static_cast<int>(length - total_sent),
                0
            );
            if (sent <= 0) {
                return false;
            }
            total_sent += static_cast<size_t>(sent);
        }
        return true;
    };

    if (!send_all(frame_header, sizeof(frame_header))) {
        return false;
    }
    if (!send_all(data.data(), data.size())) {
        return false;
    }

    return true;
}

bool InterSatComm::receiveData(socket_t socket_fd, std::vector<uint8_t>& data) {
    uint8_t frame_header[4];
    if (!recvAll(socket_fd, frame_header, sizeof(frame_header))) {
        return false;
    }

    const uint32_t frame_size = (static_cast<uint32_t>(frame_header[0]) << 24) |
                                (static_cast<uint32_t>(frame_header[1]) << 16) |
                                (static_cast<uint32_t>(frame_header[2]) << 8) |
                                static_cast<uint32_t>(frame_header[3]);
    if (frame_size == 0 || frame_size > kMaxFrameSizeBytes) {
        LOG_ERROR("[InterSatComm] Invalid frame size: " + std::to_string(frame_size));
        return false;
    }

    data.resize(frame_size);
    if (!recvAll(socket_fd, data.data(), data.size())) {
        data.clear();
        return false;
    }
    return true;
}

} // namespace coordinator
