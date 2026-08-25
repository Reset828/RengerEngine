# Dzc-RenderEngine 任务总体进度

> 文件：`docs/tasks/progress.md`  
> 输入基线：[需求文档](../requirements/spec.md)、[概要设计](../design/architectureDesign.md)、[详细设计](../design/detailDesign.md)、[项目规范](../../agent.md)  
> 模块总数：18  
> 当前完成：1  

## 1. 使用说明

- 本文只跟踪**模块级**状态；每个模块的最小任务和验收检查在对应模块文档中维护。
- 模块只有在其文档的完成规则、全部必需子任务和模块级验收均满足后，才可在此勾选。
- 可选硬件能力缺失时，相关测试必须记录为 `Skipped` 及原因，不能伪造通过；显式 `on` 模式要求的能力缺失应按设计失败。
- 状态文字使用：`未开始`、`进行中`、`阻塞`、`完成`。Checklist 只有“完成”时才勾选。
- 实施必须遵循先完成 Phase 1 OpenGL 验证，再进入 Phase 2 Vulkan 核心迁移；公共基础中不依赖后端的工作可以先行。
- 任何需要改变公共接口、模块依赖、已确认格式/参数或需求行为的实现，必须先更新需求与设计文档并取得确认。

## 2. 模块完成定义

一个模块标记完成前，必须同时满足：

- 对应模块文档中所有非阻塞必需任务已勾选；
- 模块级验收 Checklist 已全部满足；
- 单元、集成、图形或能力测试按模块要求执行并保存结果；
- 测试环境不具备某可选能力时，结果明确为 `Skipped`，并保留原因；
- 公共头不泄漏 Qt、PCL、OpenGL、Vulkan 或 CUDA 类型/句柄；
- Engine 不继承 `QObject` 或 `QWidget`，PCL 仅用于 I/O，GLM 是唯一数学库；
- C++ 资源采用 RAII，命名和依赖方向符合 `agent.md`；
- 需求、设计、测试和交接记录已同步。

## 3. 推荐执行顺序

`project-foundation` → `diagnostics` / `task-system` → `engine-core` / `point-cloud-data` / `camera-abstraction（CA-007）` → Phase 1 数据与 OpenGL/Qt → Phase 1 验收 → Phase 2 缓存/LOD/显存 → Vulkan/CUDA-Vulkan → Phase 2 验收 → 打包与最终验收。

> Camera 的透视轨道交互、输入、reset 和裁剪规则已由 CA-005 确认，并由 CA-006 `OrbitCameraController` 实现；CA-007 已提供确定性的内存事件回放。深度约定固定为 OpenGL `[-1,1]`；Engine/Qt 集成与正式 Renderer 性能验收仍必须按后续任务实施。

## 4. 公共基础

- [x] [Project Foundation](./project-foundation.md) — 状态：完成
- [x] [Diagnostics](./diagnostics.md) — 状态：完成（DG-001 至 DG-008 已完成）
- [ ] [Task System](./task-system.md) — 状态：进行中（TS-001 至 TS-009 已完成）
- [x] [Engine Core](./engine-core.md) — 状态：完成（EC-001 至 EC-011 已完成）
- [x] [Point Cloud Data](./point-cloud-data.md) — 状态：完成（PD-001 至 PD-008 已完成）
- [ ] [Camera Abstraction](./camera-abstraction.md) — 状态：进行中（CA-001 至 CA-007 已完成；Engine/Qt 集成和正式性能验收未完成）

## 5. Phase 1：OpenGL 基础渲染

- [ ] [Point Cloud I/O](./point-cloud-io.md) — 状态：进行中（IO-001 至 IO-009 已完成；模块级验收未完成）
- [ ] [Grid Chunking](./grid-chunking.md) — 状态：进行中（GC-001 至 GC-008 已完成；GC-009 未完成）
- [ ] [OpenGL Renderer](./opengl-renderer.md) — 状态：未开始
- [ ] [CUDA-OpenGL Interop](./cuda-opengl-interop.md) — 状态：未开始（可选能力）
- [ ] [Qt Application](./qt-application.md) — 状态：未开始
- [ ] [Integration Testing and Acceptance](./integration-testing.md) 的 Phase 1 任务 — 状态：未开始

