# `demo.txt` 到 `src/input` 的映射说明

- `demo.txt` 是任务 DSL/语义草稿
- `behaviorTree.json` 是把 `mission` 定义手工整理成行为树后的结果
- `schedule.json` 是把 `GroupTasks` 手工整理成调度计划后的结果

##  总体映射关系

`demo.txt` 里的内容可以分成两类：

1. `mission ... { ... }`
   这部分描述“任务做什么”，对应 `src/input/behaviorTree.json`
2. `GroupTasks { ... }`
   这部分描述“哪些卫星按什么顺序执行哪些任务”，对应 `src/input/schedule.json`


## `mission` 如何映射到 `behaviorTree.json`

###  `mission` 名称到行为定义名称

例如：

```text
mission GoToStation { ... }
mission Search { ... }
```

对应到：

```json
{
  "behavior_definitions": {
    "GoToStation": { ... },
    "Search": { ... }
  }
}
```

也就是说：

- `mission GoToStation` 映射为 `behavior_definitions.GoToStation`
- `mission Search` 映射为 `behavior_definitions.Search`

## 变量和常量的映射

`demo.txt` 中的变量定义：

```text
float sx = 0.0;
bool is_target = false;
string rec_type = "";
```

映射到：

```json
"variables": {
  "sx": { "type": "float", "init": 0.0 },
  "is_target": { "type": "bool", "init": false },
  "rec_type": { "type": "string", "init": "" }
}
```

`demo.txt` 中的常量定义：

```text
const float inf = 1000000.0;
const string target = "alpha";
```

映射到：

```json
"constants": {
  "inf": { "type": "float", "value": 1000000.0 },
  "target": { "type": "string", "value": "alpha" }
}
```

规则是：

- 普通变量进入 `variables`
- `const` 常量进入 `constants`
- `type` 保留 DSL 声明的类型
- `init` 表示变量初始值
- `value` 表示常量值

## `Observation` 的映射

DSL 写法：

```text
Observation {
  sx: POS_X;
  sy: POS_Y;
  sz: POS_Z;
}
```

在 JSON 中会变成一个 `Action` 节点：

```json
{
  "type": "Action",
  "name": "ObserveCurrentPosition",
  "command": "NO_OP",
  "observation": {
    "sx": "POS_X",
    "sy": "POS_Y",
    "sz": "POS_Z"
  }
}
```




##  `Decision` 的映射

DSL 写法：

```text
Decision {
  {
    is_target = (rec_type == target);
  }
}
```

会被拆成动作节点，例如：

```json
{
  "type": "Action",
  "name": "UpdateTargetMatchFlag",
  "command": "ASSIGN",
  "params": {
    "is_target": "rec_type == target"
  }
}
```

映射规则：

- 赋值语句通常映射为 `command: "ASSIGN"`
- `params` 的 key 是被赋值变量名
- `params` 的 value 是表达式文本

如果一个 `Decision` 里有多条顺序语句，就会拆成多个 `Action` 节点，并按原顺序放进 `children`。

例如：

```text
Decision {
  {
    dx = x - tx;
    dy = y - ty;
    dz = z - tz;
    dist = sqrt(dx * dx + dy * dy + dz * dz);
    Send(x, y, z, dist, true);
  }
}
```

会被拆成：

- 一个 `ASSIGN` 节点，计算 `dx/dy/dz/dist`
- 一个 `SEND` 节点，发送结果

##  `Orientation` 的映射

DSL 中的 `Orientation` 表示条件判断，例如：

```text
Orientation { sx == tx && sy == ty && sz == tz }
Decision {
  {
    nextTask;
  }
}
```

在 JSON 中不会直接只放一个 `Condition`，而是通常会展开成：

- 一个 `Selector`
- 第一个分支是 `Sequence`
- `Sequence` 里先放 `Condition`
- 条件成功后再继续执行对应 `Action`
- 最后再加一个 `NO_OP` 兜底分支

对应结构类似：

```json
{
  "type": "Selector",
  "children": [
    {
      "type": "Sequence",
      "children": [
        { "type": "Condition", "expression": "sx == tx && sy == ty && sz == tz" },
        { "type": "Action", "command": "NEXT_TASK" }
      ]
    },
    {
      "type": "Action",
      "command": "NO_OP"
    }
  ]
}
```

这表示一条“if / else skip”语义：

- 条件成立时，执行后续动作
- 条件不成立时，走 `NO_OP` 分支，整个树继续执行

### 多分支 `Orientation` 的映射

`Search` 任务里有：

```text
Orientation { { dist < min_dist } { dist == min_dist } }
Decision {
  {
    min_dist = dist;
    Send(x, y, z, dist, true);
    MoveTo(tx, ty, tz);
  }
  {
    MoveTo(tx, ty, tz);
  }
}
```

这会映射成一个 `Selector`，其中每个候选分支都是一个 `Sequence`：

