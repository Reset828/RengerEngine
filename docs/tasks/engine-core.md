# Engine Core 任务清单

> 文件：`docs/tasks/engine-core.md`  
> 所属阶段：公共基础  
> 模块状态：进行中
> 前置模块：[project-foundation](./project-foundation.md)、[diagnostics](./diagnostics.md)、[task-system](./task-system.md)  
> 输入基线：[需求文档](../requirements/spec.md)、[概要设计](../design/architectureDesign.md)、[详细设计](../design/detailDesign.md)、[项目规范](../../agent.md)

## 1. 模块目标

实现后端无关的 Engine 生命周期、命令/事件/快照协议、Scene 协调和模块装配边界。

## 2. 范围边界

**包含：** Engine Pimpl；状态机；EngineCommand/Event/Snapshot；Scene 参数；每帧协调；Dataset 替换框架；关闭与回滚。  
**不包含：** 具体 Reader；OpenGL/Vulkan GPU 实现；Camera 具体行为；Qt 类型。

## 3. 完成规则

只有同时满足以下条件，模块才可以在 [progress.md](./progress.md) 中标记完成：

- 本文所有非阻塞必需任务均已勾选；
- 所有自动化测试通过，能力缺失用例只能明确标记为 Skipped，不能伪造通过；
- 对应公共接口和私有实现符合 `agent.md` 的命名、Pimpl、RAII 和依赖边界；
- 相关需求、设计和测试文档已经同步；

## 4. 子任务 Checklist

- [x] **EC-001 定义 EngineCommand 值类型**
- [x] **EC-002 定义 EngineEvent 值类型**
- [x] **EC-003 定义 EngineSnapshot**
- [x] **EC-004 实现 Engine 状态机**
- [x] **EC-005 实现 Scene 参数容器**
- [ ] **EC-006 实现 Engine 公共类和 Pimpl**
- [ ] **EC-007 实现命令消费与事件溢出策略**
- [ ] **EC-008 实现原子 Snapshot 发布**
- [ ] **EC-009 实现每帧协调骨架**
- [ ] **EC-010 实现 Dataset 替换和旧结果过滤**
- [ ] **EC-011 实现初始化回滚与关闭编排**

## 5. 子任务说明

### EC-001 定义 EngineCommand 值类型

- **状态**：已完成（2026-08-17）
- **目标**：实现详细设计 6.1 的封闭 variant 命令。
- **前置任务**：project-foundation/PF-006
- **实际文件**：`include/dzc/EngineCommand.h`、`src/engine/EngineCommand.cpp`、`tests/unit/EngineCommandTests.cpp`
- **实现要求**：负载不含可变裸指针、Qt 类型或 GPU 句柄；Reset 只表达请求。
- **验收检查**：每类命令可构造、访问和移动；参数基础校验可调用。
- **测试要求**：variant 分支和非法参数测试。
- **追踪**：FR-DATA-002~004、FR-REN-002~005、FR-CAM-001/003

### EC-002 定义 EngineEvent 值类型

- **状态**：已完成（2026-08-18）
- **目标**：实现消息、错误、进度、加载完成/取消和降级事件。
- **前置任务**：EC-001
- **实际文件**：`include/dzc/EngineEvent.h`、`tests/unit/EngineEventTests.cpp`
- **实现要求**：严格遵循详细设计 6.2；错误/完成/取消不可静默丢失的队列行为留待 EC-007；持续状态不放 Event。
- **验收检查**：6 个事件分支、默认值、字段访问、移动语义和封闭 variant 测试通过。
- **测试要求**：构造、移动和上下文字段测试。
- **追踪**：FR-DATA-003、FR-UI-004/005

### EC-003 定义 EngineSnapshot

- **状态**：已完成（2026-08-18）
- **目标**：实现不可变快照、Dataset/Performance/Memory 摘要。
- **前置任务**：EC-002, diagnostics/DG-006
- **实际文件**：`include/dzc/EngineState.h`、`include/dzc/EngineSnapshot.h`、`tests/unit/EngineSnapshotTests.cpp`
- **实现要求**：不得含完整点云或底层句柄；包含最近错误和参数；发布后的不可变性由后续 `shared_ptr<const EngineSnapshot>` 机制保证。
- **验收检查**：默认快照和有数据集快照字段完整；EngineState 独立定义供后续 EC-004 复用。
- **测试要求**：字段默认值、完整字段复制和 `sizeof(EngineSnapshot) <= 512` 复制成本约束测试。
- **追踪**：FR-DATA-005、FR-STAT-001、6.3

### EC-004 实现 Engine 状态机