### 5.1 Phase 1 阶段门禁

- [ ] AC-P1-001 构建
- [ ] AC-P1-002 OpenGL 4.5
- [ ] AC-P1-003 PCD/PLY
- [ ] AC-P1-004 错误处理
- [ ] AC-P1-005 千万点
- [ ] AC-P1-006 基础渲染
- [ ] AC-P1-007 四种着色
- [ ] AC-P1-008 分块剔除
- [ ] AC-P1-009 UI 响应与取消
- [ ] AC-P1-010 状态统计
- [ ] AC-P1-011 CUDA 降级
- [ ] AC-P1-012 CUDA-OpenGL（兼容环境验证）
- [ ] AC-P1-013 架构边界
- [ ] AC-P1-014 相机待定边界

## 6. Phase 2：Vulkan 高性能迁移

- [ ] [Dzcpc Cache](./dzcpc-cache.md) — 状态：未开始
- [ ] [Octree LOD](./octree-lod.md) — 状态：未开始
- [ ] [Vulkan Memory](./vulkan-memory.md) — 状态：未开始
- [ ] [Vulkan Renderer](./vulkan-renderer.md) — 状态：未开始
- [ ] [CUDA-Vulkan Interop](./cuda-vulkan-interop.md) — 状态：未开始（可选能力）
- [ ] [Integration Testing and Acceptance](./integration-testing.md) 的 Phase 2 任务 — 状态：未开始

### 6.1 Phase 2 阶段门禁

- [ ] AC-P2-001 Vulkan 独立启动
- [ ] AC-P2-002 公共功能等价
- [ ] AC-P2-003 Swapchain
- [ ] AC-P2-004 多线程录制
- [ ] AC-P2-005 Secondary Command Buffer
- [ ] AC-P2-006 Pipeline Cache
- [ ] AC-P2-007 异步调度
- [ ] AC-P2-008 显存预算
- [ ] AC-P2-009 LOD
- [ ] AC-P2-010 亿级数据
- [ ] AC-P2-011 平均 FPS — **阻塞：等待 Camera 性能路径和正式基准环境**
- [ ] AC-P2-012 CUDA-Vulkan（兼容环境验证）
- [ ] AC-P2-013 同步正确
- [ ] AC-P2-014 后端隔离
- [ ] AC-P2-015 性能报告

## 7. 最终验收

- [ ] [Integration Testing and Acceptance](./integration-testing.md) 全部非阻塞任务完成，Phase 1/Phase 2 验收证据索引完整
- [ ] [Packaging and Release](./packaging.md) 完成 Windows 10/11 Release 打包与安装目录冒烟
- [ ] [Camera Abstraction](./camera-abstraction.md) 具体控制器和重置/性能路径任务完成
- [ ] 68 个 FR/NFR 全部具有任务和测试追踪
- [ ] 29 个 AC 全部具有 Passed、Failed、Skipped 或 Blocked 的真实证据
- [ ] OpenGL-only 和 Vulkan-only 构建、启动、加载、渲染、resize、关闭通过
- [ ] CUDA auto/on/off 在 CUDA-GL 和 CUDA-Vulkan 路径符合降级规则
- [ ] Phase 1 千万点报告归档
- [ ] Phase 2 亿级点云报告归档
- [ ] 基准环境确认后完成 1920×1080 持续相机运动平均不低于 30 FPS 的正式验收
- [ ] 发布清单、许可证、默认配置、已知限制和测试产物完整

## 8. 当前阻塞事项

