# Codex 项目说明

## 项目简介

Codex 是一个基于 C++11 的多星任务执行仿真项目。系统从调度文件中读取多颗卫星的任务列表，从行为库中加载任务对应的行为树定义，再由协调器将任务分发给各卫星模拟器执行，并回收执行确认、进度和完成消息。

当前仓库更偏向“单进程内的多节点仿真”：

- 协调器和所有卫星模拟器运行在同一个进程里。
- 节点间消息通过 `InterSatComm` 统一封装，优先走本地处理器分发。
- 行为树中的 `SEND` / `RECEIVE` 命令当前通过进程内共享邮箱模拟星间信息交换。

这意味着项目已经具备较完整的调度、通信、执行抽象，但默认示例主要用于功能验证和流程演示，而不是完整分布式部署。

## 项目目标

这个项目主要解决三类问题：

- 把“调度计划”和“任务执行逻辑”解耦。
- 用行为树描述卫星任务流程，支持条件判断、分支选择和动作执行。
- 用统一通信模型模拟协调器与多颗卫星之间的任务分发和结果回传。

从当前示例看，项目重点演示了两类任务：

- `GoToStation`：卫星机动到指定位置，并输出控制指令。
- `Search`：卫星搜索目标、共享观测结果、比较距离并最终上报目标。

## 核心架构

代码按职责分为五个核心模块：

- `src/coordinator`：协调器、节点注册表、通信协议与消息路由。
- `src/parser`：调度 JSON 和行为树 JSON 解析、行为模板实例化。
- `src/executor`：行为树执行器、变量管理器、卫星模拟器。
- `src/constraint`：条件表达式求值。
- `src/core`：公共类型和日志设施。

整体关系如下：

```text
main
  -> Coordinator
     -> ScheduleParser
     -> NodeRegistry
     -> InterSatComm
     -> SatelliteSimulator (one per satellite)
        -> GenericExecutor
           -> BehaviorLibraryParser
           -> BehaviorTreeParser
           -> VariableManager
           -> ConstraintEvaluator
```

## 运行流程

程序主流程如下：

1. `src/main.cpp` 创建 `Coordinator`，读取 `src/input/schedule.json`。
2. `Coordinator` 解析调度文件，得到卫星列表和各卫星任务段。
3. `Coordinator` 初始化 `NodeRegistry` 和 `InterSatComm`，并为每颗卫星创建一个 `SatelliteSimulator`。
4. `Coordinator` 将每颗卫星的任务打包为 `BATCH_TASK_ASSIGN` 消息并发送。
5. `SatelliteSimulator` 收到批量任务后，逐个加载行为定义并调用 `GenericExecutor` 执行。
6. `GenericExecutor` 实例化行为树、初始化变量上下文、递归执行节点。
7. 卫星模拟器回传 `TASK_PROGRESS`、`TASK_COMPLETE` 和 `BATCH_TASK_ASSIGN_ACK`。
8. `Coordinator` 汇总各卫星 ACK，输出任务执行完成结果。

## 目录说明

关键目录和文件如下：

- `src/main.cpp`：程序入口。
- `src/coordinator/coordinator.cpp`：系统初始化、任务分发、ACK 汇总。
- `src/coordinator/inter_sat_comm.cpp`：通信封装，支持本地处理器和 socket 框架。
- `src/parser/json_parser.cpp`：解析调度文件和行为库定义。
- `src/parser/behavior_parser.cpp`：将行为模板与运行时参数合并。
- `src/executor/generic_executor.cpp`：行为树执行和内置命令实现。
- `src/executor/satellite_simulator.cpp`：卫星节点侧的任务接收与执行。
- `src/input/schedule.json`：多卫星调度示例。
- `src/input/behaviorTree.json`：行为树库示例。
- `demo.txt`：任务 DSL 草稿，可作为行为树设计的语义参考。

## 输入数据模型

### 1. 调度文件

调度文件位于 `src/input/schedule.json`，核心结构是：

```json
{
  "satellites": [
    {
      "satellite_id": "S1",
      "node_id": "node_s1",
      "scheduled_tasks": [
        {
          "segment_id": "S1#0001",
          "task_id": "T1",
          "behavior_ref": "GoToStation",
          "behavior_params": {
            "tx": 0.0,
            "ty": 0.0,
            "tz": 0.0
          }
        }
      ]
    }
  ]
}
```

字段含义：

- `satellite_id`：卫星逻辑标识。
- `node_id`：通信节点标识。
- `scheduled_tasks`：该卫星要执行的任务列表。
- `segment_id`：任务段标识。
- `task_id`：任务 ID。
- `behavior_ref`：引用的行为定义名称。
- `behavior_params`：行为模板运行时参数。

