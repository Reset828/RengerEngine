# Integration Testing and Acceptance 任务清单

> 文件：`docs/tasks/integration-testing.md`  
> 所属阶段：公共基础 / Phase 1 / Phase 2 / 最终验收  
> 模块状态：未开始  
> 前置模块：[project-foundation](./project-foundation.md)、[diagnostics](./diagnostics.md)、[task-system](./task-system.md)、[engine-core](./engine-core.md)、[point-cloud-io](./point-cloud-io.md)、[point-cloud-data](./point-cloud-data.md)、[grid-chunking](./grid-chunking.md)、[camera-abstraction](./camera-abstraction.md)、[opengl-renderer](./opengl-renderer.md)、[cuda-opengl-interop](./cuda-opengl-interop.md)、[qt-application](./qt-application.md)、[dzcpc-cache](./dzcpc-cache.md)、[octree-lod](./octree-lod.md)、[vulkan-memory](./vulkan-memory.md)、[vulkan-renderer](./vulkan-renderer.md)、[cuda-vulkan-interop](./cuda-vulkan-interop.md)  
> 输入基线：[需求文档](../requirements/spec.md)、[概要设计](../design/architectureDesign.md)、[详细设计](../design/detailDesign.md)、[项目规范](../../agent.md)

## 1. 模块目标

建立自有测试基础设施，贯通数据、Engine、UI、OpenGL、Vulkan、CUDA、缓存、LOD 和性能报告，并为全部阶段验收项保存可复现证据。

## 2. 范围边界

**包含：** 自有轻量测试框架；Fake/Null 测试替身；需求追踪自动检查；跨模块和图形测试；互操作与故障注入；Phase 1/Phase 2 性能基准；验收报告与证据索引。  
**不包含：** 替代模块内单元测试；伪造缺失硬件测试结果；在基准硬件或 Camera 路径未确认前宣称最终性能通过；引入未经确认的第三方测试框架。

## 3. 完成规则

只有同时满足以下条件，模块才可以在 [progress.md](./progress.md) 中标记完成：

- 本文所有非阻塞必需任务均已勾选；
- 所有自动化测试通过，能力缺失用例只能明确标记为 Skipped，不能伪造通过；
- 对应公共接口和私有实现符合 `agent.md` 的命名、Pimpl、RAII 和依赖边界；
- 相关需求、设计和测试文档已经同步；
- AC-P2-011 及依赖最终 Camera 运动路径/基准环境的结论，在用户确认这些输入前必须保持阻塞。

## 4. 子任务 Checklist

- [ ] **IT-001 实现自有轻量测试框架**
- [ ] **IT-002 实现跨模块测试替身与确定性执行器**
- [ ] **IT-003 实现文档追踪与任务一致性检查**
- [ ] **IT-004 贯通 PCD/PLY 到 Phase 1 Chunk 集成**
- [ ] **IT-005 贯通 Engine 命令、事件与快照集成**
- [ ] **IT-006 执行 Phase 1 OpenGL 图形验收**
- [ ] **IT-007 执行 Phase 1 Qt 应用验收**
- [ ] **IT-008 执行 CUDA-OpenGL 互操作验收**
- [ ] **IT-009 贯通 .dzcpc、LOD 与流式驻留集成**
- [ ] **IT-010 执行 Vulkan 后端图形验收**
- [ ] **IT-011 执行低显存预算与淘汰故障测试**
- [ ] **IT-012 执行 CUDA-Vulkan 互操作验收**
- [ ] **IT-013 建立 Phase 1 千万点性能场景**
- [ ] **IT-014 建立 Phase 2 亿级点云性能场景** —— **阻塞：等待用户提供 Camera 参考源码、正式基准硬件、数据集、低帧率百分位阈值和持续运动路径。**
- [ ] **IT-015 生成 Phase 1 验收证据索引**
- [ ] **IT-016 生成 Phase 2 验收证据索引** —— **阻塞：AC-P2-011 等待用户确认 Camera 性能路径与正式基准环境。**

## 5. 子任务说明

### IT-001 实现自有轻量测试框架

- **状态**：未开始
- **目标**：提供注册、断言、临时目录、能力标签、文本/JUnit 输出和正确退出码。
- **前置任务**：PF-003
- **预计文件**：`tests/framework/TestRegistry.h`、`tests/framework/TestRegistry.cpp`、`tests/framework/Assertions.h`、`tests/framework/TestMain.cpp`、`tests/framework/TestReport.cpp`
- **实现要求**：不得引入 GoogleTest；失败数决定进程退出码；Skipped 必须记录能力缺失原因；随机测试记录种子。
- **验收检查**：pass/fail/skip 三类示例输出正确；失败进程非零；JUnit 可被 CI 解析。
- **测试要求**：用框架自测注册、断言、异常、临时目录、标签和退出码。
- **追踪**：NFR-TEST-002、NFR-MAIN-006、25.1

