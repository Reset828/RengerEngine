# Point Cloud I/O 任务清单

> 文件：`docs/tasks/point-cloud-io.md`  
> 所属阶段：Phase 1  
> 模块状态：进行中（IO-001 至 IO-008 已完成；IO-009 与模块级验收未完成）
> 前置模块：[project-foundation](./project-foundation.md)、[task-system](./task-system.md)、[point-cloud-data](./point-cloud-data.md)、[diagnostics](./diagnostics.md)  
> 输入基线：[需求文档](../requirements/spec.md)、[概要设计](../design/architectureDesign.md)、[详细设计](../design/detailDesign.md)、[项目规范](../../agent.md)

## 1. 模块目标

实现 PCD/PLY 流式 Reader、字段转换、错误隔离以及 PCL 依赖封装。

## 2. 范围边界

**包含：** IPointCloudReader；Reader Registry；PCL PCD/PLY 适配；字段映射；批次读取；源文件校验；取消。  
**不包含：** 分块算法；GPU 上传；网络下载；其他点云格式。

## 3. 完成规则

只有同时满足以下条件，模块才可以在 [progress.md](./progress.md) 中标记完成：

- 本文所有非阻塞必需任务均已勾选；
- 所有自动化测试通过，能力缺失用例只能明确标记为 Skipped，不能伪造通过；
- 对应公共接口和私有实现符合 `agent.md` 的命名、Pimpl、RAII 和依赖边界；
- 相关需求、设计和测试文档已经同步；

## 4. 子任务 Checklist

- [x] **IO-001 定义 Reader 公共内部契约**
- [x] **IO-002 实现扩展名 Reader Registry**
- [x] **IO-003 建立 PCL 私有 Target**
- [x] **IO-004 实现 PCD 元数据打开**
- [x] **IO-005 实现 PCD 批次转换**
- [x] **IO-006 实现 PLY 元数据打开**
- [x] **IO-007 实现 PLY 批次转换**
- [x] **IO-008 接入 I/O 并发与背压**
- [ ] **IO-009 实现 Reader 错误与进度转换**

## 5. 子任务说明

### IO-001 定义 Reader 公共内部契约

- **状态**：已完成（2026-08-21）
- **目标**：实现 open/readNext/close 和 PointCloudSourceInfo。
- **前置任务**：point-cloud-data/PD-003, task-system/TS-001
- **实际文件**：`src/data/io/IPointCloudReader.h`、`src/data/io/PointCloudSourceInfo.h`、`tests/unit/ReaderContractTests.cpp`、`tests/unit/CMakeLists.txt`
- **实现结果**：在 `namespace dzc` 中定义仅使用标准类型、既有数据值类型和 GLM（经 `PointBatch`）的 Reader 抽象及源元数据值类型；不引入 PCL、格式探测或工厂接口。Reader 不可复制、不可移动；后续工厂可用 `std::unique_ptr<IPointCloudReader>` 管理具体实现。
- **生命周期与错误语义**：成功 `open()` 后才允许读取；重复 `open()`、未打开读取和关闭后读取使用 `ErrorDomain::Task`、`TaskErrorCode::InvalidTask`（1）。成功空 `optional` 表示 EOF，EOF 后可重复读取空值；`close()` 幂等且关闭后可重新打开。零 `maximumPoints` 使用 `ErrorDomain::Configuration`、错误码 1（`InvalidValue`），不推进读取；取消使用 `ErrorDomain::Task`、`TaskErrorCode::Cancelled`（7），不产生新批次。
- **验收检查**：Fake Reader 可按最大点数读取并响应取消；每个返回批次通过 `PointBatch::validate()`。
- **测试结果**：2026-08-21 以 MSVC 19.51.36246.0、NMake Makefiles、OpenGL-only Debug 配置完成全量构建；`dzc_reader_contract` 专项测试 1/1 通过，完整 CTest 50/50 通过；`git diff --check` 通过。
- **未解决问题**：真实 PCD/PLY 读取、Registry、PCL 隔离、字段映射、进度、并发与背压留给 IO-002 至 IO-009。
- **追踪**：FR-DATA-001、10.1

