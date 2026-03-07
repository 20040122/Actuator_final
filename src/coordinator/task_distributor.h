#ifndef TASK_DISTRIBUTOR_H
#define TASK_DISTRIBUTOR_H

#include "inter_sat_comm.h"
#include "node_registry.h"
#include "../input/json_parser.h"
#include <string>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <iostream>

namespace coordinator {

class TaskDistributor {
public:
    explicit TaskDistributor(InterSatComm* comm, NodeRegistry* node_registry = nullptr) 
        : comm_(comm), node_registry_(node_registry), message_counter_(0) {}
    
    BatchTaskAssignMessage createBatchTaskMessage(
        const std::string& satellite_id,
        const std::string& node_id,
        const std::string& plan_id,
        const std::vector<TaskSegment>& tasks) {
        
        BatchTaskAssignMessage msg;
        msg.message_id = generateMessageId();
        msg.satellite_id = satellite_id;
        msg.node_id = node_id;
        msg.plan_id = plan_id;
        msg.timestamp = getCurrentTimestamp();
        msg.require_ack = true;
        msg.ack_timeout_ms = 5000;
        
        for (const auto& task_info : tasks) {
            msg.scheduled_tasks.push_back(convertTaskInfo(task_info));
        }
        
        return msg;
    }
    
    bool distributeSatelliteTasks(
        const std::string& satellite_id,
        const std::string& node_id,
        const std::string& plan_id,
        const std::vector<TaskSegment>& tasks) {
        
        if (tasks.empty()) {
            std::cout << "[TaskDistributor] No tasks for " << satellite_id << std::endl;
            return true;
        }
        
        // 检查节点是否在线（优先使用节点注册表，否则使用通信模块）
        bool node_available = false;
        std::string status_source = "";
        std::string status_info = "";
        
        if (node_registry_) {
            NodeInfo info;
            if (node_registry_->getNodeInfo(satellite_id, info)) {
                status_source = "NodeRegistry";
                status_info = nodeStatusToString(info.status);
                // 节点必须在线且状态良好
                node_available = info.is_online && 
                                (info.status == NodeStatus::HEALTHY || 
                                 info.status == NodeStatus::DEGRADED ||
                                 info.status == NodeStatus::BUSY);
            } else {
                std::cerr << "[TaskDistributor] ERROR: Node " << satellite_id 
                          << " not found in NodeRegistry" << std::endl;
                return false;
            }
        } else {
            // 如果没有NodeRegistry，回退到使用InterSatComm
            NodeStatus comm_status = comm_->getNodeStatus(satellite_id);
            status_source = "InterSatComm";
            status_info = nodeStatusToString(comm_status);
            node_available = (comm_status != NodeStatus::OFFLINE && 
                             comm_status != NodeStatus::FAULT && 
                             comm_status != NodeStatus::UNKNOWN);
        }
        
        if (!node_available) {
            std::cerr << "[TaskDistributor] ERROR: Cannot distribute tasks to " << satellite_id 
                      << " - Node is not available (status: " << status_info 
                      << " from " << status_source << ")" << std::endl;
            return false;
        }
        std::cout << "[TaskDistributor] ✓ Node " << satellite_id << " is available (status: " 
                  << status_info << " from " << status_source << "), proceeding with task distribution" << std::endl;
        
        auto msg = createBatchTaskMessage(satellite_id, node_id, plan_id, tasks);
        
        std::cout << "[TaskDistributor] Distributing " << tasks.size() 
                  << " tasks to " << satellite_id << std::endl;
        
        std::cout << "[TaskDistributor] Task Details:" << std::endl;
        for (size_t i = 0; i < msg.scheduled_tasks.size(); ++i) {
            const auto& task = msg.scheduled_tasks[i];
            std::cout << "  [" << (i+1) << "] Task ID: " << task.task_id << std::endl;
            std::cout << "      Segment: " << task.segment_id << std::endl;
            std::cout << "      Name: " << task.task_name << std::endl;
            std::cout << "      Behavior: " << task.behavior_ref << std::endl;
            std::cout << "      Target: (" << task.target.latitude_deg 
                      << "°, " << task.target.longitude_deg << "°)" << std::endl;
            std::cout << "      Window: " << task.window.start 
                      << " ~ " << task.window.end << std::endl;
            std::cout << "      Execution: " << task.execution.planned_start 
                      << " ~ " << task.execution.planned_end 
                      << " (" << task.execution.duration_s << "s)" << std::endl;
            
            if (!task.resource_requirements.empty()) {
                std::cout << "      Resources: ";
                for (const auto& res : task.resource_requirements) {
                    std::cout << res.resource_id << " ";
                }
                std::cout << std::endl;
            }
            
            if (!task.constraints.static_constraints.empty() || 
                !task.constraints.dynamic_constraints.empty()) {
                std::cout << "      Constraints: ";
                std::cout << task.constraints.static_constraints.size() << " static, ";
                std::cout << task.constraints.dynamic_constraints.size() << " dynamic";
                std::cout << std::endl;
            }
            
            if (!task.behavior_params.empty()) {
                std::cout << "      Behavior Params: ";
                for (const auto& param : task.behavior_params) {
                    std::cout << param.first << "=" << param.second << " ";
                }
                std::cout << std::endl;
            }
        }
        
        if (!comm_->sendBatchTaskAssign(satellite_id, msg)) {
            std::cerr << "[TaskDistributor] Failed to send tasks to " 
                      << satellite_id << std::endl;
            return false;
        }
        
        std::cout << "[TaskDistributor] ✓ Tasks sent to " << satellite_id << std::endl;
        return true;
    }
    