### IT-002 实现跨模块测试替身与确定性执行器

- **状态**：未开始
- **目标**：提供 FakeRenderBackend、NullRenderBackend、FakeComputeBackend、内存 Reader/Log Sink 和单线程执行器。
- **前置任务**：IT-001, EC-007, TS-001
- **预计文件**：`tests/fakes/FakeRenderBackend.h`、`tests/fakes/FakeComputeBackend.h`、`tests/fakes/InMemoryPointCloudReader.h`、`tests/fakes/DeterministicExecutor.h`、`tests/fakes/InMemoryLogSink.h`
- **实现要求**：替身记录调用和故障注入点，不包含 Qt/PCL/GPU 类型；确定性执行器显式推进任务。
- **验收检查**：Engine 测试无需图形设备即可控制加载、取消、错误和关闭时序。
- **测试要求**：为每个替身添加行为自测和故障注入测试。
- **追踪**：NFR-TEST-002、25.1

### IT-003 实现文档追踪与任务一致性检查

- **状态**：未开始
- **目标**：自动检查 FR/NFR、AC、模块链接、任务 ID 和 Markdown 基本结构。
- **前置任务**：IT-001
- **预计文件**：`tests/documentation/validate_docs.js`、`tests/documentation/CMakeLists.txt`
- **实现要求**：验证 68 个 FR/NFR 和 29 个 AC 在任务/验收文档有追踪；任务 ID 唯一；依赖模块存在；UTF-8 无 BOM；代码围栏成对。
- **验收检查**：对当前文档运行通过；删除任一追踪 ID、制造重复 ID 或坏链接时失败。
- **测试要求**：建立正向检查和临时副本故障注入测试。
- **追踪**：NFR-MAIN-005、NFR-TEST-001、26

### IT-004 贯通 PCD/PLY 到 Phase 1 Chunk 集成

- **状态**：未开始
- **目标**：验证 Reader→坐标局部化→GridChunkBuilder 完整数据管线。
- **前置任务**：IO-009, GC-009, IT-001
- **预计文件**：`tests/integration/PointCloudToChunkTests.cpp`、`tests/data/README.md`
- **实现要求**：覆盖 XYZ、RGB/RGBA、intensity、缺字段、NaN、损坏文件、大坐标和取消；小型测试数据可程序生成。
- **验收检查**：输出统计、属性模式、Chunk 点数/包围盒和错误符合预期；PCL 不泄漏到核心 Target。
- **测试要求**：运行格式矩阵、损坏输入、取消和大坐标精度测试。
- **追踪**：FR-DATA-001 至 FR-DATA-006、FR-VIS-001、AC-P1-003/004、25.3

### IT-005 贯通 Engine 命令、事件与快照集成

- **状态**：未开始
- **目标**：使用测试替身验证加载、进度、取消、替换、卸载和幂等关闭。
- **前置任务**：EC-011, IT-002, IO-009
- **预计文件**：`tests/integration/EngineWorkflowTests.cpp`
- **实现要求**：显式推进异步步骤；检查关键事件不被丢弃、Snapshot 单调更新、旧 Dataset 延迟释放和错误隔离。
- **验收检查**：全部合法工作流和故障路径可重复；无死锁、残留任务或异常越界。
- **测试要求**：覆盖加载成功/失败/取消/替换/卸载/重复 shutdown。
- **追踪**：FR-DATA-003/004/005、FR-UI-004、NFR-REL-001/002、25.3

### IT-006 执行 Phase 1 OpenGL 图形验收

- **状态**：未开始
- **目标**：在真实 OpenGL 4.5 上验证 Shader、属性布局、四种着色、分块剔除、resize 和资源释放。
- **前置任务**：GL-012, IT-004
- **预计文件**：`tests/graphics/OpenGlPhase1AcceptanceTests.cpp`、`tests/golden/opengl/README.md`
- **实现要求**：能力不足明确 Skipped；优先检查像素/统计而非脆弱全图逐像素匹配；记录 GPU/驱动/上下文版本。
- **验收检查**：基础点云可见；XYZ/RGB/intensity/height 四模式生效；resize 后继续渲染；visible point 统计正确。
- **测试要求**：运行窗口化图形测试并保存日志和必要截图/哈希。
- **追踪**：FR-REN-001、FR-REN-002、FR-REN-003、FR-REN-004、FR-REN-005、FR-GL-001/002/003、FR-VIS-002、FR-VIS-003、AC-P1-002/006/007/008

### IT-007 执行 Phase 1 Qt 应用验收

