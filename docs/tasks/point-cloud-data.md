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
- [x] **PD-002 实现 Bounds3d 数学工具**
- [x] **PD-003 实现 PointBatch SoA 校验**
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

- **状态**：已完成（2026-08-20）
- **目标**：实现扩展、有效性、中心、尺寸和退化检查。
- **前置任务**：PD-001
- **实际文件**：`src/data/chunk/Bounds3d.h`、`src/data/chunk/Bounds3d.cpp`、`tests/unit/Bounds3dTests.cpp`、`src/data/CMakeLists.txt`、`tests/unit/CMakeLists.txt`
- **实现结果**：新增 GLM `double` 精度 `Bounds3d` 值类型；默认使用最小值正无穷、最大值负无穷表示空包围盒；支持有限点扩展、有效包围盒合并、中心、尺寸和退化查询。
- **错误语义**：非有限点、无效输入包围盒以及对无效 Bounds 的中心/尺寸查询返回 `ErrorDomain::DataFormat`、错误码 `2`（`CorruptData`），失败时保持目标 Bounds 不变。
- **数值边界**：中心计算使用 `minimum + (maximum - minimum) / 2.0`，覆盖大 GIS 坐标；不新增范围校验、错误域或错误码。
- **验收检查**：常规坐标、大坐标、空包围盒、退化包围盒、NaN/Inf 和值语义测试已覆盖。
- **验证结果**：MSVC 14.51.36231 / NMake Makefiles Debug 全量构建成功；`dzc_bounds3d` 专项测试通过；完整 CTest 37/37 通过；`git diff --check` 通过；任务专用 `build-pd002` 已清理。
- **边界**：未实现 PD-003 至 PD-008、PointBatch、Intensity 量化、Chunk、Dataset 和坐标局部化。
- **追踪**：FR-DATA-006、ADR-008

### PD-003 实现 PointBatch SoA 校验

- **状态**：已完成（2026-08-20）
- **目标**：定义 double Position 和可选 Color/Intensity 流。
- **前置任务**：PD-001, PD-002
- **实际文件**：`src/data/chunk/PointBatch.h`、`src/data/chunk/PointBatch.cpp`、`tests/unit/PointBatchTests.cpp`、`src/data/CMakeLists.txt`、`tests/unit/CMakeLists.txt`
- **实现结果**：在 `namespace dzc` 中定义后端无关的 `PointBatch` 值类型，使用 `std::vector<glm::dvec3>`、`std::vector<std::uint32_t>` 和 `std::vector<std::uint16_t>` 连续保存 Position、RGBA8 Color 和 Intensity SoA 流；`validate()` 按 schema 对其结构进行校验。
- **校验规则**：Position 必须声明；点数以 `positions.size()` 为准且允许为 0；声明的 Color/Intensity 流长度必须等于点数；未声明的 Color/Intensity 流必须为空；未知 schema 位保留并允许。
- **错误语义**：结构错误返回 `ErrorDomain::DataFormat`、错误码 `2`（`CorruptData`），且包含缺少 Position、已声明流长度不匹配或未声明流含数据的诊断信息。
- **边界**：不检查 Position 中的 NaN/Inf；该过滤职责留给后续 Reader。仅存储 RGBA8 逻辑值，不实现通道拆装、序列化或端序处理。
- **验收检查**：有效批次通过，长度错配、缺少 Position 和声明/流不一致均明确失败。
- **测试要求**：assert 风格覆盖全部已知 schema 组合、空批次、短/长流、未声明流、未知位、非有限坐标不影响结构校验，以及错误域、错误码和诊断信息。
- **验证结果**：MSVC 19.51.36246.0 / Visual Studio 18 2026 Debug 全量构建成功；`dzc_point_batch` 专项测试通过；设置 `CMAKE_PREFIX_PATH=D:\vcpkg\vcpkg\installed\x64-windows` 后完整 CTest 38/38 通过；`git diff --check` 通过。
- **追踪**：8.3、NFR-REL-001

### PD-004 实现 intensity 量化器