- 分支 1：`Condition(dist < min_dist)` -> `ASSIGN` -> `SEND` -> `MOVE_TO`
- 分支 2：`Condition(dist == min_dist)` -> `MOVE_TO`
- 分支 3：`NO_OP`

因此可以把它理解成：

- DSL 的“多候选 Orientation + 多个 Decision 分支”
- 对应 JSON 的“一个 Selector + 多个 Sequence 子分支”

## 特殊语句和内置命令的映射

`demo.txt` 中的几个特殊语句，在 JSON 中会对应内置命令：

| DSL 写法 | JSON 中的 `command` | 说明 |
| --- | --- | --- |
| `MoveTo(tx, ty, tz);` | `MOVE_TO` | 更新任务位置变量，并同步位置观测值 |
| `Send(x, y, z, dist, true);` | `SEND` | 向共享邮箱写入一组键值 |
| `is_target = Receive(...);` | `RECEIVE` | 读取共享邮箱，并用 `result_to` 记录是否收到数据 |
| `ReportTarget(tx, ty, tz);` | `REPORT_TARGET` | 记录目标上报结果 |
| `nextTask;` | `NEXT_TASK` | 标记当前任务可切换到下一任务 |
| `exit;` | `EXIT` | 标记任务退出 |
| `Action { CMD_X: tx; }` | `EMIT_COMMAND` | 产生对外控制输出 |

其中 `RECEIVE` 的 JSON 比 DSL 多了一个运行时字段：

```json
{
  "type": "Action",
  "command": "RECEIVE",
  "params": {
    "tx": "${tx}",
    "ty": "${ty}",
    "tz": "${tz}",
    "min_dist": "${min_dist}",
    "is_target": "${is_target}",
    "result_to": "is_target"
  }
}
```

这里的 `result_to` 不是 `demo.txt` 原文里的字段，而是为了适配当前执行器加上的辅助参数，表示：

- 如果共享邮箱里有数据，则把 `is_target` 置为 `true`
- 如果没有数据，则把 `is_target` 置为 `false`

##  `Action` 块的映射

DSL 写法：

```text
Action {
  CMD_X: tx;
  CMD_Y: ty;
  CMD_Z: tz;
}
```

映射为：

```json
{
  "type": "Action",
  "command": "EMIT_COMMAND",
  "params": {
    "CMD_X": "${tx}",
    "CMD_Y": "${ty}",
    "CMD_Z": "${tz}"
  }
}
```

规则是：

- DSL 的 `Action { 输出键: 变量; }`
- 映射为 `command: "EMIT_COMMAND"`
- `params` 保存输出键和值表达式

`LIGHT: is_light` 也是同样的映射方式。

## 4.8 为什么 JSON 里会有 `${tx}` 这种占位符

例如：

```json
"params": {
  "x": "${tx}",
  "y": "${ty}",
  "z": "${tz}"
}
```

这是行为模板参数化的写法。实例化时会按下面顺序合并参数：

1. 行为定义里的变量初值
2. 行为定义里的常量
3. 调度文件里的 `behavior_params`

后者会覆盖前者，所以 `schedule.json` 可以为每个任务段指定不同的 `tx/ty/tz/target/target_true`。

## `GroupTasks` 如何映射到 `schedule.json`

DSL 写法：

```text
GroupTasks {
  [1,2,3,4,5] = {GoToStation, Search};
  [6] = {Search};
}
```

对应到 `schedule.json` 的核心思路是：

- `[1,2,3,4,5]` 表示第 1 到第 5 颗卫星
- `[6]` 表示第 6 颗卫星
- `{GoToStation, Search}` 表示这些卫星按顺序执行两个任务
- `{Search}` 表示只执行一个任务

在当前示例中，映射结果是：

- `1` -> `S1`
- `2` -> `S2`
- `3` -> `S3`
- `4` -> `S4`
- `5` -> `S5`
- `6` -> `S6`

因此：

- `S1` 到 `S5` 的 `scheduled_tasks` 都是 `GoToStation` 然后 `Search`
- `S6` 的 `scheduled_tasks` 只有 `Search`

##  `schedule.json` 的生成规则

以 DSL：

```text
[1,2,3,4,5] = {GoToStation, Search};
```

为例，落到 JSON 后，会展开成 5 个卫星项，每个卫星都有一份任务列表。也就是说：

- 组成员会被展开成单独的卫星对象
- 任务集合会被展开成有顺序的 `scheduled_tasks` 数组

例如 `S1`：

```json
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
    },
    {
      "segment_id": "S1#0002",
      "task_id": "T2",
      "behavior_ref": "Search",
      "behavior_params": {
        "target": "alpha",
        "target_true": "alpha_true"
      }
    }
  ]
}
```

这里有几类字段不是 DSL 原文直接提供的，而是调度层补充的运行时元数据：

- `satellite_id`
- `node_id`
- `segment_id`
- `task_id`

它们的作用是让协调器和执行器能区分“哪颗卫星”“哪个任务段”“哪个任务实例”。

## `behaviorTree.json` 字段说明

最外层结构：