- **状态**：未开始
- **目标**：验证主窗口、文件选择、参数控制、加载反馈、取消、状态和日志显示。
- **前置任务**：QT-012, IT-005, IT-006
- **预计文件**：`tests/ui/QtPhase1WorkflowTests.cpp`
- **实现要求**：使用可注入 Engine/Fake 文件选择边界；不得依赖未确认 Camera 键位；耗时操作不得阻塞 UI 线程。
- **验收检查**：加载期间 UI 可响应并可取消；点大小/着色/背景命令生效；状态栏和日志可见。
- **测试要求**：Qt 事件循环自动测试和人工冒烟清单。
- **追踪**：FR-DATA-002/003/004、FR-UI-001、FR-UI-002、FR-UI-003、FR-UI-004、FR-UI-005、NFR-PERF-003、AC-P1-009/010/013/014

### IT-008 执行 CUDA-OpenGL 互操作验收

- **状态**：未开始
- **目标**：对比 CUDA-GL 结果与 CPU 参考并验证零 CPU 回读再上传。
- **前置任务**：CG-008, IT-006
- **预计文件**：`tests/interop/CudaOpenGlAcceptanceTests.cpp`
- **实现要求**：覆盖 auto/on/off；能力不足报告 Skipped；收集 map/unmap、kernel 和 CPU 拷贝计数。
- **验收检查**：支持环境结果正确且 CPU 回读再上传为零；不支持环境 auto 降级、on 失败、off 不初始化。
- **测试要求**：运行多轮处理、降级和关闭测试。
- **追踪**：FR-CUDA-001 至 FR-CUDA-004、FR-GL-004、AC-P1-011/012、25.3

### IT-009 贯通 .dzcpc、LOD 与流式驻留集成

- **状态**：未开始
- **目标**：验证缓存写入/重开、八叉树 LOD 选择、渐进加载和 CPU/GPU 驻留。
- **前置任务**：DC-012, OL-014, IT-004
- **预计文件**：`tests/integration/DzcpcLodStreamingTests.cpp`
- **实现要求**：覆盖原子发布、随机 Chunk 校验、CRC 错误重建、1.5/2.0 px 滞回、祖先保留、取消和低预算。
- **验收检查**：缓存重开数据一致；先显示祖先再细化；视角往返无阈值抖动；预算不超限。
- **测试要求**：确定性输入运行构建、损坏恢复、相机快照切换和预算测试。
- **追踪**：FR-LOD-001 至 FR-LOD-005、AC-P2-008/009/010、25.3

### IT-010 执行 Vulkan 后端图形验收

- **状态**：未开始
- **目标**：验证独立启动、两飞行帧、传统 Render Pass、多线程 Secondary CB、Pipeline Cache 和 Swapchain 重建。
- **前置任务**：VK-018, IT-009
- **预计文件**：`tests/graphics/VulkanPhase2AcceptanceTests.cpp`
- **实现要求**：启用 Validation Layer 时，项目代码导致的 error 消息即失败；记录 worker 数、secondary 数、cache hit 和 device idle 计数。
- **验收检查**：Vulkan-only 启动并渲染；worker/secondary 证据存在；反复 resize 可恢复；二次启动复用缓存；正常路径 device idle 计数为零。
- **测试要求**：运行 Vulkan-only 构建和窗口化生命周期测试。
- **追踪**：FR-VK-001 至 FR-VK-008、AC-P2-001/002/003/004/005/006/007/013/014、25.3

### IT-011 执行低显存预算与淘汰故障测试

- **状态**：未开始
- **目标**：在人工小预算下验证分配、上传背压、LOD 降级、延迟释放和祖先保留。
- **前置任务**：VM-012, OL-014, IT-010
- **预计文件**：`tests/integration/LowMemoryBudgetTests.cpp`
- **实现要求**：不得依赖真实耗尽整机显存；通过预算注入触发路径；检测预算超限、无限等待和在途资源提前释放。
- **验收检查**：系统在低预算下保持运行，暂停/延迟上传并选择较粗 LOD；预算恢复后可继续细化。
- **测试要求**：多种预算、上传并发和视角切换压力测试。
- **追踪**：FR-VK-006/007/008、FR-LOD-004/005、NFR-REL-001/002、AC-P2-008

### IT-012 执行 CUDA-Vulkan 互操作验收

- **状态**：未开始
- **目标**：验证外部内存、双向信号量、同 GPU 匹配、结果正确和零拷贝。
- **前置任务**：CV-012, IT-010
- **预计文件**：`tests/interop/CudaVulkanAcceptanceTests.cpp`
- **实现要求**：记录 UUID/LUID 匹配证据、信号量轮次和 CPU 拷贝计数；错误测试覆盖错配 GPU、导入失败和关闭顺序。
- **验收检查**：兼容环境完整轮次通过且 Validation/CUDA 错误为零；auto/on/off 行为正确；不兼容环境明确 Skipped/失败。
- **测试要求**：运行连续多帧、错配、故障注入、淘汰和 shutdown。
- **追踪**：FR-VKCUDA-001/002/003、FR-CUDA-003/004、AC-P2-012/013、25.3

