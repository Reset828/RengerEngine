# Octree LOD and Residency 任务清单

> 文件：`docs/tasks/octree-lod.md`  
> 所属阶段：Phase 2  
> 模块状态：未开始  
> 前置模块：[point-cloud-data](./point-cloud-data.md)、[dzcpc-cache](./dzcpc-cache.md)、[camera-abstraction](./camera-abstraction.md)、[task-system](./task-system.md)、[diagnostics](./diagnostics.md)、[engine-core](./engine-core.md)  
> 输入基线：[需求文档](../requirements/spec.md)、[概要设计](../design/architectureDesign.md)、[详细设计](../design/detailDesign.md)、[项目规范](../../agent.md)

## 1. 模块目标

实现确定性八叉树构建、屏幕空间 LOD、渐进显示以及 CPU/GPU 驻留预算和淘汰调度。

## 2. 范围边界

**包含：** 八叉树拓扑；代表点；geometric error；LOD 滞回；祖先回退；请求优先级；Residency 状态；CPU/GPU 预算；淘汰。  
**不包含：** Vulkan Buffer 分配实现；Camera 具体操作；磁盘格式编解码；GPU draw 命令。

## 3. 完成规则

只有同时满足以下条件，模块才可以在 [progress.md](./progress.md) 中标记完成：

- 本文所有非阻塞必需任务均已勾选；
- 所有自动化测试通过，能力缺失用例只能明确标记为 Skipped，不能伪造通过；
- 对应公共接口和私有实现符合 `agent.md` 的命名、Pimpl、RAII 和依赖边界；
- 相关需求、设计和测试文档已经同步；

## 4. 子任务 Checklist

- [ ] **OL-001 实现根立方体与 Octant 规则**
- [ ] **OL-002 实现稳定 Octant 分区**
- [ ] **OL-003 实现确定性体素代表点**
- [ ] **OL-004 实现八叉树递归构建**
- [ ] **OL-005 计算 geometric error**
- [ ] **OL-006 实现屏幕空间误差**
- [ ] **OL-007 实现 LOD 选择与滞回**
- [ ] **OL-008 实现渐进祖先回退**
- [ ] **OL-009 实现驻留请求优先级**
- [ ] **OL-010 实现自动 CPU 预算**
- [ ] **OL-011 实现 GPU 预算策略接口**
- [ ] **OL-012 实现 ResidencyManager 状态和租约**
- [ ] **OL-013 实现预算淘汰**
- [ ] **OL-014 接入异步读取/上传请求**

## 5. 子任务说明

### OL-001 实现根立方体与 Octant 规则

- **状态**：未开始
- **目标**：从 Dataset bounds 创建根立方体并固定 bit0/1/2 子节点规则。
- **前置任务**：point-cloud-data/PD-002
- **预计文件**：`src/data/lod/OctreeGeometry.h`、`src/data/lod/OctreeGeometry.cpp`、`tests/unit/OctreeGeometryTests.cpp`
- **实现要求**：分割面进入正半轴；退化轴扩展到最小正边长；最大深度 16。
- **验收检查**：边界点和退化 bounds 的 child index 稳定。
- **测试要求**：八个象限、中心平面、大坐标测试。
- **追踪**：DDD-010、12.1/12.2

### OL-002 实现稳定 Octant 分区

- **状态**：未开始
- **目标**：保留每个 Octant 内源稳定序号。
- **前置任务**：OL-001
- **预计文件**：`src/data/lod/StableOctantPartition.h`、`src/data/lod/StableOctantPartition.cpp`、`tests/unit/StableOctantPartitionTests.cpp`
- **实现要求**：输入批次边界和线程调度不改变输出顺序。
- **验收检查**：点数守恒且重复执行结果一致。
- **测试要求**：输入顺序固定下的批次扰动测试。
- **追踪**：12.2、NFR-TEST-002

### OL-003 实现确定性体素代表点

- **状态**：未开始
- **目标**：每体素按 Morton key/源序号选点并限制 65536。
- **前置任务**：OL-002
- **预计文件**：`src/data/lod/RepresentativeSampler.h`、`src/data/lod/RepresentativeSampler.cpp`、`tests/unit/RepresentativeSamplerTests.cpp`
- **实现要求**：同输入不同线程数结果相同；输出按 Morton key 排序。
- **验收检查**：代表点不超过上限，来自源点，结果确定。
- **测试要求**：密集、重复、超限和顺序扰动测试。
- **追踪**：DDD-010、12.2

