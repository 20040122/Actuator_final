#include "semaphore_manager.h"
#include <fstream>
#include <iostream>
#include <chrono>
#include "../third_party/nlohmann/json.hpp"

void SemaphoreManager::loadFromConfig(const std::string& global_file) {
    std::ifstream file(global_file);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open global config file: " + global_file);
    }
    nlohmann::json config;
    try {
        file >> config;
    } catch (const std::exception& e) {
        throw std::runtime_error("Failed to parse global config JSON: " + std::string(e.what()));
    }
    if (!config.contains("shared_resources") || 
        !config["shared_resources"].contains("semaphores")) {
        std::cerr << "Warning: No semaphores defined in global config" << std::endl;
        return;
    }
    const auto& semaphores_json = config["shared_resources"]["semaphores"];
    for (const auto& sem_json : semaphores_json) {
        if (!sem_json.contains("semaphore_id")) {
            std::cerr << "Warning: Semaphore missing semaphore_id, skipping" << std::endl;
            continue;
        }
        std::string sem_id = sem_json["semaphore_id"];        
        int max_permits = sem_json.value("max_permits", 1);
        int available_permits = sem_json.value("available_permits", max_permits);
        std::string queue_policy = sem_json.value("queue_policy", "FIFO");
        if (available_permits > max_permits) {
            available_permits = max_permits;
        }
        auto& sem = semaphores_[sem_id];
        sem.max_permits = max_permits;
        sem.available_permits = available_permits;
        sem.queue_policy = queue_policy;
        std::cout << "加载信号量: " << sem_id 
                  << ", max_permits=" << max_permits
                  << ", queue_policy=" << queue_policy << std::endl;
    }
}
bool SemaphoreManager::acquire(const std::string& sem_id, int timeout_s) {
    auto it = semaphores_.find(sem_id);
    if (it == semaphores_.end()) {
        std::cerr << "Error: Semaphore not found: " << sem_id << std::endl;
        return false;
    }
    Semaphore& sem = it->second;
    std::unique_lock<std::mutex> lock(sem.mtx);
    if (timeout_s <= 0) {
        while (sem.available_permits <= 0) {
            sem.cv.wait(lock);
        }
        sem.available_permits--;
        std::cout << "获取信号量成功: " << sem_id 
                  << " (剩余: " << sem.available_permits << "/" << sem.max_permits << ")" 
                  << std::endl;
        return true;
    } else {
        auto deadline = std::chrono::steady_clock::now() + 
                       std::chrono::seconds(timeout_s);
        while (sem.available_permits <= 0) {
            if (sem.cv.wait_until(lock, deadline) == std::cv_status::timeout) {
                std::cerr << "获取信号量超时: " << sem_id 
                          << " (timeout=" << timeout_s << "s)" << std::endl;
                return false;
            }
        }    
        sem.available_permits--;
        std::cout << "获取信号量成功: " << sem_id 
                  << " (剩余: " << sem.available_permits << "/" << sem.max_permits << ")" 
                  << std::endl;
        return true;
    }
}
void SemaphoreManager::release(const std::string& sem_id) {
    auto it = semaphores_.find(sem_id);
    if (it == semaphores_.end()) {
        std::cerr << "Error: Semaphore not found: " << sem_id << std::endl;
        return;
    }
    Semaphore& sem = it->second;
    std::unique_lock<std::mutex> lock(sem.mtx);
    if (sem.available_permits >= sem.max_permits) {
        std::cerr << "Warning: Semaphore " << sem_id 
                  << " already at max permits, ignoring release" << std::endl;
        return;
    }
    sem.available_permits++;
    std::cout << "释放信号量: " << sem_id 
              << " (剩余: " << sem.available_permits << "/" << sem.max_permits << ")" 
              << std::endl;
    sem.cv.notify_one();
}
