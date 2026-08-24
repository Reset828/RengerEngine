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
- [x] **GC-004 实现临时 run 写读**
- [x] **GC-005 实现确定性 Cell 合并**
- [x] **GC-006 实现超限 Cell 拆分**
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

- **状态**：已完成（2026-08-24）
- **目标**：预算超过阈值时把稳定分桶 run 写入调用方提供的缓存临时目录。
- **前置任务**：GC-003, task-system/TS-008
- **实现文件**：`src/data/chunk/GridRunFile.h`、`src/data/chunk/GridRunFile.cpp`、`tests/unit/GridRunFileTests.cpp`
- **接口**：`GridRunFile::create(directory)` 创建唯一 RAII 临时文件；`write(buckets, token)` 写入一次完整 `GridBucket` 快照；`complete()` 将 pending 文件原子提升为完成 run；`read(token)` 读取完成 run；`path()` 和 `isComplete()` 查询生命周期状态。
- **内部格式**：文件使用 `DZGR` magic、版本号 `1`、桶数量、按 `GridCellKey` 字典序排列的完整桶记录以及固定结束标记；每个桶保存 `GridCellKey`、`AttributeSchema`、positions、colorsRgba8、intensities 和 sourceIndices。格式仅供内部使用，读写均执行长度、边界、schema、流一致性和尾部检查。
- **生命周期与取消**：未完成、写入失败、取消、读取失败或格式损坏时自动删除文件；`complete()` 成功后文件保留供 GC-005 读取；写读接口接收 `dzc::tasks::CancellationToken`，取消统一返回 `ErrorDomain::Task`、错误码 `7`。
- **错误语义**：目录/打开失败返回 `FileIo/1`，读失败返回 `FileIo/2`，写失败返回 `FileIo/3`，格式损坏返回 `DataFormat/2`，非法生命周期调用返回 `Task/1`；内存分配失败返回 `Resource/1`。
- **验收检查**：多 Cell、多属性 run 往返数据一致；源文件 bucket 不被修改；异常后无遗留临时/损坏产物；重复读取结果确定。
- **测试要求**：临时目录往返、RAII 清理、完成文件保留、取消写读、重复完成、目录错误、magic/version 错误、截断和损坏文件测试。
- **追踪**：NFR-PERF-003、9.2

### GC-005 实现确定性 Cell 合并

- **状态**：已完成（2026-08-24）
- **目标**：按 CellKey 排序合并内存桶和临时 run 的读取结果。
- **前置任务**：GC-004
- **实现文件**：`src/data/chunk/GridRunMerger.h`、`src/data/chunk/GridRunMerger.cpp`、`tests/unit/GridRunMergerTests.cpp`
- **接口**：`GridRunMerger::merge(inputs, token)` 接收 `std::vector<std::vector<GridBucket>>`；每个输入组可来自 `GridBucketStore::snapshot()` 或 `GridRunFile::read()`，返回 `Result<std::vector<GridBucket>>`。合并器不接管或删除输入 run 文件。
- **确定性规则**：每个输入组必须按 `GridCellKey` 的 `x → y → z` 顺序严格递增；相同 Cell 的点跨输入组按全局 `sourceIndices` 升序合并，重复 source index 返回 `DataFormat/2`；不依赖输入组、批次边界或 run 分布顺序。
- **属性与 schema**：空输入组和空桶忽略且不固定 schema；所有非空桶必须拥有相同有效 `AttributeSchema`，Position、Color、Intensity 和 `sourceIndices` 按同一排序保持一一对应。
- **错误与取消**：非法 schema、流长度、非有限坐标、CellKey 顺序、非递增 source index、重复 source index 或 checked 计数失败返回 `ErrorDomain::DataFormat/2`；内存不足返回 `Resource/1`；取消返回 `Task/7`。所有检查完成后才构建和返回结果。
- **验收检查**：输出 CellKey 有序、同 Cell source index 全局有序、属性流一致，且同一逻辑输入在不同输入分布下得到相同结果。
- **测试要求**：单 Cell 多输入、多 Cell 排序、输入分布确定性、属性一致性、空输入、非法输入、重复 source index、取消、重复调用和 GridRunFile 往返测试。
- **追踪**：9.2、NFR-TEST-002

### GC-006 实现超限 Cell 拆分

