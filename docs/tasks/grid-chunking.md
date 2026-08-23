# Grid Chunking 任务清单

> 文件：`docs/tasks/grid-chunking.md`  
> 所属阶段：Phase 1  
> 模块状态：进行中
> 前置模块：[point-cloud-data](./point-cloud-data.md)、[point-cloud-io](./point-cloud-io.md)、[task-system](./task-system.md)、[diagnostics](./diagnostics.md)  
> 输入基线：[需求文档](../requirements/spec.md)、[概要设计](../design/architectureDesign.md)、[详细设计](../design/detailDesign.md)、[项目规范](../../agent.md)

## 1. 模块目标

实现 Phase 1 可流式、可取消、受内存约束的空间网格 Chunk 构建以及 Chunk 级视锥体剔除。

## 2. 范围边界

**包含：** 网格参数估算；CellKey；流式分桶；临时 run；确定性拆分；ChunkId；视锥体剔除；可见统计。  
**不包含：** 八叉树 LOD；磁盘 .dzcpc 格式；GPU 上传；Camera 控制行为。

## 3. 完成规则

只有同时满足以下条件，模块才可以在 [progress.md](./progress.md) 中标记完成：

- 本文所有非阻塞必需任务均已勾选；
- 所有自动化测试通过，能力缺失用例只能明确标记为 Skipped，不能伪造通过；
- 对应公共接口和私有实现符合 `agent.md` 的命名、Pimpl、RAII 和依赖边界；
- 相关需求、设计和测试文档已经同步；

## 4. 子任务 Checklist

- [x] **GC-001 实现网格参数估算**
- [x] **GC-002 实现 checked CellKey 计算**
- [x] **GC-003 实现内存分桶器**
- [ ] **GC-004 实现临时 run 写读**
- [ ] **GC-005 实现确定性 Cell 合并**
- [ ] **GC-006 实现超限 Cell 拆分**
- [ ] **GC-007 生成 Chunk 数据和稳定 ID**
- [ ] **GC-008 实现 ViewFrustum 数学剔除**
- [ ] **GC-009 实现 Phase 1 可见列表**

## 5. 子任务说明

### GC-001 实现网格参数估算

- **状态**：已完成（2026-08-23）
- **目标**：根据 bounds、点数和目标 262144 点估算初始立方网格边长。
- **前置任务**：point-cloud-data/PD-002
- **预计文件**：`src/data/chunk/GridParameters.h`、`src/data/chunk/GridParameters.cpp`、`tests/unit/GridParametersTests.cpp`
- **实现要求**：退化 bounds 和未知点数必须有稳定保守结果；使用 double 和 checked 计算。
- **验收检查**：常规、稀疏、退化和大坐标输入得到有限正边长。
- **测试要求**：常规、稀疏、退化、未知点数、零点数、非法 bounds 和溢出测试。
- **估算规则**：已知正点数使用 `cbrt(effectiveVolume * 262144 / pointCount)`；未知或零点数使用最长轴；零尺寸轴替换为最长非零轴；三轴全零时使用 `1.0`；所有中间结果必须保持有限且为正。
- **实现结果**：已提供固定目标点数 262144 的无状态 cell-size 估算；未知/零点数使用最长轴保守值；部分退化轴使用最长非零轴替代；全零 bounds 使用 1.0；非法 bounds、不可表示范围或非有限结果返回 DataFormat/2。
- **追踪**：DDD-007、9.1

### GC-002 实现 checked CellKey 计算

- **状态**：已完成（2026-08-23）
- **目标**：把点坐标映射到 int64 三维网格键。
- **前置任务**：GC-001
- **预计文件**：`src/data/chunk/GridCellKey.h`、`src/data/chunk/GridCellKey.cpp`、`tests/unit/GridCellKeyTests.cpp`
- **接口**：`GridCellKey::fromPosition(position, datasetMinimum, cellSize) -> Result<GridCellKey>`；CellKey 为包含 `int64` 的值类型，提供相等比较和 `x -> y -> z` 字典序比较。
- **映射规则**：每个轴严格计算 `floor((position - datasetMinimum) / cellSize)`；负值遵循数学 floor；最大边界点不压回 bounds 内，允许得到外侧 Cell。
- **错误语义**：点坐标、数据集最小点或边长非有限，边长非正，减法/除法/floor 结果非有限，或结果超出 `int64` 可表示范围时，统一返回 `ErrorDomain::DataFormat`、错误码 `2`。
- **验收检查**：边界点、负坐标、大坐标、非有限输入和计算/转换溢出得到确定结果或明确 Error。
- **测试要求**：专项测试覆盖分割面、负值、大坐标、非法边长、NaN/Inf、减法溢出、int64 范围、比较和确定性。
- **追踪**：FR-VIS-001、9.2
### GC-003 实现内存分桶器

