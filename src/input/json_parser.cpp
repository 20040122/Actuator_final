#include "json_parser.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include "../third_party/nlohmann/json.hpp"
#include "../core/logger.h"

using json = nlohmann::json;
static NodeType stringToNodeType(const std::string& type_str) {
    if (type_str == "Sequence") return NodeType::SEQUENCE;
    if (type_str == "Selector") return NodeType::SELECTOR;
    if (type_str == "Parallel") return NodeType::PARALLEL;
    if (type_str == "Action") return NodeType::ACTION;
    if (type_str == "Condition") return NodeType::CONDITION;
    if (type_str == "SubTree") return NodeType::SUBTREE;
    return NodeType::ACTION; 
}
static BehaviorNode parseNodeFromJson(const json& node_json) {
    BehaviorNode node;
    node.name = node_json.value("name", "");
    node.type = stringToNodeType(node_json.value("type", "Action"));
    node.description = node_json.value("description", "");
    node.command = node_json.value("command", "");
    node.expression = node_json.value("expression", ""); 
    if (node_json.contains("params")) {
        for (auto it = node_json["params"].begin(); it != node_json["params"].end(); ++it) {
            const std::string& key = it.key();
            const json& value = it.value();
            if (value.is_string()) {
                node.params[key] = value.get<std::string>();
            } else if (value.is_number()) {
                node.params[key] = std::to_string(value.get<double>());
            } else if (value.is_boolean()) {
                node.params[key] = value.get<bool>() ? "true" : "false";
            }
        }
    }
    if (node_json.contains("variables")) {
        for (auto& var : node_json["variables"]) {
            node.variables.push_back(var.get<std::string>());
        }
    }
    if (node_json.contains("children")) {
        for (auto& child_json : node_json["children"]) {
            node.children.push_back(parseNodeFromJson(child_json));
        }
    }
    return node;
}
std::vector<TaskSegment> ScheduleParser::parseSatelliteTasks(
    const std::string& schedule_file,
    const std::string& satellite_id) {
    std::vector<TaskSegment> tasks;
    try {
        std::ifstream file(schedule_file);
        if (!file.is_open()) {
            std::cerr << "无法打开调度文件: " << schedule_file << std::endl;
            return tasks;
        }
        json schedule_json;
        file >> schedule_json;
        file.close(); 
        std::cout << "解析任务调度文件: " << schedule_file << std::endl;
        if (!schedule_json.contains("satellites")) {
            std::cerr << "调度文件缺少satellites字段" << std::endl;
            return tasks;
        }
        for (auto& satellite : schedule_json["satellites"]) {
            std::string sat_id = satellite.value("satellite_id", "");
            if (sat_id == satellite_id && satellite.contains("scheduled_tasks")) {
                for (auto& task_json : satellite["scheduled_tasks"]) {
                    TaskSegment task;
                    task.satellite_id = satellite_id;
                    task.segment_id = task_json.value("segment_id", "");
                    task.task_id = task_json.value("task_id", "");
                    task.behavior_ref = task_json.value("behavior_ref", "");
                    if (task_json.contains("execution")) {
                        auto& exec = task_json["execution"];
                        task.execution.planned_start = exec.value("planned_start", "");
                        task.execution.planned_end = exec.value("planned_end", "");
                        task.execution.duration_s = exec.value("duration_s", 0);
                    }       
                    if (task_json.contains("window")) {
                        auto& win = task_json["window"];
                        task.window.window_id = win.value("window_id", "");
                        task.window.window_seq = win.value("window_seq", 0);
                        task.window.start = win.value("start", "");
                        task.window.end = win.value("end", "");
                    }      
                    if (task_json.contains("behavior_params")) {
                        for (auto it = task_json["behavior_params"].begin(); it != task_json["behavior_params"].end(); ++it) {
                            const std::string& key = it.key();
                            const json& value = it.value();
                            if (value.is_string()) {
                                task.behavior_params[key] = value.get<std::string>();
                            } else if (value.is_number_integer()) {
                                task.behavior_params[key] = std::to_string(value.get<int>());
                            } else if (value.is_number_float()) {
                                std::ostringstream oss;
                                oss << value.get<double>();
                                task.behavior_params[key] = oss.str();
                            } else if (value.is_boolean()) {
                                task.behavior_params[key] = value.get<bool>() ? "true" : "false";
                            } else {
                                task.behavior_params[key] = value.dump();
                            }
                        }
                    }                   
                    tasks.push_back(task);
                }
                break;
            }
        }       
        std::cout << "找到 " << tasks.size() << " 个任务用于卫星 " << satellite_id << std::endl;    
    } catch (const json::exception& e) {
        std::cerr << "JSON解析错误: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "错误: " << e.what() << std::endl;
    } 
    return tasks;
}
BehaviorNode BehaviorLibraryParser::parseBehaviorDefinition(
    const std::string& library_file,
    const std::string& behavior_name) {
    BehaviorNode root_node;
    try {
        std::ifstream file(library_file);
        if (!file.is_open()) {
            std::cerr << "无法打开行为库文件: " << library_file << std::endl;
            return root_node;
        }    
        json library_json;
        file >> library_json;
        file.close();
        LOG("解析行为库文件: " + library_file);    
        if (!library_json.contains("behavior_definitions")) {
            LOG_ERROR("行为库文件缺少behavior_definitions字段");
            return root_node;
        }
        auto& behaviors = library_json["behavior_definitions"];
        if (!behaviors.contains(behavior_name)) {
            LOG_ERROR("未找到行为定义: " + behavior_name);
            return root_node;
        }
        root_node = parseNodeFromJson(behaviors[behavior_name]);
        {
            std::ostringstream oss;
            oss << "成功加载行为定义: " << behavior_name 
                << " (类型: " << behaviors[behavior_name].value("type", "Unknown") << ")";
            LOG(oss.str());
        }    
    } catch (const json::exception& e) {
        std::cerr << "JSON解析错误: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "错误: " << e.what() << std::endl;
    }
    return root_node;
}

