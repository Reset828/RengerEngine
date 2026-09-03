# CUDA-Vulkan Interop 任务清单

> 文件：`docs/tasks/cuda-vulkan-interop.md`  
> 所属阶段：Phase 2（可选能力）  
> 模块状态：暂缓（本阶段跳过）
> 前置模块：[vulkan-renderer](./vulkan-renderer.md)、[vulkan-memory](./vulkan-memory.md)、[task-system](./task-system.md)、[diagnostics](./diagnostics.md)  
> 输入基线：[需求文档](../requirements/spec.md)、[概要设计](../design/architectureDesign.md)、[详细设计](../design/detailDesign.md)、[项目规范](../../agent.md)

## 本阶段暂缓说明

按主人要求，本阶段暂缓/跳过本模块的全部 CUDA 实现与验收，不视为完成。保持所有任务和模块级验收未勾选，不执行 CUDA Target、设备匹配、Interop、Kernel、同步或相关验收测试；统一使用 `DZC_ENABLE_CUDA=OFF` 构建和验证。待 Qt/Phase 1 当前工作完成后，由主人明确重新启动 CUDA 任务。

## 1. 模块目标

实现同一物理 GPU 上 Vulkan Buffer 与 CUDA 的外部内存、外部二进制信号量互操作，在支持环境中完成无 CPU 回读再上传的原地预处理，并提供 auto/on/off 降级语义。

## 2. 范围边界

**包含：** 设备 UUID/LUID 匹配；可导出专用内存；OS Handle 所有权；CUDA External Memory；双向外部信号量；状态机与 Barrier；可选能力降级；互操作生命周期。  
**不包含：** 通用 CUDA 算法库；跨 GPU 拷贝；基于设备名称的匹配；CPU 轮询后 memcpy 的伪零拷贝实现。

## 3. 完成规则

只有同时满足以下条件，模块才可以在 [progress.md](./progress.md) 中标记完成：

- 本文所有非阻塞必需任务均已勾选；
- 所有自动化测试通过，能力缺失用例只能明确标记为 Skipped，不能伪造通过；
- 对应公共接口和私有实现符合 `agent.md` 的命名、Pimpl、RAII 和依赖边界；
- 相关需求、设计和测试文档已经同步；

## 4. 子任务 Checklist

- [ ] **CV-001 配置 CUDA-Vulkan 可选 Target**
- [ ] **CV-002 实现 Vulkan 与 CUDA 设备身份匹配**
- [ ] **CV-003 实现可导出专用 Vulkan Buffer 分配**
- [ ] **CV-004 实现 OS Handle 导出与关闭规则**
- [ ] **CV-005 实现 CUDA External Memory 导入 RAII**
- [ ] **CV-006 创建双向可导出 Vulkan 二进制信号量**
- [ ] **CV-007 实现 CUDA External Semaphore 导入**
- [ ] **CV-008 实现互操作资源状态机与 Barrier 规划**
- [ ] **CV-009 实现最小原地 CUDA 预处理 Kernel**
- [ ] **CV-010 实现 auto/on/off 能力选择与降级**
- [ ] **CV-011 实现互操作槽生命周期与精确等待**
- [ ] **CV-012 完成 CUDA-Vulkan 零拷贝集成验证**

## 5. 子任务说明

### CV-001 配置 CUDA-Vulkan 可选 Target

- **状态**：未开始
- **目标**：建立仅在 Vulkan 和 CUDA 同时开启时编译的互操作 Target。
- **前置任务**：PF-002, VK-001
- **预计文件**：`src/compute/cuda_vulkan/CMakeLists.txt`、`cmake/DzcCudaVulkan.cmake`
- **实现要求**：DZC_ENABLE_CUDA=OFF 时不查找 CUDA；启用互操作时检查平台外部内存/信号量 API；不得让 OpenGL-only 构建依赖 Vulkan/CUDA。
- **验收检查**：四种后端开关组合按预期配置；缺失 CUDA 时 auto 能构建无 CUDA 版本，显式 CUDA 构建明确失败。
- **测试要求**：执行 CMake 配置矩阵测试。
- **追踪**：FR-COM-001、FR-CUDA-001、FR-VKCUDA-001/002、24.2

### CV-002 实现 Vulkan 与 CUDA 设备身份匹配

- **状态**：未开始
- **目标**：比较 Vulkan UUID/LUID/node mask 与 CUDA 设备身份。
- **前置任务**：VK-003, CV-001
- **预计文件**：`src/compute/cuda_vulkan/CudaVulkanDeviceMatcher.h`、`src/compute/cuda_vulkan/CudaVulkanDeviceMatcher.cpp`、`tests/unit/CudaVulkanDeviceMatcherTests.cpp`
- **实现要求**：优先 UUID/LUID；Windows 要求 LUID 和 node mask 兼容；禁止仅按设备名称匹配。
- **验收检查**：匹配、错配、缺少标识和多 GPU 输入得到确定结果；错误包含双方身份摘要。
- **测试要求**：Fake 设备表驱动测试；有能力环境运行真实设备匹配测试。
- **追踪**：FR-COM-002、FR-VKCUDA-003、18.1

