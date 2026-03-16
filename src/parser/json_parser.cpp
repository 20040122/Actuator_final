#include "json_parser.h"

#include <fstream>
#include <sstream>
#include <stdexcept>

#include "../core/logger.h"
#include "../third_party/nlohmann/json.hpp"

using json = nlohmann::json;

namespace {

NodeType stringToNodeType(const std::string& type_str) {
    if (type_str == "Sequence") return NodeType::SEQUENCE;
    if (type_str == "Selector") return NodeType::SELECTOR;
    if (type_str == "Parallel") return NodeType::PARALLEL;
    if (type_str == "Action") return NodeType::ACTION;
    if (type_str == "Condition") return NodeType::CONDITION;
    if (type_str == "SubTree") return NodeType::SUBTREE;

    LOG_WARN("未知节点类型，按 Action 处理: " + type_str);
    return NodeType::ACTION;
}

std::string jsonScalarToString(const json& value, bool* converted = NULL) {
    bool ok = true;
    std::string text;

    if (value.is_string()) {
        text = value.get<std::string>();
    } else if (value.is_number_integer()) {
        text = std::to_string(value.get<long long>());
    } else if (value.is_number_unsigned()) {
        text = std::to_string(value.get<unsigned long long>());
    } else if (value.is_number_float()) {
        std::ostringstream oss;
        oss << value.get<double>();
        text = oss.str();
    } else if (value.is_boolean()) {
        text = value.get<bool>() ? "true" : "false";
    } else {
        ok = false;
    }

    if (converted != NULL) {
        *converted = ok;
    }
    return text;
}

void parseScalarObjectToMap(const json& object_json,
                            std::map<std::string, std::string>& out) {
    if (!object_json.is_object()) {
        return;
    }

    for (json::const_iterator it = object_json.begin(); it != object_json.end(); ++it) {
        bool converted = false;
        const std::string value = jsonScalarToString(it.value(), &converted);
        if (converted) {
            out[it.key()] = value;
        }
    }
}

void parseVariables(const json& vars_json, BehaviorNode& node) {
    if (vars_json.is_object()) {
        for (json::const_iterator it = vars_json.begin(); it != vars_json.end(); ++it) {
            node.variables.push_back(it.key());
            if (it.value().is_object() && it.value().contains("init")) {
                bool converted = false;
                const std::string init_value = jsonScalarToString(it.value()["init"], &converted);
                if (converted) {
                    node.variable_inits[it.key()] = init_value;
                }
            }
        }
        return;
    }

    if (vars_json.is_array()) {
        for (size_t i = 0; i < vars_json.size(); ++i) {
            if (vars_json[i].is_string()) {
                node.variables.push_back(vars_json[i].get<std::string>());
            }
        }
    }
}

void parseConstants(const json& constants_json, BehaviorNode& node) {
    if (!constants_json.is_object()) {
        return;
    }

    for (json::const_iterator it = constants_json.begin(); it != constants_json.end(); ++it) {
        const json& constant_def = it.value();
        bool converted = false;

        if (constant_def.is_object() && constant_def.contains("value")) {
            const std::string value = jsonScalarToString(constant_def["value"], &converted);
            if (converted) {
                node.constants[it.key()] = value;
            }
            continue;
        }

        const std::string value = jsonScalarToString(constant_def, &converted);
        if (converted) {
            node.constants[it.key()] = value;
        }
    }
}

BehaviorNode parseNodeFromJson(const json& node_json) {
    BehaviorNode node;
    node.name = node_json.value("name", "");
    node.type = stringToNodeType(node_json.value("type", "Action"));
    node.description = node_json.value("description", "");
    node.command = node_json.value("command", "");
    node.expression = node_json.value("expression", "");

    if (node_json.contains("params")) {
        parseScalarObjectToMap(node_json["params"], node.params);
    }

    if (node_json.contains("variables")) {
        parseVariables(node_json["variables"], node);
    }

    if (node_json.contains("constants")) {
        parseConstants(node_json["constants"], node);
    }

    if (node_json.contains("observation") && node_json["observation"].is_object()) {
        for (json::const_iterator it = node_json["observation"].begin();
             it != node_json["observation"].end(); ++it) {
            if (it.value().is_string()) {
                node.observation[it.key()] = it.value().get<std::string>();
            }
        }
    }

    if (node_json.contains("children") && node_json["children"].is_array()) {
        for (size_t i = 0; i < node_json["children"].size(); ++i) {
            const json& child_json = node_json["children"][i];
            if (child_json.is_object()) {
                node.children.push_back(parseNodeFromJson(child_json));
            }
        }
    }

    return node;
}

TaskSegment parseTaskSegment(const json& task_json, const std::string& sat_id) {
    TaskSegment task;
    task.satellite_id = sat_id;
    task.segment_id = task_json.value("segment_id", "");
    task.task_id = task_json.value("task_id", "");
    task.behavior_ref = task_json.value("behavior_ref", "");

    if (task_json.contains("behavior_params")) {
        parseScalarObjectToMap(task_json["behavior_params"], task.behavior_params);
    }

    return task;
}

} // namespace

