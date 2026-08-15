# Task System 任务清单

> 文件：`docs/tasks/task-system.md`  
> 所属阶段：公共基础  
> 模块状态：进行中（TS-001 至 TS-008 已完成）
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

- [x] **TS-001 实现 CancellationToken/Source**
- [x] **TS-002 实现通用有界队列**
- [x] **TS-003 实现命令合并辅助器**
- [x] **TS-004 实现线程数自动计算**
- [x] **TS-005 实现优先级线程池**
- [x] **TS-006 实现任务完成队列**
- [x] **TS-007 实现 I/O 并发闸门**
- [x] **TS-008 实现高低水位背压器**
- [ ] **TS-009 实现 TaskSystem 安全关闭**

## 5. 子任务说明

### TS-001 实现 CancellationToken/Source

- **状态**：已完成
- **目标**：建立共享取消状态和轻量查询接口。
- **前置任务**：project-foundation/PF-005
- **实现文件**：`src/tasks/Cancellation.h`、`src/tasks/Cancellation.cpp`、`tests/unit/CancellationTests.cpp`
- **最终接口**：`CancellationToken` 支持默认构造与 `isCancellationRequested()` 轮询；`CancellationSource` 不可复制、可移动，并提供 `token()` 和幂等 `requestCancellation()`。
- **实现结果**：Source 与 Token 通过共享状态保持生命周期；状态使用原子布尔值和 acquire/release 内存序。默认 Token 始终未取消；首次取消返回 `true`，后续取消返回 `false`。Source 析构会请求取消，移动赋值会先取消目标原状态，确保其已发出 Token 不会悬空或保持未取消。
- **范围边界**：TS-001 仅提供协作式轮询取消，不包含回调、阻塞等待、任务队列、线程池或跨线程异常传播。
- **验收检查**：多个 Token 可观察同一取消；Source 销毁后存活 Token 安全观察取消；移动构造和移动赋值保持已确认的取消语义。
- **测试结果**：`dzc_cancellation` 覆盖默认 Token、Token 副本、幂等取消、析构取消、移动、并发取消和并发查询；2026-08-15 的 Debug 全量 CTest 为 **16/16 通过**。
- **追踪**：FR-DATA-004、7.2

### TS-002 实现通用有界队列

- **状态**：已完成
- **目标**：实现固定容量、FIFO、多生产者单消费者的通用有界队列。
- **前置任务**：TS-001
- **实现文件**：`src/tasks/BoundedQueue.h`、`tests/unit/BoundedQueueTests.cpp`
- **最终接口**：提供 `BoundedQueue<T>(capacity = 1024)`、`tryPush(T)`、`tryPop()`、`tryPopBatch(maxCount)` 和 `close()`；队列自身不可复制、不可移动。
- **容量与元素**：容量必须大于零，零容量构造抛出 `std::invalid_argument`；元素要求可移动构造，不额外要求可复制。
- **入队/出队**：使用固定容量环形存储；入队和出队均立即返回，不提供阻塞等待；满队列入队失败，空队列出队失败；批量出队按 FIFO 最多返回 `maxCount` 个当前元素。
- **关闭语义**：`close()` 幂等，关闭后拒绝新元素，但保留并允许排空已入队元素；析构函数自动调用 `close()`。
- **并发语义**：所有公开操作由互斥量保护，支持多生产者和单消费者安全访问；条件变量仅用于状态通知，不向调用方暴露阻塞接口。
- **测试结果**：`dzc_bounded_queue` 覆盖默认/自定义容量、零容量异常、FIFO、满/空、批量出队、关闭排空、析构、移动元素、多生产者并发和并发关闭；2026-08-15 的 Debug 全量 CTest 为 **17/17 通过**。
- **追踪**：DDD-005、6.4
### TS-003 实现命令合并辅助器