### IO-002 实现扩展名 Reader Registry

- **状态**：已完成（2026-08-21）
- **目标**：按大小写无关的 .pcd/.ply 选择工厂。
- **前置任务**：IO-001
- **实际文件**：`src/data/io/PointCloudReaderRegistry.h`、`src/data/io/PointCloudReaderRegistry.cpp`、`src/data/CMakeLists.txt`、`tests/unit/ReaderRegistryTests.cpp`、`tests/unit/CMakeLists.txt`
- **最终接口**：定义 `PointCloudReaderCreator = std::function<std::unique_ptr<IPointCloudReader>()>`；`PointCloudReaderRegistry` 构造时固定注入 PCD 与 PLY Creator，`create(const std::string&) const` 返回 `Result<std::unique_ptr<IPointCloudReader>>`。Registry 为不可复制、可移动的 Pimpl 类型；移动后源对象返回 Internal/1，移动目标保留 Creator。
- **路由与错误边界**：仅以 `std::filesystem::path(path).extension()` 取得最后扩展名并作 ASCII 大小写无关 `.pcd`/`.ply` 匹配；不访问磁盘、不检查文件状态、不按内容探测格式。空路径、无扩展名、未知扩展名和 `.pcd`/`.ply` 隐藏式文件名返回 `ErrorDomain::DataFormat`、错误码 2（`CorruptData`），且不调用 Creator。匹配 Creator 为空、返回空 Reader、抛出标准/未知异常或 Registry 已移动时返回 `ErrorDomain::Internal`、错误码 1；异常文本仅保留在诊断信息。
- **验收检查**：Fake Creator 覆盖 PCD/PLY、混合大小写、含点目录、多点文件名和不存在但扩展名合法的路径；也覆盖所有约定失败路径及移动语义。
- **测试结果**：2026-08-21 使用 MSVC 19.51.36246.0、NMake Makefiles、OpenGL-only Debug 配置完成干净全量构建；`ctest --test-dir build-io002-clean -R "^dzc_reader_registry$" --output-on-failure` 为 1/1 通过，`ctest --test-dir build-io002-clean --output-on-failure` 为 51/51 通过；`git diff --check` 通过。
- **未解决问题**：真实 PCL Creator、PCD/PLY 文件打开及字段/批次转换、I/O 并发与背压、进度和错误转换留给 IO-003 至 IO-009。
- **追踪**：FR-DATA-001

### IO-003 建立 PCL 私有 Target

- **状态**：已完成（2026-08-21）
- **目标**：建立真实 `dzc_data_pcl` STATIC 实现 Target，并将 PCL 头、编译属性和链接边界限制在其中。
- **前置任务**：project-foundation/PF-003
- **实际文件**：`src/data/CMakeLists.txt`、`src/data/io/pcl/CMakeLists.txt`、`src/data/io/pcl/PclTargetAnchor.cpp`、`src/CMakeLists.txt`、`CTestConfig.cmake`、`tests/cmake/ConfigureSmoke.cmake`、`tests/cmake/TargetBoundaryCheck.cmake`、`docs/design/detailDesign.md`、`agent.md`
- **实现结果**：`src/data/io/pcl/CMakeLists.txt` 使用 `find_package(PCL CONFIG REQUIRED COMPONENTS io)` 创建 `dzc_data_pcl`，C++17 编译；PCL include directories、compile definitions 与 `pcl_io` 均为 `PRIVATE`，而 `dzc_data_core` 是其 `PUBLIC` 项目依赖。锚定源文件只包含 `<pcl/io/pcd_io.h>` 并引用 `pcl::PCDReader`，不提供 Reader、Creator/Factory 或运行时 I/O。
- **依赖与配置边界**：PCL 1.15.1 的 `io` 组件由外部 `-DPCL_DIR=...` 注入；未提供可用 PCL CONFIG 时配置失败，项目 CMake 不硬编码本机 PCL 路径。Target manifest 与边界检查验证 `dzc_data_pcl` 直接链接 `pcl_io` 并保留 `dzc_data_core` 依赖，其他项目 Target 不得具有 PCL 依赖；CTest 的嵌套配置会转发外层 `PCL_DIR`。
- **验收检查**：`dzc_data_pcl` 可在不向公共头或非 PCL Target 泄露 PCL 类型的前提下配置、编译并链接 PCL I/O。
- **测试结果**：2026-08-21 使用 MSVC 19.51.36246.0、NMake Makefiles、PCL 1.15.1（`io` 组件）、OpenGL-only Debug 干净配置完成全量构建；`cmake --build build-io003-clean --target dzc_data_pcl` 通过，`dzc_target_boundary` 1/1 通过，`dzc_reader_contract` 与 `dzc_reader_registry` 2/2 通过，完整 `ctest --test-dir build-io003-clean --output-on-failure` 为 51/51 通过；公共头及非 PCL Target 源码的 PCL 扫描、`git diff --check` 均通过。
- **未解决问题**：真实 PCD 批次转换、PLY Reader、Creator/Factory、字段转换、I/O 并发与背压、进度和错误转换留给 IO-005 至 IO-009。
- **追踪**：ADR-003、PCL 仅 I/O
### IO-004 实现 PCD 元数据打开

