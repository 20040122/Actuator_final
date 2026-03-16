#include "variable_manager.h"
#include <algorithm>
#include <ctime>
#include <fstream>

void VariableManager::set(const std::string& name, const VariableValue& value, Scope scope) {
    std::lock_guard<std::mutex> lock(mutex_);
    getScopeMap(scope)[name] = value;
}
VariableValue VariableManager::get(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = local_vars_.find(name);
    if (it != local_vars_.end()) {
        return it->second;
    }
    it = intermediate_vars_.find(name);
    if (it != intermediate_vars_.end()) {
        return it->second;
    }
    it = global_vars_.find(name);
    if (it != global_vars_.end()) {
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
    return true;
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
std::string VariableManager::generateSnapshotId() {
    std::ostringstream oss;
    oss << "snapshot_" << (++snapshot_counter_);
    return oss.str();
}
