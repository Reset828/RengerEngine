# Task System 任务清单

> 文件：`docs/tasks/task-system.md`  
> 所属阶段：公共基础  
> 模块状态：未开始  
> 前置模块：[project-foundation](./project-foundation.md)、[diagnostics](./diagnostics.md)  
> 输入基线：[需求文档](../requirements/spec.md)、[概要设计](../design/architectureDesign.md)、[详细设计](../design/detailDesign.md)、[项目规范](../../agent.md)

## 1. 模块目标

提供 Engine 可复用的有界队列、线程池、取消令牌、优先级调度和 I/O 背压能力。

## 2. 范围边界

**包含：** BoundedQueue；CancellationToken/Source；通用 worker 池；I/O 并发闸门；任务完成消息；安全关闭。  
**不包含：** Vulkan 命令录制专属实现；具体文件 Reader；Scene 修改。

## 3. 完成规则

只有同时满足以下条件，模块才可以在 [progress.md](./progress.md) 中标记完成：

- 本文所有非阻塞必需任务均已勾选；
- 所有自动化测试通过，能力缺失用例只能明确标记为 Skipped，不能伪造通过；
- 对应公共接口和私有实现符合 `agent.md` 的命名、Pimpl、RAII 和依赖边界；
- 相关需求、设计和测试文档已经同步；

## 4. 子任务 Checklist

- [ ] **TS-001 实现 CancellationToken/Source**
- [ ] **TS-002 实现通用有界队列**
- [ ] **TS-003 实现命令合并辅助器**
- [ ] **TS-004 实现线程数自动计算**
- [ ] **TS-005 实现优先级线程池**
- [ ] **TS-006 实现任务完成队列**
- [ ] **TS-007 实现 I/O 并发闸门**
- [ ] **TS-008 实现高低水位背压器**
- [ ] **TS-009 实现 TaskSystem 安全关闭**

## 5. 子任务说明

### TS-001 实现 CancellationToken/Source

- **状态**：未开始
- **目标**：建立共享取消状态和轻量查询接口。
- **前置任务**：project-foundation/PF-005
- **预计文件**：`src/tasks/Cancellation.h`、`src/tasks/Cancellation.cpp`、`tests/unit/CancellationTests.cpp`
- **实现要求**：Source 拥有状态，Token 只观察；取消幂等且线程安全。
- **验收检查**：多个 Token 能观察一次取消；Source 销毁不会造成悬空。
- **测试要求**：并发取消和重复取消测试。
- **追踪**：FR-DATA-004、7.2

### TS-002 实现通用有界队列

- **状态**：未开始
- **目标**：实现 mutex/condition_variable 环形队列和关闭语义。
- **前置任务**：TS-001
- **预计文件**：`src/tasks/BoundedQueue.h`、`tests/unit/BoundedQueueTests.cpp`
- **实现要求**：容量固定；支持 tryPush、tryPop、批量弹出和 close；不得无限阻塞 UI。
- **验收检查**：容量、FIFO、关闭、多生产者单消费者行为正确。
- **测试要求**：并发压力、满/空/关闭测试。
- **追踪**：DDD-005、6.4

### TS-003 实现命令合并辅助器

- **状态**：未开始
- **目标**：为最后值生效的参数命令提供队列内合并策略。
- **前置任务**：TS-002
- **预计文件**：`src/tasks/CommandCoalescer.h`、`src/tasks/CommandCoalescer.cpp`、`tests/unit/CommandCoalescerTests.cpp`
- **实现要求**：只合并点大小、着色、颜色、CUDA 模式、Resize；不得重排加载/取消/输入/关闭。
- **验收检查**：相同参数保留最后值，屏障命令前后顺序不变。
- **测试要求**：覆盖连续参数、屏障分隔和队列满。
- **追踪**：6.1、6.4

### TS-004 实现线程数自动计算