- **状态**：已完成（2026-08-21）
- **目标**：使用 PCL 只读取 Header 的字段、声明点数和基础校验；不读取或转换批次。
- **前置任务**：IO-003, IO-001
- **实际文件**：`src/data/io/pcl/PcdReader.h`、`src/data/io/pcl/PcdReader.cpp`、`src/data/io/pcl/CMakeLists.txt`、`CMakeLists.txt`、`tests/integration/CMakeLists.txt`、`tests/integration/PcdReaderTests.cpp`、`CTestConfig.cmake`、`tests/cmake/ConfigureSmoke.cmake`
- **实现结果**：`PcdReader final` 以 Pimpl 封装 `pcl::PCDReader`、`pcl::PCLPointCloud2` 和打开状态；公共头不暴露 PCL 类型。`open()` 调用 `PCDReader::readHeader()`，安全计算 `width × height`，允许 0 点；仅接受精确小写且各唯一的 `x/y/z` 数值标量字段（`COUNT == 1`）。成功 schema 始终有 Position，精确 `rgb`/`rgba` 与 `intensity` 分别附加 Color/Intensity；Bounds 和 intensity metadata 保持默认值。文件、Header、字段、溢出和 PCL 异常统一为 `DataFormat/2`；重复 open 为 `Task/1`。IO-005 前已打开的 `readNext()` 依次处理未打开 `Task/1`、零上限 `Configuration/1`、取消 `Task/7`，其余返回 `Internal/1`，不伪造 EOF；`close()` 幂等且可重开。
- **集成测试**：RAII 临时目录生成 ASCII XYZ、ASCII XYZ/rgb/intensity、binary XYZ/rgba、0 点和损坏 fixture；覆盖 schema、声明点数、默认 metadata、源文件字节不变、缺失/重复/大小写不符/无效 XYZ、生命周期及 `readNext()` 优先级。测试仅通过 `PcdReader.h` 使用 Reader，不直接包含 PCL。
- **验证结果**：在 OpenGL-only Debug / NMake Makefiles 的干净配置中，全量构建通过；`dzc_pcd_reader` 1/1、`dzc_reader_contract` 与 `dzc_reader_registry` 2/2、`dzc_target_boundary` 1/1 均通过，完整 CTest 为 52/52 通过。PCL Debug 运行时测试通过前，将 `D:\PCL\PCL 1.15.1\bin`、`D:\PCL\PCL 1.15.1\3rdParty\VTK\bin` 和 `C:\Program Files\OpenNI2\Redist` 临时置于 PATH 前部；OpenNI2 2.2 由 PCL 随附安装包安装。配置冒烟测试现同时转发外层 `PCL_DIR` 与 `CMAKE_PREFIX_PATH`，避免嵌套 CMake 丢失 GLM 包路径。
- **追踪**：FR-DATA-001、FR-DATA-005、NFR-REL-001
### IO-005 实现 PCD 批次转换