### 行为库文件

行为库位于 `src/input/behaviorTree.json`，每个行为定义由行为树节点组成。当前支持的节点类型包括：

- `Sequence`
- `Selector`
- `Parallel`
- `Action`
- `Condition`

每个节点可以带有以下信息：

- `name`：节点名称。
- `type`：节点类型。
- `command`：动作命令。
- `expression`：条件表达式。
- `params`：动作参数。
- `variables`：局部变量及默认值。
- `constants`：常量定义。
- `observation`：观测变量到传感器键的映射。
- `children`：子节点列表。

## 执行模型

### 行为树执行

执行器 `GenericExecutor` 会对任务执行以下步骤：

1. 初始化任务上下文，将 `satellite_id`、`task_id`、`segment_id` 写入变量空间。
2. 为行为定义中的变量默认值和常量赋初值。
3. 用 `behavior_params` 替换模板中的 `${var}` 占位符。
4. 按节点类型递归执行行为树。
5. 在节点失败时按快照回滚变量状态。

### 变量作用域

变量由 `VariableManager` 管理，分为三类：

- `GLOBAL`：跨任务保留的全局变量。
- `LOCAL`：单次任务执行上下文变量。
- `INTERMEDIATE`：动作输出等中间结果。

### 条件与表达式

项目支持两类表达式：

- 布尔条件表达式：由 `ConstraintEvaluator` 处理，支持 `==`、`!=`、`<`、`>`、`<=`、`>=`、`&&`、`||`、`!`。
- 算术表达式：由执行器中的数值表达式解析器处理，支持 `+`、`-`、`*`、`/` 和 `sqrt(...)`。

## 当前内置命令

`GenericExecutor` 当前实现了以下内置命令：

- `NO_OP`：空操作。
- `ASSIGN`：计算表达式并写入局部变量。
- `MOVE_TO`：更新位置变量，并同步写入 `POS_X`、`POS_Y`、`POS_Z`。
- `SEND`：把参数打包到共享邮箱，模拟广播。
- `RECEIVE`：从共享邮箱读取数据并写回局部变量。
- `EMIT_COMMAND`：输出控制指令到中间变量。
- `NEXT_TASK`：设置下一任务标记。
- `REPORT_TARGET`：写入目标上报结果。
- `EXIT`：设置任务退出标记。
- `SETVAR ... = ...`：直接设置全局变量。

## 示例场景

默认示例调度了 6 颗卫星：

- `S1` 到 `S5` 依次执行 `GoToStation` 和 `Search`。
- `S6` 只执行 `Search`。

这组示例主要用于演示：

- 多卫星批量任务下发。
- 参数化行为树实例化。
- 目标搜索过程中的信息共享。
- 协调器对各卫星任务 ACK 的汇总。

## 通信机制说明

`InterSatComm` 具备 socket 初始化、监听、发送队列、接收队列和心跳线程等基础能力，但当前示例默认通过本地处理器快速分发消息：

- 如果目标节点已在本进程注册本地处理器，消息会直接投递到对应处理函数。
- 否则消息会进入发送队列，等待真实网络连接节点消费。

因此，当前工程同时具备“通信协议抽象”和“单进程仿真捷径”两层能力。

## 构建与运行

### 配置

```bash
cmake -S . -B build -G Ninja
```

### 编译

```bash
cmake --build build
```

### 运行

Unix:

```bash
MS_LOG_LEVEL=debug ./build/main
```

Windows PowerShell:

```powershell
$env:MS_LOG_LEVEL="debug"
.\build\main.exe
```

## 当前实现边界

为了让使用者快速理解项目现状，下面几点值得注意：

- 项目目前没有独立自动化测试，主要依赖示例 JSON 和程序日志验证。
- `Parallel` 节点在语义上表示并行，但当前实现是顺序遍历子节点并汇总结果。
- 行为树里的 `SEND` / `RECEIVE` 目前使用进程级共享邮箱，不是独立的星间网络协议。
- 调度器当前更接近“静态任务分发表”，还没有复杂的在线重规划能力。
- 示例输入文件路径在 `main.cpp` 中写死为 `src/input/schedule.json`。

## 适合继续扩展的方向

如果后续继续演进，这个项目比较自然的扩展方向包括：

- 增加 `tests/` 自动化测试并接入 `ctest`。
- 将行为树命令扩展为可注册插件，而不是固定写在执行器里。
- 把共享邮箱式的 `SEND` / `RECEIVE` 升级为真正的节点间消息协议。
- 支持更丰富的调度约束、任务状态机和失败恢复策略。
- 将示例输入、运行参数和端口配置改为命令行或配置文件注入。