- **状态**：已完成
- **目标**：为最后值生效的参数命令提供线程安全的队列内合并策略。
- **前置任务**：TS-002
- **实现文件**：`src/tasks/CommandCoalescer.h`、`src/tasks/CommandCoalescer.cpp`、`tests/unit/CommandCoalescerTests.cpp`
- **固定命令集合**：复用 `DatasetId`、`ColorRgba`、`RenderSize`、`ShadingMode` 和 `OptionalFeatureMode`，提供加载、取消、卸载、点大小、着色、固定颜色、背景色、CUDA 模式、重置视图、Resize 和 Shutdown 命令；`SubmitInputCommand` 等待 CA-003 提供公共 `InputEvent` 后接入。
- **公共接口**：`CommandCoalescer(capacity = 1024)`、`push`、`pop`、`popBatch` 和 `close`；对象不可复制/移动，容量为零抛出 `std::invalid_argument`。
- **合并规则**：点大小、着色、固定颜色、CUDA 模式和 Resize 只在最后一个屏障后的当前命令段内合并；同类命令保留首次入队位置并更新为最后值，不增加队列长度。背景色以及加载、取消、卸载、重置、Shutdown 均为屏障，不跨屏障合并。
- **容量与关闭**：队列固定容量、FIFO、立即返回；满队列时已有同类参数命令仍可原位更新，没有可更新目标时返回 `false`；`close()` 拒绝新命令但保留并排空已接受命令，Shutdown 命令本身不自动关闭队列。
- **职责边界**：不校验路径、浮点、颜色或尺寸值域；不新增 InputEvent、动态字符串指标、Qt 或 Fatal 语义。
- **测试结果**：已新增自有 `assert` 测试，覆盖容量、FIFO、批量消费、连续参数、屏障分段、满队列更新、关闭、Shutdown、生产者/消费者并发和并发关闭。2026-08-15 已使用指定 MSVC 14.44 工具链完成 Debug 构建与完整 CTest；完整 CTest **19/19 通过**。
- **追踪**：6.1、6.4
### TS-004 实现线程数自动计算

- **状态**：已完成
- **目标**：实现可注入硬件并发数的确定性线程配置解析，并支持 Phase 1/Phase 2 自动公式和配置覆盖。
- **前置任务**：project-foundation/PF-006
- **实现文件**：`src/tasks/ThreadConfiguration.h`、`src/tasks/ThreadConfiguration.cpp`、`tests/unit/ThreadConfigurationTests.cpp`
- **最终接口**：`ResolvedThreadConfig` 固定包含 `phase1WorkerThreads`、`phase2RecordingThreads` 和 `maxConcurrentIoTasks`；无状态 `ThreadConfiguration::resolve(const dzc::ThreadConfig&, std::uint32_t) noexcept` 返回独立的解析结果。
- **自动计算**：注入的硬件并发数为 0 时按 4 处理；Phase 1 使用 `clamp(H - 1, 2, 8)`，Phase 2 使用 `clamp(H / 2, 2, 8)`，并在计算前完成回退以避免无符号下溢；I/O 并发默认值为 2。
- **配置覆盖**：`ThreadConfig` 的三个非零字段分别覆盖自动/默认结果，所有覆盖统一钳制到 `[1, 8]`；I/O 字段为 0 时保留默认值 2；解析过程不修改输入配置。
- **职责边界**：不创建线程、不读取真实硬件、不使用全局状态；不引入 Qt、图形 API、任务队列、线程池或 Fatal 语义。
- **测试结果**：表驱动测试覆盖 H=0、1、4、16，Phase 1/Phase 2/I/O 覆盖、零 I/O 默认、上限钳制、输入不变与重复确定性解析；2026-08-15 指定 MSVC 14.44 工具链下 Debug 完整 CTest **19/19 通过**。
- **追踪**：DDD-006、7.1
### TS-005 实现优先级线程池

- **状态**：已完成
- **目标**：实现四级 FIFO 任务队列和 worker 生命周期。
- **前置任务**：TS-001, TS-002, TS-004
- **实现文件**：`src/tasks/TaskSystem.h`、`src/tasks/TaskSystem.cpp`、`tests/unit/TaskSystemTests.cpp`；`src/tasks/CMakeLists.txt` 与 `tests/unit/CMakeLists.txt` 已接入 `dzc_tasks` 和 `dzc_task_system` CTest。
- **最终接口**：TS-005 建立的 `TaskSystem` 由 TS-006 扩展为 `TaskSystem(workerThreads, queueCapacity = 1024U, completionQueueCapacity = 1024U)`；`TaskPriority` 固定为 `Critical`、`High`、`Normal`、`Low`。`TaskErrorCode` 的基础错误码为 `InvalidTask`、`NotAccepting`、`QueueFull`、`TaskIdExhausted`、`UnhandledException` 与 `UnknownException`；TS-006 另增加 `Cancelled`。对象不可复制、不可移动。
- **队列与提交**：四个优先级各自具有独立的固定容量 FIFO；worker 始终先取最高非空优先级，同级保持 FIFO。容量和 worker 数为 0 时构造抛出 `std::invalid_argument`；成功提交从 `TaskId{1}` 单调分配且不复用。空函数或非法优先级、停止接收、队列满和 ID 耗尽均返回 `ErrorDomain::Task` 的稳定错误码。
- **取消与异常**：每个接受任务拥有独立内部取消源；任务取得的 Token 将调用方 Token 与内部取消合并，任一取消即为取消。`requestCancelAll()` 只取消调用时已接受的排队/运行任务，后续任务不受影响；已排队任务仍会被协作式调用。worker 在锁外运行任务，捕获标准和未知异常为 `TaskId + Error` 私有记录，异常不会跨线程且后续任务继续执行。
- **停止与汇合**：`stopAccepting()` 幂等地拒绝后续提交，不中断已有任务；`waitForCompletion()` 停止接收、排空已接受任务、通知 worker 退出并汇合，支持多个外部控制线程并发/重复调用。析构函数自动执行该流程；任务函数和 worker 不得调用 `waitForCompletion()`。
- **测试结果**：自有 `assert` 测试覆盖构造校验、TaskId、同级 FIFO、严格优先级、每级容量、提交错误、标准/未知异常、外部和全局取消、取消后新任务、停止排空、重复等待、析构以及多生产者/多 worker 压力。2026-08-15 指定 MSVC 14.44/Qt MSVC 2022 工具链下 Debug 完整 CTest **20/20 通过**。
- **追踪**：7.2、7.4、NFR-REL-001

