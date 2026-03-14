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
      request_counter_(0),
      grant_token_counter_(0) {
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
    const std::string& owner_id,
    uint64_t* out_grant_token,
    std::string* out_request_id,
    const std::chrono::system_clock::time_point& deadline
) {
    std::lock_guard<std::mutex> lock(global_mutex_);

    auto it = semaphores_.find(semaphore_id);
    if (it == semaphores_.end()) {
        LOG_ERROR("[DistributedSemaphore] Semaphore not found: " + semaphore_id);
        return false;
    }

    auto semaphore = it->second;
    std::lock_guard<std::mutex> sem_lock(semaphore->mutex);

    if (permits <= 0 || permits > semaphore->config.max_permits) {
        LOG_ERROR("[DistributedSemaphore] Invalid permits: " + std::to_string(permits));
        return false;
    }

    SemaphoreRequest request;
    request.request_id = generateRequestId();
    request.node_id = local_node_id_;
    request.owner_id = owner_id.empty() ? local_node_id_ : owner_id;
    request.task_id = task_id;
    request.semaphore_id = semaphore_id;
    request.permits = permits;
    request.priority = priority;
    request.requested_at = std::chrono::system_clock::now();
    request.deadline = deadline;
    request.timeout_s = (timeout_s == 0) ? semaphore->config.default_timeout_s : timeout_s;
    request.status = SemaphoreRequestStatus::PENDING;

    if (out_request_id) {
        *out_request_id = request.request_id;
    }

    uint64_t grant_token = 0;
    if (tryGrantLocal(semaphore, request, &grant_token)) {
        request.status = SemaphoreRequestStatus::GRANTED;
        if (out_grant_token) {
            *out_grant_token = grant_token;
        }
        const int available = semaphore->getAvailablePermits();
        const int allocated = semaphore->getAllocatedPermits();
        std::ostringstream oss;
        oss << "[DistributedSemaphore] Acquired: " << semaphore_id
            << " request_id=" << request.request_id
            << " grant_token=" << grant_token
            << " (requested=" << permits
            << ", allocated=" << allocated
            << ", available=" << available << ")"
            << " by " << request.owner_id;
        if (!task_id.empty()) {
            oss << " task=" << task_id;
        }
        LOG(oss.str());
        return true;
    }

    sendAcquireRequest(semaphore_id, request);

    semaphore->request_queue.push(request);

    PendingRequest pending;
    pending.request = request;
    pending.expire_time = std::chrono::system_clock::now() + std::chrono::seconds(request.timeout_s);
    pending_requests_[request.request_id] = std::move(pending);

    LOG("[DistributedSemaphore] Request queued: " + request.request_id);
    if (out_grant_token) {
        *out_grant_token = 0;
    }
    return false;
}

bool DistributedSemaphore::release(
    const std::string& semaphore_id,
    int permits,
    const std::string& owner_id
) {
    std::unique_lock<std::mutex> lock(global_mutex_);

    auto it = semaphores_.find(semaphore_id);
    if (it == semaphores_.end()) {
        std::ostringstream oss;
        oss << "[DistributedSemaphore] Semaphore not found: " << semaphore_id;
        if (!owner_id.empty()) oss << " (owner: " << owner_id << ")";
        LOG_ERROR(oss.str());
        return false;
    }

    auto semaphore = it->second;
    std::lock_guard<std::mutex> sem_lock(semaphore->mutex);

    const std::string owner = owner_id.empty() ? local_node_id_ : owner_id;
    std::vector<uint64_t> owner_tokens;
    for (const auto& holder : semaphore->holders) {
        if (holder.owner_id == owner) {
            owner_tokens.push_back(holder.grant_token);
        }
    }

    if (owner_tokens.empty()) {
        LOG_ERROR("[DistributedSemaphore] Release denied: no active grant for owner " + owner);
        return false;
    }

    if (owner_tokens.size() > 1) {
        LOG_ERROR("[DistributedSemaphore] Release denied: multiple grants found for owner " + owner + ", use releaseByGrantToken/releaseByRequestId");
        return false;
    }

    const uint64_t target_token = owner_tokens.front();
    lock.unlock();
    return releaseByGrantToken(semaphore_id, target_token, permits, owner);
}

