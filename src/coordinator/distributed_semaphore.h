#ifndef COORDINATOR_DISTRIBUTED_SEMAPHORE_H
#define COORDINATOR_DISTRIBUTED_SEMAPHORE_H

#include <string>
#include <map>
#include <vector>
#include <mutex>
#include <memory>
#include <chrono>
#include <queue>
#include <atomic>
#include <thread>
#include <future>
#include "message_types.h"

namespace coordinator {

class InterSatComm;

enum class ResourceType {
    GROUND_STATION,
    RELAY_SATELLITE,
    OBSERVATION_TARGET,
    COMMUNICATION_LINK,
    PROCESSING_UNIT,
    STORAGE_SPACE,
    UNKNOWN
};

enum class SemaphoreQueuePolicy {
    FIFO,
    PRIORITY,
    DEADLINE
};

enum class SemaphoreRequestStatus {
    PENDING,
    GRANTED,
    DENIED,
    TIMEOUT,
    CANCELLED
};

struct SemaphoreRequest {
    std::string request_id;
    std::string node_id;
    std::string task_id;
    std::string semaphore_id;
    int permits;
    int priority;
    std::chrono::system_clock::time_point requested_at;
    std::chrono::system_clock::time_point deadline;
    int timeout_s;
    SemaphoreRequestStatus status;

    bool operator<(const SemaphoreRequest& other) const;
};

struct SemaphoreHolder {
    std::string node_id;
    std::string task_id;
    int permits;
    std::chrono::system_clock::time_point acquired_at;
    std::chrono::system_clock::time_point deadline;
    int priority;
};

struct DistributedSemaphoreConfig {
    std::string semaphore_id;
    std::string resource_name;
    ResourceType resource_type;
    int max_permits;
    int available_permits;
    SemaphoreQueuePolicy queue_policy;
    int default_timeout_s;
    bool priority_enabled;
    std::map<std::string, std::string> extra_params;
};

class DistributedSemaphore {
public:
    DistributedSemaphore();
    ~DistributedSemaphore();

    bool initialize(std::shared_ptr<InterSatComm> comm, const std::string& local_node_id);

    bool registerSemaphore(const DistributedSemaphoreConfig& config);

    bool acquire(
        const std::string& semaphore_id,
        int permits = 1,
        int priority = 5,
        int timeout_s = 0,
        const std::string& task_id = "",
        const std::chrono::system_clock::time_point& deadline = std::chrono::system_clock::time_point()
    );

    bool release(
        const std::string& semaphore_id,
        int permits = 0,
        const std::string& caller_id = ""
    );

    void clear();

private:
    struct LocalSemaphore {
        DistributedSemaphoreConfig config;
        std::vector<SemaphoreHolder> holders;
        std::priority_queue<SemaphoreRequest> request_queue;
        std::mutex mutex;

        int getAllocatedPermits() const;
        int getAvailablePermits() const;
    };

    struct PendingRequest {
        SemaphoreRequest request;
        std::promise<bool> promise;
        std::chrono::system_clock::time_point expire_time;
    };

    std::shared_ptr<InterSatComm> comm_;
    std::string local_node_id_;
    std::map<std::string, std::shared_ptr<LocalSemaphore>> semaphores_;
    std::map<std::string, PendingRequest> pending_requests_;
    std::map<std::string, std::vector<std::string>> node_semaphore_map_;
    mutable std::mutex global_mutex_;
    std::atomic<bool> running_;
    std::atomic<uint64_t> request_counter_;

    std::string generateRequestId();
    void sendAcquireRequest(const std::string& semaphore_id, const SemaphoreRequest& request);
    void sendReleaseMessage(const std::string& semaphore_id, int permits);
    void processLocalQueue(std::shared_ptr<LocalSemaphore> semaphore);
    bool tryGrantLocal(std::shared_ptr<LocalSemaphore> semaphore, const SemaphoreRequest& request);
    SemaphoreQueuePolicy parseQueuePolicy(const std::string& policy_str) const;
    ResourceType parseResourceType(const std::string& type_str) const;
    std::string resourceTypeToString(ResourceType type) const;
};

} // namespace coordinator

#endif // COORDINATOR_DISTRIBUTED_SEMAPHORE_H