### CV-003 实现可导出专用 Vulkan Buffer 分配

- **状态**：未开始
- **目标**：为互操作资源创建带正确 external handle type 的 Buffer 和专用 DeviceMemory。
- **前置任务**：CV-002, VM-007
- **预计文件**：`src/compute/cuda_vulkan/ExportableVulkanBuffer.h`、`src/compute/cuda_vulkan/ExportableVulkanBuffer.cpp`、`tests/graphics/ExportableVulkanBufferTests.cpp`
- **实现要求**：必须查询外部 Buffer 属性并使用专用分配；资源大小/对齐符合 memory requirements；不得混入普通内存页。
- **验收检查**：支持格式可创建、绑定并导出；不支持组合在分配前失败；预算记账可见。
- **测试要求**：真实 Vulkan 能力标签测试和不支持 handle type 测试。
- **追踪**：FR-VK-006、FR-VKCUDA-001、18.2

### CV-004 实现 OS Handle 导出与关闭规则

- **状态**：未开始
- **目标**：封装 Win32 Handle 或文件描述符的导出、转移和关闭。
- **前置任务**：CV-003
- **预计文件**：`src/platform/ExternalMemoryHandle.h`、`src/platform/ExternalMemoryHandle.cpp`、`tests/unit/ExternalMemoryHandleTests.cpp`
- **实现要求**：使用移动专属 RAII；导入 CUDA 成功后按平台规则立即关闭临时本地 Handle；禁止 double-close。
- **验收检查**：移动、release、close 和失败回滚只关闭一次；泄漏计数为零。
- **测试要求**：Fake 平台句柄单元测试；Windows 实际导出冒烟测试。
- **追踪**：FR-VKCUDA-001、NFR-REL-002、18.2

### CV-005 实现 CUDA External Memory 导入 RAII

- **状态**：未开始
- **目标**：把导出的 Vulkan 内存导入 CUDA 并映射为设备指针。
- **前置任务**：CV-004
- **预计文件**：`src/compute/cuda_vulkan/CudaExternalMemory.h`、`src/compute/cuda_vulkan/CudaExternalMemory.cpp`、`tests/cuda/CudaExternalMemoryTests.cu`
- **实现要求**：保留 allocation 大小和映射范围；所有 cudaError 转为 Error；析构不抛异常；导入失败保持 Vulkan 资源可回滚。
- **验收检查**：CUDA 可访问映射指针；越界映射拒绝；导入/映射失败无 Handle 或 CUDA 对象泄漏。
- **测试要求**：CUDA 能力标签测试写入模式并由 Vulkan/主机校验。
- **追踪**：FR-CUDA-003、FR-VKCUDA-001、18.2

### CV-006 创建双向可导出 Vulkan 二进制信号量

- **状态**：未开始
- **目标**：为每个互操作槽创建 VulkanToCuda 和 CudaToVulkan 信号量。
- **前置任务**：CV-002, VK-004
- **预计文件**：`src/compute/cuda_vulkan/ExportableVulkanSemaphore.h`、`src/compute/cuda_vulkan/ExportableVulkanSemaphore.cpp`、`tests/graphics/ExportableVulkanSemaphoreTests.cpp`
- **实现要求**：使用平台支持的可导出二进制 handle；不得假定 CUDA 支持导出 Timeline Semaphore；只有完整轮次结束后复用。
- **验收检查**：两个方向信号量职责明确并能导出；不支持时返回能力错误；销毁顺序安全。
- **测试要求**：真实导出测试和不支持能力测试。
- **追踪**：FR-CUDA-004、FR-VKCUDA-002、18.3

### CV-007 实现 CUDA External Semaphore 导入

- **状态**：未开始
- **目标**：把双向 Vulkan 信号量导入 CUDA 并封装 stream wait/signal。
- **前置任务**：CV-006, CV-004
- **预计文件**：`src/compute/cuda_vulkan/CudaExternalSemaphorePair.h`、`src/compute/cuda_vulkan/CudaExternalSemaphorePair.cpp`、`tests/cuda/CudaExternalSemaphoreTests.cu`
- **实现要求**：导入后按平台规则关闭临时 Handle；方向不可混用；API 错误包含 CUDA 返回码和资源 ID。
- **验收检查**：CUDA stream 可等待 VulkanToCuda 并 signal CudaToVulkan；失败回滚无泄漏。
- **测试要求**：CUDA/Vulkan 最小跨 API signal/wait 测试。
- **追踪**：FR-CUDA-004、FR-VKCUDA-002、18.3

### CV-008 实现互操作资源状态机与 Barrier 规划

- **状态**：未开始
- **目标**：编码 VulkanWritable→ReadyForCuda→CudaWritable→ReadyForVulkan→VulkanReadable 状态迁移。
- **前置任务**：CV-005, CV-007
- **预计文件**：`src/compute/cuda_vulkan/CudaVulkanResourceState.h`、`src/compute/cuda_vulkan/CudaVulkanResourceState.cpp`、`tests/unit/CudaVulkanResourceStateTests.cpp`
- **实现要求**：每次迁移同时产出所需队列操作、外部信号量和 Vulkan memory barrier；非法/重复迁移失败，不以 CPU 标志代替同步。
- **验收检查**：合法循环可重复；任一越序操作被拒绝；Vulkan stage/access 与 vertex/SSBO 用途一致。
- **测试要求**：状态机全迁移表和 Barrier 表驱动测试。
- **追踪**：FR-CUDA-004、FR-VKCUDA-002、18.2/18.3