bool DistributedSemaphore::releaseByGrantToken(
    const std::string& semaphore_id,
    uint64_t grant_token,
    int permits,
    const std::string& owner_id
) {
    std::lock_guard<std::mutex> lock(global_mutex_);

    auto sem_it = semaphores_.find(semaphore_id);
    if (sem_it == semaphores_.end()) {
        LOG_ERROR("[DistributedSemaphore] Semaphore not found: " + semaphore_id);
        return false;
    }

    auto semaphore = sem_it->second;
    std::lock_guard<std::mutex> sem_lock(semaphore->mutex);

    auto& holders = semaphore->holders;
    auto holder_it = std::find_if(holders.begin(), holders.end(),
        [grant_token](const SemaphoreHolder& holder) {
            return holder.grant_token == grant_token;
        });

    if (holder_it == holders.end()) {
        LOG_ERROR("[DistributedSemaphore] Release denied: grant token not found: " + std::to_string(grant_token));
        return false;
    }

    const std::string owner = owner_id.empty() ? holder_it->owner_id : owner_id;
    if (owner != holder_it->owner_id) {
        LOG_ERROR("[DistributedSemaphore] Release denied: owner mismatch for token " + std::to_string(grant_token));
        return false;
    }

    const std::string request_id = holder_it->request_id;
    const std::string task_id = holder_it->task_id;
    int release_amount = (permits <= 0) ? holder_it->permits : std::min(permits, holder_it->permits);
    if (release_amount <= 0) {
        return false;
    }

    holder_it->permits -= release_amount;
    if (holder_it->permits == 0) {
        holders.erase(holder_it);
        request_to_grant_token_.erase(request_id);
    }

    const int available = semaphore->getAvailablePermits();
    const int allocated = semaphore->getAllocatedPermits();
    std::ostringstream oss;
    oss << "[DistributedSemaphore] Released: " << semaphore_id
        << " request_id=" << request_id
        << " grant_token=" << grant_token
        << " (released=" << release_amount
        << ", allocated=" << allocated
        << ", available=" << available << ")"
        << " by " << owner;
    if (!task_id.empty()) {
        oss << " task=" << task_id;
    }
    LOG(oss.str());

    sendReleaseMessage(semaphore_id, release_amount, grant_token, request_id, owner, task_id);
    processLocalQueue(semaphore);
    return true;
}

bool DistributedSemaphore::releaseByRequestId(
    const std::string& semaphore_id,
    const std::string& request_id,
    int permits,
    const std::string& owner_id
) {
    std::lock_guard<std::mutex> lock(global_mutex_);
    auto it = request_to_grant_token_.find(request_id);
    if (it == request_to_grant_token_.end()) {
        LOG_ERROR("[DistributedSemaphore] Release denied: unknown request_id: " + request_id);
        return false;
    }
    const uint64_t grant_token = it->second;

    auto sem_it = semaphores_.find(semaphore_id);
    if (sem_it == semaphores_.end()) {
        LOG_ERROR("[DistributedSemaphore] Semaphore not found: " + semaphore_id);
        return false;
    }

    auto semaphore = sem_it->second;
    std::lock_guard<std::mutex> sem_lock(semaphore->mutex);
    auto& holders = semaphore->holders;
    auto holder_it = std::find_if(holders.begin(), holders.end(),
        [&request_id, grant_token](const SemaphoreHolder& holder) {
            return holder.request_id == request_id && holder.grant_token == grant_token;
        });
    if (holder_it == holders.end()) {
        LOG_ERROR("[DistributedSemaphore] Release denied: request_id does not match semaphore holder: " + request_id);
        return false;
    }

    const std::string owner = owner_id.empty() ? holder_it->owner_id : owner_id;
    if (owner != holder_it->owner_id) {
        LOG_ERROR("[DistributedSemaphore] Release denied: owner mismatch for request_id " + request_id);
        return false;
    }

    int release_amount = (permits <= 0) ? holder_it->permits : std::min(permits, holder_it->permits);
    if (release_amount <= 0) {
        return false;
    }

    const std::string task_id = holder_it->task_id;
    holder_it->permits -= release_amount;
    if (holder_it->permits == 0) {
        holders.erase(holder_it);
        request_to_grant_token_.erase(request_id);
    }

    const int available = semaphore->getAvailablePermits();
    const int allocated = semaphore->getAllocatedPermits();
    std::ostringstream oss;
    oss << "[DistributedSemaphore] Released: " << semaphore_id
        << " request_id=" << request_id
        << " grant_token=" << grant_token
        << " (released=" << release_amount
        << ", allocated=" << allocated
        << ", available=" << available << ")"
        << " by " << owner;
    if (!task_id.empty()) {
        oss << " task=" << task_id;
    }
    LOG(oss.str());

    sendReleaseMessage(semaphore_id, release_amount, grant_token, request_id, owner, task_id);
    processLocalQueue(semaphore);
    return true;
}