### TS-006 实现任务完成队列

- **状态**：已完成
- **目标**：让 worker 以值对象向 Engine 单消费者安全报告成功、失败与取消结果。
- **前置任务**：TS-005
- **实现文件**：`src/tasks/TaskCompletion.h`、`src/tasks/TaskCompletionQueue.h`、`src/tasks/TaskCompletionQueue.cpp`、`tests/unit/TaskCompletionTests.cpp`；`src/tasks/TaskSystem.h/.cpp`、`src/tasks/CMakeLists.txt` 与 `tests/unit/CMakeLists.txt` 已同步接入。
- **完成值对象**：`TaskCompletion` 固定包含 `TaskId taskId`、`std::optional<DatasetId> datasetId` 与 `Result<void> result`。无数据集任务保留空 DatasetId；数据集任务由 `submitForDataset()` 提交。Engine 读取后自行将 DatasetId 与当前数据集比较并过滤旧结果，队列不擅自丢弃。
- **队列与读取**：`TaskCompletionQueue(capacity = 1024U)` 是不可复制、不可移动的有界 FIFO；零容量抛出 `std::invalid_argument`。`push()` 在队列满时阻塞等待空间，不静默丢弃完成消息；`tryPop()` 与 `tryPopBatch(maxCount)` 为 Engine 的立即返回式读取接口。`close()` 幂等、唤醒阻塞发布者、拒绝新发布，但仍允许排空已接受消息；析构自动关闭。
- **TaskSystem 集成**：构造函数增加 `completionQueueCapacity = 1024U`，零值构造失败；公开 `tryPopCompletion()`、`tryPopCompletionBatch(maxCount)`。提交回调以 `Result<void>` 表达结果，同时保留 TS-005 的 `void(CancellationToken)` 调用兼容适配。worker 捕获的异常转换为 Task 域失败完成消息；若回调成功返回但组合 Token 在完成时已取消，则发布 `TaskErrorCode::Cancelled`。`waitForCompletion()` 仅在全部任务完成消息已发布后关闭完成队列、退出并汇合 worker。
- **调用约定**：由于完成队列满时 worker 会阻塞，调用方必须在 `waitForCompletion()` 前或并发期间持续消费完成消息；否则 worker 与等待方都会等待可用完成队列空间。这是有意避免静默丢失完成消息的背压语义。
- **验收检查**：成功、业务失败、标准异常、取消结果及可选 DatasetId 均可安全传递；关闭、FIFO 与旧 Dataset 调用方过滤语义明确。
- **测试结果**：`dzc_task_completion` 覆盖零容量、FIFO、批量读取、满队列阻塞、关闭唤醒和排空、成功/失败/异常/取消完成、DatasetId 和旧数据集过滤、完成容量校验。2026-08-15 指定 MSVC 14.44/Qt MSVC 2022 工具链下 Debug 完整 CTest **21/21 通过**。
- **追踪**：5.3、7.4

### TS-007 实现 I/O 并发闸门

