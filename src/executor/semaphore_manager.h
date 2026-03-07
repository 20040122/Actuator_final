#ifndef EXECUTOR_SEMAPHORE_MANAGER_H
#define EXECUTOR_SEMAPHORE_MANAGER_H

#include <map>
#include <string>
#include <mutex>
#include <condition_variable>

struct Semaphore {
    int max_permits;
    int available_permits;
    std::string queue_policy; 
    std::mutex mtx;
    std::condition_variable cv;
};
class SemaphoreManager {
public:
    void loadFromConfig(const std::string& global_file);
    bool acquire(const std::string& sem_id, int timeout_s = 30);
    void release(const std::string& sem_id);  
private:
    std::map<std::string, Semaphore> semaphores_;
};

#endif