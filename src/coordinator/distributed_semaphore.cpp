#include "distributed_semaphore.h"
#include "inter_sat_comm.h"
#include "../third_party/nlohmann/json.hpp"
#include "../core/logger.h"
#include <sstream>
#include <iostream>
#include <algorithm>

using json = nlohmann::json;

namespace coordinator {

bool SemaphoreRequest::operator<(const SemaphoreRequest& other) const {
    if (priority != other.priority) {
        return priority < other.priority;
    }
    return requested_at > other.requested_at;
}

int DistributedSemaphore::LocalSemaphore::getAllocatedPermits() const {
    int total = 0;
    for (const auto& holder : holders) {
        total += holder.permits;
    }
    return total;
}

int DistributedSemaphore::LocalSemaphore::getAvailablePermits() const {
    return config.max_permits - getAllocatedPermits();
}

DistributedSemaphore::DistributedSemaphore() 
    : running_(false), 
      request_counter_(0) {
}

DistributedSemaphore::~DistributedSemaphore() {
    clear();
}

bool DistributedSemaphore::initialize(std::shared_ptr<InterSatComm> comm, const std::string& local_node_id) {
    if (!comm) {
        LOG_ERROR("[DistributedSemaphore] Invalid communication module");
        return false;
    }

    comm_ = comm;
    local_node_id_ = local_node_id;
    running_ = true;

    LOG("[DistributedSemaphore] Initialized for node: " + local_node_id_);
    return true;
}

bool DistributedSemaphore::registerSemaphore(const DistributedSemaphoreConfig& config) {
    std::lock_guard<std::mutex> lock(global_mutex_);

    if (config.semaphore_id.empty()) {
        LOG_ERROR("[DistributedSemaphore] Empty semaphore ID");
        return false;
    }

    if (semaphores_.find(config.semaphore_id) != semaphores_.end()) {
        LOG_ERROR("[DistributedSemaphore] Semaphore already exists: " + config.semaphore_id);
        return false;
    }

    auto semaphore = std::make_shared<LocalSemaphore>();
    semaphore->config = config;
    semaphores_[config.semaphore_id] = semaphore;

    {
        std::ostringstream oss;
        oss << "[DistributedSemaphore] Registered: " << config.semaphore_id 
            << " (" << config.resource_name << "), type=" 
            << resourceTypeToString(config.resource_type) 
            << ", max_permits=" << config.max_permits;
        LOG(oss.str());
    }
    return true;
}

bool DistributedSemaphore::acquire(
    const std::string& semaphore_id,
    int permits,
    int priority,
    int timeout_s,
    const std::string& task_id,
    const std::chrono::system_clock::time_point& deadline
) {
    std::lock_guard<std::mutex> lock(global_mutex_);

    auto it = semaphores_.find(semaphore_id);
    if (it == semaphores_.end()) {
        std::cerr << "[DistributedSemaphore] Semaphore not found: " << semaphore_id << std::endl;
        return false;
    }

    auto semaphore = it->second;
    std::lock_guard<std::mutex> sem_lock(semaphore->mutex);

    if (permits <= 0 || permits > semaphore->config.max_permits) {
        std::cerr << "[DistributedSemaphore] Invalid permits: " << permits << std::endl;
        return false;
    }

    SemaphoreRequest request;
    request.request_id = generateRequestId();
    request.node_id = local_node_id_;
    request.task_id = task_id;
    request.semaphore_id = semaphore_id;
    request.permits = permits;
    request.priority = priority;
    request.requested_at = std::chrono::system_clock::now();
    request.deadline = deadline;
    request.timeout_s = (timeout_s == 0) ? semaphore->config.default_timeout_s : timeout_s;
    request.status = SemaphoreRequestStatus::PENDING;

    if (tryGrantLocal(semaphore, request)) {
        request.status = SemaphoreRequestStatus::GRANTED;
        LOG("[DistributedSemaphore] Acquired: " + semaphore_id + " (permits=" + std::to_string(permits) + ")");
        return true;
    }

    sendAcquireRequest(semaphore_id, request);

    semaphore->request_queue.push(request);

    PendingRequest pending;
    pending.request = request;
    pending.expire_time = std::chrono::system_clock::now() + std::chrono::seconds(request.timeout_s);
    pending_requests_[request.request_id] = std::move(pending);

    std::cout << "[DistributedSemaphore] Request queued: " << request.request_id << std::endl;
    return false;
}

bool DistributedSemaphore::release(
    const std::string& semaphore_id,
    int permits,
    const std::string& caller_id
) {
    std::lock_guard<std::mutex> lock(global_mutex_);

    auto it = semaphores_.find(semaphore_id);
    if (it == semaphores_.end()) {
        std::ostringstream oss;
        oss << "[DistributedSemaphore] Semaphore not found: " << semaphore_id;
        if (!caller_id.empty()) oss << " (caller: " << caller_id << ")";
        LOG_ERROR(oss.str());
        return false;
    }

    auto semaphore = it->second;
    std::lock_guard<std::mutex> sem_lock(semaphore->mutex);

    int released = 0;
    auto& holders = semaphore->holders;

    if (permits == 0) {
        auto remove_it = std::remove_if(holders.begin(), holders.end(),
            [this, &released](const SemaphoreHolder& holder) {
                if (holder.node_id == local_node_id_) {
                    released += holder.permits;
                    return true;
                }
                return false;
            });
        holders.erase(remove_it, holders.end());
    } else {
        int to_release = permits;
        for (auto it = holders.begin(); it != holders.end() && to_release > 0;) {
            if (it->node_id == local_node_id_) {
                int release_amount = std::min(to_release, it->permits);
                it->permits -= release_amount;
                to_release -= release_amount;
                released += release_amount;

                if (it->permits == 0) {
                    it = holders.erase(it);
                } else {
                    ++it;
                }
            } else {
                ++it;
            }
        }
    }

    if (released > 0) {
        {
            std::ostringstream oss;
            oss << "[DistributedSemaphore] Released: " << semaphore_id 
                << " (permits=" << released << ")";
            if (!caller_id.empty()) oss << " by " << caller_id;
            LOG(oss.str());
        }

        auto& node_sems = node_semaphore_map_[local_node_id_];
        node_sems.erase(std::remove(node_sems.begin(), node_sems.end(), semaphore_id), node_sems.end());

        sendReleaseMessage(semaphore_id, released);
        processLocalQueue(semaphore);
        return true;
    }

    return false;
}

void DistributedSemaphore::clear() {
    running_ = false;

    std::lock_guard<std::mutex> lock(global_mutex_);

    for (auto& pair : pending_requests_) {
        pair.second.promise.set_value(false);
    }

    semaphores_.clear();
    pending_requests_.clear();
    node_semaphore_map_.clear();

    std::cout << "[DistributedSemaphore] Cleared" << std::endl;
}

std::string DistributedSemaphore::generateRequestId() {
    auto count = ++request_counter_;
    auto timestamp = std::chrono::system_clock::now().time_since_epoch().count();
    return "sem_req_" + local_node_id_ + "_" + std::to_string(timestamp) + "_" + std::to_string(count);
}

void DistributedSemaphore::sendAcquireRequest(const std::string& semaphore_id, const SemaphoreRequest& request) {
    if (!comm_) {
        return;
    }

    Message msg;
    msg.header.msg_type = MessageType::SEM_ACQUIRE_REQUEST;
    msg.header.source_node_id = local_node_id_;
    msg.header.dest_node_id = "COORDINATOR";
    msg.header.priority = Priority::NORMAL;

    json payload;
    payload["request_id"] = request.request_id;
    payload["semaphore_id"] = semaphore_id;
    payload["permits"] = request.permits;
    payload["priority"] = request.priority;
    payload["timeout_s"] = request.timeout_s;

    std::string payload_str = payload.dump();
    msg.payload.assign(payload_str.begin(), payload_str.end());
    msg.header.payload_size = msg.payload.size();

    comm_->sendMessage("COORDINATOR", msg);
}

void DistributedSemaphore::sendReleaseMessage(const std::string& semaphore_id, int permits) {
    if (!comm_) {
        return;
    }

    Message msg;
    msg.header.msg_type = MessageType::SEM_RELEASE;
    msg.header.source_node_id = local_node_id_;
    msg.header.dest_node_id = "COORDINATOR";
    msg.header.priority = Priority::NORMAL;

    json payload;
    payload["semaphore_id"] = semaphore_id;
    payload["permits"] = permits;

    std::string payload_str = payload.dump();
    msg.payload.assign(payload_str.begin(), payload_str.end());
    msg.header.payload_size = msg.payload.size();

    comm_->sendMessage("COORDINATOR", msg);
}

void DistributedSemaphore::processLocalQueue(std::shared_ptr<LocalSemaphore> semaphore) {
    while (!semaphore->request_queue.empty()) {
        auto request = semaphore->request_queue.top();

        if (tryGrantLocal(semaphore, request)) {
            semaphore->request_queue.pop();
            std::cout << "[DistributedSemaphore] Processed queued request: " 
                      << request.request_id << std::endl;
        } else {
            break;
        }
    }
}

bool DistributedSemaphore::tryGrantLocal(std::shared_ptr<LocalSemaphore> semaphore, const SemaphoreRequest& request) {
    if (semaphore->getAvailablePermits() < request.permits) {
        return false;
    }

    SemaphoreHolder holder;
    holder.node_id = request.node_id;
    holder.task_id = request.task_id;
    holder.permits = request.permits;
    holder.acquired_at = std::chrono::system_clock::now();
    holder.deadline = request.deadline;
    holder.priority = request.priority;

    semaphore->holders.push_back(holder);
    node_semaphore_map_[request.node_id].push_back(request.semaphore_id);

    return true;
}

SemaphoreQueuePolicy DistributedSemaphore::parseQueuePolicy(const std::string& policy_str) const {
    if (policy_str == "FIFO") return SemaphoreQueuePolicy::FIFO;
    if (policy_str == "PRIORITY") return SemaphoreQueuePolicy::PRIORITY;
    if (policy_str == "DEADLINE") return SemaphoreQueuePolicy::DEADLINE;
    return SemaphoreQueuePolicy::FIFO;
}

ResourceType DistributedSemaphore::parseResourceType(const std::string& type_str) const {
    if (type_str == "GROUND_STATION") return ResourceType::GROUND_STATION;
    if (type_str == "RELAY_SATELLITE") return ResourceType::RELAY_SATELLITE;
    if (type_str == "OBSERVATION_TARGET") return ResourceType::OBSERVATION_TARGET;
    if (type_str == "COMMUNICATION_LINK") return ResourceType::COMMUNICATION_LINK;
    if (type_str == "PROCESSING_UNIT") return ResourceType::PROCESSING_UNIT;
    if (type_str == "STORAGE_SPACE") return ResourceType::STORAGE_SPACE;
    return ResourceType::UNKNOWN;
}

std::string DistributedSemaphore::resourceTypeToString(ResourceType type) const {
    switch (type) {
        case ResourceType::GROUND_STATION: return "GROUND_STATION";
        case ResourceType::RELAY_SATELLITE: return "RELAY_SATELLITE";
        case ResourceType::OBSERVATION_TARGET: return "OBSERVATION_TARGET";
        case ResourceType::COMMUNICATION_LINK: return "COMMUNICATION_LINK";
        case ResourceType::PROCESSING_UNIT: return "PROCESSING_UNIT";
        case ResourceType::STORAGE_SPACE: return "STORAGE_SPACE";
        default: return "UNKNOWN";
    }
}

} // namespace coordinator
