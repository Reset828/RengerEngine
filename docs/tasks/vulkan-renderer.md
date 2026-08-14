# Vulkan Renderer 任务清单

> 文件：`docs/tasks/vulkan-renderer.md`  
> 所属阶段：Phase 2  
> 模块状态：未开始  
> 前置模块：[engine-core](./engine-core.md)、[point-cloud-data](./point-cloud-data.md)、[camera-abstraction](./camera-abstraction.md)、[octree-lod](./octree-lod.md)、[vulkan-memory](./vulkan-memory.md)、[task-system](./task-system.md)、[diagnostics](./diagnostics.md)、[qt-application](./qt-application.md)  
> 输入基线：[需求文档](../requirements/spec.md)、[概要设计](../design/architectureDesign.md)、[详细设计](../design/detailDesign.md)、[项目规范](../../agent.md)

## 1. 模块目标

实现 Vulkan 1.2 渲染后端，以两飞行帧、传统 Render Pass、多线程 Secondary Command Buffer、Pipeline Cache 和可恢复 Swapchain 生命周期完成后端等价迁移。

## 2. 范围边界

**包含：** Vulkan 初始化和设备选择；Surface/Swapchain 生命周期；帧上下文和同步；Descriptor/Pipeline/Shader；多线程命令录制；Pipeline Cache；Chunk 绘制与 GPU 时间统计。  
**不包含：** 通用显存分配器与上传算法（由 vulkan-memory 负责）；CUDA 外部内存与信号量（由 cuda-vulkan-interop 负责）；Camera 具体交互映射；动态渲染和光栅化以外的新渲染功能。

## 3. 完成规则

只有同时满足以下条件，模块才可以在 [progress.md](./progress.md) 中标记完成：

- 本文所有非阻塞必需任务均已勾选；
- 所有自动化测试通过，能力缺失用例只能明确标记为 Skipped，不能伪造通过；
- 对应公共接口和私有实现符合 `agent.md` 的命名、Pimpl、RAII 和依赖边界；
- 相关需求、设计和测试文档已经同步；

## 4. 子任务 Checklist

- [ ] **VK-001 配置 Vulkan Target 与 SPIR-V 构建**
- [ ] **VK-002 实现 Instance、验证层与 Debug Messenger**
- [ ] **VK-003 实现物理设备能力查询与选择**
- [ ] **VK-004 实现 Logical Device 与 Queue 上下文**
- [ ] **VK-005 实现 Surface 与 Swapchain 创建**
- [ ] **VK-006 实现两飞行帧 FrameContext**
- [ ] **VK-007 实现传统 Render Pass 与 Framebuffer**
- [ ] **VK-008 实现 Descriptor 布局与逐帧资源**
- [ ] **VK-009 实现 Pipeline Layout 与 Push Constant 契约**
- [ ] **VK-010 实现 Shader Module 与 Graphics Pipeline**
- [ ] **VK-011 实现 Pipeline Cache 持久化**
- [ ] **VK-012 接入 Chunk Vulkan GPU 资源**
- [ ] **VK-013 实现可见绘制列表冻结与切分**
- [ ] **VK-014 实现多线程 Secondary Command Buffer 录制**
- [ ] **VK-015 实现 Primary Command Buffer 提交与呈现**
- [ ] **VK-016 实现 resize、最小化与 Swapchain 重建**
- [ ] **VK-017 实现 Vulkan GPU Timestamp 统计**
- [ ] **VK-018 完成 VulkanBackend 生命周期与后端集成**

## 5. 子任务说明

### VK-001 配置 Vulkan Target 与 SPIR-V 构建

- **状态**：未开始
- **目标**：建立可独立开关的 Vulkan 1.2 后端 Target 和构建期 Shader 编译链。
- **前置任务**：PF-002, PF-003
- **预计文件**：`src/render/vulkan/CMakeLists.txt`、`cmake/DzcVulkanShaders.cmake`、`shaders/vulkan/point.vert`、`shaders/vulkan/point.frag`
- **实现要求**：只有 DZC_ENABLE_VULKAN=ON 时查找 Vulkan SDK；Shader custom command 跟踪 include 依赖，编译失败必须终止构建。
- **验收检查**：Vulkan-off 不需要 SDK；Vulkan-on 可生成 SPIR-V 并复制到运行目录；任一 Shader 错误使构建失败。
- **测试要求**：执行 Vulkan-off、Vulkan-on 和故意破坏 Shader 的 CMake/构建测试。
- **追踪**：FR-COM-001、FR-VK-001、FR-GL-003、DDD-016、24.2