| 编号 | 阻塞事项 | 影响范围 | 解除条件 |
| 2026-08-24 | GC-006 完成 | 已实现无状态 `GridCellSplitter`：对单个 `GridBucket` 执行固定目标 262144、最大 524288 的确定性超限拆分；按 `ceil(pointCount / 262144)` 生成子桶，使用最长轴递归空间稳定秩、`x → y → z` 平局规则和 source index 并列规则，子桶内部保留源序号顺序并保持属性流对齐。非法输入返回 `DataFormat/2`，容量不足返回 `Resource/1`，取消返回 `Task/7`。构建 `dzc_grid_cell_splitter_tests` 成功，GC-006 专项测试 `1/1` 通过；已临时复制 355 个运行时 DLL 尝试完整 CTest，测试结束后已全部删除；由于该构建目录未生成其他测试可执行文件且全量构建在既有 `engine_core/Engine.cpp` 阶段失败，完整 CTest 未完成，不能记录为全绿；Grid Chunking 模块继续进行中，GC-007 至 GC-009 未完成。
| 2026-08-24 | GC-005 完成 | 已实现无状态 `GridRunMerger`：统一接收内存桶快照或 `GridRunFile::read()` 结果，按 `GridCellKey` 字典序输出，并按全局 `sourceIndices` 升序合并同 Cell 点及属性流；严格校验 schema、流长度、有限坐标、输入顺序和重复源序号，错误分别返回 `DataFormat/2`、`Resource/1` 或取消 `Task/7`。构建 `dzc_grid_run_merger_tests` 和 GC-005 专项测试通过；Grid Chunking 模块继续进行中，GC-006 至 GC-009 未完成。 |
|---|---|---|---|
| BLK-002 | 正式 Camera 性能采集未完成 | IT-014、IT-016、AC-P2-011 | 确认基准硬件、正式数据集并接入 Renderer 后执行采集 |
| BLK-003 | 正式基准硬件/软件环境未确定 | Phase 1/2 正式性能结论 | 确认 CPU、GPU、显存、内存、存储、Windows、驱动和 CUDA 版本 |
| BLK-004 | 正式性能数据集未确定 | Phase 1/2 性能可比性 | 确认千万点和亿级正式数据集及身份信息 |
| BLK-005 | 低帧率百分位阈值未确定 | Phase 2 性能报告的低帧率判定 | 用户确认百分位类型和门槛；此前只报告原始数据和已确认平均 FPS |

## 9. 进度统计

| 阶段 | 唯一模块数 | 完成 | 进行中 | 阻塞 | 未开始 |
|---|---:|---:|---:|---:|---:|
| 公共基础 | 6 | 4 | 2 | 0 | 0 |
| Phase 1 专属 | 5 | 0 | 0 | 0 | 5 |
| Phase 2 专属 | 5 | 0 | 0 | 0 | 5 |
| 跨阶段/最终验收 | 2 | 0 | 0 | 0 | 2 |
| **合计** | **18** | **4** | **2** | **0** | **12** |

> 统计规则：跨阶段模块 `integration-testing` 和 `packaging` 只在“跨阶段/最终验收”计一次；Camera 为进行中，CA-001 至 CA-007 已完成，Engine/Qt 集成和正式性能验收仍待完成。

## 10. 变更记录

