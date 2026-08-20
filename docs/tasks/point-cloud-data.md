# Point Cloud Data 任务清单

> 文件：`docs/tasks/point-cloud-data.md`  
> 所属阶段：公共基础  
> 模块状态：进行中
> 前置模块：[project-foundation](./project-foundation.md)、[engine-core](./engine-core.md)  
> 输入基线：[需求文档](../requirements/spec.md)、[概要设计](../design/architectureDesign.md)、[详细设计](../design/detailDesign.md)、[项目规范](../../agent.md)

## 1. 模块目标

实现后端无关的点云属性、批次、Chunk、Dataset、包围盒和大坐标局部化数据模型。

## 2. 范围边界

**包含：** AttributeSchema；IntensityMetadata；Bounds3d；PointBatch；ChunkCpuData/Metadata；Dataset 元数据；坐标局部化；Chunk 状态。  
**不包含：** PCL 读取；网格/八叉树构建；磁盘缓存；GPU Buffer。

## 3. 完成规则

只有同时满足以下条件，模块才可以在 [progress.md](./progress.md) 中标记完成：

- 本文所有非阻塞必需任务均已勾选；
- 所有自动化测试通过，能力缺失用例只能明确标记为 Skipped，不能伪造通过；
- 对应公共接口和私有实现符合 `agent.md` 的命名、Pimpl、RAII 和依赖边界；
- 相关需求、设计和测试文档已经同步；

## 4. 子任务 Checklist

- [x] **PD-001 实现属性模式和 intensity 元数据**
- [ ] **PD-002 实现 Bounds3d 数学工具**
- [ ] **PD-003 实现 PointBatch SoA 校验**
- [ ] **PD-004 实现 intensity 量化器**
- [ ] **PD-005 实现 Chunk 局部坐标转换**
- [ ] **PD-006 实现 Chunk 元数据和状态机**
- [ ] **PD-007 实现 Dataset 元数据容器**
- [ ] **PD-008 实现相机相对原点计算工具**

## 5. 子任务说明

### PD-001 实现属性模式和 intensity 元数据

- **状态**：已完成（2026-08-20）
- **目标**：定义 Position/Color/Intensity 位掩码、属性存在性查询和 Intensity 范围元数据。
- **前置任务**：project-foundation/PF-004
- **实际文件**：`src/data/chunk/PointAttributes.h`、`tests/unit/PointAttributesTests.cpp`、`tests/unit/CMakeLists.txt`
- **实现结果**：在 `namespace dzc` 中定义 `PointAttribute`、`AttributeSchema` 和 `IntensityMetadata`。Schema 默认掩码为 0，查询函数只检查三个已定义属性位；未知位不会影响已定义属性查询。
- **边界**：Position 必需性由后续 Reader/PointBatch 校验负责；本任务不新增 `isValid()`、位运算符或范围校验。Intensity 的 `uint16_t` 存储、无效值统计和线性量化留给 PD-004。
- **验收检查**：默认值、单属性、组合属性、缺失属性、未知位和 Intensity 元数据值语义测试已覆盖。
- **测试要求**：使用 assert 风格验证固定底层类型、掩码查询、默认元数据及复制/移动。
- **追踪**：FR-REN-003、DDD-009

### PD-002 实现 Bounds3d 数学工具

- **状态**：未开始
- **目标**：实现扩展、有效性、中心、尺寸和退化检查。
- **前置任务**：PD-001
- **预计文件**：`src/data/chunk/Bounds3d.h`、`src/data/chunk/Bounds3d.cpp`、`tests/unit/Bounds3dTests.cpp`
- **实现要求**：只使用 GLM；double 精度；忽略或报告非有限输入。
- **验收检查**：大 GIS 坐标和退化包围盒计算正确。
- **测试要求**：常规、大坐标、NaN/Inf 和空包围盒测试。
- **追踪**：FR-DATA-006、ADR-008

### PD-003 实现 PointBatch SoA 校验

- **状态**：未开始
- **目标**：定义 double Position 和可选 Color/Intensity 流。
- **前置任务**：PD-001, PD-002
- **预计文件**：`src/data/chunk/PointBatch.h`、`src/data/chunk/PointBatch.cpp`、`tests/unit/PointBatchTests.cpp`
- **实现要求**：存在的属性流长度必须等于点数；颜色逻辑 RGBA8。
- **验收检查**：有效批次通过，长度错配明确失败。
- **测试要求**：各种 schema 与流长度组合测试。
- **追踪**：8.3、NFR-REL-001

### PD-004 实现 intensity 量化器