### IT-013 建立 Phase 1 千万点性能场景

- **状态**：未开始
- **目标**：生成或登记不少于 10,000,000 点的数据集并执行预热、固定采集和报告。
- **前置任务**：DG-008, IT-006, IT-007
- **预计文件**：`tests/performance/Phase1Benchmark.cpp`、`tests/performance/BenchmarkDatasetManifest.md`、`tests/performance/Phase1Scenario.md`
- **实现要求**：记录 CPU/GPU/驱动/分辨率/数据集/构建配置；只报告已测指标，不虚构未确认阈值；场景不依赖未确认 Camera 键位。
- **验收检查**：测试完整加载并持续渲染千万点；CSV 原始帧和 Markdown 摘要可复现。
- **测试要求**：执行报告链路测试；正式硬件运行结果单独归档。
- **追踪**：NFR-PERF-001/003/004、NFR-TEST-001、AC-P1-005、25.4

### IT-014 建立 Phase 2 亿级点云性能场景

- **状态**：阻塞/未开始
- **目标**：生成或登记不少于 100,000,000 点数据，验证缓存、LOD、流式、Vulkan 渲染和报告链。
- **前置任务**：IT-009, IT-010, IT-011, DG-008
- **预计文件**：`tests/performance/Phase2Benchmark.cpp`、`tests/performance/BenchmarkDatasetManifest.md`、`tests/performance/Phase2Scenario.md`
- **实现要求**：1920×1080；预热后固定采集；记录平均 FPS、CPU/GPU 时间、可见/驻留点、内存、上传和录制统计；正式持续运动路径等待 Camera 源码。
- **验收检查**：亿级数据可渐进显示并完成报告；在未确认运动路径前不得勾选 AC-P2-011 最终通过。
- **测试要求**：先运行静态/人工输入报告链；基准输入确认后执行正式持续运动测试。
- **追踪**：NFR-PERF-001/002/004、NFR-TEST-001、AC-P2-010/011/015、25.4

### IT-015 生成 Phase 1 验收证据索引

- **状态**：未开始
- **目标**：汇总 AC-P1-001 至 AC-P1-014 的测试命令、环境、结果和产物链接。
- **前置任务**：PF-008, IT-004 至 IT-008, IT-013
- **预计文件**：`docs/testing/phase1-acceptance.md`
- **实现要求**：每项必须为 Passed/Failed/Skipped/Blocked 之一；Skipped/Blocked 写明原因；不得用模块完成替代测试证据。
- **验收检查**：14 个 Phase 1 验收 ID 全部有唯一条目和可访问证据；相机具体交互边界按待定状态说明。
- **测试要求**：运行文档追踪检查并抽查命令可执行。
- **追踪**：AC-P1-001 至 AC-P1-014、25.5

### IT-016 生成 Phase 2 验收证据索引

- **状态**：阻塞/未开始
- **目标**：汇总 AC-P2-001 至 AC-P2-015 的测试、性能和能力证据。
- **前置任务**：IT-009 至 IT-014
- **预计文件**：`docs/testing/phase2-acceptance.md`
- **实现要求**：每项显式状态；AC-P2-011 在 Camera 路径和基准环境确认前保持 Blocked；CUDA 不支持环境不能伪造 AC-P2-012 通过。
- **验收检查**：15 个 Phase 2 验收 ID 全部有唯一条目；Passed 项均链接原始日志/CSV/JUnit/配置。
- **测试要求**：运行追踪检查和报告文件完整性检查。
- **追踪**：AC-P2-001 至 AC-P2-015、25.6

## 6. 模块级验收

- [ ] 自有测试框架能区分 Passed、Failed、Skipped 和 Blocked，失败退出码正确
- [ ] 68 个 FR/NFR 与 29 个阶段验收项均有自动追踪
- [ ] Phase 1 数据、Engine、OpenGL、Qt 与可选 CUDA-GL 集成链有可复现证据
- [ ] Phase 2 缓存、LOD、显存、Vulkan 与可选 CUDA-Vulkan 集成链有可复现证据
- [ ] 性能测试输出原始 CSV 和 Markdown 摘要，且不对未确认基准或 Camera 路径作最终结论

## 7. 交接记录

- 完成日期：
- 完成人：
- 关键变更：
- 未解决问题：
- 测试命令与结果：
- 关联提交：

## 8. 变更约束

若实现需要改变公共接口、模块依赖、已确认参数或需求行为，必须先更新需求与设计文档，不得在编码任务中自行改变。
