#ifndef CORE_LOGGER_H
#define CORE_LOGGER_H

#include <string>
#include <mutex>
#include <iostream>
#include <sstream>

namespace core {

class Logger {
public:
    static Logger& getInstance() {
        static Logger instance;
        return instance;
    }

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void log(const std::string& message) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::cout << message << std::endl;
    }

    void log(const std::string& prefix, const std::string& message) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::cout << prefix << message << std::endl;
    }

    void logError(const std::string& message) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::cerr << message << std::endl;
    }

    void logError(const std::string& prefix, const std::string& message) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::cerr << prefix << message << std::endl;
    }

    template<typename... Args>
    void logf(const char* format, Args... args) {
        std::lock_guard<std::mutex> lock(mutex_);
        printf(format, args...);
        fflush(stdout);
    }

    void logMultiLine(const std::vector<std::string>& lines) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& line : lines) {
            std::cout << line << std::endl;
        }
    }

    std::mutex& getMutex() {
        return mutex_;
    }

private:
    Logger() = default;
    std::mutex mutex_;
};

#define LOG(msg) core::Logger::getInstance().log(msg)
#define LOG_PREFIX(prefix, msg) core::Logger::getInstance().log(prefix, msg)
#define LOG_ERROR(msg) core::Logger::getInstance().logError(msg)
#define LOG_ERROR_PREFIX(prefix, msg) core::Logger::getInstance().logError(prefix, msg)

#define LOG_LOCK() std::lock_guard<std::mutex> _log_lock_(core::Logger::getInstance().getMutex())

} // namespace core

#endif 