- **状态**：未开始
- **目标**：实现 H=0 回退和 Phase 1/Phase 2 公式。
- **前置任务**：project-foundation/PF-006
- **预计文件**：`src/tasks/ThreadConfiguration.h`、`src/tasks/ThreadConfiguration.cpp`、`tests/unit/ThreadConfigurationTests.cpp`
- **实现要求**：Phase1 clamp(H-1,2,8)，Phase2 clamp(H/2,2,8)，配置非零覆盖且安全限制。
- **验收检查**：H=0、1、4、16 和覆盖值结果正确。
- **测试要求**：参数化单元测试。
- **追踪**：DDD-006、7.1

### TS-005 实现优先级线程池

- **状态**：未开始
- **目标**：实现四级 FIFO 任务队列和 worker 生命周期。
- **前置任务**：TS-001, TS-002, TS-004
- **预计文件**：`src/tasks/TaskSystem.h`、`src/tasks/TaskSystem.cpp`、`tests/unit/TaskSystemTests.cpp`
- **实现要求**：任务入口捕获异常；返回 TaskId；worker 不直接修改 Scene。
- **验收检查**：优先级顺序、任务完成、异常转换和停止接收正确。
- **测试要求**：确定性单 worker 测试与多 worker 压力测试。
- **追踪**：7.2、7.4、NFR-REL-001

### TS-006 实现任务完成队列

- **状态**：未开始
- **目标**：让 worker 以值对象向 Engine 单消费者报告结果。
- **前置任务**：TS-005
- **预计文件**：`src/tasks/TaskCompletion.h`、`src/tasks/TaskCompletionQueue.h`、`tests/unit/TaskCompletionTests.cpp`
- **实现要求**：完成消息包含 TaskId、DatasetId、Result；旧 Dataset 结果可识别。
- **验收检查**：成功、失败、取消结果均可安全传递。
- **测试要求**：完成顺序、队列关闭和旧 Dataset 过滤测试。
- **追踪**：5.3、7.4

### TS-007 实现 I/O 并发闸门

- **状态**：未开始
- **目标**：用 C++17 mutex/condition_variable 实现默认容量 2 的许可。
- **前置任务**：TS-005
- **预计文件**：`src/tasks/ConcurrencyGate.h`、`src/tasks/ConcurrencyGate.cpp`、`tests/unit/ConcurrencyGateTests.cpp`
- **实现要求**：等待可被 CancellationToken 唤醒；RAII Lease 自动释放许可。
- **验收检查**：活跃 I/O 不超过限制，取消等待者及时退出。
- **测试要求**：并发峰值、取消和异常释放测试。
- **追踪**：DDD-006、7.3

### TS-008 实现高低水位背压器

- **状态**：未开始
- **目标**：提供 80% 高水位、60% 低水位的暂停/恢复控制。
- **前置任务**：TS-002, TS-007
- **预计文件**：`src/tasks/BackpressureController.h`、`src/tasks/BackpressureController.cpp`、`tests/unit/BackpressureTests.cpp`
- **实现要求**：阈值可配置；取消和关闭优先于恢复。
- **验收检查**：跨过高水位暂停，降到低水位恢复，无抖动。
- **测试要求**：边界值、取消和关闭测试。
- **追踪**：NFR-PERF-003、7.3

### TS-009 实现 TaskSystem 安全关闭

- **状态**：未开始
- **目标**：按停止接收、取消、唤醒、汇合顺序关闭。
- **前置任务**：TS-005, TS-007, TS-008
- **预计文件**：`src/tasks/TaskSystem.cpp`、`tests/unit/TaskSystemShutdownTests.cpp`
- **实现要求**：shutdown 幂等；析构兜底；不得 detach worker。
- **验收检查**：活动任务、等待许可任务均能在时限内结束。
- **测试要求**：故障注入与重复 shutdown 测试。
- **追踪**：NFR-REL-002、23.2

## 6. 模块级验收

- [ ] 线程数公式与配置覆盖测试通过
- [ ] 队列、取消、优先级和背压并发测试通过
- [ ] TaskSystem 关闭无悬挂线程
- [ ] 任务异常不会跨线程或模块传播

## 7. 交接记录

- 完成日期：
- 完成人：
- 关键变更：
- 未解决问题：
- 测试命令与结果：
- 关联提交：

## 8. 变更约束

若实现需要改变公共接口、模块依赖、已确认参数或需求行为，必须先更新需求与设计文档，不得在编码任务中自行改变。
