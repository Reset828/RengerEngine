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
- [x] **EC-006 实现 Engine 公共类和 Pimpl**
- [x] **EC-007 实现命令消费与事件溢出策略**
- [x] **EC-008 实现原子 Snapshot 发布**
- [x] **EC-009 实现每帧协调骨架**
- [x] **EC-010 实现 Dataset 替换和旧结果过滤**
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

- **状态**：已完成（2026-08-18）
- **目标**：实现 init/enqueueCommand/update/render/resize/getSnapshot/pollEvents/shutdown。
- **前置任务**：EC-004, EC-005, task-system/TS-009
- **实际文件**：`include/dzc/Engine.h`、`include/dzc/FrameInput.h`、`src/engine/Engine.cpp`、`src/engine/CMakeLists.txt`、`tests/unit/EnginePublicApiTests.cpp`、`tests/unit/CMakeLists.txt`
- **实现要求**：公共头最小化；Engine 不继承 QObject/QWidget；私有实现只在 cpp。
- **实现结果**：实现 Pimpl、内部 Fake Render/Compute Backend、基础命令/事件有界队列、互斥量保护的非原子快照读取，以及基于 EngineStateMachine 的最小生命周期骨架。`FrameInput` 为后端无关的空值类型；命令不在本任务消费或合并，事件不实现溢出策略，Snapshot 不实现原子发布，每帧协调留给后续任务。
- **验收检查**：生命周期调用符合状态规则，重复 shutdown 安全；非法业务调用返回 `Internal/InvalidState`；配置队列容量为零返回 `Configuration/1`；命令队列满返回 `Task/QueueFull`。
- **测试要求**：Fake 后端下的 API 生命周期测试。
- **验证结果**：MSVC 14.44 / NMake Makefiles Debug 全量构建成功；`dzc_engine_public_api` 通过；完整 CTest 30/30 通过；`git diff --check` 通过；任务专用 `build-ec006` 已清理。
- **后续任务**：EC-007 实现命令消费与事件溢出策略。
- **追踪**：ADR-004、NFR-MAIN-004、4

### EC-007 实现命令消费与事件溢出策略

- **状态**：已完成（2026-08-19）
- **目标**：接入 CommandCoalescer 和关键事件保留槽。
- **前置任务**：EC-006, task-system/TS-003
- **实际文件**：`src/tasks/CommandCoalescer.h`、`src/engine/Engine.cpp`、`src/engine/EngineQueues.h`、`src/engine/EngineQueues.cpp`、`src/engine/CMakeLists.txt`、`tests/unit/EnginePublicApiTests.cpp`、`tests/unit/EngineQueuePolicyTests.cpp`、`tests/unit/CMakeLists.txt`
- **实现结果**：CommandCoalescer 复用公共 `dzc::EngineCommand` 并保留 `dzc::tasks` 兼容别名。Engine 在 `update()` 中按容量批量消费命令：合并后的点大小、着色、固定色、背景色和尺寸更新 Scene/Snapshot；CUDA 模式仅保存请求；Dataset/视图命令按 FIFO 消费为占位无操作。Shutdown 通过私有原子停止请求保证在命令队列满时仍可提交，并在当前批次后进入正常关闭。
- **事件策略**：私有 EngineEventQueue 使用互斥保护的固定容量 FIFO；同数据集进度原位置合并，满时可替换最旧进度；普通 Message 满时丢弃；关键 Error/Loaded/Cancelled/FeatureDegraded 满时写入 stderr 并保留一个 `RecoverableError` 的 `Task/QueueFull` 事件丢失槽。下一次 `pollEvents()` 优先返回该槽，关闭后仍可排空已接受事件。
- **验收检查**：参数命令合并、QueueFull、Shutdown 可达均符合设计；不实现 EC-008 原子 Snapshot 发布、EC-009 每帧协调、EC-010 数据集生命周期或真实 GPU 行为。
- **测试要求**：小容量命令/事件队列、进度合并、关键事件丢失槽、关闭后排空和 Engine 公共 API 消费行为测试。
- **验证结果**：MSVC 14.44 / NMake Makefiles Debug 全量构建成功；`dzc_engine_queue_policy`、`dzc_engine_public_api` 专项测试通过；完整 CTest 31/31 通过；`git diff --check` 通过；任务专用 `build-ec007` 已清理。
- **后续任务**：EC-008 实现原子 Snapshot 发布。
- **追踪**：DDD-005、6.4
### EC-008 实现原子 Snapshot 发布

