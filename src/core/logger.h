#ifndef CORE_LOGGER_H
#define CORE_LOGGER_H

#include <string>
#include <atomic>
#include <chrono>
#include <ctime>
#include <cstdlib>
#include <iomanip>
#include <mutex>
#include <algorithm>
#include <cctype>
#include <iostream>
#include <sstream>
#include <vector>

namespace core {

enum class LogLevel {
    DEBUG = 0,
    INFO = 1,
    WARN = 2,
    ERROR = 3,
    OFF = 4
};

class Logger {
public:
    static Logger& getInstance() {
        // Intentionally leaked to avoid static-destruction-order issues.
        static Logger* instance = new Logger();
        return *instance;
    }

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void log(const std::string& message) {
        write(LogLevel::INFO, message);
    }

    void log(const std::string& prefix, const std::string& message) {
        write(LogLevel::INFO, prefix + message);
    }

    void logError(const std::string& message) {
        write(LogLevel::ERROR, message);
    }

    void logError(const std::string& prefix, const std::string& message) {
        write(LogLevel::ERROR, prefix + message);
    }

    void logDebug(const std::string& message) {
        write(LogLevel::DEBUG, message);
    }

    void logWarn(const std::string& message) {
        write(LogLevel::WARN, message);
    }

    void setMinLevel(LogLevel level) {
        min_level_.store(static_cast<int>(level));
    }

    LogLevel getMinLevel() const {
        return static_cast<LogLevel>(min_level_.load());
    }

    template<typename... Args>
    void logf(const char* format, Args... args) {
        if (!shouldLog(LogLevel::INFO)) {
            return;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        printf(format, args...);
        fflush(stdout);
    }

    void logMultiLine(const std::vector<std::string>& lines) {
        if (!shouldLog(LogLevel::INFO)) {
            return;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& line : lines) {
            std::cout << formatPrefix(LogLevel::INFO) << line << std::endl;
        }
    }

    std::mutex& getMutex() {
        return mutex_;
    }

private:
    Logger() : min_level_(static_cast<int>(LogLevel::INFO)) {
        loadMinLevelFromEnv();
    }

    bool shouldLog(LogLevel level) const {
        return static_cast<int>(level) >= min_level_.load();
    }

    void write(LogLevel level, const std::string& message) {
        if (!shouldLog(level)) {
            return;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        std::ostream& os = (level == LogLevel::ERROR) ? std::cerr : std::cout;
        os << formatPrefix(level) << message << std::endl;
    }

    std::string formatPrefix(LogLevel level) const {
        using Clock = std::chrono::system_clock;
        const auto now = Clock::now();
        const auto time_t_now = Clock::to_time_t(now);
        const long long ms_since_epoch =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()).count();
        const long long ms_part = ms_since_epoch % 1000;

        std::tm local_tm = *std::localtime(&time_t_now);
        std::ostringstream oss;
        oss << "[" << std::put_time(&local_tm, "%H:%M:%S")
            << "." << std::setw(3) << std::setfill('0') << ms_part << "]"
            << "[" << levelToString(level) << "] ";
        return oss.str();
    }

    const char* levelToString(LogLevel level) const {
        switch (level) {
            case LogLevel::DEBUG: return "DEBUG";
            case LogLevel::INFO: return "INFO";
            case LogLevel::WARN: return "WARN";
            case LogLevel::ERROR: return "ERROR";
            case LogLevel::OFF: return "OFF";
        }
        return "INFO";
    }

    void loadMinLevelFromEnv() {
        const char* env = std::getenv("MS_LOG_LEVEL");
        if (!env) {
            return;
        }
        std::string value(env);
        std::transform(value.begin(), value.end(), value.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (value == "debug") {
            min_level_.store(static_cast<int>(LogLevel::DEBUG));
        } else if (value == "info") {
            min_level_.store(static_cast<int>(LogLevel::INFO));
        } else if (value == "warn") {
            min_level_.store(static_cast<int>(LogLevel::WARN));
        } else if (value == "error") {
            min_level_.store(static_cast<int>(LogLevel::ERROR));
        } else if (value == "off") {
            min_level_.store(static_cast<int>(LogLevel::OFF));
        }
    }

    std::atomic<int> min_level_;
    std::mutex mutex_;
};

#define LOG(msg) core::Logger::getInstance().log(msg)
#define LOG_PREFIX(prefix, msg) core::Logger::getInstance().log(prefix, msg)
#define LOG_DEBUG(msg) core::Logger::getInstance().logDebug(msg)
#define LOG_WARN(msg) core::Logger::getInstance().logWarn(msg)
#define LOG_ERROR(msg) core::Logger::getInstance().logError(msg)
#define LOG_ERROR_PREFIX(prefix, msg) core::Logger::getInstance().logError(prefix, msg)

#define LOG_LOCK() std::lock_guard<std::mutex> _log_lock_(core::Logger::getInstance().getMutex())

} // namespace core

#endif 