- **状态**：已完成（2026-08-22）
- **目标**：将 PCL 数据转换为 PointBatch 并过滤无效坐标。
- **前置任务**：IO-004, point-cloud-data/PD-004
- **实际文件**：`src/data/io/pcl/PcdReader.cpp`、`src/data/io/pcl/CMakeLists.txt`、`tests/integration/CMakeLists.txt`、`tests/integration/PcdReaderTests.cpp`、`tests/integration/PcdBatchTests.cpp`、`docs/design/detailDesign.md`、`docs/tasks/point-cloud-io.md`、`docs/tasks/progress.md`、`agent.md`
- **实现结果**：`open()` 保持 Header-only，并只接受 ASCII/Binary PCD；首次有效 `readNext()` 以 PCL 全量读取数据体后私有转换为 SoA，再按上限返回 `PointBatch`。严格校验读体后的点数、记录步长、字段偏移和数据长度；格式或转换失败统一为稳定 `DataFormat/2`，直到 `close()`。XYZ 数值标量转换为 double，NaN/Inf 坐标跳过；非空文件若全部无效失败，0 点直接 EOF。`rgba` 优先 `rgb`，合法 packed FLOAT32/UINT32 颜色输出 `0xRRGGBBAA`，rgb 补 alpha 255；intensity 对所有有效坐标点按统一全文件范围量化，非有限值写 0。`readNext()` 保持 Task/1、Configuration/1、Task/7 的既定优先级；取消不产生部分批次且可用未取消 Token 从首批重试。`open()` 返回的 intensity metadata 仍保持默认值。
- **集成测试**：新增无 PCL include 的 `dzc_pcd_batch`，覆盖 ASCII 跨批次/EOF、Binary rgb+rgba 优先级和全文件 intensity、NaN/Inf 过滤、全无效、空云、截断数据、无效可选字段、压缩 PCD 拒绝、取消/重开以及源文件不修改；更新 `dzc_pcd_reader` 的 IO-004 回归读取断言。
- **验证结果**：2026-08-22 使用 MSVC 19.51.36246.0、NMake Makefiles、OpenGL-only Debug、PCL 1.15.1/io 和显式 `CMAKE_MAKE_PROGRAM=D:\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.44.35207\bin\HostX64\x64\nmake.exe` 完成干净全量构建。PCL/VTK/OpenNI2 DLL 路径仅在测试进程 PATH 前置；`dzc_pcd_reader`、`dzc_pcd_batch`、Reader 回归和 Target 边界专项为 5/5 通过，完整 CTest 为 53/53 通过；`git diff --check` 通过。
- **未解决问题**：PLY 批次转换（IO-007）、Creator/Factory、I/O 并发/背压（IO-008）、进度与错误事件转换（IO-009）及模块级验收仍未完成。
- **追踪**：10.2、FR-DATA-003/004
### IO-006 实现 PLY 元数据打开