### OL-004 实现八叉树递归构建

- **状态**：未开始
- **目标**：按目标 262144、最大 524288、深度 16 构建 Node/Leaf。
- **前置任务**：OL-002, OL-003, dzcpc-cache/DC-006
- **预计文件**：`src/data/lod/OctreeBuilder.h`、`src/data/lod/OctreeBuilder.cpp`、`tests/unit/OctreeBuilderTests.cpp`
- **实现要求**：叶超限时确定性分区；内部节点有代表 Chunk；父子索引连续稳定。
- **验收检查**：所有叶不超最大值、点数守恒、树引用有效。
- **测试要求**：边界规模、极端聚集、取消和确定性测试。
- **追踪**：FR-LOD-001、DDD-010

### OL-005 计算 geometric error

- **状态**：未开始
- **目标**：内部节点使用代表体素对角线，叶为 0。
- **前置任务**：OL-003, OL-004
- **预计文件**：`src/data/lod/GeometricError.h`、`src/data/lod/GeometricError.cpp`、`tests/unit/GeometricErrorTests.cpp`
- **实现要求**：单位与世界坐标一致；结果有限非负。
- **验收检查**：已知 bounds/分辨率误差与公式一致。
- **测试要求**：数学边界和退化节点测试。
- **追踪**：FR-LOD-002、12.2

### OL-006 实现屏幕空间误差

- **状态**：未开始
- **目标**：根据 CameraMatrices/参数计算 projectedErrorPixels。
- **前置任务**：OL-005, camera-abstraction/CA-004
- **预计文件**：`src/data/lod/ScreenSpaceError.h`、`src/data/lod/ScreenSpaceError.cpp`、`tests/unit/ScreenSpaceErrorTests.cpp`
- **实现要求**：细化 2.0 px、回退 1.5 px；distance 使用 nearDistance 下限；不推断相机控制。
- **验收检查**：已知场景误差和阈值分支正确。
- **测试要求**：距离、FOV、viewport、阈值边界测试。
- **追踪**：FR-LOD-002、DDD-010

### OL-007 实现 LOD 选择与滞回

- **状态**：未开始
- **目标**：根据上一帧选择保持 1.5~2.0 px 状态。
- **前置任务**：OL-006
- **预计文件**：`src/data/lod/LodSelector.h`、`src/data/lod/LodSelector.cpp`、`tests/unit/LodSelectorTests.cpp`
- **实现要求**：先视锥体剔除；相同优先级按 NodeId；不闪烁。
- **验收检查**：连续阈值变化的选择序列符合滞回。
- **测试要求**：细化/保持/回退和不可见节点测试。
- **追踪**：FR-LOD-002、12.3

### OL-008 实现渐进祖先回退

- **状态**：未开始
- **目标**：子节点未驻留时保持最近可用祖先。
- **前置任务**：OL-007
- **预计文件**：`src/data/lod/ProgressiveLodResolver.h`、`src/data/lod/ProgressiveLodResolver.cpp`、`tests/unit/ProgressiveLodResolverTests.cpp`
- **实现要求**：细化替换前子节点需可用；回退前祖先需可用；不得产生可覆盖区域空洞。
- **验收检查**：任意缺页组合仍输出可用覆盖或明确无数据状态。
- **测试要求**：逐层加载、部分子节点和回退测试。
- **追踪**：FR-LOD-003、12.4

### OL-009 实现驻留请求优先级

- **状态**：未开始
- **目标**：按可见、误差、中心距离、上一帧、深度、NodeId 排序。
- **前置任务**：OL-007
- **预计文件**：`src/data/lod/ResidencyPriority.h`、`src/data/lod/ResidencyPriority.cpp`、`tests/unit/ResidencyPriorityTests.cpp`
- **实现要求**：排序稳定且可复现；值对象不含 GPU 句柄。
- **验收检查**：人工请求列表顺序符合设计。
- **测试要求**：逐字段优先级和相同值测试。
- **追踪**：12.4/12.5、NFR-TEST-002

