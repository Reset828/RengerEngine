# CUDA OpenGL Interop 任务清单

> 文件：`docs/tasks/cuda-opengl-interop.md`  
> 所属阶段：Phase 1（可选能力）  
> 模块状态：暂缓（本阶段跳过）
> 前置模块：[opengl-renderer](./opengl-renderer.md)、[task-system](./task-system.md)、[diagnostics](./diagnostics.md)  
> 输入基线：[需求文档](../requirements/spec.md)、[概要设计](../design/architectureDesign.md)、[详细设计](../design/detailDesign.md)、[项目规范](../../agent.md)

## 本阶段暂缓说明

按主人要求，本阶段暂缓/跳过本模块的全部 CUDA 实现与验收，不视为完成。保持所有任务和模块级验收未勾选，不执行 CUDA Target、设备匹配、Interop、Kernel、同步或相关验收测试；统一使用 `DZC_ENABLE_CUDA=OFF` 构建和验证。待 Qt/Phase 1 当前工作完成后，由主人明确重新启动 CUDA 任务。

## 1. 模块目标

实现可选 CUDA-OpenGL 资源注册、原地预处理、精确同步和 auto/on/off 降级语义。

## 2. 范围边界

**包含：** 能力检测；同 GPU 验证；GL Buffer 注册；映射/解除映射；Kernel 调度；资源 fence；降级；生命周期。  
**不包含：** CUDA-Vulkan；CPU 回读再上传；复杂点云算法；非 NVIDIA 平台强制支持。

## 3. 完成规则

只有同时满足以下条件，模块才可以在 [progress.md](./progress.md) 中标记完成：

- 本文所有非阻塞必需任务均已勾选；
- 所有自动化测试通过，能力缺失用例只能明确标记为 Skipped，不能伪造通过；
- 对应公共接口和私有实现符合 `agent.md` 的命名、Pimpl、RAII 和依赖边界；
- 相关需求、设计和测试文档已经同步；

## 4. 子任务 Checklist

- [ ] **CG-001 配置可选 CUDA-GL Target**
- [ ] **CG-002 实现 CUDA-GL 能力与设备匹配**
- [ ] **CG-003 实现 GL Buffer 注册 RAII**
- [ ] **CG-004 实现映射租约**
- [ ] **CG-005 实现精确 GL/CUDA 同步**
- [ ] **CG-006 实现最小原地预处理 Kernel**
- [ ] **CG-007 实现 CUDA 模式降级策略**
- [ ] **CG-008 完成 CUDA-GL 零拷贝验收测试**

## 5. 子任务说明

### CG-001 配置可选 CUDA-GL Target

- **状态**：未开始
- **目标**：仅 DZC_ENABLE_CUDA=ON 时建立 CUDA 与互操作 Target。
- **前置任务**：project-foundation/PF-002, opengl-renderer/GL-001
- **预计文件**：`src/compute/cuda/CMakeLists.txt`、`src/compute/interop/gl/CMakeLists.txt`
- **实现要求**：CUDA OFF 时不产生 CUDA 链接依赖；依赖缺失时启用配置明确失败。
- **验收检查**：CUDA OFF 的 OpenGL 构建成功，ON 的兼容环境可配置。
- **测试要求**：CMake on/off 配置测试。
- **追踪**：FR-CUDA-001、NFR-PORT-003

### CG-002 实现 CUDA-GL 能力与设备匹配

- **状态**：未开始
- **目标**：检查 CUDA Device 与当前 GL Device 兼容。
- **前置任务**：CG-001, diagnostics/DG-001
- **预计文件**：`src/compute/interop/gl/CudaGlCapabilities.h`、`src/compute/interop/gl/CudaGlCapabilities.cpp`、`tests/interop/CudaGlCapabilitiesTests.cpp`
- **实现要求**：不得按设备名称猜测；输出明确原因。
- **验收检查**：兼容设备通过，不兼容或无 CUDA 返回稳定状态。
- **测试要求**：真实能力测试；无环境 Skipped。
- **追踪**：FR-CUDA-001、FR-GL-004

### CG-003 实现 GL Buffer 注册 RAII