    bool distributeAllTasks(const ScheduleParser::MultiSatSchedule& schedule) {
        std::cout << "\n[TaskDistributor] ===== Task Distribution =====" << std::endl;
        
        int success_count = 0;
        for (const auto& sat : schedule.satellites) {
            std::cout << "\n[TaskDistributor] Satellite: " << sat.satellite_id 
                      << " (" << sat.name << ")" << std::endl;
            auto it = schedule.satellite_tasks.find(sat.satellite_id);
            if (it == schedule.satellite_tasks.end() || it->second.empty()) {
                std::cout << "[TaskDistributor]   No tasks for this satellite" << std::endl;
                continue;
            }
            
            const std::vector<TaskSegment>& tasks = it->second;
            std::cout << "[TaskDistributor]   Tasks: " << tasks.size() << std::endl;
            
            if (distributeSatelliteTasks(sat.satellite_id, sat.node_id, 
                                        schedule.plan_id, tasks)) {
                success_count++;
            }
        }
        
        std::cout << "\n[TaskDistributor] Summary: " 
                  << success_count << "/" << schedule.satellites.size() << " satellites" << std::endl;
        std::cout << "[TaskDistributor] ===== Distribution Complete =====" << std::endl;
        
        return success_count == schedule.satellites.size();
    }
    
private:
    InterSatComm* comm_;
    NodeRegistry* node_registry_;
    uint64_t message_counter_;
    
    std::string generateMessageId() {
        std::ostringstream oss;
        oss << "ASSIGN_" << std::setfill('0') << std::setw(6) << (++message_counter_);
        return oss.str();
    }
    
    uint64_t getCurrentTimestamp() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }
    
    TaskAssignMessage convertTaskInfo(const TaskSegment& info) {
        TaskAssignMessage task;
        task.segment_id = info.segment_id;
        task.task_id = info.task_id;
        task.task_name = info.task_id; 
        task.profit = 0;
        task.priority = Priority::NORMAL;
        

        if (info.behavior_params.find("target_lat") != info.behavior_params.end()) {
            task.target.latitude_deg = std::stod(info.behavior_params.at("target_lat"));
        }
        if (info.behavior_params.find("target_lon") != info.behavior_params.end()) {
            task.target.longitude_deg = std::stod(info.behavior_params.at("target_lon"));
        }
        
        task.window.window_id = info.window.window_id;
        task.window.window_seq = info.window.window_seq;
        task.window.start = info.window.start;
        task.window.end = info.window.end;
        
        task.execution.planned_start = info.execution.planned_start;
        task.execution.planned_end = info.execution.planned_end;
        task.execution.duration_s = info.execution.duration_s;
        
        task.behavior_ref = info.behavior_ref;
        task.behavior_params = info.behavior_params;
        
        // TaskSegment 中没有 resource_requirements 和 constraints
        // 如果需要这些信息，需要从 schedule.json 完整解析或者扩展 TaskSegment 结构
        
        return task;
    }
};

} 

#endif // TASK_DISTRIBUTOR_H
