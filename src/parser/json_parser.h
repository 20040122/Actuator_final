#ifndef INPUT_JSON_PARSER_H
#define INPUT_JSON_PARSER_H

#include <map>
#include <string>
#include <vector>

#include "../core/types.h"

struct SatelliteInfo {
    std::string satellite_id;
    std::string node_id;
    std::string name;
};

class ScheduleParser {
public:
    struct MultiSatSchedule {
        std::map<std::string, std::vector<TaskSegment> > satellite_tasks;
        std::vector<std::string> satellite_ids;
        std::vector<SatelliteInfo> satellites;
    };

    MultiSatSchedule parseAllSatellites(const std::string& schedule_file);
};

class BehaviorLibraryParser {
public:
    BehaviorNode parseBehaviorDefinition(
        const std::string& library_file,
        const std::string& behavior_name
    );
};

#endif