- **状态**：已完成（2026-08-18）
- **目标**：编码 Created 到 Stopped 的合法迁移表。
- **前置任务**：EC-003
- **实际文件**：`src/engine/EngineStateMachine.h`、`src/engine/EngineStateMachine.cpp`、`tests/unit/EngineStateMachineTests.cpp`、`tests/unit/CMakeLists.txt`
- **实现要求**：实现单线程、非线程安全状态机；文件错误不进入 Failed；非法迁移返回 `Internal/InvalidState` 且保持原状态。
- **验收检查**：实现 Created、Initializing、Ready、Running、Loading、Failed、ShuttingDown、Stopped 的详细设计迁移表；Loading 结果恢复到进入前的 Ready 或 Running；Stopped 为终态。
- **测试要求**：使用表驱动测试覆盖所有设计迁移、关闭路径、Loading 恢复路径和禁止迁移，并验证稳定触发器底层类型与错误码。
- **验证结果**：MSVC 14.44 / NMake Makefiles Debug 构建成功；`dzc_engine_state_machine` 专项测试通过；完整 CTest 28/28 通过；`git diff --check` 通过。
- **追踪**：5.1、NFR-REL-001

### EC-005 实现 Scene 参数容器

- **状态**：已完成（2026-08-18）
- **目标**：保存当前 Dataset 引用、渲染参数和后端无关帧输入。
- **前置任务**：EC-003
- **实际文件**：`src/scene/Scene.h`、`src/scene/Scene.cpp`、`tests/unit/SceneTests.cpp`、`tests/unit/CMakeLists.txt`
- **实现要求**：采用私有 Scene；使用 `std::optional<DatasetId>` 表示当前 Dataset；Scene 不拥有 GPU 对象；参数更新只在 Engine 单消费者线程执行。
- **接口语义**：`SceneParameters` 包含点大小、着色模式、固定颜色、背景颜色和渲染尺寸；`SceneFrameInput` 按值返回 Dataset 引用和完整参数；非法点大小返回 `Configuration/1` 并保持旧参数不变。
- **验收检查**：点大小、模式、颜色、尺寸和 Dataset 引用可通过一次参数提交及帧输入读取完成切换；Dataset 可设置和清空。
- **测试要求**：默认值、完整参数和 Dataset 字段访问、Dataset 清空、点大小边界、非法点大小失败及失败后旧参数保持测试。
- **验证结果**：MSVC 14.44 / NMake Makefiles Debug 构建成功；`dzc_scene` 专项测试通过；完整 CTest 29/29 通过；`git diff --check` 通过。
- **追踪**：FR-REN-002~004、6.4
### EC-006 实现 Engine 公共类和 Pimpl

- **状态**：未开始
- **目标**：实现 init/enqueueCommand/update/render/resize/getSnapshot/pollEvents/shutdown。
- **前置任务**：EC-004, EC-005, task-system/TS-009
- **预计文件**：`include/dzc/Engine.h`、`src/engine/Engine.cpp`、`tests/unit/EnginePublicApiTests.cpp`
- **实现要求**：公共头最小化；Engine 不继承 QObject/QWidget；私有实现只在 cpp。
- **验收检查**：生命周期调用符合线程和状态规则，重复 shutdown 安全。
- **测试要求**：Fake 后端下的 API 生命周期测试。
- **追踪**：ADR-004、NFR-MAIN-004、4

### EC-007 实现命令消费与事件溢出策略

- **状态**：未开始
- **目标**：接入 CommandCoalescer 和关键事件保留槽。
- **前置任务**：EC-006, task-system/TS-003
- **预计文件**：`src/engine/Engine.cpp`、`src/engine/EngineQueues.cpp`、`tests/unit/EngineQueuePolicyTests.cpp`
- **实现要求**：关键事件队列满时写日志并在下次 poll 优先返回事件丢失错误。
- **验收检查**：参数命令合并、QueueFull、Shutdown 可达均符合设计。
- **测试要求**：小容量队列故障测试。
- **追踪**：DDD-005、6.4

### EC-008 实现原子 Snapshot 发布

- **状态**：未开始
- **目标**：使用 C++17 atomic_load/store shared_ptr release/acquire。
- **前置任务**：EC-006
- **预计文件**：`src/engine/Engine.cpp`、`tests/unit/SnapshotPublicationTests.cpp`
- **实现要求**：发布后对象不可修改；UI 读取无全局共享锁。
- **验收检查**：并发生产/读取无崩溃且 frameId 单调。
- **测试要求**：多线程压力和 ThreadSanitizer 可用环境测试。
- **追踪**：DDD-005、6.3

### EC-009 实现每帧协调骨架