void DistributedSemaphore::clear() {
    running_ = false;

    std::lock_guard<std::mutex> lock(global_mutex_);

    for (auto& pair : pending_requests_) {
        try {
            pair.second.promise.set_value(false);
        } catch (...) {
        }
    }

    semaphores_.clear();
    pending_requests_.clear();
    request_to_grant_token_.clear();

    LOG("[DistributedSemaphore] Cleared");
}

std::string DistributedSemaphore::generateRequestId() {
    auto count = ++request_counter_;
    auto timestamp = std::chrono::system_clock::now().time_since_epoch().count();
    return "sem_req_" + local_node_id_ + "_" + std::to_string(timestamp) + "_" + std::to_string(count);
}

uint64_t DistributedSemaphore::generateGrantToken() {
    return ++grant_token_counter_;
}

void DistributedSemaphore::sendAcquireRequest(const std::string& semaphore_id, const SemaphoreRequest& request) {
    if (!comm_) {
        return;
    }

    Message msg;
    msg.header.msg_type = MessageType::SEM_ACQUIRE_REQUEST;
    msg.header.source_node_id = local_node_id_;
    msg.header.dest_node_id = local_node_id_;
    msg.header.priority = Priority::NORMAL;

    json payload;
    payload["request_id"] = request.request_id;
    payload["semaphore_id"] = semaphore_id;
    payload["permits"] = request.permits;
    payload["priority"] = request.priority;
    payload["timeout_s"] = request.timeout_s;
    payload["owner_id"] = request.owner_id;
    payload["task_id"] = request.task_id;

    std::string payload_str = payload.dump();
    msg.payload.assign(payload_str.begin(), payload_str.end());
    msg.header.payload_size = msg.payload.size();

    comm_->sendMessage(local_node_id_, msg);
}

void DistributedSemaphore::sendReleaseMessage(
    const std::string& semaphore_id,
    int permits,
    uint64_t grant_token,
    const std::string& request_id,
    const std::string& owner_id,
    const std::string& task_id
) {
    if (!comm_) {
        return;
    }

    Message msg;
    msg.header.msg_type = MessageType::SEM_RELEASE;
    msg.header.source_node_id = local_node_id_;
    msg.header.dest_node_id = local_node_id_;
    msg.header.priority = Priority::NORMAL;

    json payload;
    payload["semaphore_id"] = semaphore_id;
    payload["permits"] = permits;
    payload["grant_token"] = grant_token;
    payload["request_id"] = request_id;
    payload["owner_id"] = owner_id;
    payload["task_id"] = task_id;

    std::string payload_str = payload.dump();
    msg.payload.assign(payload_str.begin(), payload_str.end());
    msg.header.payload_size = msg.payload.size();

    comm_->sendMessage(local_node_id_, msg);
}

void DistributedSemaphore::processLocalQueue(std::shared_ptr<LocalSemaphore> semaphore) {
    while (!semaphore->request_queue.empty()) {
        auto request = semaphore->request_queue.top();

        uint64_t grant_token = 0;
        if (tryGrantLocal(semaphore, request, &grant_token)) {
            semaphore->request_queue.pop();
            auto pending_it = pending_requests_.find(request.request_id);
            if (pending_it != pending_requests_.end()) {
                try {
                    pending_it->second.promise.set_value(true);
                } catch (...) {
                }
                pending_requests_.erase(pending_it);
            }

            std::ostringstream oss;
            oss << "[DistributedSemaphore] Processed queued request: " << request.request_id
                << " grant_token=" << grant_token
                << " owner=" << request.owner_id;
            if (!request.task_id.empty()) {
                oss << " task=" << request.task_id;
            }
            LOG(oss.str());
        } else {
            break;
        }
    }
}

bool DistributedSemaphore::tryGrantLocal(
    std::shared_ptr<LocalSemaphore> semaphore,
    const SemaphoreRequest& request,
    uint64_t* out_grant_token
) {
    if (semaphore->getAvailablePermits() < request.permits) {
        return false;
    }

    SemaphoreHolder holder;
    holder.request_id = request.request_id;
    holder.grant_token = generateGrantToken();
    holder.node_id = request.node_id;
    holder.owner_id = request.owner_id;
    holder.task_id = request.task_id;
    holder.permits = request.permits;
    holder.acquired_at = std::chrono::system_clock::now();
    holder.deadline = request.deadline;
    holder.priority = request.priority;

    semaphore->holders.push_back(holder);
    request_to_grant_token_[request.request_id] = holder.grant_token;
    if (out_grant_token) {
        *out_grant_token = holder.grant_token;
    }

    return true;
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