- **状态**：未开始
- **目标**：封装 cudaGraphicsGLRegisterBuffer/unregister。
- **前置任务**：CG-002, opengl-renderer/GL-006
- **预计文件**：`src/compute/interop/gl/CudaGlResource.h`、`src/compute/interop/gl/CudaGlResource.cpp`、`tests/interop/CudaGlResourceTests.cpp`
- **实现要求**：注册状态单一所有者；析构不抛；先注销再销毁 GL Buffer。
- **验收检查**：注册、移动、重复释放和失败回滚正确。
- **测试要求**：兼容环境资源生命周期测试。
- **追踪**：NFR-REL-002、15.1

### CG-004 实现映射租约

- **状态**：未开始
- **目标**：用 RAII map/unmap 暴露 CUDA 设备指针给私有 Kernel。
- **前置任务**：CG-003
- **预计文件**：`src/compute/interop/gl/CudaGlMapping.h`、`src/compute/interop/gl/CudaGlMapping.cpp`、`tests/interop/CudaGlMappingTests.cpp`
- **实现要求**：指针不得进入公共接口；映射期间 GL 不可使用资源。
- **验收检查**：映射大小正确，异常路径自动 unmap。
- **测试要求**：映射、Kernel 失败和提前返回测试。
- **追踪**：FR-CUDA-003/004

### CG-005 实现精确 GL/CUDA 同步

- **状态**：未开始
- **目标**：用资源 fence 和映射语义避免 glFinish。
- **前置任务**：CG-004
- **预计文件**：`src/compute/interop/gl/CudaGlSynchronizer.h`、`src/compute/interop/gl/CudaGlSynchronizer.cpp`、`tests/interop/CudaGlSynchronizationTests.cpp`
- **实现要求**：只等待目标资源前序 GL 访问；记录同步耗时。
- **验收检查**：验证无 glFinish，输出结果在后续 GL 绘制可见。
- **测试要求**：同步压力和多资源交错测试。
- **追踪**：FR-CUDA-004、15.2

### CG-006 实现最小原地预处理 Kernel

- **状态**：未开始
- **目标**：实现需求允许的简单属性预处理参考 Kernel。
- **前置任务**：CG-004, CG-005
- **预计文件**：`src/compute/cuda/PointPreprocess.cu`、`src/compute/cuda/PointPreprocess.h`、`tests/interop/CudaGlKernelTests.cpp`
- **实现要求**：只处理已确认的简单属性变换；不加入配准、修补或过滤业务扩展。
- **验收检查**：GPU 输出与 CPU 参考在误差范围内一致，无 CPU 回读再上传路径。
- **测试要求**：小数据 CPU/GPU 对比和边界测试。
- **追踪**：FR-CUDA-002/003

### CG-007 实现 CUDA 模式降级策略

- **状态**：未开始
- **目标**：接入 off/on/auto 到 ComputeBackend。
- **前置任务**：CG-002, CG-006, engine-core/EC-009
- **预计文件**：`src/compute/cuda/CudaComputeBackend.cpp`、`src/compute/common/DisabledComputeBackend.cpp`、`tests/integration/CudaGlModeTests.cpp`
- **实现要求**：auto 失败降级并发事件；on 失败返回错误；off 不创建 CUDA Context。
- **验收检查**：三种模式行为与快照状态正确。
- **测试要求**：Fake 能力和真实环境模式测试。
- **追踪**：DDD-004、FR-CUDA-001

### CG-008 完成 CUDA-GL 零拷贝验收测试

- **状态**：未开始
- **目标**：证明结果直接供 OpenGL 使用且生命周期正确。
- **前置任务**：CG-007
- **预计文件**：`tests/interop/CudaOpenGLZeroCopyTests.cpp`
- **实现要求**：测试记录注册、map、Kernel、unmap、draw 路径；CPU upload 计数必须为 0。
- **验收检查**：兼容环境输出正确；无环境明确 Skipped；auto 可继续基础渲染。
- **测试要求**：CTest interop/cuda-gl 标签。
- **追踪**：AC-P1-011/012、FR-GL-004

## 6. 模块级验收

- [ ] CUDA OFF 时 OpenGL 后端完全可构建运行
- [ ] auto/on/off 行为符合设计
- [ ] 互操作路径无 CPU 回读再上传且无 glFinish
- [ ] 资源注销顺序和故障回滚测试通过

## 7. 交接记录

- 完成日期：
- 完成人：
- 关键变更：
- 未解决问题：
- 测试命令与结果：
- 关联提交：

## 8. 变更约束

若实现需要改变公共接口、模块依赖、已确认参数或需求行为，必须先更新需求与设计文档，不得在编码任务中自行改变。