- **状态**：未开始
- **目标**：将有效源标量线性量化为 uint16 并保留范围。
- **前置任务**：PD-003
- **预计文件**：`src/data/chunk/IntensityQuantizer.h`、`src/data/chunk/IntensityQuantizer.cpp`、`tests/unit/IntensityQuantizerTests.cpp`
- **实现要求**：无效值不参与范围；退化范围行为固定且可诊断。
- **验收检查**：min/max 映射到 0/65535，中间值误差可控。
- **测试要求**：整数、浮点、负值、退化、NaN 测试。
- **追踪**：DDD-009、10.2

### PD-005 实现 Chunk 局部坐标转换

- **状态**：未开始
- **目标**：从 double 源坐标生成 Chunk origin 和 float3 local positions。
- **前置任务**：PD-002, PD-003
- **预计文件**：`src/data/chunk/CoordinateLocalizer.h`、`src/data/chunk/CoordinateLocalizer.cpp`、`tests/unit/CoordinateLocalizerTests.cpp`
- **实现要求**：Chunk origin 取包围盒中心；所有转换检查 float 有限性。
- **验收检查**：重构位置误差处于 float 局部坐标可接受范围。
- **测试要求**：亿级坐标、很小局部范围和溢出测试。
- **追踪**：FR-DATA-006、8.2

### PD-006 实现 Chunk 元数据和状态机

- **状态**：未开始
- **目标**：定义 ChunkMetadata、ChunkCpuData 和允许状态迁移。
- **前置任务**：PD-005
- **预计文件**：`src/data/chunk/Chunk.h`、`src/data/chunk/Chunk.cpp`、`tests/unit/ChunkStateTests.cpp`
- **实现要求**：逻辑 Chunk 不含 GPU 句柄；ChunkId 稳定；非法迁移失败。
- **验收检查**：Metadata→CPU→Upload→GPU→Evict 路径及错误路径正确。
- **测试要求**：状态表和 SoA 数据一致性测试。
- **追踪**：FR-VIS-001、7.4

### PD-007 实现 Dataset 元数据容器

- **状态**：未开始
- **目标**：保存源身份、schema、范围、bounds、origin 和 Chunk 索引。
- **前置任务**：PD-006
- **预计文件**：`src/data/chunk/Dataset.h`、`src/data/chunk/Dataset.cpp`、`tests/unit/DatasetTests.cpp`
- **实现要求**：Dataset 拥有逻辑数据，不拥有后端资源；只暴露稳定查询。
- **验收检查**：统计和 Chunk 查找正确；不同 DatasetId 隔离。
- **测试要求**：创建、查询、空集、重复 ID 测试。
- **追踪**：FR-DATA-005、8.4

### PD-008 实现相机相对原点计算工具

- **状态**：未开始
- **目标**：根据 double camera origin 生成 float relative chunk origin。
- **前置任务**：PD-005
- **预计文件**：`src/data/chunk/RelativeOrigin.h`、`src/data/chunk/RelativeOrigin.cpp`、`tests/unit/RelativeOriginTests.cpp`
- **实现要求**：只实现坐标数学，不选择 Camera 控制行为。
- **验收检查**：大坐标下 relative origin 有限且重构一致。
- **测试要求**：大距离、近距离和越界测试。
- **追踪**：ADR-008、19.2

## 6. 模块级验收

- [ ] 属性流和 SoA 一致性测试通过
- [ ] 大坐标局部化测试通过
- [ ] Chunk/Dataset 不含任何 GPU 或 PCL 类型
- [ ] intensity uint16 量化与范围元数据符合设计

## 7. 交接记录

### PD-001（2026-08-20）

- 完成人：Codex
- 关键变更：新增私有 `PointAttribute` 位掩码、`AttributeSchema` 查询值类型和 `IntensityMetadata` 元数据值类型；新增 `dzc_point_attributes` CTest。
- 验证结果：MSVC 14.44 / NMake Makefiles Debug 全量构建成功；`dzc_point_attributes` 专项测试通过；完整 CTest 36/36 通过；`git diff --check` 通过；任务专用 `build-pd001` 已清理。
- 未实现边界：未实现 PD-002 至 PD-008、Intensity 量化、PointBatch、Bounds3d、Chunk 或 Dataset 容器。
- 后续任务：PD-002 实现 Bounds3d 数学工具；Point Cloud Data 模块仍为“进行中”。
- 关联提交：未提交。



- 完成日期：
- 完成人：
- 关键变更：
- 未解决问题：
- 测试命令与结果：
- 关联提交：

## 8. 变更约束

若实现需要改变公共接口、模块依赖、已确认参数或需求行为，必须先更新需求与设计文档，不得在编码任务中自行改变。