| 日期 | 变更 | 说明 |
| 2026-08-24 | GC-007 完成 | 已实现无状态 `GridChunkBuilder`：接收 Cell 分组和稳定子块顺序，校验 schema、属性流、有限坐标、source index、重复 Cell 与跨子块重复 source index；按 CellKey 字典序输出 `CpuResident` Chunk，计算 bounds/origin 和 float 局部坐标，使用 little-endian FNV-1a 64 位生成确定性 ChunkId 并检测碰撞。构建 `dzc_grid_chunk_builder_tests` 成功，GC-007 专项测试 `1/1` 通过；Grid Chunking 模块继续进行中，GC-008 至 GC-009 未完成。 |
| 2026-08-25 | GC-008 完成 | 已实现无状态 `FrustumCulling`：对有限 `Bounds3d` 执行六平面 AABB `Inside`/`Intersecting`/`Outside` 三态分类，使用 positive/negative vertex 和逐项 checked 浮点乘加；支持上一帧 separating plane 提示并返回当前 Outside 平面。非法 bounds、平面、索引或非有限中间结果返回 `DataFormat/2`。新增 `dzc_frustum_culling_tests`；由于当前环境缺少可用 C++ 编译器，GC-008 专项构建、专项 CTest 和完整 CTest 未执行，不能记为通过；`git diff --check` 已执行。Grid Chunking 模块继续进行中，GC-009 未完成。
| 2026-08-24 | GC-006 完成 | 已实现无状态 `GridCellSplitter`：对单个 `GridBucket` 执行固定目标 262144、最大 524288 的确定性超限拆分；按 `ceil(pointCount / 262144)` 生成子桶，使用最长轴递归空间稳定秩、`x → y → z` 平局规则和 source index 并列规则，子桶内部保留源序号顺序并保持属性流对齐。非法输入返回 `DataFormat/2`，容量不足返回 `Resource/1`，取消返回 `Task/7`。构建 `dzc_grid_cell_splitter_tests` 成功，GC-006 专项测试 `1/1` 通过；已临时复制 355 个运行时 DLL 尝试完整 CTest，测试结束后已全部删除；由于该构建目录未生成其他测试可执行文件且全量构建在既有 `engine_core/Engine.cpp` 阶段失败，完整 CTest 未完成，不能记录为全绿；Grid Chunking 模块继续进行中，GC-007 至 GC-009 未完成。
| 2026-08-24 | GC-005 完成 | 已实现无状态 `GridRunMerger`：统一接收内存桶快照或 `GridRunFile::read()` 结果，按 `GridCellKey` 字典序输出，并按全局 `sourceIndices` 升序合并同 Cell 点及属性流；严格校验 schema、流长度、有限坐标、输入顺序和重复源序号，错误分别返回 `DataFormat/2`、`Resource/1` 或取消 `Task/7`。构建 `dzc_grid_run_merger_tests` 成功，GC-005 专项测试 `1/1` 通过；完整 CTest 受既有 Windows PCL/VTK/OpenNI2 DLL 装载问题影响，未记为全绿；Grid Chunking 模块继续进行中，GC-006 至 GC-009 未完成。
| 2026-08-24 | GC-004 完成 | 已实现 RAII `GridRunFile` 临时 run：保存完整 `GridBucket`、属性流和稳定 sourceIndices，使用 magic/version/结束标记的内部二进制格式；支持原子完成、确定性读回、取消检查以及失败/损坏/未完成文件自动清理。构建 `dzc_grid_run_file_tests` 成功，GC-004 专项测试 `1/1` 通过；临时复制 176 个 Debug PCL/VTK/OpenNI2 DLL 后完整 CTest `61/61` 通过，测试结束后已删除全部临时 DLL；Grid Chunking 模块继续进行中，GC-005 至 GC-009 未完成。 |
| 2026-08-23 | GC-003 完成 | 已实现配置校验创建和内存 `GridBucketStore`：按 checked `GridCellKey` 聚合多批次 `PointBatch`，固定首个非空 schema，保留从 0 开始且拒绝批次不消耗的稳定源序号；快照按 `x → y → z` 字典序返回独立副本。按 positions、颜色、强度和 sourceIndices 的逻辑载荷执行 checked CPU 字节预算，非法数据返回 `DataFormat/2`，预算不足或零预算返回 `Resource/1`。构建 `dzc_grid_bucket_store_tests` 成功，GC-003 专项测试 `1/1` 通过；临时复制 355 个 PCL/VTK/OpenNI2/vcpkg DLL 后完整 CTest `60/60` 通过，测试结束后已全部删除。Grid Chunking 模块继续进行中，GC-004 至 GC-009 未完成。 |
|---|---|---|