- **状态**：已完成（2026-08-23）
- **目标**：按 checked `GridCellKey` 聚合多个 `PointBatch`，保留稳定全局源序号并跟踪 CPU 逻辑载荷字节。
- **前置任务**：GC-002, point-cloud-data/PD-003
- **实现文件**：`src/data/chunk/GridBucketStore.h`、`src/data/chunk/GridBucketStore.cpp`、`tests/unit/GridBucketStoreTests.cpp`
- **接口**：`GridBucketStore::create(datasetMinimum, cellSize, byteBudget)` 创建值语义分桶器；`appendBatch()` 批量追加；`snapshot()` 返回按 `x → y → z` 排序的独立 `GridBucket` 副本；`clear()` 清空桶、schema、计数并将源序号重置为 `0`。
- **稳定顺序**：首个非空批次固定 `AttributeSchema`，后续批次必须完全一致；源序号按追加顺序从 `0` 递增，被拒绝批次和合法空批次均不消耗源序号；同 Cell 内点、属性流和 `sourceIndices` 保持对应及稳定顺序。
- **预算与错误语义**：`residentBytes()` 只统计 positions、颜色、强度和 sourceIndices 的实际逻辑载荷；追加前执行 checked 字节乘加，正好达到预算允许，超预算或字节不可表示返回 `ErrorDomain::Resource/1`。非法配置、非法批次、非有限坐标、schema 不一致或 CellKey 计算失败返回 `ErrorDomain::DataFormat/2`；零预算创建返回 `ErrorDomain::Resource/1`。
- **验收检查**：批次跨界追加后点数、属性、CellKey 字典序和源序号正确，失败追加保持已有状态不变。
- **测试要求**：已覆盖合法/非法配置、单批次多 Cell、多批次稳定顺序、多属性、空批次、非法批次、非有限点、schema 不一致、预算边界、快照副本、clear 重置和确定性。
- **追踪**：NFR-PERF-001、9.2
### GC-004 实现临时 run 写读

- **状态**：未开始
- **目标**：预算超过阈值时把稳定分桶 run 写入缓存临时目录。
- **前置任务**：GC-003, task-system/TS-008
- **预计文件**：`src/data/chunk/GridRunFile.h`、`src/data/chunk/GridRunFile.cpp`、`tests/unit/GridRunFileTests.cpp`
- **实现要求**：临时格式仅内部使用；RAII 文件；取消/失败删除；不修改源文件。
- **验收检查**：run 往返数据一致，异常后无遗留有效产物。
- **测试要求**：临时目录往返、截断、取消和写失败测试。
- **追踪**：NFR-PERF-003、9.2

### GC-005 实现确定性 Cell 合并

- **状态**：未开始
- **目标**：按 CellKey 排序合并内存桶和临时 run。
- **前置任务**：GC-004
- **预计文件**：`src/data/chunk/GridRunMerger.h`、`src/data/chunk/GridRunMerger.cpp`、`tests/unit/GridRunMergerTests.cpp`
- **实现要求**：相同输入不同批次边界产生相同输出顺序。
- **验收检查**：输出 CellKey 有序且属性流一致。
- **测试要求**：改变批次大小和 run 分布验证确定性。
- **追踪**：9.2、NFR-TEST-002

### GC-006 实现超限 Cell 拆分

- **状态**：未开始
- **目标**：把超过 524288 点的 Cell 确定性拆成子块。
- **前置任务**：GC-005
- **预计文件**：`src/data/chunk/GridCellSplitter.h`、`src/data/chunk/GridCellSplitter.cpp`、`tests/unit/GridCellSplitterTests.cpp`
- **实现要求**：目标 262144、最大 524288；按最长轴/稳定规则拆分；不得产生超限块。
- **验收检查**：所有输出非空且不超最大值，点总数守恒。
- **测试要求**：最大值边界、重复坐标和极端分布测试。
- **追踪**：DDD-007、9.2

### GC-007 生成 Chunk 数据和稳定 ID

