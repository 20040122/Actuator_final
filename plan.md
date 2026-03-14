## 当前状态

### ✅ 已完成
- 行为树执行引擎（Sequence / Selector / Parallel / Action / Condition）
- JSON 配置解析（全局配置、调度计划、行为树定义）
- 分布式信号量（本地模式）
- 约束评估器（表达式词法分析 + 语法分析）
- 变量管理（全局/局部/中间作用域 + 快照回滚）
- 节点注册表 & 心跳机制框架
- 日志系统（线程安全，多级别）

### ❌ 未完成 / 模拟实现
- InterSatComm 网络通信（socket 接口声明但未实现）
- 命令执行（非信号量命令直接返回成功）
- 远程任务分发（所有任务在协调器本地执行）
- 单元测试（无测试框架、无测试用例）
- CI/CD 流水线
- 死锁检测（配置项存在但未实现）
- 故障转移 & 状态持久化

---

## 开发路线图

### 第一阶段：基础完善

#### 1.1 添加测试框架
- [ ] 集成 Google Test（CMakeLists.txt 添加 FetchContent）
- [ ] 创建 tests/ 目录结构
- [ ] 编写 ConstraintEvaluator 单元测试（表达式解析、变量求值、边界条件）
- [ ] 编写 VariableManager 单元测试（作用域、快照、类型转换）
- [ ] 编写 BehaviorTreeParser 单元测试（模板替换、递归展开）
- [ ] 编写 DistributedSemaphore 单元测试（获取/释放/超时/并发）
- [ ] 编写 JSON Parser 单元测试（global.json / schedule.json / behaviorTree.json）
- [ ] 编写 GenericExecutor 集成测试（完整行为树执行流）

#### 1.2 统一错误处理
- [ ] 定义错误码枚举（ErrorCode）和 Result<T> 类型
- [ ] 修复 coordinator.cpp 中 catch 吞异常问题（添加 LOG_ERROR）
- [ ] 修复 inter_sat_comm.cpp 中 socket 错误处理缺失
- [ ] 修复 json_parser.cpp 中解析失败无详细信息
- [ ] 统一所有模块的错误返回格式

#### 1.3 消除硬编码
- [ ] 提取端口配置（50000, 8800）到 GlobalConfig
- [ ] 提取超时配置（60s 任务等待、5000ms 心跳间隔）到 CoordinatorConfig
- [ ] 提取线程池大小到配置
- [ ] 提取消息队列容量到配置
- [ ] 更新 global.json 模板，添加新配置项

---

### 第二阶段：核心功能实现

#### 2.1 InterSatComm 真实网络通信
- [ ] 设计通信协议（消息头 + 序列化 payload）
- [ ] 实现 TCP 连接管理（connectSocket / acceptConnection）
- [ ] 实现消息序列化/反序列化（JSON → 二进制 / protobuf）
- [ ] 实现 sendThreadFunc()（发送队列 + 重试逻辑）
- [ ] 实现 receiveThreadFunc()（接收 + 消息分发）
- [ ] 实现 acceptThreadFunc()（新连接接入）
- [ ] 添加消息确认机制（ACK / NACK）
- [ ] 添加消息重传（超时重发，最大重试次数）
- [ ] 添加连接池管理（断线重连、心跳保活）

#### 2.2 远程任务执行
- [ ] 设计卫星端执行器进程（satellite_node 独立可执行文件）
- [ ] 实现任务接收 → 本地执行 → 结果回报流程
- [ ] 支持多卫星进程并行部署
- [ ] 实现远程命令执行结果回传
- [ ] 添加任务执行进度上报

#### 2.3 真实命令执行框架
- [ ] 定义 CommandHandler 接口（抽象基类）
- [ ] 实现 ATTITUDE_ADJUST 命令处理器
- [ ] 实现 PAYLOAD_CONFIG 命令处理器
- [ ] 实现 PAYLOAD_CAPTURE 命令处理器
- [ ] 实现 DATA_STORE 命令处理器
- [ ] 支持自定义命令插件注册机制

---

### 第三阶段：可靠性与健壮性

#### 3.1 故障检测与恢复
- [ ] 实现 NodeRegistry 心跳超时检测（节点离线判定）
- [ ] 实现任务重分配（节点故障 → 任务迁移到其他卫星）
- [ ] 添加任务重试机制（可配置重试次数、退避策略）
- [ ] 实现优雅降级（部分节点离线时的调度策略）

#### 3.2 分布式信号量增强
- [ ] 实现跨节点信号量同步（通过 InterSatComm）
- [ ] 实现死锁检测算法（等待图 / 资源分配图）
- [ ] 添加优先级反转避免（优先级继承协议）
- [ ] 添加信号量状态持久化（崩溃恢复）
- [ ] 实现公平调度策略（防止饥饿）

#### 3.3 状态持久化
- [ ] 设计状态快照格式（JSON / SQLite）
- [ ] 实现协调器状态保存（任务队列、节点状态、信号量状态）
- [ ] 实现崩溃恢复流程（从快照恢复执行上下文）
- [ ] 添加检查点机制（定期保存）

---

### 第四阶段：工程化与可观测性

#### 4.1 CI/CD 流水线
- [ ] 创建 GitHub Actions 工作流（build + test）
- [ ] 支持 Linux (clang/gcc) + Windows (MSVC/g++) 矩阵构建
- [ ] 添加代码覆盖率报告（gcov + Codecov）
- [ ] 添加静态分析（clang-tidy / cppcheck）
- [ ] 添加 PR 检查（构建成功 + 测试通过 + 覆盖率阈值）

#### 4.2 性能监控
- [ ] 添加行为树节点执行耗时统计
- [ ] 添加信号量等待时间监控
- [ ] 添加通信延迟指标
- [ ] 实现性能基准测试套件
- [ ] 添加运行时性能报告输出

#### 4.3 文档完善
- [ ] 编写架构设计文档（模块职责、交互关系、数据流）
- [ ] 编写 API 文档（核心类接口说明）
- [ ] 编写部署指南（多节点部署步骤）
- [ ] 编写 JSON 配置说明文档（字段含义、取值范围、示例）
- [ ] 更新 README.md（项目介绍、快速开始、构建说明）

---

## 技术债务清单

| 优先级 | 问题 | 位置 | 修复建议 |
|--------|------|------|----------|
| 🔴 高 | catch 吞异常 | coordinator.cpp:127-128 | 添加 LOG_ERROR(e.what()) |
| 🔴 高 | socket 未检查 INVALID | inter_sat_comm.cpp:77 | 添加返回值检查 |
| 🔴 高 | 并行节点无线程限制 | generic_executor.cpp | 添加线程池大小配置 |
| 🟡 中 | 消息队列满直接丢弃 | inter_sat_comm.cpp:183-184 | 添加背压机制 |
| 🟡 中 | active_contexts_ 未清理 | generic_executor.cpp | 定期清理已完成上下文 |
| 🟡 中 | 变量类型转换异常 | variable_manager.h:27-39 | 添加安全转换 + 默认值 |
| 🟢 低 | 日志无文件输出 | logger.h | 添加文件日志 sink |
| 🟢 低 | 无配置文件校验 | json_parser.cpp | 添加 JSON Schema 验证 |

---