// ============ 新增：解析所有卫星 ============
ScheduleParser::MultiSatSchedule ScheduleParser::parseAllSatellites(
    const std::string& schedule_file) {
    
    MultiSatSchedule result;
    
    try {
        std::ifstream file(schedule_file);
        if (!file.is_open()) {
            std::cerr << "无法打开调度文件: " << schedule_file << std::endl;
            return result;
        }
        
        json schedule_json;
        file >> schedule_json;
        file.close();
        
        result.plan_id = schedule_json.value("plan_id", "");
        result.schedule_id = schedule_json.value("generated_at", "");
        
        if (!schedule_json.contains("satellites")) {
            std::cerr << "调度文件缺少satellites字段" << std::endl;
            return result;
        }
        
        for (auto& satellite : schedule_json["satellites"]) {
            std::string sat_id = satellite.value("satellite_id", "");
            result.satellite_ids.push_back(sat_id);
            
            std::vector<TaskSegment> tasks;
            if (satellite.contains("scheduled_tasks")) {
                for (auto& task_json : satellite["scheduled_tasks"]) {
                    TaskSegment task;
                    task.satellite_id = sat_id;
                    task.segment_id = task_json.value("segment_id", "");
                    task.task_id = task_json.value("task_id", "");
                    task.behavior_ref = task_json.value("behavior_ref", "");
                    
                    if (task_json.contains("execution")) {
                        auto& exec = task_json["execution"];
                        task.execution.planned_start = exec.value("planned_start", "");
                        task.execution.planned_end = exec.value("planned_end", "");
                        task.execution.duration_s = exec.value("duration_s", 0);
                    }
                    
                    if (task_json.contains("window")) {
                        auto& win = task_json["window"];
                        task.window.window_id = win.value("window_id", "");
                        task.window.window_seq = win.value("window_seq", 0);
                        task.window.start = win.value("start", "");
                        task.window.end = win.value("end", "");
                    }
                    
                    if (task_json.contains("behavior_params")) {
                        for (auto it = task_json["behavior_params"].begin(); 
                             it != task_json["behavior_params"].end(); ++it) {
                            const std::string& key = it.key();
                            const json& value = it.value();
                            if (value.is_string()) {
                                task.behavior_params[key] = value.get<std::string>();
                            } else if (value.is_number_integer()) {
                                task.behavior_params[key] = std::to_string(value.get<int>());
                            } else if (value.is_number_float()) {
                                std::ostringstream oss;
                                oss << value.get<double>();
                                task.behavior_params[key] = oss.str();
                            } else if (value.is_boolean()) {
                                task.behavior_params[key] = value.get<bool>() ? "true" : "false";
                            }
                        }
                    }
                    
                    tasks.push_back(task);
                }
            }
            
            result.satellite_tasks[sat_id] = tasks;
        }
        
        // 解析卫星节点信息
        for (auto& satellite : schedule_json["satellites"]) {
            SatelliteInfo sat_info;
            sat_info.satellite_id = satellite.value("satellite_id", "");
            sat_info.node_id = satellite.value("node_id", "");
            sat_info.name = satellite.value("name", "");
            sat_info.status = satellite.value("status", "UNKNOWN");
            
            if (satellite.contains("system_state")) {
                auto& state = satellite["system_state"];
                sat_info.battery_percent = state.value("battery_percent", 0);
                sat_info.storage_available_mb = state.value("storage_available_mb", 0);
                sat_info.payload_status = state.value("payload_status", "");
                sat_info.thermal_status = state.value("thermal_status", "");
            }
            
            result.satellites.push_back(sat_info);
        }
        
        std::cout << "解析完成，共 " << result.satellite_ids.size() 
                  << " 颗卫星" << std::endl;
        
    } catch (const json::exception& e) {
        std::cerr << "JSON解析错误: " << e.what() << std::endl;
    }
    
    return result;
}

