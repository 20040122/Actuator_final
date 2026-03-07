#ifndef EXECUTOR_VARIABLE_MANAGER_H
#define EXECUTOR_VARIABLE_MANAGER_H

#include <map>
#include <string>
#include <memory>
#include <vector>
#include <stdexcept>
#include <mutex>
#include <sstream>
#include <ctime>
#include "../third_party/nlohmann/json.hpp"

enum class Scope { 
    GLOBAL,      
    LOCAL,       
    INTERMEDIATE 
};
class VariableValue {
public:
    enum class Type { INT, DOUBLE, STRING, BOOL, UNKNOWN };
    VariableValue() : type_(Type::UNKNOWN), int_val_(0), double_val_(0.0), bool_val_(false) {}
    explicit VariableValue(int val) : type_(Type::INT), int_val_(val), double_val_(0.0), bool_val_(false) {}
    explicit VariableValue(double val) : type_(Type::DOUBLE), int_val_(0), double_val_(val), bool_val_(false) {}
    explicit VariableValue(const std::string& val) : type_(Type::STRING), string_val_(val), int_val_(0), double_val_(0.0), bool_val_(false) {}
    explicit VariableValue(bool val) : type_(Type::BOOL), int_val_(0), double_val_(0.0), bool_val_(val) {}
    Type getType() const { return type_; }
    int asInt() const {
        if (type_ == Type::INT) return int_val_;
        if (type_ == Type::DOUBLE) return static_cast<int>(double_val_);
        if (type_ == Type::BOOL) return bool_val_ ? 1 : 0;
        if (type_ == Type::STRING) {
            try {
                return std::stoi(string_val_);
            } catch (...) {
                throw std::runtime_error("Cannot convert string '" + string_val_ + "' to int");
            }
        }
        throw std::runtime_error("Cannot convert to int");
    }
    double asDouble() const {
        if (type_ == Type::DOUBLE) return double_val_;
        if (type_ == Type::INT) return static_cast<double>(int_val_);
        throw std::runtime_error("Cannot convert to double");
    }
    std::string asString() const {
        if (type_ == Type::STRING) return string_val_;
        if (type_ == Type::INT) return std::to_string(int_val_);
        if (type_ == Type::DOUBLE) return std::to_string(double_val_);
        if (type_ == Type::BOOL) return bool_val_ ? "true" : "false";
        return "";
    }
    bool asBool() const {
        if (type_ == Type::BOOL) return bool_val_;
        if (type_ == Type::INT) return int_val_ != 0;
        if (type_ == Type::STRING) return !string_val_.empty();
        throw std::runtime_error("Cannot convert to bool");
    }
private:
    Type type_;
    int int_val_;
    double double_val_;
    std::string string_val_;
    bool bool_val_;
};
struct VariableSnapshot {
    std::string snapshot_id;
    std::time_t timestamp;
    std::map<std::string, VariableValue> global_vars;
    std::map<std::string, VariableValue> local_vars;
    std::map<std::string, VariableValue> intermediate_vars;
    std::string description;
};
class VariableManager {
public:
    VariableManager() : snapshot_counter_(0) {}
    void set(const std::string& name, const VariableValue& value, Scope scope);
    VariableValue get(const std::string& name) const;
    bool exists(const std::string& name) const;
    bool exists(const std::string& name, Scope scope) const;
    void remove(const std::string& name, Scope scope);
    void clearScope(Scope scope);
    void clearLocal() { clearScope(Scope::LOCAL); }
    void clearIntermediate() { clearScope(Scope::INTERMEDIATE); }
    void initSystemVars();
    void loadFromGlobalConfig(const std::string& global_json_path);
    void loadFromScheduleConfig(const std::string& schedule_json_path, const std::string& satellite_id = "");
    void setFromParams(const std::map<std::string, std::string>& params, Scope scope);
    std::string createSnapshot(const std::string& description = "");
    bool restoreSnapshot(const std::string& snapshot_id);
    void clearSnapshots();
    std::vector<std::string> listSnapshots() const;
    std::map<std::string, VariableValue> getAllVariables(Scope scope) const;
    std::map<std::string, VariableValue> getAllVariables() const; // 合并所有作用域
    void enableTracking(bool enable) { tracking_enabled_ = enable; }
    std::vector<std::string> getAccessLog() const { return access_log_; }
    void clearAccessLog() { access_log_.clear(); }
    std::string exportToString() const;
    void importFromString(const std::string& data); 
private:
    std::map<std::string, VariableValue> global_vars_;
    std::map<std::string, VariableValue> local_vars_;
    std::map<std::string, VariableValue> intermediate_vars_;
    std::map<std::string, VariableSnapshot> snapshots_;
    unsigned int snapshot_counter_;
    bool tracking_enabled_ = false;
    mutable std::vector<std::string> access_log_;
    mutable std::mutex mutex_;
    std::map<std::string, VariableValue>& getScopeMap(Scope scope);
    const std::map<std::string, VariableValue>& getScopeMap(Scope scope) const;
    void logAccess(const std::string& operation, const std::string& name, Scope scope) const;
    std::string generateSnapshotId();
};

#endif