### VK-002 实现 Instance、验证层与 Debug Messenger

- **状态**：未开始
- **目标**：封装 VkInstance、可选 Validation Layer 和调试回调的 RAII 生命周期。
- **前置任务**：VK-001, DG-001
- **预计文件**：`src/render/vulkan/VulkanInstanceContext.h`、`src/render/vulkan/VulkanInstanceContext.cpp`、`tests/graphics/VulkanInstanceContextTests.cpp`
- **实现要求**：最低请求 Vulkan 1.2；Debug/测试配置按可用性启用验证层；Release 不依赖验证层存在；回调转换为 Diagnostics 消息。
- **验收检查**：可创建/销毁 Instance；验证层缺失时按配置明确记录而非 Release 启动失败；error 级验证消息可被测试捕获。
- **测试要求**：运行有/无验证层能力标签测试并检查资源回收。
- **追踪**：FR-COM-002、FR-VK-001、NFR-REL-002、16.3

### VK-003 实现物理设备能力查询与选择

- **状态**：未开始
- **目标**：查询 Queue、Swapchain、Feature、Limit、内存和设备标识并生成确定性评分。
- **前置任务**：VK-002
- **预计文件**：`src/render/vulkan/VulkanPhysicalDeviceSelector.h`、`src/render/vulkan/VulkanPhysicalDeviceSelector.cpp`、`tests/unit/VulkanPhysicalDeviceSelectorTests.cpp`
- **实现要求**：强制图形队列、Surface present、Swapchain 扩展和项目所需限制；优先独立 GPU/transfer queue；CUDA=on 时必须接受同 GPU 匹配约束，不得按设备名称猜测。
- **验收检查**：Fake 能力输入下选择结果稳定；缺少强制能力返回结构化错误；选择结果包含 UUID/LUID、Queue Family 和可用可选能力。
- **测试要求**：表驱动测试强制能力、评分并列、无设备和 CUDA 约束。
- **追踪**：FR-COM-002、FR-VK-001、FR-VKCUDA-003、16.3、18.1

### VK-004 实现 Logical Device 与 Queue 上下文

- **状态**：未开始
- **目标**：按选择结果创建最小 Feature/Extension 集合和 Queue 句柄。
- **前置任务**：VK-003
- **预计文件**：`src/render/vulkan/VulkanDeviceContext.h`、`src/render/vulkan/VulkanDeviceContext.cpp`、`tests/graphics/VulkanDeviceContextTests.cpp`
- **实现要求**：只启用实际使用能力；支持 graphics/present/可选 transfer queue；对象销毁不抛异常并记录原始 VkResult。
- **验收检查**：设备和 Queue 创建成功；同/异 Queue Family 描述正确；初始化失败无泄漏。
- **测试要求**：真实 Vulkan 冒烟测试和注入失败回滚测试。
- **追踪**：FR-VK-001、NFR-REL-001、16.3、23.1

### VK-005 实现 Surface 与 Swapchain 创建

- **状态**：未开始
- **目标**：从平台私有原生窗口描述创建 Surface，并选择格式、Present Mode、Extent 和 Image View。
- **前置任务**：VK-004, QT-004
- **预计文件**：`src/platform/VulkanSurfaceFactory.h`、`src/platform/VulkanSurfaceFactory.cpp`、`src/render/vulkan/VulkanSwapchain.h`、`src/render/vulkan/VulkanSwapchain.cpp`、`tests/graphics/VulkanSwapchainTests.cpp`
- **实现要求**：公共 Engine API 不出现 QWindow/VkSurfaceKHR；处理当前 extent 与可变 extent；数量受能力上下限约束。
- **验收检查**：可在 Qt 原生窗口上创建 Swapchain；所有 image view 可逆序释放；不支持 present 时返回明确错误。
- **测试要求**：窗口化能力测试创建、获取图像并销毁 Swapchain。
- **追踪**：FR-REN-005、FR-VK-002、NFR-PORT-002、16.1、16.9

### VK-006 实现两飞行帧 FrameContext

