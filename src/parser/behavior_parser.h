#ifndef PARSER_BEHAVIOR_PARSER_H
#define PARSER_BEHAVIOR_PARSER_H

#include "../core/types.h"

class BehaviorTreeParser {
public:
    std::vector<BehaviorNode> instantiate(
        const BehaviorNode& definition,
        const std::map<std::string, std::string>& params
    );
private:
    void substituteVariables(
        BehaviorNode& node,
        const std::map<std::string, std::string>& params
    );
};

#endif