# Vulkan Memory and Upload 任务清单

> 文件：`docs/tasks/vulkan-memory.md`  
> 所属阶段：Phase 2  
> 模块状态：未开始  
> 前置模块：[project-foundation](./project-foundation.md)、[diagnostics](./diagnostics.md)、[octree-lod](./octree-lod.md)  
> 输入基线：[需求文档](../requirements/spec.md)、[概要设计](../design/architectureDesign.md)、[详细设计](../design/detailDesign.md)、[项目规范](../../agent.md)

## 1. 模块目标

实现 Vulkan 私有显存页分配器、专用/外部分配路径、Staging Ring、Timeline 完成值和延迟释放。

## 2. 范围边界

**包含：** memory type 选择；内存页；best-fit；专用分配；外部分配入口；Host 映射；Staging Ring；上传提交描述；延迟回收；统计。  
**不包含：** Swapchain；绘制 Pipeline；CUDA 导入；资源压缩搬迁。

## 3. 完成规则

只有同时满足以下条件，模块才可以在 [progress.md](./progress.md) 中标记完成：

- 本文所有非阻塞必需任务均已勾选；
- 所有自动化测试通过，能力缺失用例只能明确标记为 Skipped，不能伪造通过；
- 对应公共接口和私有实现符合 `agent.md` 的命名、Pimpl、RAII 和依赖边界；
- 相关需求、设计和测试文档已经同步；

## 4. 子任务 Checklist

- [ ] **VM-001 定义私有 AllocationHandle**
- [ ] **VM-002 实现 memory type 选择**
- [ ] **VM-003 实现空闲区间 best-fit**
- [ ] **VM-004 实现 VulkanMemoryPage**
- [ ] **VM-005 实现专用与外部分配路径**
- [ ] **VM-006 实现 VulkanMemoryAllocator**
- [ ] **VM-007 实现 Staging Ring 分配**
- [ ] **VM-008 实现临时 Staging 回退**
- [ ] **VM-009 实现上传命令与 Barrier 规划**
- [ ] **VM-010 实现 Timeline 提交跟踪**
- [ ] **VM-011 实现延迟释放队列**
- [ ] **VM-012 完成上传管理器集成测试**

## 5. 子任务说明

### VM-001 定义私有 AllocationHandle

- **状态**：未开始
- **目标**：保存 page/offset/size/type/mapped/dedicated/external 信息。
- **前置任务**：project-foundation/PF-005
- **预计文件**：`src/render/vulkan/memory/AllocationHandle.h`
- **实现要求**：类型只在 Vulkan Target 可见；移动所有权；无裸拥有指针。
- **验收检查**：默认无效、移动和释放状态明确。
- **测试要求**：纯值类型和移动测试。
- **追踪**：17.1、NFR-MAIN-003

### VM-002 实现 memory type 选择

- **状态**：未开始
- **目标**：按 required/preferred flags 和 typeBits 选择内存类型。
- **前置任务**：VM-001
- **预计文件**：`src/render/vulkan/memory/MemoryTypeSelector.h`、`src/render/vulkan/memory/MemoryTypeSelector.cpp`、`tests/unit/MemoryTypeSelectorTests.cpp`
- **实现要求**：无匹配返回 Resource/AllocationFailed；可注入物理设备属性。
- **验收检查**：候选优先级和失败结果稳定。
- **测试要求**：表驱动属性组合测试。
- **追踪**：FR-VK-006、17.2

### VM-003 实现空闲区间 best-fit

- **状态**：未开始
- **目标**：维护 offset 有序和 size 可检索索引，支持对齐切分合并。
- **前置任务**：VM-001
- **预计文件**：`src/render/vulkan/memory/FreeRangeAllocator.h`、`src/render/vulkan/memory/FreeRangeAllocator.cpp`、`tests/unit/FreeRangeAllocatorTests.cpp`
- **实现要求**：checked 64 位运算；选择满足请求的最小区间。
- **验收检查**：分配不重叠，释放后相邻区间合并，记账守恒。
- **测试要求**：碎片、随机序列、对齐和溢出测试。
- **追踪**：DDD-012、17.2

### VM-004 实现 VulkanMemoryPage

- **状态**：未开始
- **目标**：封装 VkDeviceMemory、映射和 FreeRangeAllocator。
- **前置任务**：VM-002, VM-003
- **预计文件**：`src/render/vulkan/memory/VulkanMemoryPage.h`、`src/render/vulkan/memory/VulkanMemoryPage.cpp`、`tests/graphics/VulkanMemoryPageTests.cpp`
- **实现要求**：Device Local 256 MiB，Host Visible 64 MiB；按 memoryTypeIndex 分池。
- **验收检查**：页面分配、映射、flush/invalidate 和销毁正确。
- **测试要求**：真实 Vulkan 小页配置测试；无环境 Skipped。
- **追踪**：DDD-012、17.2

### VM-005 实现专用与外部分配路径

- **状态**：未开始
- **目标**：大于半页或 Dedicated 要求时独立分配；外部资源强制专用。
- **前置任务**：VM-004
- **预计文件**：`src/render/vulkan/memory/VulkanDedicatedAllocation.h`、`src/render/vulkan/memory/VulkanMemoryAllocator.cpp`、`tests/graphics/VulkanDedicatedAllocationTests.cpp`
- **实现要求**：外部分配带导出信息且不进入普通页；不做搬迁。
- **验收检查**：阈值、required/preferred dedicated 和 external 标记正确。
- **测试要求**：Fake requirements 与真实 Buffer 测试。
- **追踪**：DDD-012、17.2、18.2