- **状态**：未开始
- **目标**：局部化每个分区并生成 CellKey+子块序号的 ChunkId。
- **前置任务**：GC-006, point-cloud-data/PD-006
- **预计文件**：`src/data/chunk/GridChunkBuilder.h`、`src/data/chunk/GridChunkBuilder.cpp`、`tests/unit/GridChunkBuilderTests.cpp`
- **实现要求**：Chunk bounds/origin/schema 正确；ID 在同一 Dataset 构建中稳定。
- **验收检查**：多次构建 ID、点数和局部坐标一致。
- **测试要求**：确定性、取消和属性缺失测试。
- **追踪**：FR-VIS-001、DDD-007

### GC-008 实现 ViewFrustum 数学剔除

- **状态**：未开始
- **目标**：提供 AABB 与六平面相交测试及上一帧剔除平面提示。
- **前置任务**：camera-abstraction/CA-002, point-cloud-data/PD-002
- **预计文件**：`src/scene/FrustumCulling.h`、`src/scene/FrustumCulling.cpp`、`tests/unit/FrustumCullingTests.cpp`
- **实现要求**：只使用 Camera 提供的 ViewFrustum；不决定相机模型。
- **验收检查**：内部、外部和相交 Chunk 分类正确。
- **测试要求**：轴对齐、旋转平面、退化 box 和大坐标测试。
- **追踪**：FR-VIS-002、9.3

### GC-009 实现 Phase 1 可见列表

- **状态**：未开始
- **目标**：对 Dataset Chunk 执行剔除并输出 DrawChunk 和统计。
- **前置任务**：GC-007, GC-008, engine-core/EC-009
- **预计文件**：`src/scene/GridVisibilitySelector.h`、`src/scene/GridVisibilitySelector.cpp`、`tests/integration/GridVisibilityTests.cpp`
- **实现要求**：不可见块不得进入 draw 列表；统计总点、可见点和可见块。
- **验收检查**：固定 Frustum 的输出 ID 和计数符合预期。
- **测试要求**：小型人工场景集成测试。
- **追踪**：FR-VIS-002/003、AC-P1-008

## 6. 模块级验收

- [ ] Chunk 目标/最大点数和点数守恒测试通过
- [ ] 大数据构建路径支持临时 run、取消和背压
- [ ] 相同输入在批次变化下得到相同 ChunkId
- [ ] 不可见 Chunk 不进入绘制列表且统计正确

## 7. 交接记录

- 完成日期：2026-08-23（GC-001、GC-002、GC-003）
- 完成人：Codex
- 关键变更：完成 GridParameters cell-size 估算、checked GridCellKey 计算和内存 GridBucketStore；新增值语义 CellKey、floor 映射、非有限/溢出检查、按 CellKey 排序的内存分桶、稳定源序号、schema 固定、CPU 逻辑载荷预算、单元测试和 CMake 接入；GC-004 至 GC-009 尚未实现。
- GC-002 接口：`GridCellKey::fromPosition(position, datasetMinimum, cellSize)` 返回 `Result<GridCellKey>`；错误统一为 `DataFormat/2`。`GridBucketStore` 使用该接口逐点 checked 计算 CellKey。
- GC-003 接口：`GridBucketStore::create(datasetMinimum, cellSize, byteBudget)`、`appendBatch()`、`snapshot()`、`clear()`、`residentBytes()` 和 `pointCount()`；快照返回独立值副本，预算只统计逻辑载荷，拒绝追加不改变状态。错误为非法数据 `DataFormat/2`、预算/零预算 `Resource/1`。
- 未解决问题：后续任务仍需定义分桶、run、拆分、Chunk 构建和可见性行为。
- 测试命令与结果：GC-002 专项测试 `1/1` 通过；将 Debug PCL/VTK/OpenNI2 及 MSVC/UCRT 运行时 DLL 临时复制到测试目录后，完整 CTest `59/59` 通过；GC-003 使用 `build-gc003-nmake` 构建 `dzc_grid_bucket_store_tests` 成功，`ctest --test-dir build-gc003-nmake -R '^dzc_grid_bucket_store$' --output-on-failure` 专项测试 `1/1` 通过；随后临时复制 355 个 PCL/VTK/OpenNI2/vcpkg DLL 后完整 CTest `60/60` 通过，测试结束后已删除全部临时 DLL。
- 关联提交：无（按要求未创建提交）。
## 8. 变更约束

若实现需要改变公共接口、模块依赖、已确认参数或需求行为，必须先更新需求与设计文档，不得在编码任务中自行改变。
