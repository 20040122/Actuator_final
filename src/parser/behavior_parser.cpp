#include "behavior_parser.h"

namespace {

std::string replaceVariables(const std::string& text,
                             const std::map<std::string, std::string>& params) {
    std::string result = text;
    size_t pos = 0;
    while ((pos = result.find("${", pos)) != std::string::npos) {
        size_t end_pos = result.find("}", pos);
        if (end_pos == std::string::npos) {
            break;
        }
        const std::string var_name = result.substr(pos + 2, end_pos - pos - 2);
        std::map<std::string, std::string>::const_iterator it = params.find(var_name);
        if (it != params.end()) {
            result.replace(pos, end_pos - pos + 1, it->second);
            pos += it->second.length();
        } else {
            pos = end_pos + 1;
        }
    }
    return result;
}

std::map<std::string, std::string> buildTemplateParams(
    const BehaviorNode& definition,
    const std::map<std::string, std::string>& runtime_params) {

    std::map<std::string, std::string> merged;

    for (std::map<std::string, std::string>::const_iterator it = definition.variable_inits.begin();
         it != definition.variable_inits.end(); ++it) {
        merged[it->first] = it->second;
    }

    for (std::map<std::string, std::string>::const_iterator it = definition.constants.begin();
         it != definition.constants.end(); ++it) {
        merged[it->first] = it->second;
    }

    for (std::map<std::string, std::string>::const_iterator it = runtime_params.begin();
         it != runtime_params.end(); ++it) {
        merged[it->first] = it->second;
    }

    return merged;
}

BehaviorNode expandNode(const BehaviorNode& node,
                        const std::map<std::string, std::string>& params) {
    BehaviorNode expanded_node = node;

    for (std::map<std::string, std::string>::iterator it = expanded_node.params.begin();
         it != expanded_node.params.end(); ++it) {
        it->second = replaceVariables(it->second, params);
    }

    expanded_node.command = replaceVariables(expanded_node.command, params);
    expanded_node.expression = replaceVariables(expanded_node.expression, params);

    if (expanded_node.type == NodeType::CONDITION &&
        expanded_node.expression.empty() &&
        !expanded_node.observation.empty()) {
        expanded_node.expression = "true";
    }

    expanded_node.children.clear();
    expanded_node.children.reserve(node.children.size());
    for (size_t i = 0; i < node.children.size(); ++i) {
        expanded_node.children.push_back(expandNode(node.children[i], params));
    }

    return expanded_node;
}

} // namespace

std::vector<BehaviorNode> BehaviorTreeParser::instantiate(
    const BehaviorNode& definition,
    const std::map<std::string, std::string>& params) {

    std::vector<BehaviorNode> nodes;
    nodes.reserve(1);

    const std::map<std::string, std::string> merged_params =
        buildTemplateParams(definition, params);
    nodes.push_back(expandNode(definition, merged_params));

    return nodes;
}
