#ifndef EXECUTOR_COMMAND_HANDLERS_H
#define EXECUTOR_COMMAND_HANDLERS_H

#include "generic_executor.h"
#include <iostream>
#include <thread>
#include <chrono>

namespace executor {

class ObservationHandler : public ICommandHandler {
public:
    ExecutionResult execute(
        const std::string& command,
        const std::map<std::string, std::string>& params,
        VariableManager& var_mgr) override {
        
        std::cout << "[ObservationHandler] 开始执行观测命令" << std::endl;
        
        int duration_s = 10;
        if (var_mgr.exists("observation_duration_s")) {
            duration_s = var_mgr.get("observation_duration_s").asInt();
        }
        
        std::string target = "unknown";
        if (var_mgr.exists("target_id")) {
            target = var_mgr.get("target_id").asString();
        }
        
        std::cout << "[ObservationHandler] 目标: " << target 
                  << ", 持续时间: " << duration_s << "s" << std::endl;
        
        var_mgr.set("observation_status", VariableValue("completed"), Scope::INTERMEDIATE);
        var_mgr.set("data_size_mb", VariableValue(100), Scope::INTERMEDIATE);
        
        std::cout << "[ObservationHandler] 观测完成" << std::endl;
        
        ExecutionResult result;
        result.success = true;
        result.message = "观测完成";
        result.outputs["observation_status"] = VariableValue("completed");
        result.outputs["data_size_mb"] = VariableValue(100);
        return result;
    }
    
    std::string getCommandType() const override {
        return "OBSERVATION_EXECUTE";
    }
};

class DataTransmitHandler : public ICommandHandler {
public:
    ExecutionResult execute(
        const std::string& command,
        const std::map<std::string, std::string>& params,
        VariableManager& var_mgr) override {
        
        std::cout << "[DataTransmitHandler] 开始数据下传" << std::endl;
        
        std::string ground_station = "default";
        if (var_mgr.exists("ground_station_id")) {
            ground_station = var_mgr.get("ground_station_id").asString();
        }
        
        int data_size = 0;
        if (var_mgr.exists("data_size_mb")) {
            data_size = var_mgr.get("data_size_mb").asInt();
        }
        
        std::cout << "[DataTransmitHandler] 地面站: " << ground_station
                  << ", 数据量: " << data_size << "MB" << std::endl;
        
        var_mgr.set("transmit_status", VariableValue("success"), Scope::INTERMEDIATE);
        
        ExecutionResult result;
        result.success = true;
        result.message = "数据下传完成";
        return result;
    }
    
    std::string getCommandType() const override {
        return "DATA_TRANSMIT";
    }
};

class AttitudeControlHandler : public ICommandHandler {
public:
    ExecutionResult execute(
        const std::string& command,
        const std::map<std::string, std::string>& params,
        VariableManager& var_mgr) override {
        
        std::cout << "[AttitudeControlHandler] 执行姿态调整" << std::endl;
        
        double target_angle = 0.0;
        if (var_mgr.exists("target_angle")) {
            target_angle = var_mgr.get("target_angle").asDouble();
        }
        
        std::cout << "[AttitudeControlHandler] 目标角度: " << target_angle << std::endl;
        
        var_mgr.set("attitude_status", VariableValue("stable"), Scope::GLOBAL);
        var_mgr.set("current_angle", VariableValue(target_angle), Scope::GLOBAL);
        
        ExecutionResult result;
        result.success = true;
        result.message = "姿态调整完成";
        return result;
    }
    
    std::string getCommandType() const override {
        return "ATTITUDE_ADJUST";
    }
};

class CommandHandlerFactory {
public:
    static void registerDefaultHandlers(GenericExecutor& executor) {
        executor.registerHandler(std::make_shared<ObservationHandler>());
        executor.registerHandler(std::make_shared<DataTransmitHandler>());
        executor.registerHandler(std::make_shared<AttitudeControlHandler>());
    }
};

} // namespace executor

#endif