- **状态**：未开始
- **目标**：创建固定两个包含主命令资源、worker 命令池、二进制信号量、Fence 和临时租约的帧上下文。
- **前置任务**：VK-004, TS-006
- **预计文件**：`src/render/vulkan/VulkanFrameContext.h`、`src/render/vulkan/VulkanFrameContext.cpp`、`tests/graphics/VulkanFrameContextTests.cpp`
- **实现要求**：帧开始只等待即将复用上下文的 Fence；每个 worker 每帧独占 CommandPool；重置顺序必须在 Fence 完成后。
- **验收检查**：两个 FrameContext 独立；连续多帧轮换无命令池并发使用和同步错误。
- **测试要求**：验证层开启运行多帧轮换与 shutdown 测试。
- **追踪**：FR-VK-003、FR-VK-008、DDD-011、16.4

### VK-007 实现传统 Render Pass 与 Framebuffer

- **状态**：未开始
- **目标**：创建单颜色 Attachment、可选深度 Attachment 的传统 Render Pass 和每图像 Framebuffer。
- **前置任务**：VK-005
- **预计文件**：`src/render/vulkan/VulkanRenderPass.h`、`src/render/vulkan/VulkanRenderPass.cpp`、`tests/graphics/VulkanRenderPassTests.cpp`
- **实现要求**：颜色布局从 undefined 到 present；clear 在 Render Pass 开始；格式变化时可重建兼容对象。
- **验收检查**：Framebuffer 数量与 Swapchain 图像一致；begin/end 后可提交并 present；销毁顺序正确。
- **测试要求**：离屏/窗口图形测试检查 clear 结果和验证层消息。
- **追踪**：FR-REN-001、FR-VK-002、16.5

### VK-008 实现 Descriptor 布局与逐帧资源

- **状态**：未开始
- **目标**：建立 set 0 的 FrameData UBO、ChunkData SSBO 布局和逐飞行帧 Descriptor 资源。
- **前置任务**：VK-006, VK-007
- **预计文件**：`src/render/vulkan/VulkanDescriptorManager.h`、`src/render/vulkan/VulkanDescriptorManager.cpp`、`tests/graphics/VulkanDescriptorManagerTests.cpp`
- **实现要求**：set 0 binding 0 为 FrameData UBO，binding 1 为 ChunkData SSBO；不得更新仍在 GPU 使用的集合。
- **验收检查**：两个帧集合相互独立，更新后 Shader 可读取预期数据；容量不足返回结构化错误。
- **测试要求**：真实 Descriptor 写入/读取测试和低容量失败测试。
- **追踪**：FR-GL-002、FR-VK-003、16.7、13.2

### VK-009 实现 Pipeline Layout 与 Push Constant 契约

- **状态**：未开始
- **目标**：定义统一顶点布局、Descriptor Set Layout 和不超过 32 bytes 的 Push Constant。
- **前置任务**：VK-008, PD-007
- **预计文件**：`src/render/vulkan/VulkanPipelineLayout.h`、`src/render/vulkan/VulkanPipelineLayout.cpp`、`src/render/common/ShaderLayout.h`、`tests/unit/ShaderLayoutTests.cpp`
- **实现要求**：Vulkan location 与后端无关 Shader 逻辑布局一致；编译期检查结构尺寸、偏移和对齐。
- **验收检查**：布局静态断言通过；Push Constant range 不超过 32 bytes；OpenGL/Vulkan 语义字段一一对应。
- **测试要求**：编译期布局测试和反射/配置表测试。
- **追踪**：FR-REN-002/003、FR-GL-002、16.7、13.2

### VK-010 实现 Shader Module 与 Graphics Pipeline

- **状态**：未开始
- **目标**：加载并校验 SPIR-V，按 Pipeline key 创建点云 Graphics Pipeline。
- **前置任务**：VK-001, VK-007, VK-009
- **预计文件**：`src/render/vulkan/VulkanPipelineLibrary.h`、`src/render/vulkan/VulkanPipelineLibrary.cpp`、`tests/graphics/VulkanPipelineLibraryTests.cpp`
- **实现要求**：校验文件存在、长度为 4 的倍数和 SPIR-V magic；key 至少含颜色格式、深度格式、着色变体和点渲染状态。
- **验收检查**：四种着色变体可创建；损坏 SPIR-V 明确失败；相同 key 复用 Pipeline。
- **测试要求**：图形测试创建全部变体并注入缺失/损坏文件。
- **追踪**：FR-REN-001/002/003、FR-VK-005、16.7