- **状态**：已完成
- **目标**：提供不接入具体 Reader 或 TaskSystem 调度的、可取消的 C++17 I/O 许可控制。
- **前置任务**：TS-005
- **实现文件**：`src/tasks/ConcurrencyGate.h`、`src/tasks/ConcurrencyGate.cpp`、`tests/unit/ConcurrencyGateTests.cpp`；`src/tasks/Cancellation.h/.cpp` 增加仅供 Gate 使用的私有取消通知注册；`src/tasks/CMakeLists.txt` 与 `tests/unit/CMakeLists.txt` 已接入 `dzc_tasks` 和 `dzc_concurrency_gate` CTest。
- **最终接口**：`ConcurrencyGate(capacity = 2U)`、`acquire(CancellationToken = {})` 和幂等 `close()`；零容量构造抛出 `std::invalid_argument`。成功获取返回不可复制、可移动的 RAII `Lease`，不提供手动释放、容量或活动数查询。
- **许可与关闭**：Gate 使用共享 Pimpl 状态、互斥量与条件变量保存固定许可数。许可不足时等待；Lease 析构或移动赋值会归还原许可并唤醒等待者。`close()` 拒绝新的获取、唤醒所有等待者但不撤销既有 Lease；Gate 析构自动关闭，晚于 Gate 析构的 Lease 仍通过共享状态安全归还许可。
- **取消唤醒**：已取消 Token、等待期间取消或关闭均使 `acquire()` 返回空 optional；取消和关闭在发放许可前优先检查。Cancellation 保持既有公开轮询接口，仅在私有实现中登记弱引用通知，并对组合 Token 的全部底层状态登记，因此外部或 TaskSystem 内部取消均可及时唤醒等待；等待结束后注册失效，不保留对 acquire 栈帧的访问。
- **范围边界**：TS-007 仅控制并发许可；不自动读取 ThreadConfig、不实现 Reader、TaskSystem 自动接入、高低水位背压或完整 TaskSystem 关闭。
- **测试结果**：`dzc_concurrency_gate` 覆盖默认/自定义容量、零容量、并发峰值、作用域/移动/异常展开释放、默认等待、预取消/等待中取消/组合 Token 取消、关闭语义和 acquire/release/close/cancel 并发压力。2026-08-15 指定 MSVC 14.44/Qt MSVC 2022 工具链下 Debug 完整 CTest **22/22 通过**。
- **追踪**：DDD-006、7.3

### TS-008 实现高低水位背压器

- **状态**：已完成
- **目标**：提供由调用方显式上报当前用量的、可取消的高低水位暂停/恢复控制，不绑定具体 Reader、队列、TaskSystem 或 I/O Gate。
- **实现文件**：`src/tasks/BackpressureController.h`、`src/tasks/BackpressureController.cpp`、`tests/unit/BackpressureTests.cpp`；`src/tasks/Cancellation.h` 新增仅供 Controller 使用的私有通知友元；`src/tasks/CMakeLists.txt` 和 `tests/unit/CMakeLists.txt` 已接入 `dzc_tasks` 与 `dzc_backpressure` CTest。
- **最终接口**：`BackpressureController(capacity, highWatermarkPercent = 80U, lowWatermarkPercent = 60U)`、`updateUsage(usage)`、`waitUntilResumed(CancellationToken = {})` 和幂等 `close()`；容量为 0、高水位为 0 或大于 100、低水位大于等于高水位时构造抛出 `std::invalid_argument`。
- **水位与滞回**：高水位以 `ceil(capacity * highPercent / 100)` 计算，低水位以 `floor(capacity * lowPercent / 100)` 计算；实现通过整百部分和余数部分计算，避免乘法溢出。用量达到或超过高水位进入暂停，降至或低于低水位恢复；两水位之间保持既有状态。超过容量按过载处理并保持/进入暂停。
- **等待、取消与关闭**：未暂停时等待立即成功；暂停时等待低水位恢复。取消和关闭在恢复前优先返回 `false`，并使用 Cancellation 的私有、失效安全弱引用通知唤醒普通与 TaskSystem 组合 Token 的等待者。`close()` 永久拒绝等待成功、唤醒全部等待者；关闭后的 `updateUsage()` 不会重新打开 Controller；析构自动关闭。
- **范围边界**：不提供暂停/容量/水位/用量查询或手动恢复，不接入 Reader、TaskSystem、ConcurrencyGate 或完整关闭流程。
- **测试结果**：`dzc_backpressure` 覆盖默认与自定义百分比、非法参数、向上/向下取整边界、高低水位滞回、超容量、默认/预取消/等待中取消/组合 Token 取消、关闭和 update/wait/cancel/close 并发压力。2026-08-15 指定 MSVC 14.44/Qt MSVC 2022 工具链下 Debug 完整 CTest **23/23 通过**。
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