- **状态**：已完成（2026-08-22）
- **目标**：使用 PCL 读取标准 PLY 字段并校验。
- **前置任务**：IO-003, IO-001
- **实际文件**：`src/data/io/pcl/PlyReader.h`、`src/data/io/pcl/PlyReader.cpp`、`src/data/io/pcl/CMakeLists.txt`、`tests/integration/PlyReaderTests.cpp`、`tests/integration/CMakeLists.txt`、`docs/design/detailDesign.md`、`docs/tasks/progress.md`、`上下文.md`
- **实现结果**：新增 Pimpl 封装的 Header-only `PlyReader`。`open()` 调用 `pcl::PLYReader::readHeader()`，支持 ASCII 与 binary_little_endian，仅读取头部并保持源文件不变；同时扫描至 `end_header` 的原始声明，严格校验唯一数值标量 x/y/z，完整 uint8 red/green/blue 与可选 uint8 alpha，以及唯一数值标量 intensity。未知顶点标量属性和非顶点元素不进入 schema；PCL 归并后的 `rgb`/`rgba` 仅在原始组件校验成功后映射为 Color。声明点数安全计算，Bounds 和 IntensityMetadata 保持默认值；批次读取留给 IO-007。
- **错误与生命周期**：文件、PLY 解析、格式、字段、PCL 或点数溢出错误统一为 `ErrorDomain::DataFormat`、错误码 2；重复 `open()` 返回 `Task/1`；`close()` 幂等并支持重新打开；IO-007 前 `readNext()` 的过渡 `Internal/1` 行为已由 IO-007 的真实批次转换替换。
- **集成测试**：新增无 PCL include 的 `dzc_ply_reader`，覆盖 ASCII XYZ、ASCII RGB/intensity/未知属性/face、binary_little_endian RGBA、零点、损坏/缺坐标/重复/大小写/部分颜色/非法类型/不支持格式、生命周期和源文件不修改。
- **验证结果**：2026-08-22 使用 MSVC 19.51.36246.0、NMake Makefiles、OpenGL-only Debug、PCL 1.15.1/io 完成 `build-io006-clean` 干净全量构建；设置测试进程 PATH 为 PCL bin、VTK bin 和 OpenNI2 Redist 后，`dzc_ply_reader` 及 Reader/PCL 专项通过；完整 CTest 与隔离扫描待最终验证。
- **追踪**：FR-DATA-001、10.2

### IO-007 实现 PLY 批次转换

- **状态**：已完成（2026-08-22）
- **目标**：输出规范化 `PointBatch` 并支持取消。
- **前置任务**：IO-006, point-cloud-data/PD-004
- **实际文件**：`src/data/io/pcl/PlyReader.cpp`、`tests/integration/PlyReaderTests.cpp`、`tests/integration/PlyBatchTests.cpp`、`tests/integration/CMakeLists.txt`、`docs/design/detailDesign.md`、`docs/tasks/point-cloud-io.md`、`docs/tasks/progress.md`、`上下文.md`
- **实现结果**：`PlyReader` 在首次有效 `readNext()` 惰性调用 `pcl::PLYReader::read()`，私有转换并缓存 SoA，随后按 `maximumPoints` 返回 `PointBatch`；公共接口和公共头不暴露 PCL。读取后严格对照 `open()` 的原始 PLY Header 语义和声明点数，校验 PCL 输出的字段、步长、偏移及数据长度。XYZ 转为 `glm::dvec3` 并跳过 NaN/Inf；非空云全部坐标无效时返回稳定 `DataFormat/2`。已预校验的 RGB/RGBA 输出 `0xRRGGBBAA`（RGB alpha 固定 `0xFF`）；intensity 只对有效坐标点用整份数据的统一范围量化，非有限值输出 0。数据、字段、布局、转换或 PCL 异常均在关闭前稳定返回 `DataFormat/2`。`readNext()` 优先级为未打开 `Task/1`、零上限 `Configuration/1`、已取消 `Task/7`、零点 EOF、稳定格式错误、首次转换、批次和重复 EOF；取消不产生部分批次、不推进游标，未取消调用可从未提交的转换或当前批次继续。
- **集成测试**：新增无 PCL include 的 `dzc_ply_batch`，覆盖 ASCII 多批次/EOF/源文件不修改、little-endian Binary RGB/RGBA、全文件 intensity 量化、NaN/Inf 坐标过滤、全无效/空云、ASCII/Binary 截断、稳定错误、读取前后取消、零上限和关闭后重新打开；更新 `dzc_ply_reader` 的生命周期断言以验证真实读取。
- **验证结果**：2026-08-22 使用 MSVC 19.51.36246.0、NMake Makefiles、OpenGL-only Debug、PCL 1.15.1/io 和外部 DLL PATH 完成干净构建；专项、回归、Target 边界、完整 CTest、隔离扫描与 `git diff --check` 的最终记录见本次交接记录。
- **未解决问题**：Creator/Factory、I/O 并发与背压（IO-008）、进度与错误事件转换（IO-009）及模块级验收仍未完成。
- **追踪**：FR-DATA-003/004、NFR-PORT-003、10.2