### VK-011 实现 Pipeline Cache 持久化

- **状态**：未开始
- **目标**：加载兼容缓存并在关闭时原子保存 VkPipelineCache 数据。
- **前置任务**：VK-010, DG-007
- **预计文件**：`src/render/vulkan/VulkanPipelineCacheStore.h`、`src/render/vulkan/VulkanPipelineCacheStore.cpp`、`tests/graphics/VulkanPipelineCacheStoreTests.cpp`
- **实现要求**：缓存键校验 vendor/device/driver/pipelineCacheUUID/应用 Shader 版本；无效缓存忽略并重建；临时文件原子替换。
- **验收检查**：首次启动生成缓存；第二次加载兼容缓存；截断或身份不匹配缓存不导致启动失败。
- **测试要求**：临时目录运行首次、复用、损坏和身份变化测试。
- **追踪**：FR-VK-005、NFR-REL-003、16.8

### VK-012 接入 Chunk Vulkan GPU 资源

- **状态**：未开始
- **目标**：把 VulkanUploadManager 输出的 Device Local Buffer 封装为可租赁的 Chunk 渲染资源。
- **前置任务**：VM-012, VK-008
- **预计文件**：`src/render/vulkan/VulkanChunkResource.h`、`src/render/vulkan/VulkanChunkResource.cpp`、`tests/graphics/VulkanChunkResourceTests.cpp`
- **实现要求**：资源记录最后使用 timeline/fence 值；淘汰只进入延迟释放；不得暴露 VkBuffer 到公共接口。
- **验收检查**：上传后的 Chunk 可形成 DrawChunk；在途资源不会提前释放；预算统计一致。
- **测试要求**：上传—绘制租约—延迟释放集成测试。
- **追踪**：FR-VK-006/007/008、FR-LOD-004/005、17.4

### VK-013 实现可见绘制列表冻结与切分

- **状态**：未开始
- **目标**：把本帧可见 Chunk 快照转换为稳定 DrawChunk 列表并切为连续区间。
- **前置任务**：OL-009, VK-012
- **预计文件**：`src/render/vulkan/VulkanDrawListBuilder.h`、`src/render/vulkan/VulkanDrawListBuilder.cpp`、`tests/unit/VulkanDrawListBuilderTests.cpp`
- **实现要求**：worker 数为 min(recordingThreadCount, drawCount)；空列表不创建任务；排序和切分确定；任务期间资源列表不可移动。
- **验收检查**：任意 draw/worker 数下无遗漏、重复或空中间区间；输出顺序稳定。
- **测试要求**：表驱动覆盖 0/1/N 绘制与 worker 上下界。
- **追踪**：FR-VIS-002、FR-VIS-003、FR-VK-003、FR-VK-004、16.6

### VK-014 实现多线程 Secondary Command Buffer 录制

- **状态**：未开始
- **目标**：每个 worker 使用专属 CommandPool 录制一个连续 DrawChunk 区间。
- **前置任务**：VK-006, VK-010, VK-013, TS-006
- **预计文件**：`src/render/vulkan/VulkanCommandRecorder.h`、`src/render/vulkan/VulkanCommandRecorder.cpp`、`tests/graphics/VulkanSecondaryCommandTests.cpp`
- **实现要求**：使用 RENDER_PASS_CONTINUE 和正确 inheritance；绑定共享 Pipeline/Descriptor；worker 失败则整帧失败，不提交部分结果。
- **验收检查**：多个 worker 可并发完成且结果按区间序号稳定；验证层无命令池并发或 inheritance 错误。
- **测试要求**：1/2/多 worker 图形测试及单 worker 故障注入测试。
- **追踪**：FR-VK-003、FR-VK-004、NFR-PERF-002、16.6

### VK-015 实现 Primary Command Buffer 提交与呈现