| 2026-08-22 | IO-009 完成 | 已为公共 Reader 增加以“已消费源点数”为单位的 PointCloudReadProgress；Pcd/PLY 成功打开后报告 0/声明总量，首次完整读体转换后推进到源总量。PointCloudLoadTask 在 worker 使用公共 EngineEvent 交付打开/读取阶段、已知总量进度、Loaded、Cancelled 和 RecoverableError，并验证进度不倒退、总量稳定、EOF 达总量；未知总量不伪造百分比，异常稳定映射 Internal/1，错误事件不会递归重试。干净构建、专项、非 PCL 聚合 CTest 53/53 和 PCL 边界扫描通过；完整 CTest 53/57，4 个既有 PCL CTest 仍为 Windows 0xc0000135，而显式 DLL PATH 下逐个可执行文件通过。未接入 Engine、DatasetSession、UI、Factory/Registry 或 Dataset 写入；模块级验收仍未完成。 |
| 2026-08-22 | IO-008 完成 | 已新增无 PCL 的 PointCloudLoadTask：Reader 独占交由 TaskSystem::submitForDataset() 后台执行，Gate 仅覆盖 open/readNext，背压等待发生在每次读取前；回调在 worker 交付元数据与已验证批次，取消、流控关闭和业务错误保持既有 Result 语义。IO-009、Engine/Factory/Dataset 写入和模块级验收仍未完成；干净构建与专项回归通过，完整聚合 CTest 仍有四项既有 PCL 0xc0000135 DLL 装载失败。 |
| 2026-08-21 | CA-007 完成 | 已新增仅测试/性能路径验证使用的内存 CameraPath/CameraPathReplayer：从默认 OrbitCameraController 通过 InputEvent 回放，按绝对时间戳生成 delta 并逐步采样状态/矩阵；确定性 CTest 覆盖重复回放和错误路径。Engine/Qt 集成以及正式 Renderer FPS 采集仍未完成 || 2026-08-21 | CA-006 完成 | 已新增 GLM-only `OrbitCameraController`，实现轨迹球、防翻转、平移、缩放、延迟 reset、动态裁剪面、相机相对矩阵和全局世界空间视锥；固定 OpenGL `[-1,1]` 深度约定；未实现 CA-007、Engine 注入或 Qt 映射 |
| 2026-08-21 | CA-005 完成 | 已基于用户提供参考源码确认透视轨道球、输入、reset、动态裁剪面及错误语义；新增相机交互设计并同步 FR-CAM-002/003、概要/详细设计与任务状态；未实现具体 Controller、Engine/Qt 集成或性能路径 |
| 2026-08-14 | PF-008 完成 / Project Foundation 完成 | 已注册默认与显式 OpenGL-only 的 configure;smoke CTest 用例；默认配置、构建和完整 CTest 7/7 通过，configure 标签 2/2 通过；Project Foundation 全部必需任务和模块级验收已满足 |
| 2026-08-14 | PF-007 完成 | 已定义注入式 Render/Compute 后端工厂契约和 ApplicationComposition；Render/Compute 显式失败语义明确，CUDA Off/On/Auto 语义符合确认方案；Fake 工厂与装配测试通过，默认 OpenGL 配置下 5/5 CTest 通过 |
| 2026-08-14 | PF-006 完成 | 已实现公共 Engine 配置类型和默认值；队列容量规则按主人确认通过 hasValidQueueCapacities() 校验；默认 OpenGL 配置下 4/4 CTest 通过 |
| 2026-08-14 | PF-005 完成 | 已实现 ErrorDomain、Error、Result<T> 和 Result<void>；错误访问采用主人确认的 Debug assert/Release std::terminate 保护；默认 OpenGL 配置下 3/3 CTest 通过 |
| 2026-08-14 | PF-004 完成 | 已实现 DatasetId、ChunkId、FrameId、TaskId、RenderSize、ColorRgba；公共头保持后端无关；新增自包含 CMake/CTest 单元测试，默认值、比较和强类型静态断言通过 |
| 2026-08-14 | PF-003 完成 | 已创建 15 个模块 Target、单向依赖图和 Target 边界 CTest；Qt/PCL 依赖按确认延迟到后续具体实现任务 |
| 2026-08-14 | PF-002 完成 | 已加入 CMake 3.21、C++17 基线及 OpenGL/Vulkan/CUDA/Tests 构建选项；完成三种配置验收 |
| 2026-08-14 | PF-001 完成 | 已创建项目源码与测试目录骨架；Project Foundation 模块进入进行中状态 |
|  | 初始化任务划分 | 18 个模块和最小任务已建立；所有 checklist 初始未完成 |