### IO-008 接入 I/O 并发与背压

- **状态**：已完成（2026-08-22）
- **目标**：将调用方独占的 Reader 任务接入共享 ConcurrencyGate 和 BackpressureController。
- **前置任务**：IO-005, IO-007, task-system/TS-008
- **实际文件**：`src/data/io/PointCloudLoadTask.h`、`src/data/io/PointCloudLoadTask.cpp`、`src/data/CMakeLists.txt`、`tests/integration/PointCloudLoadTaskTests.cpp`、`tests/integration/CMakeLists.txt`、`docs/design/detailDesign.md`、`docs/tasks/progress.md`、`上下文.md`
- **实现结果**：新增无 PCL 依赖的可移动 `PointCloudLoadRequest` 与静态 `PointCloudLoadTask::submit()`。请求绑定 DatasetId、源路径、独占 `std::unique_ptr<IPointCloudReader>`、每批上限、共享 Gate/背压器、打开和批次回调，并可带调用方 Token 与优先级。提交只经 `TaskSystem::submitForDataset()` 排队；空路径、零上限、空 Reader/流控对象/回调均返回 `Configuration/1` 且不入队。
- **并发、背压与生命周期**：后台 worker 以 RAII 在所有退出路径关闭 Reader。Gate 仅覆盖 `open()` 和每一次 `readNext()`；打开后先交付一次 `onOpened`，每次下一次读取前都等待背压恢复，批次经 `PointBatch::validate()` 后按 Reader 顺序交付 `onBatch`。任务不创建、关闭或更新调用方共享的 Gate/背压器；回调运行在 TaskSystem worker，调用方定义并维护下游使用量。
- **错误与取消**：在等待、Reader 返回及回调前后检查组合 Token；取消后不交付新元数据或批次、不再发起读取，并以 `Task/7` 完成。等待中的 Gate/背压器关闭在未取消时为 `Internal/1`，取消同时发生时仍为 `Task/7`；Reader、批次校验和回调业务错误原样发布在同 DatasetId 的完成结果中。未实现 IO-009 进度/错误事件转换，未改 Engine、Factory/Registry、Dataset 写入或 PCL Reader。
- **集成测试**：新增无 PCL include/link 的 `dzc_point_cloud_load_task`，以可阻塞 Fake Reader 覆盖后台执行、打开/批次顺序和 EOF、共享 Gate 并发峰值、背压暂停/低水位恢复、预取消/等待 Gate/等待背压/阻塞 Reader/首批后的取消、Reader/批次校验/回调错误、流控关闭与全部请求校验。
- **验证结果**：2026-08-22 的 `build-io008-clean` OpenGL-only Debug/NMake 干净构建通过；`dzc_point_cloud_load_task`、TaskSystem/Gate/背压、Reader 合约/Registry 和 Target 边界专项均通过。显式前置 PCL、VTK 与 OpenNI2 DLL PATH 后，四个 PCL Reader/Batch 可执行文件逐个运行通过；聚合 CTest 的实际结果见交接记录。
- **追踪**：NFR-PERF-003/004、FR-DATA-004、10.3

### IO-009 实现 Reader 错误与进度转换

- **状态**：未开始
- **目标**：生成稳定进度单位和可恢复 Error/Event 输入。
- **前置任务**：IO-008, diagnostics/DG-001
- **预计文件**：`src/data/io/PointCloudLoadTask.cpp`、`tests/integration/ReaderProgressTests.cpp`
- **实现要求**：声明总量不可用时报告阶段状态，不伪造百分比。
- **验收检查**：有效文件进度单调；失败可继续加载其他文件。
- **测试要求**：进度单调、读取失败和后续重试测试。
- **追踪**：FR-DATA-003、NFR-REL-001/003