- **状态**：未开始
- **目标**：协调 acquire、Primary Render Pass、execute secondary、submit 和 present。
- **前置任务**：VK-005, VK-006, VK-007, VK-014
- **预计文件**：`src/render/vulkan/VulkanFrameExecutor.h`、`src/render/vulkan/VulkanFrameExecutor.cpp`、`tests/graphics/VulkanFrameExecutorTests.cpp`
- **实现要求**：Primary 按稳定顺序执行 Secondary CB；submit 等待 imageAvailable 并 signal renderFinished；正常帧禁止 vkDeviceWaitIdle。
- **验收检查**：连续渲染可见点并正确呈现；空帧也安全；每帧统计 draw/point 数。
- **测试要求**：验证层开启执行多帧、空列表和提交失败测试。
- **追踪**：FR-REN-001、FR-REN-005、FR-VK-003、FR-VK-004、FR-VK-008、16.4/16.6

### VK-016 实现 resize、最小化与 Swapchain 重建

- **状态**：未开始
- **目标**：处理 out-of-date、suboptimal、窗口尺寸变化和 0×0 最小化。
- **前置任务**：VK-015
- **预计文件**：`src/render/vulkan/VulkanSwapchainRebuilder.h`、`src/render/vulkan/VulkanSwapchainRebuilder.cpp`、`tests/graphics/VulkanSwapchainRebuildTests.cpp`
- **实现要求**：暂停 0×0 窗口渲染；只等待相关在途帧；格式变化重建 Render Pass/Pipeline，纯尺寸变化尽量复用兼容对象；禁止正常重建 vkDeviceWaitIdle。
- **验收检查**：重复 resize/最小化/恢复无崩溃、无旧资源使用；out-of-date 可恢复。
- **测试要求**：自动调整窗口尺寸并检查验证层、资源计数和恢复帧。
- **追踪**：FR-REN-005、FR-VK-002/008、AC-P2-003、16.9

### VK-017 实现 Vulkan GPU Timestamp 统计

- **状态**：未开始
- **目标**：为渲染阶段写入 Timestamp Query 并异步汇总 GPU 时间。
- **前置任务**：VK-015, DG-004
- **预计文件**：`src/render/vulkan/VulkanGpuProfiler.h`、`src/render/vulkan/VulkanGpuProfiler.cpp`、`tests/graphics/VulkanGpuProfilerTests.cpp`
- **实现要求**：仅在设备支持时启用；读取已完成 FrameContext 结果，不阻塞当前帧；换算使用 timestampPeriod。
- **验收检查**：支持设备产生有限非负 GPU 毫秒；不支持设备明确标记 unavailable；无全局等待。
- **测试要求**：能力标签测试查询池轮换与单位换算。
- **追踪**：FR-STAT-001/002、NFR-PERF-004、21.3

### VK-018 完成 VulkanBackend 生命周期与后端集成

- **状态**：未开始
- **目标**：组合所有 Vulkan 私有组件并实现 RenderBackend 契约、回滚和幂等关闭。
- **前置任务**：VK-002 至 VK-017, EC-007
- **预计文件**：`src/render/vulkan/VulkanBackend.h`、`src/render/vulkan/VulkanBackend.cpp`、`tests/graphics/VulkanBackendIntegrationTests.cpp`
- **实现要求**：初始化按 RAII 事务完成；协调线程独占 acquire/submit/present；shutdown 只等待具体同步，最终兜底 device idle 必须记录原因。
- **验收检查**：Vulkan 后端可独立启动、加载并绘制数据、resize、卸载和重复 shutdown；失败注入无线程/资源泄漏。
- **测试要求**：运行完整生命周期、初始化各阶段失败、device lost 边界和 Validation 测试。
- **追踪**：FR-COM-001/002、FR-VK-001 至 FR-VK-008、AC-P2-001/002/013/014、23

## 6. 模块级验收

- [ ] Vulkan 1.2 后端可在不构建 OpenGL 后端时独立启动
- [ ] 两飞行帧和多线程 Secondary Command Buffer 路径通过 Validation 测试
- [ ] Swapchain 反复重建、最小化和恢复无泄漏或失效资源访问
- [ ] Pipeline Cache 可安全复用并可忽略损坏/不兼容数据
- [ ] 正常渲染、上传与 Swapchain 重建路径不调用 vkDeviceWaitIdle

## 7. 交接记录

- 完成日期：
- 完成人：
- 关键变更：
- 未解决问题：
- 测试命令与结果：
- 关联提交：

## 8. 变更约束

若实现需要改变公共接口、模块依赖、已确认参数或需求行为，必须先更新需求与设计文档，不得在编码任务中自行改变。