- **状态**：已完成（2026-08-24）
- **目标**：把超过 524288 点的 Cell 确定性拆成子块。
- **前置任务**：GC-005
- **实现文件**：`src/data/chunk/GridCellSplitter.h`、`src/data/chunk/GridCellSplitter.cpp`、`tests/unit/GridCellSplitterTests.cpp`
- **接口**：`GridCellSplitter::split(const GridBucket&, tasks::CancellationToken)` 返回 `Result<std::vector<GridBucket>>`；输入和输出均为单个 Cell 的值语义桶，拆分器不创建新 CellKey。
- **固定参数**：目标点数固定为 262144，最大点数固定为 524288；点数不超过最大值时返回一个独立副本；超限时输出 `ceil(pointCount / 262144)` 个非空子桶。
- **确定性规则**：递归节点选择当前点集最长轴，轴长度相等时固定 `x → y → z`；沿所选轴坐标升序建立空间稳定秩，坐标相等时按 `sourceIndices` 升序；递归按目标子桶数量尽量均衡分配点数。重复坐标、单轴退化和极端分布通过稳定秩正常拆分，不产生空子桶。
- **顺序与属性**：空间稳定秩只决定子桶成员和子桶输出顺序；每个子桶内部按原输入 `sourceIndices` 顺序搬运 Position、Color、Intensity 和 `sourceIndices`，并保留原 CellKey 与 schema。
- **错误与取消**：schema、流长度、source index 顺序、非有限坐标及 checked 算术失败返回 `DataFormat/2`；内存或容器容量不足返回 `Resource/1`；取消返回 `Task/7`；失败时不返回部分结果。
- **验收检查**：所有输出非空且不超过目标/最大点数，点数和属性流守恒、逐点对齐，重复调用结果一致。
- **测试要求**：专项测试覆盖空桶、262144/524288/524289 边界、ceil 子块数量、最长轴与轴平局、重复坐标、非法输入、坐标范围溢出、取消、确定性和属性对齐；构建 `dzc_grid_cell_splitter_tests` 成功，GC-006 专项测试 `1/1` 通过。
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

- 完成日期：2026-08-24（GC-001、GC-002、GC-003、GC-004、GC-005、GC-006）
- 完成人：Codex
- 关键变更：完成 GridParameters cell-size 估算、checked GridCellKey 计算、内存 GridBucketStore、RAII GridRunFile、确定性 GridRunMerger 和超限 Cell 拆分；新增值语义 CellKey、floor 映射、非有限/溢出检查、按 CellKey 排序的内存分桶、稳定源序号、schema 固定、CPU 逻辑载荷预算、完整 GridBucket 临时 run 二进制写读、跨输入组的 Cell 合并，以及固定目标/最大点数的最长轴稳定拆分。
- GC-002 接口：`GridCellKey::fromPosition(position, datasetMinimum, cellSize)` 返回 `Result<GridCellKey>`；错误统一为 `DataFormat/2`。`GridBucketStore` 使用该接口逐点 checked 计算 CellKey。
- GC-003 接口：`GridBucketStore::create(datasetMinimum, cellSize, byteBudget)`、`appendBatch()`、`snapshot()`、`clear()`、`residentBytes()` 和 `pointCount()`；快照返回独立值副本，预算只统计逻辑载荷，拒绝追加不改变状态。错误为非法数据 `DataFormat/2`、预算/零预算 `Resource/1`。
- GC-004 接口：`GridRunFile::create(directory)`、`write(buckets, token)`、`complete()`、`read(token)`、`path()` 和 `isComplete()`；保存完整 `GridBucket` 属性流及 sourceIndices，要求输入桶按 CellKey 字典序排列；pending/损坏/失败/取消文件由 RAII 自动删除，完成文件保留供 GC-005 使用。
- GC-005 接口：`GridRunMerger::merge(inputs, token)` 返回独立 `std::vector<GridBucket>`；输入组可来自内存快照或 `GridRunFile::read()`，非空 schema 必须一致；每个输入组按 CellKey 严格递增校验，同 Cell 按 source index 全局升序合并，重复 source index 返回 `DataFormat/2`，取消返回 `Task/7`，内存不足返回 `Resource/1`；不删除调用方拥有的输入 run。
- GC-006 接口：GridCellSplitter::split(bucket, token) 仅处理单个 GridBucket；不超过 524288 点返回独立副本，超限时按 ceil(pointCount / 262144) 生成非空子桶。递归选择最长轴并以 x → y → z 平局，坐标相同时按 source index 排序；子桶内部保留原 source index 顺序，非法输入为 DataFormat/2，容量不足为 Resource/1，取消为 Task/7。
- 未解决问题：GC-007 至 GC-009 仍需实现 Chunk 构建、稳定 ChunkId、视锥体剔除和可见列表；Grid Chunking 模块继续进行中。
- 测试命令与结果：构建 `dzc_grid_cell_splitter_tests` 成功；`ctest --test-dir build-gc006-nmake -R "^dzc_grid_cell_splitter$" --output-on-failure` 专项测试 `1/1` 通过。已按 `agent.md` 临时复制 355 个运行时 DLL 尝试完整 CTest，测试结束后全部删除；完整 CTest 未完成：该构建目录未生成其他测试可执行文件，尝试全量构建时在既有 `engine_core/Engine.cpp` 编译阶段失败，因此不能记录为全绿。`git diff --check` 已执行。
- 关联提交：无（按要求未创建提交）。
## 8. 变更约束

若实现需要改变公共接口、模块依赖、已确认参数或需求行为，必须先更新需求与设计文档，不得在编码任务中自行改变。
- GC-006 接口：`GridCellSplitter::split(bucket, token)` 只接收单个 `GridBucket`；不超过 524288 点时返回独立副本，超限时按 `ceil(pointCount / 262144)` 输出子桶。使用最长轴递归二分、`x → y → z` 平局规则、坐标后接 source index 的稳定排序；子桶内部保留原 source index 顺序，所有输出保留原 CellKey/schema。非法输入和 checked 算术返回 `DataFormat/2`，容量失败返回 `Resource/1`，取消返回 `Task/7`。