BehaviorNode BehaviorLibraryParser::parseBehaviorDefinition(
    const std::string& library_file,
    const std::string& behavior_name) {

    std::ifstream file(library_file.c_str());
    if (!file.is_open()) {
        throw std::runtime_error("无法打开行为库文件: " + library_file);
    }

    json library_json;
    file >> library_json;

    if (!library_json.contains("behavior_definitions") ||
        !library_json["behavior_definitions"].is_object()) {
        throw std::runtime_error("行为库文件缺少 behavior_definitions 对象");
    }

    const json& behaviors = library_json["behavior_definitions"];
    if (!behaviors.contains(behavior_name) || !behaviors[behavior_name].is_object()) {
        throw std::runtime_error("未找到行为定义: " + behavior_name);
    }

    BehaviorNode root_node = parseNodeFromJson(behaviors[behavior_name]);
    if (root_node.name.empty()) {
        root_node.name = behavior_name;
    }

    LOG_DEBUG("成功加载行为定义: " + behavior_name);
    return root_node;
}

ScheduleParser::MultiSatSchedule ScheduleParser::parseAllSatellites(
    const std::string& schedule_file) {

    MultiSatSchedule result;

    try {
        std::ifstream file(schedule_file.c_str());
        if (!file.is_open()) {
            LOG_ERROR("无法打开调度文件: " + schedule_file);
            return result;
        }

        json schedule_json;
        file >> schedule_json;

        if (!schedule_json.contains("satellites") || !schedule_json["satellites"].is_array()) {
            LOG_ERROR("调度文件缺少 satellites 数组");
            return result;
        }

        const json& satellites = schedule_json["satellites"];
        for (size_t i = 0; i < satellites.size(); ++i) {
            const json& satellite = satellites[i];
            if (!satellite.is_object()) {
                LOG_WARN("satellites 中存在非对象项，已跳过");
                continue;
            }

            const std::string sat_id = satellite.value("satellite_id", "");
            if (sat_id.empty()) {
                LOG_WARN("存在缺少 satellite_id 的卫星项，已跳过");
                continue;
            }

            result.satellite_ids.push_back(sat_id);

            SatelliteInfo sat_info;
            sat_info.satellite_id = sat_id;
            sat_info.node_id = satellite.value("node_id", sat_id);
            sat_info.name = satellite.value("name", sat_id);
            result.satellites.push_back(sat_info);

            std::vector<TaskSegment> tasks;
            if (satellite.contains("scheduled_tasks") && satellite["scheduled_tasks"].is_array()) {
                const json& scheduled_tasks = satellite["scheduled_tasks"];
                tasks.reserve(scheduled_tasks.size());
                for (size_t task_idx = 0; task_idx < scheduled_tasks.size(); ++task_idx) {
                    const json& task_json = scheduled_tasks[task_idx];
                    if (!task_json.is_object()) {
                        LOG_WARN("scheduled_tasks 中存在非对象项，已跳过");
                        continue;
                    }
                    tasks.push_back(parseTaskSegment(task_json, sat_id));
                }
            }
            result.satellite_tasks[sat_id] = tasks;
        }

        std::ostringstream oss;
        oss << "解析完成，共 " << result.satellite_ids.size() << " 颗卫星";
        LOG(oss.str());

    } catch (const json::exception& e) {
        LOG_ERROR(std::string("调度 JSON 解析错误: ") + e.what());
    }

    return result;
}