- **状态**：已完成（2026-08-19）
- **目标**：使用 C++17 atomic_load/store shared_ptr release/acquire。
- **前置任务**：EC-006
- **实际文件**：`src/engine/Engine.cpp`、`tests/unit/SnapshotPublicationTests.cpp`、`tests/unit/CMakeLists.txt`
- **实现结果**：Snapshot 发布采用 Copy-on-Write：先以 acquire 读取当前快照，构造并完整填充新的 `EngineSnapshot`，转换为 `std::shared_ptr<const EngineSnapshot>` 后通过 `std::atomic_store_explicit(..., std::memory_order_release)` 发布。`getSnapshot()` 通过 `std::atomic_load_explicit(..., std::memory_order_acquire)` 无全局共享锁读取；已发布对象不再修改。
- **验收检查**：默认、初始化、首次更新与关闭快照均非空且状态/FrameId 正确；旧 Snapshot 在新发布后保持不变；单个生产者与多读取线程压力路径无崩溃，每个读取线程观察到的 FrameId 单调不下降。
- **测试要求**：新增 CTest `dzc_snapshot_publication`，覆盖不可变性和多线程读写压力；本机未配置 ThreadSanitizer，因此不伪造其运行结果。
- **验证结果**：MSVC 14.44 / NMake Makefiles Debug 全量构建成功；`dzc_snapshot_publication` 专项测试通过；完整 CTest 32/32 通过；`git diff --check` 通过；任务专用 `build-ec008` 已清理。
- **后续任务**：EC-009 实现每帧协调骨架。
- **追踪**：DDD-005、6.3
### EC-009 实现每帧协调骨架

- **状态**：已完成（2026-08-19）
- **目标**：按详细设计 5.3 固定顺序调用可注入模块。
- **前置任务**：EC-007, EC-008
- **实际文件**：`src/engine/EngineCoordinator.h`、`src/engine/EngineCoordinator.cpp`、`src/engine/Engine.cpp`、`src/engine/CMakeLists.txt`、`tests/unit/EngineCoordinatorTests.cpp`、`tests/unit/EnginePublicApiTests.cpp`、`tests/unit/CMakeLists.txt`
- **实现结果**：新增私有 `EngineCoordinator` 及八个阶段回调：命令消费、任务完成、相机、可见性/LOD、Residency、后端无关帧描述、诊断和 Snapshot 发布。协调器严格按该顺序调用，阶段失败立即原样返回 `Error` 且不执行剩余阶段；阶段返回正常停止时成功结束本帧。未实现模块在 Engine 中配置为显式无操作成功回调。
- **Engine 集成**：`Engine::update()` 只进行既有状态校验并委托协调器；命令阶段复用 CommandCoalescer 批量消费、FIFO 应用及 Shutdown 可达路径。Snapshot 阶段执行首次 `Ready -> Running`、递增 FrameId 并保留 EC-008 的 atomic shared_ptr Copy-on-Write 发布。Shutdown 正常停止协调帧，不执行后续阶段或发布新的帧 Snapshot；`render` 仍为非阻塞 Fake 后端路径。
- **验收检查**：Fake 回调完整成功路径顺序稳定；对每个阶段注入失败时保留原错误、之前阶段仅执行一次、之后阶段不执行；命令阶段正常停止时跳过后续阶段；所有阶段接收同一 `FrameInput` 引用。
- **测试要求**：新增 CTest `dzc_engine_coordinator` 覆盖调用序列、错误注入、短路和正常停止；扩展 `dzc_engine_public_api` 覆盖首次协调帧的 FrameId/Running 语义及 Shutdown 阻止 Snapshot 阶段。
- **验证结果**：MSVC 14.44 / NMake Makefiles Debug 全量构建成功；`dzc_engine_coordinator`、`dzc_engine_public_api` 专项测试通过；完整 CTest 33/33 通过；`git diff --check` 通过；任务专用 `build-ec009` 已清理。
- **后续任务**：EC-010 实现 Dataset 替换和旧结果过滤；Engine Core 模块仍为“进行中”。
- **追踪**：5.3、NFR-PERF-003
### EC-010 实现 Dataset 替换和旧结果过滤