```json
{
  "behavior_definitions": {
    "GoToStation": { ... },
    "Search": { ... }
  }
}
```

###  顶层字段

| 字段 | 含义 |
| --- | --- |
| `behavior_definitions` | 行为定义库，key 是任务名，value 是对应行为树根节点 |

### 行为定义/节点通用字段

| 字段 | 含义 |
| --- | --- |
| `type` | 节点类型。当前示例主要使用 `Sequence`、`Selector`、`Action`、`Condition` |
| `name` | 节点名称。用于 debug 日志、状态转换日志和输出标识 |
| `children` | 子节点数组。`Sequence`/`Selector` 这类组合节点依靠它组织执行顺序 |
| `expression` | 条件表达式。主要用于 `Condition` 节点 |
| `command` | 动作命令名。主要用于 `Action` 节点 |
| `params` | 动作参数。key 为参数名，value 为表达式或占位符文本 |
| `observation` | 观测映射。把本地变量名映射到观测键，如 `sx -> POS_X` |
| `variables` | 行为级局部变量定义 |
| `constants` | 行为级常量定义 |

###  `variables` 内部字段

示例：

```json
"sx": { "type": "float", "init": 0.0 }
```

| 字段 | 含义 |
| --- | --- |
| 变量名，如 `sx` | 变量标识 |
| `type` | 变量类型，保留 DSL 中的声明类型 |
| `init` | 初始值。行为实例创建时先写入变量管理器 |

###  `constants` 内部字段

示例：

```json
"target": { "type": "string", "value": "alpha" }
```

| 字段 | 含义 |
| --- | --- |
| 常量名，如 `target` | 常量标识 |
| `type` | 常量类型 |
| `value` | 常量值 |

### 当前示例中常见的 `command`

| `command` | 含义 |
| --- | --- |
| `NO_OP` | 空操作，常用于观测节点或兜底分支 |
| `ASSIGN` | 计算表达式并写回局部变量 |
| `MOVE_TO` | 写入目标位置，更新位置相关变量 |
| `SEND` | 把参数写入共享邮箱，模拟信息共享 |
| `RECEIVE` | 从共享邮箱读入数据 |
| `EMIT_COMMAND` | 生成中间控制输出 |
| `NEXT_TASK` | 标记下一任务 |
| `REPORT_TARGET` | 标记并记录目标上报 |
| `EXIT` | 标记任务退出 |

##  `schedule.json` 字段说明

最外层结构：

```json
{
  "satellites": [
    {
      "satellite_id": "S1",
      "node_id": "node_s1",
      "scheduled_tasks": [ ... ]
    }
  ]
}
```

### 顶层字段

| 字段 | 含义 |
| --- | --- |
| `satellites` | 全部卫星的调度列表 |

###  每个卫星对象的字段

| 字段 | 含义 |
| --- | --- |
| `satellite_id` | 卫星逻辑 ID，例如 `S1` |
| `node_id` | 通信节点 ID，例如 `node_s1` |
| `scheduled_tasks` | 该卫星要依次执行的任务段数组 |

###  每个任务段对象的字段

| 字段 | 含义 |
| --- | --- |
| `segment_id` | 任务段唯一标识。通常把卫星 ID 和任务序号拼起来 |
| `task_id` | 任务实例 ID。用于调度、回执和日志区分 |
| `behavior_ref` | 引用的行为定义名称，必须能在 `behaviorTree.json.behavior_definitions` 中找到 |
| `behavior_params` | 本任务实例传给行为模板的参数，会覆盖行为定义里的默认值 |

### `behavior_params` 的作用

示例：

```json
"behavior_params": {
  "tx": 0.0,
  "ty": 0.0,
  "tz": 0.0
}
```

或：

```json
"behavior_params": {
  "target": "alpha",
  "target_true": "alpha_true"
}
```

它的作用是：

- 给每个任务段注入自己的运行参数
- 替换行为树里的 `${...}` 占位符
- 覆盖行为定义中已有的默认初值或常量值

因此在当前示例里，即使 `behaviorTree.json` 里已经给出了默认值，`schedule.json` 仍然保留 `behavior_params`，这样后续更容易按卫星或按任务实例做差异化配置。

## 从 DSL 到 JSON 的简化规则总结


1. `mission Xxx { ... }` 对应 `behavior_definitions.Xxx`
2. 变量声明对应 `variables`
3. 常量声明对应 `constants`
4. `Observation` 对应带 `observation` 的 `NO_OP` 动作节点
5. `Decision` 中的语句按顺序拆成一个或多个 `Action`
6. `Orientation + Decision` 常展开为 `Selector -> Sequence(Condition + Actions) + NO_OP`
7. 多候选 `Orientation` 展开为一个 `Selector` 的多个 `Sequence` 分支
8. `Action { ... }` 对应 `EMIT_COMMAND`
9. `GroupTasks` 对应 `schedule.json.satellites[*].scheduled_tasks`
10. `behavior_ref` 必须引用行为库里的同名 `mission`
11. `behavior_params` 是任务实例级覆盖参数