- **状态**：已完成（2026-08-20）
- **目标**：将有限源标量按实际有效范围线性量化为 `uint16_t`，并保留源声明范围与实际有效范围。
- **前置任务**：PD-003
- **实际文件**：`src/data/chunk/IntensityQuantizer.h`、`src/data/chunk/IntensityQuantizer.cpp`、`tests/unit/IntensityQuantizerTests.cpp`、`src/data/CMakeLists.txt`、`tests/unit/CMakeLists.txt`
- **实现结果**：新增后端无关的 `IntensityQuantizer`、命名的可选 `IntensitySourceRange`、`IntensityQuantizationStatus`（`Normal`、`DegenerateRange`、`NoValidValues`）和带量化结果、`IntensityMetadata`、状态及 `invalidCount` 的结果值类型；`quantize()` 返回 `Result<IntensityQuantizationResult>`。
- **量化语义**：仅有限 `double` 是有效样本；以实际有效范围而非可选声明源范围映射到 `[0, 65535]`，钳制后按最近整数舍入，有限 min/max 精确得到 `0/65535`。NaN 与正负无穷不参与范围统计、输出 `0` 并计入 `invalidCount`。未提供声明范围时 source 范围回退为有效范围；提供时仅原样保存，即使实际值越界仍继续量化。
- **退化/全无效语义**：有效值全相同时返回 `DegenerateRange`，所有输出为 `0`，`available=true` 且有效范围保留相同值；空输入或全无效输入返回 `NoValidValues`，输出等长全 `0`、`available=false`、有效范围为 `0/0`，合法声明源范围仍会保留。
- **错误语义**：声明源范围含 NaN、无穷或 `minimum > maximum` 时，不产生部分结果，返回 `ErrorDomain::DataFormat`、错误码 `2`（`CorruptData`）及明确诊断信息。
- **边界**：不修改 PointBatch、Reader/PCL、Chunk、Dataset、坐标局部化或 GPU 路径；不对声明范围外的有限实际值作拒绝。
- **验收检查**：普通范围的端点映射、最近整数舍入、无效值隔离、退化/全无效状态、元数据回退与声明范围保留均已覆盖。
- **测试要求**：assert 风格覆盖整数、浮点、负值、NaN/正负无穷、退化范围、空输入、全无效输入、声明范围越界和非法声明范围的错误域、错误码及诊断。
- **验证结果**：使用 `D:\vcpkg\vcpkg\installed\x64-windows\share\glm` 的 GLM CMake 包配置，MSVC 19.51.36246.0 / Visual Studio 18 2026 OpenGL-only Debug 全量构建成功；`dzc_intensity_quantizer` 专项测试 1/1 通过；设置 `CMAKE_PREFIX_PATH=D:\vcpkg\vcpkg\installed\x64-windows` 后完整 CTest 39/39 通过；`git diff --check` 已通过。
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

### PD-002（2026-08-20）

- 完成人：Codex
- 关键变更：新增私有 `Bounds3d`，使用 GLM `double` 精度；空值哨兵为 `(+inf, +inf, +inf)` / `(-inf, -inf, -inf)`；新增点/Bounds 扩展、有效性、中心、尺寸和退化判断，并接入 `dzc_data_core` 静态库与 GLM。
- 验证结果：MSVC 14.51.36231 / NMake Makefiles Debug 全量构建成功；`dzc_bounds3d` 专项测试通过；完整 CTest 与 `git diff --check` 已执行；任务专用 `build-pd002` 已清理。
- 未实现边界：未实现 PD-003 至 PD-008、PointBatch、Intensity 量化、Chunk、Dataset 或坐标局部化。
- 后续任务：PD-003 实现 PointBatch SoA 校验；Point Cloud Data 模块仍为“进行中”。
- 关联提交：未提交。



- 完成日期：
- 完成人：
- 关键变更：
- 未解决问题：
- 测试命令与结果：
- 关联提交：

## 8. 变更约束

若实现需要改变公共接口、模块依赖、已确认参数或需求行为，必须先更新需求与设计文档，不得在编码任务中自行改变。