### CV-009 实现最小原地 CUDA 预处理 Kernel

- **状态**：未开始
- **目标**：提供可验证的强度归一化或颜色映射原地 Kernel，证明共享 Buffer 可计算。
- **前置任务**：CV-005, CV-008
- **预计文件**：`src/compute/cuda_vulkan/CudaVulkanKernels.cuh`、`src/compute/cuda_vulkan/CudaVulkanKernels.cu`、`tests/cuda/CudaVulkanKernelTests.cu`
- **实现要求**：算法只使用已确认点属性；输入缺少所需属性时跳过或明确失败；不得 CPU 回读再上传。
- **验收检查**：固定小数据输出与 CPU 参考一致；边界点数和空输入安全。
- **测试要求**：CUDA 单元测试 CPU/GPU 对比、空输入和非法参数。
- **追踪**：FR-CUDA-002/003、18.3

### CV-010 实现 auto/on/off 能力选择与降级

- **状态**：未开始
- **目标**：把设备匹配和导入能力接入 ComputeBackend 创建策略。
- **前置任务**：CV-002 至 CV-009, PF-007
- **预计文件**：`src/compute/cuda_vulkan/CudaVulkanInteropFactory.h`、`src/compute/cuda_vulkan/CudaVulkanInteropFactory.cpp`、`tests/unit/CudaVulkanInteropFactoryTests.cpp`
- **实现要求**：off 永不初始化；auto 任一步失败记录原因并禁用 CUDA 继续 Vulkan；on 任一步失败阻止启动；不得静默切到另一 GPU。
- **验收检查**：三种模式和能力组合行为符合约定，Snapshot/日志暴露实际状态与降级原因。
- **测试要求**：Fake 能力矩阵测试和无 CUDA 环境启动测试。
- **追踪**：FR-CUDA-001、FR-VKCUDA-003、NFR-PORT-003、12.1、18.1

### CV-011 实现互操作槽生命周期与精确等待

- **状态**：未开始
- **目标**：组合 Buffer、External Memory 和信号量，按精确同步完成值回收。
- **前置任务**：CV-008, CV-010, VM-011
- **预计文件**：`src/compute/cuda_vulkan/CudaVulkanInteropSlot.h`、`src/compute/cuda_vulkan/CudaVulkanInteropSlot.cpp`、`tests/cuda/CudaVulkanInteropLifecycleTests.cu`
- **实现要求**：停止新提交后只等待关联 fence/semaphore；先销毁 CUDA 映射/external memory/semaphore，再销毁 Vulkan Buffer/Semaphore/Memory；shutdown 幂等。
- **验收检查**：在途资源不会提前销毁；重复 shutdown 安全；各阶段故障注入后对象计数归零。
- **测试要求**：生命周期顺序监控、故障注入和重复创建销毁测试。
- **追踪**：FR-CUDA-004、FR-VKCUDA-001/002、NFR-REL-002、18.4、23.2

### CV-012 完成 CUDA-Vulkan 零拷贝集成验证

- **状态**：未开始
- **目标**：执行 Vulkan→CUDA→Vulkan 完整轮次并核对结果和拷贝计数。
- **前置任务**：CV-009, CV-011, VK-015
- **预计文件**：`tests/interop/CudaVulkanInteropTests.cpp`、`tests/interop/CudaVulkanZeroCopyCounters.cpp`
- **实现要求**：Vulkan signal 后 CUDA wait/原地 kernel/signal，再由 Vulkan wait 和 barrier 后绘制或读取；禁止 CPU 轮询和中间 memcpy。
- **验收检查**：GPU 结果等于 CPU 参考；零 CPU 回读再上传计数；Validation/CUDA 错误均为零；能力不足时明确 Skipped。
- **测试要求**：在兼容 GPU 上连续多轮、错误注入、资源淘汰和关闭测试。
- **追踪**：FR-CUDA-003/004、FR-VKCUDA-001/002/003、AC-P2-012/013、25.3

## 6. 模块级验收

- [ ] 支持环境完成 Vulkan→CUDA→Vulkan 原地处理且结果与 CPU 参考一致
- [ ] 设备匹配只使用 UUID/LUID/node mask 等可靠身份，不按名称猜测
- [ ] 互操作验证证明不存在 CPU 回读后再上传
- [ ] auto/on/off 降级语义和错误诊断通过矩阵测试
- [ ] 关闭顺序先释放 CUDA 对象再释放对应 Vulkan 导出资源

## 7. 交接记录

- 完成日期：
- 完成人：
- 关键变更：
- 未解决问题：
- 测试命令与结果：
- 关联提交：

## 8. 变更约束

若实现需要改变公共接口、模块依赖、已确认参数或需求行为，必须先更新需求与设计文档，不得在编码任务中自行改变。