// ============ 解析卫星节点信息 ============
std::vector<SatelliteInfo> ScheduleParser::parseSatelliteNodes(
    const std::string& schedule_file) {
    
    std::vector<SatelliteInfo> satellites;
    
    try {
        std::ifstream file(schedule_file);
        if (!file.is_open()) {
            std::cerr << "无法打开调度文件: " << schedule_file << std::endl;
            return satellites;
        }
        
        json schedule_json;
        file >> schedule_json;
        file.close();
        
        if (!schedule_json.contains("satellites")) {
            std::cerr << "调度文件缺少satellites字段" << std::endl;
            return satellites;
        }
        
        for (auto& satellite : schedule_json["satellites"]) {
            SatelliteInfo sat_info;
            sat_info.satellite_id = satellite.value("satellite_id", "");
            sat_info.node_id = satellite.value("node_id", "");
            sat_info.name = satellite.value("name", "");
            sat_info.status = satellite.value("status", "UNKNOWN");
            
            if (satellite.contains("system_state")) {
                auto& state = satellite["system_state"];
                sat_info.battery_percent = state.value("battery_percent", 0);
                sat_info.storage_available_mb = state.value("storage_available_mb", 0);
                sat_info.payload_status = state.value("payload_status", "");
                sat_info.thermal_status = state.value("thermal_status", "");
            }
            
            satellites.push_back(sat_info);
            std::cout << "  解析卫星节点: " << sat_info.satellite_id 
                      << " (node_id: " << sat_info.node_id << ")" << std::endl;
        }
        
        std::cout << "卫星节点解析完成，共 " << satellites.size() << " 个节点" << std::endl;
        
    } catch (const json::exception& e) {
        std::cerr << "JSON解析错误: " << e.what() << std::endl;
    }
    
    return satellites;
}

// ============ 新增：全局配置解析 ============
GlobalConfigParser::GlobalConfig GlobalConfigParser::parse(
    const std::string& config_file) {
    
    GlobalConfig config;
    
    try {
        std::ifstream file(config_file);
        if (!file.is_open()) {
            std::cerr << "无法打开全局配置文件: " << config_file << std::endl;
            return config;
        }
        
        json config_json;
        file >> config_json;
        file.close();
        
        config.plan_id = config_json.value("plan_id", "");
        
        // 解析多节点配置
        if (config_json.contains("multi_node_config")) {
            auto& multi = config_json["multi_node_config"];
            config.total_nodes = multi.value("total_nodes", 0);
            
            if (multi.contains("active_nodes")) {
                for (auto& node : multi["active_nodes"]) {
                    config.active_nodes.push_back(node.get<std::string>());
                }
            }
            
            if (multi.contains("node_communication")) {
                auto& comm = multi["node_communication"];
                config.node_communication.protocol = comm.value("protocol", "");
                config.node_communication.max_latency_ms = comm.value("max_latency_ms", 500);
                config.node_communication.retry_count = comm.value("retry_count", 3);
            }
        }
        
        // 解析共享资源/信号量
        if (config_json.contains("shared_resources") && 
            config_json["shared_resources"].contains("semaphores")) {
            for (auto& sem : config_json["shared_resources"]["semaphores"]) {
                SemaphoreConfig sc;
                sc.semaphore_id = sem.value("semaphore_id", "");
                sc.resource_name = sem.value("resource_name", "");
                sc.resource_type = sem.value("resource_type", "");
                sc.max_permits = sem.value("max_permits", 1);
                sc.available_permits = sem.value("available_permits", 1);
                sc.queue_policy = sem.value("queue_policy", "FIFO");
                sc.timeout_s = sem.value("timeout_s", 60);
                sc.priority_enabled = sem.value("priority_enabled", false);
                config.semaphores.push_back(sc);
            }
        }
        
        if (config_json.contains("sync_points") && 
            config_json["sync_points"].contains("barriers")) {
            for (auto& barrier : config_json["sync_points"]["barriers"]) {
                SyncBarrierConfig bc;
                bc.sync_id = barrier.value("sync_id", "");
                bc.type = barrier.value("type", "");
                bc.anchor_time = barrier.value("anchor_time", "");
                bc.window_s = barrier.value("window_s", 30);
                bc.timeout_s = barrier.value("timeout_s", 120);
                
                if (barrier.contains("participants")) {
                    for (auto& p : barrier["participants"]) {
                        bc.participants.push_back(p.get<std::string>());
                    }
                }
                config.barriers.push_back(bc);
            }
        }
        
        if (config_json.contains("resource_allocation_policy")) {
            auto& policy = config_json["resource_allocation_policy"];
            config.deadlock_detection = policy.value("deadlock_detection", false);
            config.deadlock_resolution = policy.value("deadlock_resolution", "");
        }
        
        std::cout << "全局配置解析完成" << std::endl;
        std::cout << "  - 活跃节点: " << config.active_nodes.size() << std::endl;
        std::cout << "  - 信号量: " << config.semaphores.size() << std::endl;
        std::cout << "  - 同步屏障: " << config.barriers.size() << std::endl;
        
    } catch (const json::exception& e) {
        std::cerr << "JSON解析错误: " << e.what() << std::endl;
    }
    
    return config;
}