### VM-006 实现 VulkanMemoryAllocator

- **状态**：未开始
- **目标**：按类型管理页池、专用分配和统计。
- **前置任务**：VM-004, VM-005, octree-lod/OL-011
- **预计文件**：`src/render/vulkan/memory/VulkanMemoryAllocator.h`、`src/render/vulkan/memory/VulkanMemoryAllocator.cpp`、`tests/graphics/VulkanMemoryAllocatorTests.cpp`
- **实现要求**：记账 reserved/allocated/free/dedicated/external/fragmentation；遵守 GPU 预算。
- **验收检查**：低预算分配失败可诊断，无越界或重复释放。
- **测试要求**：随机分配释放、预算、失败注入测试。
- **追踪**：FR-VK-006、FR-LOD-004

### VM-007 实现 Staging Ring 分配

- **状态**：未开始
- **目标**：用绝对 head/tail 和完成值管理环形切片。
- **前置任务**：VM-006
- **预计文件**：`src/render/vulkan/upload/StagingRing.h`、`src/render/vulkan/upload/StagingRing.cpp`、`tests/unit/StagingRingTests.cpp`
- **实现要求**：考虑 Buffer 对齐和 nonCoherentAtomSize；空间不足先回收再换行。
- **验收检查**：切片不重叠，完成值到达后可复用。
- **测试要求**：绕回、对齐、满环、乱序完成测试。
- **追踪**：DDD-012、17.3

### VM-008 实现临时 Staging 回退

- **状态**：未开始
- **目标**：Ring 无空间时创建预算内专用 staging 或推迟。
- **前置任务**：VM-007
- **预计文件**：`src/render/vulkan/upload/StagingAllocator.h`、`src/render/vulkan/upload/StagingAllocator.cpp`、`tests/unit/StagingAllocatorTests.cpp`
- **实现要求**：不得无限等待或越过预算；低优先级上传可返回 Deferred。
- **验收检查**：ring/专用/deferred 三条路径可控。
- **测试要求**：小 ring 和低预算测试。
- **追踪**：FR-VK-007、17.3

### VM-009 实现上传命令与 Barrier 规划

- **状态**：未开始
- **目标**：生成 copy、transfer→vertex/SSBO barrier 和 queue ownership transfer 描述。
- **前置任务**：VM-008
- **预计文件**：`src/render/vulkan/upload/UploadPlanner.h`、`src/render/vulkan/upload/UploadPlanner.cpp`、`tests/unit/UploadPlannerTests.cpp`
- **实现要求**：不同/相同 queue family 路径明确；无全局 device idle。
- **验收检查**：规划中的 stage/access/family 与资源用途一致。
- **测试要求**：表驱动 Barrier 规划测试。
- **追踪**：FR-VK-007/008、17.3

### VM-010 实现 Timeline 提交跟踪

- **状态**：未开始
- **目标**：分配单调完成值并查询回收。
- **前置任务**：VM-009
- **预计文件**：`src/render/vulkan/sync/TimelineTracker.h`、`src/render/vulkan/sync/TimelineTracker.cpp`、`tests/graphics/TimelineTrackerTests.cpp`
- **实现要求**：内部上传/回收优先 Timeline；值溢出前安全失败。
- **验收检查**：提交值单调，查询完成值可驱动回收。
- **测试要求**：Fake API 单元测试和真实 Semaphore 测试。
- **追踪**：DDD-011、16.4

### VM-011 实现延迟释放队列

- **状态**：未开始
- **目标**：按 timelineValue 排序回收 Buffer/Allocation。
- **前置任务**：VM-006, VM-010
- **预计文件**：`src/render/vulkan/memory/DeferredReleaseQueue.h`、`src/render/vulkan/memory/DeferredReleaseQueue.cpp`、`tests/unit/DeferredReleaseQueueTests.cpp`
- **实现要求**：资源先销毁 Buffer 再归还内存；未完成值不释放。
- **验收检查**：乱序入队按完成值正确释放且 shutdown 可 drain。
- **测试要求**：顺序、相同值、未完成和最终 drain 测试。
- **追踪**：17.4、NFR-REL-002

### VM-012 完成上传管理器集成测试

- **状态**：未开始
- **目标**：从 CPU Chunk 上传到 Device Local Buffer 并延迟释放。
- **前置任务**：VM-008, VM-009, VM-010, VM-011
- **预计文件**：`src/render/vulkan/upload/VulkanUploadManager.h`、`src/render/vulkan/upload/VulkanUploadManager.cpp`、`tests/graphics/VulkanUploadManagerTests.cpp`
- **实现要求**：使用 staging 和 timeline；记录字节/预算；不得 vkDeviceWaitIdle。
- **验收检查**：上传后 GPU 读取内容正确，释放只在完成后发生。
- **测试要求**：真实 Vulkan copy/校验 Buffer 和低预算测试。
- **追踪**：AC-P2-007/008、FR-VK-007/008

## 6. 模块级验收

- [ ] best-fit、对齐、合并和预算随机测试通过
- [ ] 页大小、专用阈值和外部分配路径符合设计
- [ ] Staging Ring 与 Timeline 上传测试通过
- [ ] 正常上传/释放路径不调用 vkDeviceWaitIdle

## 7. 交接记录

- 完成日期：
- 完成人：
- 关键变更：
- 未解决问题：
- 测试命令与结果：
- 关联提交：

## 8. 变更约束

若实现需要改变公共接口、模块依赖、已确认参数或需求行为，必须先更新需求与设计文档，不得在编码任务中自行改变。