- **状态**：未开始
- **目标**：按详细设计 5.3 固定顺序调用可注入模块。
- **前置任务**：EC-007, EC-008
- **预计文件**：`src/engine/EngineCoordinator.h`、`src/engine/EngineCoordinator.cpp`、`tests/unit/EngineCoordinatorTests.cpp`
- **实现要求**：render 不等待文件解析；模块返回错误按恢复边界转换。
- **验收检查**：Fake 模块调用顺序稳定，失败时后续步骤符合策略。
- **测试要求**：调用序列和错误注入测试。
- **追踪**：5.3、NFR-PERF-003

### EC-010 实现 Dataset 替换和旧结果过滤

- **状态**：未开始
- **目标**：用 DatasetId 和取消源隔离替换任务。
- **前置任务**：EC-009, task-system/TS-006
- **预计文件**：`src/engine/DatasetSession.h`、`src/engine/DatasetSession.cpp`、`tests/unit/DatasetReplacementTests.cpp`
- **实现要求**：新 Dataset 未就绪前不破坏旧有效数据；旧任务结果不得写入新 Scene。
- **验收检查**：替换、失败、取消三种路径结果正确。
- **测试要求**：可控执行器测试乱序完成。
- **追踪**：23.3、NFR-REL-001

### EC-011 实现初始化回滚与关闭编排

- **状态**：未开始
- **目标**：按详细设计 23.1/23.2 协调各模块。
- **前置任务**：EC-010
- **预计文件**：`src/engine/Engine.cpp`、`tests/unit/EngineLifecycleRollbackTests.cpp`
- **实现要求**：每阶段 RAII；关闭顺序可观察；析构兜底且不抛异常。
- **验收检查**：任意初始化阶段失败均无线程或资源泄漏。
- **测试要求**：逐阶段故障注入和关闭顺序测试。
- **追踪**：NFR-REL-002、23

## 6. 模块级验收

- [ ] Engine 公共头无 Qt/PCL/GPU 类型且使用 Pimpl
- [ ] 状态机、队列、快照和替换测试通过
- [ ] Fake 后端可完整运行 update/render 生命周期
- [ ] 初始化失败与 shutdown 无资源泄漏

## 7. 交接记录

### EC-002 与 EC-003（2026-08-18）

- 完成人：Codex
- 关键变更：新增 `include/dzc/EngineEvent.h`、`include/dzc/EngineState.h`、`include/dzc/EngineSnapshot.h`、`tests/unit/EngineEventTests.cpp` 和 `tests/unit/EngineSnapshotTests.cpp`；注册 `dzc_engine_event` 与 `dzc_engine_snapshot` CTest。
- EC-002：严格实现详细设计 6.2 的 `EventSeverity`、`EventContext`、6 个事件值类型及封闭 `EngineEvent` variant；未增加统一上下文/严重级别，未实现队列或消费逻辑。
- EC-003：严格实现详细设计 5.1/6.3 的 `EngineState`、`DatasetState`、数据集/性能/内存摘要和 `EngineSnapshot`；快照保持可复制值类型，验证 `shared_ptr<const EngineSnapshot>` 和 `sizeof(EngineSnapshot) <= 512`。
- 验证结果：MSVC 14.44 / NMake Makefiles 下 Debug 构建成功；新增事件和快照测试 2/2 通过；完整 CTest 27/27 通过；`git diff --check` 通过；任务专用构建目录已清理。
- 后续任务：EC-004 实现 Engine 状态机；Engine Core 模块仍为“进行中”。
- 关联提交：未提交。
### EC-001（2026-08-17）

- 完成人：Codex
- 关键变更：新增 `include/dzc/EngineCommand.h`、`src/engine/EngineCommand.cpp` 和 `tests/unit/EngineCommandTests.cpp`；定义 11 种命令值类型及封闭 `dzc::EngineCommand` variant；实现非空严格 UTF-8 路径校验和点大小有限数/`[1.0F, 64.0F]` 校验；已接入 `dzc_engine_api` 测试目标与 CTest。
- 未解决问题：EC-001 无未解决问题；当时后续任务为 EC-002。Engine Core 模块仍为“进行中”。
- 测试命令与结果：`cmake -G "NMake Makefiles" -S . -B build-ec001 -DDZC_ENABLE_OPENGL=ON -DDZC_ENABLE_VULKAN=OFF -DDZC_ENABLE_CUDA=OFF -DDZC_BUILD_TESTS=ON`；`cmake --build build-ec001 --config Debug`；`ctest --test-dir build-ec001 -C Debug -R '^dzc_engine_command$' --output-on-failure`（1/1 通过）；`ctest --test-dir build-ec001 -C Debug --output-on-failure`（25/25 通过）；`git diff --check`。
- 关联提交：未提交。

## 8. 变更约束

若实现需要改变公共接口、模块依赖、已确认参数或需求行为，必须先更新需求与设计文档，不得在编码任务中自行改变。