## 6. 模块级验收

- [ ] PCD 和 PLY 的 XYZ 文件均可流式读取
- [ ] 缺 XYZ、损坏、NaN/Inf 不导致崩溃
- [ ] PCL 类型和依赖不离开 dzc_data_pcl
- [ ] 加载进度、取消、并发限制和背压测试通过

## 7. 交接记录

- 完成日期：2026-08-22
- 完成人：Codex
- 关键变更：完成 IO-008。新增无 PCL 依赖的 `PointCloudLoadTask`，用 DatasetId 经 `TaskSystem::submitForDataset()` 交付最终完成结果；worker 回调交付 Reader 元数据和已验证批次。Gate 仅保护实际 `open()`/`readNext()`，背压等待和下游回调不占 I/O 许可；调用方拥有共享流控对象并自行维护用量。PCL 类型、include 和链接依赖继续限制在 `dzc_data_pcl`，模块保持进行中状态。
- 未解决问题：Creator/Factory、IO-009 的进度和错误事件转换以及模块级验收仍未完成；Engine、DatasetSession 和 UI 尚未接入加载任务。
- 测试命令与结果：IO-008 使用 MSVC 19.51.36246.0、NMake Makefiles、OpenGL-only Debug 在 `build-io008-clean` 完成干净全量构建；`dzc_point_cloud_load_task`、`dzc_task_system`、`dzc_concurrency_gate`、`dzc_backpressure`、`dzc_task_system_shutdown`、`dzc_reader_contract`、`dzc_reader_registry` 和 `dzc_target_boundary` 专项通过。前置 `D:\PCL\PCL 1.15.1\bin`、VTK bin 与 OpenNI2 Redist 后，`dzc_pcd_reader_tests.exe`、`dzc_pcd_batch_tests.exe`、`dzc_ply_reader_tests.exe`、`dzc_ply_batch_tests.exe` 逐个运行通过；完整聚合 CTest 为 52/56 通过，四项 PCL CTest（51 至 54）仍以 Windows `0xc0000135` 失败，不能表述为全绿。最终 `git diff --check` 通过；验证后 `build-io008-clean` 已安全删除。历史记录：IO-001 使用 MSVC 19.51.36246.0、NMake Makefiles、OpenGL-only Debug 配置成功全量构建；`ctest --test-dir build-io001-clean --output-on-failure` 为 50/50 通过，其中 `dzc_reader_contract` 通过。IO-002 使用相同工具链完成干净全量构建；`ctest --test-dir build-io002-clean -R "^dzc_reader_registry$" --output-on-failure` 为 1/1 通过，完整 `ctest --test-dir build-io002-clean --output-on-failure` 为 51/51 通过。IO-003 使用相同工具链与 PCL 1.15.1/io 完成干净全量构建；`dzc_data_pcl`、Target 边界检查、Reader 回归均通过，完整 CTest 为 51/51 通过；`git diff --check` 通过。IO-004 使用相同工具链和干净 OpenGL-only Debug 配置，`dzc_pcd_reader` 1/1、Reader 回归 2/2、`dzc_target_boundary` 1/1 通过，完整 CTest 为 52/52 通过；`git diff --check` 通过。IO-005 使用显式记录的 NMake 路径完成干净全量构建；`dzc_pcd_reader`、`dzc_pcd_batch`、Reader 回归和 Target 边界专项为 5/5 通过，完整 CTest 为 53/53 通过；最终 `git diff --check` 通过。 IO-006 使用相同工具链完成 `build-io006-clean` 干净全量构建；`dzc_ply_reader` 1/1 通过，Reader/PCL 专项在显式前置 PCL/VTK/OpenNI2 DLL PATH 后通过；最终完整 CTest、隔离扫描和 `git diff --check` 待完成。
- 关联提交：未提交（按用户要求不创建提交）。

## 8. 变更约束

若实现需要改变公共接口、模块依赖、已确认参数或需求行为，必须先更新需求与设计文档，不得在编码任务中自行改变。