### OL-010 实现自动 CPU 预算

- **状态**：未开始
- **目标**：取物理内存 25%，常规限制 1~8 GiB，低内存时降级。
- **前置任务**：project-foundation/PF-006
- **预计文件**：`src/data/lod/CpuBudgetCalculator.h`、`src/data/lod/CpuBudgetCalculator.cpp`、`tests/unit/CpuBudgetTests.cpp`
- **实现要求**：平台内存查询可注入；不得强制分配 1 GiB。
- **验收检查**：不同总量/可用量和用户覆盖结果正确。
- **测试要求**：低内存、边界、覆盖和查询失败测试。
- **追踪**：FR-LOD-005、DDD-013

### OL-011 实现 GPU 预算策略接口

- **状态**：未开始
- **目标**：接受 memory budget 或 heap 信息计算 70%/60%。
- **前置任务**：OL-010
- **预计文件**：`src/data/lod/GpuBudgetCalculator.h`、`src/data/lod/GpuBudgetCalculator.cpp`、`tests/unit/GpuBudgetTests.cpp`
- **实现要求**：具体 Vulkan 查询由后端提供；用户覆盖不得超过安全可用量。
- **验收检查**：扩展可用/不可用/覆盖三种结果正确。
- **测试要求**：表驱动预算测试。
- **追踪**：FR-LOD-004、DDD-013

### OL-012 实现 ResidencyManager 状态和租约

- **状态**：未开始
- **目标**：管理 Metadata/CPU/GPU/Upload/Evict 状态与使用计数。
- **前置任务**：OL-009, OL-010, OL-011
- **预计文件**：`src/data/lod/ResidencyManager.h`、`src/data/lod/ResidencyManager.cpp`、`tests/unit/ResidencyManagerTests.cpp`
- **实现要求**：任务和帧用 RAII Lease；最后同步值未完成不可释放。
- **验收检查**：状态迁移、字节记账和租约保护正确。
- **测试要求**：多租约、乱序完成和非法迁移测试。
- **追踪**：FR-LOD-004/005、12.5

### OL-013 实现预算淘汰

- **状态**：未开始
- **目标**：95% 高水位触发，目标回收到 85%。
- **前置任务**：OL-012
- **预计文件**：`src/data/lod/EvictionPolicy.h`、`src/data/lod/EvictionPolicy.cpp`、`tests/unit/EvictionPolicyTests.cpp`
- **实现要求**：仅不可见、无租约、同步完成资源可淘汰；不足时暂停细化并保留祖先。
- **验收检查**：候选评分与回收目标正确，受保护资源不被选中。
- **测试要求**：预算压力、分配失败和祖先保护测试。
- **追踪**：FR-LOD-004/005、12.7

### OL-014 接入异步读取/上传请求

- **状态**：未开始
- **目标**：把 LOD 请求转换为受背压的 Cache read 和 backend upload。
- **前置任务**：OL-008, OL-012, task-system/TS-008, dzcpc-cache/DC-012
- **预计文件**：`src/data/lod/StreamingScheduler.h`、`src/data/lod/StreamingScheduler.cpp`、`tests/integration/LodStreamingTests.cpp`
- **实现要求**：结果只经完成队列更新；旧 Dataset/过期请求可丢弃；不阻塞帧循环。
- **验收检查**：逐步加载时祖先持续显示，预算内收敛到目标 LOD。
- **测试要求**：Fake Cache/Backend 的渐进、取消和预算集成测试。
- **追踪**：FR-LOD-003~005、FR-VK-007

## 6. 模块级验收

- [ ] 八叉树点数、深度、代表点和确定性测试通过
- [ ] LOD 2.0/1.5 px 滞回及祖先回退无空洞
- [ ] CPU/GPU 预算和淘汰在人工低预算下受控
- [ ] 异步流式调度不阻塞 Engine 帧循环

## 7. 交接记录

- 完成日期：
- 完成人：
- 关键变更：
- 未解决问题：
- 测试命令与结果：
- 关联提交：

## 8. 变更约束

若实现需要改变公共接口、模块依赖、已确认参数或需求行为，必须先更新需求与设计文档，不得在编码任务中自行改变。
