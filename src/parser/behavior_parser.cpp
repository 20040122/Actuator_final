#include "behavior_parser.h"
#include <iostream>
#include <sstream>


static std::string replaceVariables(const std::string& text, const std::map<std::string, std::string>& params) {
    std::string result = text;
    size_t pos = 0;
    while ((pos = result.find("${", pos)) != std::string::npos) {
        size_t end_pos = result.find("}", pos);
        if (end_pos == std::string::npos) break;
        std::string var_name = result.substr(pos + 2, end_pos - pos - 2);
        auto it = params.find(var_name);
        if (it != params.end()) {
            result.replace(pos, end_pos - pos + 1, it->second);
            pos += it->second.length();
        } else {
            pos = end_pos + 1;
        }
    }
    return result;
}
static BehaviorNode expandNode(const BehaviorNode& node,
                               const std::map<std::string, std::string>& params) {
    BehaviorNode expanded_node = node;

    for (std::map<std::string, std::string>::iterator it = expanded_node.params.begin();
         it != expanded_node.params.end(); ++it) {
        it->second = replaceVariables(it->second, params);
    }

    expanded_node.children.clear();
    expanded_node.children.reserve(node.children.size());
    for (size_t i = 0; i < node.children.size(); ++i) {
        expanded_node.children.push_back(expandNode(node.children[i], params));
    }

    return expanded_node;
}
std::vector<BehaviorNode> BehaviorTreeParser::instantiate(
    const BehaviorNode& definition,
    const std::map<std::string, std::string>& params) {
    std::vector<BehaviorNode> nodes;
    nodes.reserve(1);
    nodes.push_back(expandNode(definition, params));
    return nodes;
}
void BehaviorTreeParser::substituteVariables(
    BehaviorNode& node,
    const std::map<std::string, std::string>& params) {
    for (std::map<std::string, std::string>::iterator it = node.params.begin(); 
         it != node.params.end(); ++it) {
        it->second = replaceVariables(it->second, params);
    }
}