- **状态**：已完成（2026-08-19）
- **目标**：用 DatasetId 和取消源隔离替换任务。
- **前置任务**：EC-009, task-system/TS-006
- **实际文件**：`src/engine/DatasetSession.h`、`src/engine/DatasetSession.cpp`、`src/engine/EngineTestAccess.h`、`src/engine/Engine.cpp`、`src/engine/CMakeLists.txt`、`tests/unit/DatasetReplacementTests.cpp`、`tests/unit/EnginePublicApiTests.cpp`、`tests/unit/CMakeLists.txt`
- **实现结果**：私有 `DatasetSession` 分离当前有效 Dataset 与候选加载 Dataset。每次 Load 分配新的 `DatasetId`；新候选会请求取消旧候选。任务完成仅在结果的 `DatasetId` 与当前候选一致时生效，过期或乱序结果被忽略，绝不写入新的 Scene。
- **替换规则**：候选成功时才替换 Scene 的当前 Dataset 并发布 `DatasetLoadedEvent`；候选失败或取消时保留旧有效 Dataset，分别发布带 Dataset/Task 上下文的可恢复 `ErrorEvent` 或 `DatasetLoadCancelledEvent`。首次加载失败会在 Snapshot 中保留失败摘要。
- **测试要求**：新增 CTest `dzc_dataset_replacement`，通过私有测试接缝注入可控完成结果，覆盖替换、失败、取消、乱序旧结果过滤、首次失败及事件；更新公共 API 测试以反映 Load 后的 `Loading/Opening` 状态。
- **验证结果**：MSVC 14.51.36231 / NMake Makefiles Debug 全量构建成功；`dzc_dataset_replacement` 与 `dzc_engine_public_api` 专项测试通过；完整 CTest 34/34 通过；`git diff --check` 通过；任务专用 `build-ec010` 待本任务验证结束后清理。
- **后续任务**：EC-011 实现初始化回滚与关闭编排；Engine Core 模块仍为“进行中”。
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
### EC-006（2026-08-18）

- 完成人：Codex
- 关键变更：新增公共 `Engine` Pimpl 接口和最小 `FrameInput` 值类型；`Engine::Impl` 仅在 `src/engine/Engine.cpp` 内完整定义，内部拥有状态机、Scene、基础命令/事件有界队列、快照互斥量和私有 Fake Render/Compute Backend。
- 生命周期：`init` 校验队列容量后执行 `Created -> Initializing -> Ready`；第一次成功 `update` 执行 `Ready -> Running` 并递增快照帧号；`render` 使用 Fake 后端无操作成功路径；`resize` 更新 Scene 渲染尺寸；业务调用只允许 Ready/Running/Loading，其他状态返回 `Internal/InvalidState`。
- 边界：命令仅进行值校验和入队，不消费或合并；事件只支持基础 FIFO 排空；快照读取由互斥量保护，未实现 EC-008 原子发布；未实现真实 GPU、Dataset 生命周期、完整帧协调、状态机失败注入或关闭回滚编排。
- CMake：`dzc_engine_core` 已改为静态库并编入 Engine、EngineCommand、状态机和 Scene 实现；既有状态机及 Scene 测试改为链接该核心库，新增 `dzc_engine_public_api`。
- 验证结果：MSVC 14.44 / NMake Makefiles Debug 全量构建成功；`dzc_engine_public_api` 通过；完整 CTest 30/30 通过；`git diff --check` 通过；任务专用构建目录已清理。
- 后续任务：EC-007 实现命令消费与事件溢出策略；Engine Core 模块仍为“进行中”。
- 关联提交：未提交。
