#include "variable_manager.h"
#include <algorithm>
#include <ctime>
#include <iomanip>
#include <fstream>
#include <iostream>

void VariableManager::set(const std::string& name, const VariableValue& value, Scope scope) {
    std::lock_guard<std::mutex> lock(mutex_);
    getScopeMap(scope)[name] = value;
    logAccess("SET", name, scope);
}
VariableValue VariableManager::get(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = local_vars_.find(name);
    if (it != local_vars_.end()) {
        logAccess("GET", name, Scope::LOCAL);
        return it->second;
    }
    it = intermediate_vars_.find(name);
    if (it != intermediate_vars_.end()) {
        logAccess("GET", name, Scope::INTERMEDIATE);
        return it->second;
    }
    it = global_vars_.find(name);
    if (it != global_vars_.end()) {
        logAccess("GET", name, Scope::GLOBAL);
        return it->second;
    }
    throw std::runtime_error("Variable not found: " + name);
}
bool VariableManager::exists(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return local_vars_.find(name) != local_vars_.end() ||
           intermediate_vars_.find(name) != intermediate_vars_.end() ||
           global_vars_.find(name) != global_vars_.end();
}
bool VariableManager::exists(const std::string& name, Scope scope) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto& vars = getScopeMap(scope);
    return vars.find(name) != vars.end();
}
void VariableManager::remove(const std::string& name, Scope scope) {
    std::lock_guard<std::mutex> lock(mutex_);
    getScopeMap(scope).erase(name);
    logAccess("REMOVE", name, scope);
}
void VariableManager::clearScope(Scope scope) {
    std::lock_guard<std::mutex> lock(mutex_);
    getScopeMap(scope).clear();
}
void VariableManager::setFromParams(const std::map<std::string, std::string>& params, Scope scope) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& pair : params) {
        const std::string& key = pair.first;
        const std::string& value_str = pair.second;
        VariableValue value;
        try {
            size_t pos;
            int int_val = std::stoi(value_str, &pos);
            if (pos == value_str.length()) {
                value = VariableValue(int_val);
                getScopeMap(scope)[key] = value;
                continue;
            }
        } catch (...) {}  
        try {
            size_t pos;
            double double_val = std::stod(value_str, &pos);
            if (pos == value_str.length()) {
                value = VariableValue(double_val);
                getScopeMap(scope)[key] = value;
                continue;
            }
        } catch (...) {}
        if (value_str == "true" || value_str == "TRUE" || value_str == "True") {
            value = VariableValue(true);
            getScopeMap(scope)[key] = value;
            continue;
        }
        if (value_str == "false" || value_str == "FALSE" || value_str == "False") {
            value = VariableValue(false);
            getScopeMap(scope)[key] = value;
            continue;
        }
        value = VariableValue(value_str);
        getScopeMap(scope)[key] = value;
    }

    logAccess("SET_BATCH", std::to_string(params.size()) + "_params", scope);
}
void VariableManager::loadFromGlobalConfig(const std::string& global_json_path) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ifstream file(global_json_path);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open global config file: " + global_json_path);
    }
    nlohmann::json config;
    try {
        file >> config;
    } catch (const std::exception& e) {
        throw std::runtime_error("Failed to parse global config JSON: " + std::string(e.what()));
    }
    if (config.contains("plan_id")) {
        global_vars_["plan_id"] = VariableValue(config["plan_id"].get<std::string>());
    }
    if (config.contains("version")) {
        global_vars_["version"] = VariableValue(config["version"].get<std::string>());
    }
    if (config.contains("system_defaults")) {
        auto& defaults = config["system_defaults"];
        if (defaults.contains("battery_percent")) {
            global_vars_["default_battery"] = VariableValue(defaults["battery_percent"].get<int>());
        }
        if (defaults.contains("storage_available_mb")) {
            global_vars_["default_storage_available"] = VariableValue(defaults["storage_available_mb"].get<int>());
        }
        if (defaults.contains("payload_status")) {
            global_vars_["default_payload_status"] = VariableValue(defaults["payload_status"].get<std::string>());
        }
    }
    std::time_t now = std::time(nullptr);
    char time_buf[32];
    std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%dT%H:%M:%S", std::gmtime(&now));
    global_vars_["current_time"] = VariableValue(std::string(time_buf));
    global_vars_["semaphore_acquired"] = VariableValue(false);
    global_vars_["resource_allocated"] = VariableValue(false);
    if (config.contains("multi_node_config")) {
        auto& node_config = config["multi_node_config"];
        if (node_config.contains("total_nodes")) {
            global_vars_["total_nodes"] = VariableValue(node_config["total_nodes"].get<int>());
        }
        if (node_config.contains("node_communication")) {
            auto& comm = node_config["node_communication"];
            if (comm.contains("protocol")) {
                global_vars_["comm_protocol"] = VariableValue(comm["protocol"].get<std::string>());
            }
            if (comm.contains("max_latency_ms")) {
                global_vars_["max_latency_ms"] = VariableValue(comm["max_latency_ms"].get<int>());
            }
            if (comm.contains("retry_count")) {
                global_vars_["retry_count"] = VariableValue(comm["retry_count"].get<int>());
            }
        }
    }
    if (config.contains("shared_resources") && config["shared_resources"].contains("semaphores")) {
        auto& semaphores = config["shared_resources"]["semaphores"];
        int sem_count = 0;
        for (const auto& sem : semaphores) {
            std::string sem_id = sem.value("semaphore_id", "");
            if (!sem_id.empty()) {
                global_vars_[sem_id + "_max_permits"] = VariableValue(sem.value("max_permits", 0));
                global_vars_[sem_id + "_available_permits"] = VariableValue(sem.value("available_permits", 0));
                global_vars_[sem_id + "_type"] = VariableValue(sem.value("resource_type", std::string("UNKNOWN")));
                global_vars_[sem_id + "_queue_policy"] = VariableValue(sem.value("queue_policy", std::string("FIFO")));
                sem_count++;
            }
        }
        global_vars_["semaphore_count"] = VariableValue(sem_count);
    }
    if (config.contains("site_selection")) {
        auto& site_sel = config["site_selection"];
        global_vars_["site_selection_enabled"] = VariableValue(site_sel.value("enabled", false));
        global_vars_["site_selection_mode"] = VariableValue(site_sel.value("mode", std::string("STATIC")));
        
        if (site_sel.contains("strategies")) {
            int strategy_count = 0;
            for (const auto& strategy : site_sel["strategies"]) {
                std::string strategy_id = strategy.value("strategy_id", "");
                if (!strategy_id.empty()) {
                    global_vars_["strategy_" + strategy_id + "_weight"] = VariableValue(strategy.value("weight", 0.0));
                    strategy_count++;
                }
            }
            global_vars_["strategy_count"] = VariableValue(strategy_count);
        }
    }
    logAccess("LOAD_GLOBAL_CONFIG", global_json_path, Scope::GLOBAL);
}
void VariableManager::loadFromScheduleConfig(const std::string& schedule_json_path, const std::string& satellite_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ifstream file(schedule_json_path);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open schedule config file: " + schedule_json_path);
    }
    nlohmann::json config;
    try {
        file >> config;
    } catch (const std::exception& e) {
        throw std::runtime_error("Failed to parse schedule config JSON: " + std::string(e.what()));
    }
    if (config.contains("plan_id")) {
        global_vars_["plan_id"] = VariableValue(config["plan_id"].get<std::string>());
    }
    if (config.contains("generated_at")) {
        global_vars_["plan_generated_at"] = VariableValue(config["generated_at"].get<std::string>());
    }
    if (config.contains("timebase") && config["timebase"].contains("date")) {
        global_vars_["timebase_date"] = VariableValue(config["timebase"]["date"].get<std::string>());
    }
    if (config.contains("summary")) {
        auto& summary = config["summary"];
        global_vars_["total_satellites"] = VariableValue(summary.value("total_satellites", 0));
        global_vars_["total_tasks"] = VariableValue(summary.value("total_tasks", 0));
        global_vars_["scheduled_tasks"] = VariableValue(summary.value("scheduled_tasks", 0));
        global_vars_["unscheduled_tasks"] = VariableValue(summary.value("unscheduled_tasks", 0));
        global_vars_["total_profit"] = VariableValue(summary.value("total_profit", 0));
    }
    if (config.contains("shared_resources") && config["shared_resources"].contains("resources")) {
        auto& resources = config["shared_resources"]["resources"];
        int resource_count = 0;
        for (const auto& res : resources) {
            std::string res_id = res.value("resource_id", "");
            if (!res_id.empty()) {
                global_vars_[res_id + "_type"] = VariableValue(res.value("type", std::string("UNKNOWN")));
                global_vars_[res_id + "_name"] = VariableValue(res.value("name", std::string("")));
                global_vars_[res_id + "_capacity"] = VariableValue(res.value("capacity", 0));
                resource_count++;
            }
        }
        global_vars_["resource_count"] = VariableValue(resource_count);
    }
    if (!satellite_id.empty() && config.contains("satellites")) {
        for (const auto& sat : config["satellites"]) {
            if (sat.value("satellite_id", "") == satellite_id) {
                global_vars_["satellite_id"] = VariableValue(satellite_id);
                global_vars_["node_id"] = VariableValue(sat.value("node_id", std::string("")));
                global_vars_["satellite_name"] = VariableValue(sat.value("name", std::string("")));
                global_vars_["satellite_status"] = VariableValue(sat.value("status", std::string("UNKNOWN")));
                if (sat.contains("attitude_adjustment_time_s")) {
                    global_vars_["attitude_adjustment_time"] = VariableValue(sat["attitude_adjustment_time_s"].get<int>());
                }
                if (sat.contains("orbital_params")) {
                    auto& orbit = sat["orbital_params"];
                    global_vars_["semi_major_axis_km"] = VariableValue(orbit.value("semi_major_axis_km", 0.0));
                    global_vars_["inclination_deg"] = VariableValue(orbit.value("inclination_deg", 0.0));
                    global_vars_["eccentricity"] = VariableValue(orbit.value("eccentricity", 0.0));
                }
                if (sat.contains("scheduled_tasks")) {
                    global_vars_["my_scheduled_task_count"] = VariableValue(static_cast<int>(sat["scheduled_tasks"].size()));
                }
                if (sat.contains("system_state")) {
                    auto& sys_state = sat["system_state"];
                    if (sys_state.contains("battery_percent")) {
                        global_vars_["battery"] = VariableValue(sys_state["battery_percent"].get<int>());
                    }
                    if (sys_state.contains("storage_available_mb")) {
                        global_vars_["storage_available"] = VariableValue(sys_state["storage_available_mb"].get<int>());
                    }
                    if (sys_state.contains("payload_status")) {
                        global_vars_["payload_status"] = VariableValue(sys_state["payload_status"].get<std::string>());
                    }
                    if (sys_state.contains("thermal_status")) {
                        global_vars_["thermal_status"] = VariableValue(sys_state["thermal_status"].get<std::string>());
                    }
                } else {
                    if (global_vars_.count("default_battery")) {
                        global_vars_["battery"] = global_vars_["default_battery"];
                    }
                    if (global_vars_.count("default_storage_available")) {
                        global_vars_["storage_available"] = global_vars_["default_storage_available"];
                    }
                    if (global_vars_.count("default_payload_status")) {
                        global_vars_["payload_status"] = global_vars_["default_payload_status"];
                    }
                }
                break;
            }
        }
    }
    logAccess("LOAD_SCHEDULE_CONFIG", schedule_json_path, Scope::GLOBAL);
}
std::string VariableManager::createSnapshot(const std::string& description) {
    std::lock_guard<std::mutex> lock(mutex_); 
    std::string snapshot_id = generateSnapshotId(); 
    VariableSnapshot snapshot;
    snapshot.snapshot_id = snapshot_id;
    snapshot.timestamp = std::time(nullptr);
    snapshot.global_vars = global_vars_;
    snapshot.local_vars = local_vars_;
    snapshot.intermediate_vars = intermediate_vars_;
    snapshot.description = description;
    snapshots_[snapshot_id] = snapshot;
    logAccess("SNAPSHOT_CREATE", snapshot_id, Scope::GLOBAL); 
    return snapshot_id;
}
bool VariableManager::restoreSnapshot(const std::string& snapshot_id) {
    std::lock_guard<std::mutex> lock(mutex_); 
    auto it = snapshots_.find(snapshot_id);
    if (it == snapshots_.end()) {
        return false;
    } 
    const VariableSnapshot& snapshot = it->second;
    global_vars_ = snapshot.global_vars;
    local_vars_ = snapshot.local_vars;
    intermediate_vars_ = snapshot.intermediate_vars;
    logAccess("SNAPSHOT_RESTORE", snapshot_id, Scope::GLOBAL); 
    return true;
}
void VariableManager::clearSnapshots() {
    std::lock_guard<std::mutex> lock(mutex_);
    snapshots_.clear();
}
std::vector<std::string> VariableManager::listSnapshots() const {
    std::lock_guard<std::mutex> lock(mutex_);   
    std::vector<std::string> ids;
    for (const auto& pair : snapshots_) {
        ids.push_back(pair.first);
    }
    return ids;
}
std::map<std::string, VariableValue> VariableManager::getAllVariables(Scope scope) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return getScopeMap(scope);
}
std::map<std::string, VariableValue> VariableManager::getAllVariables() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::map<std::string, VariableValue> all_vars;
    all_vars.insert(global_vars_.begin(), global_vars_.end());
    all_vars.insert(intermediate_vars_.begin(), intermediate_vars_.end());
    all_vars.insert(local_vars_.begin(), local_vars_.end());
    return all_vars;
}
std::string VariableManager::exportToString() const {
    std::lock_guard<std::mutex> lock(mutex_);  
    std::ostringstream oss;
    auto exportScope = [&](const std::map<std::string, VariableValue>& vars, const std::string& scope_name) {
        for (const auto& pair : vars) {
            oss << scope_name << ":" << pair.first << "=";           
            const VariableValue& val = pair.second;
            switch (val.getType()) {
                case VariableValue::Type::INT:
                    oss << "int:" << val.asInt();
                    break;
                case VariableValue::Type::DOUBLE:
                    oss << "double:" << val.asDouble();
                    break;
                case VariableValue::Type::STRING:
                    oss << "string:" << val.asString();
                    break;
                case VariableValue::Type::BOOL:
                    oss << "bool:" << (val.asBool() ? "true" : "false");
                    break;
                default:
                    oss << "unknown:";
            }
            oss << "\n";
        }
    };    
    exportScope(global_vars_, "global");
    exportScope(local_vars_, "local");
    exportScope(intermediate_vars_, "intermediate");    
    return oss.str();
}
void VariableManager::importFromString(const std::string& data) {
    std::lock_guard<std::mutex> lock(mutex_);   
    std::istringstream iss(data);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.empty()) continue;
        size_t colon1 = line.find(':');
        size_t equals = line.find('=');
        size_t colon2 = line.find(':', equals);       
        if (colon1 == std::string::npos || equals == std::string::npos || colon2 == std::string::npos) {
            continue;
        }
        std::string scope_name = line.substr(0, colon1);
        std::string name = line.substr(colon1 + 1, equals - colon1 - 1);
        std::string type_str = line.substr(equals + 1, colon2 - equals - 1);
        std::string value_str = line.substr(colon2 + 1);
        Scope scope;
        if (scope_name == "global") scope = Scope::GLOBAL;
        else if (scope_name == "local") scope = Scope::LOCAL;
        else if (scope_name == "intermediate") scope = Scope::INTERMEDIATE;
        else continue;
        VariableValue value;
        if (type_str == "int") {
            value = VariableValue(std::stoi(value_str));
        } else if (type_str == "double") {
            value = VariableValue(std::stod(value_str));
        } else if (type_str == "bool") {
            value = VariableValue(value_str == "true");
        } else if (type_str == "string") {
            value = VariableValue(value_str);
        } else {
            continue;
        }
        getScopeMap(scope)[name] = value;
    }
}
std::map<std::string, VariableValue>& VariableManager::getScopeMap(Scope scope) {
    switch (scope) {
        case Scope::GLOBAL:
            return global_vars_;
        case Scope::LOCAL:
            return local_vars_;
        case Scope::INTERMEDIATE:
            return intermediate_vars_;
        default:
            throw std::runtime_error("Unknown scope");
    }
}
const std::map<std::string, VariableValue>& VariableManager::getScopeMap(Scope scope) const {
    switch (scope) {
        case Scope::GLOBAL:
            return global_vars_;
        case Scope::LOCAL:
            return local_vars_;
        case Scope::INTERMEDIATE:
            return intermediate_vars_;
        default:
            throw std::runtime_error("Unknown scope");
    }
}
void VariableManager::logAccess(const std::string& operation, const std::string& name, Scope scope) const {
    if (!tracking_enabled_) return;
    std::string scope_str;
    switch (scope) {
        case Scope::GLOBAL: scope_str = "GLOBAL"; break;
        case Scope::LOCAL: scope_str = "LOCAL"; break;
        case Scope::INTERMEDIATE: scope_str = "INTERMEDIATE"; break;
    }
    std::time_t now = std::time(nullptr);
    char time_buf[32];
    std::strftime(time_buf, sizeof(time_buf), "%H:%M:%S", std::localtime(&now));
    std::string log_entry = std::string(time_buf) + " [" + operation + "] " + scope_str + "::" + name;
    access_log_.push_back(log_entry);
}
std::string VariableManager::generateSnapshotId() {
    std::ostringstream oss;
    oss << "snapshot_" << (++snapshot_counter_);
    return oss.str();
}
