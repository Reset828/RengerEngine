# Point Cloud I/O 任务清单

> 文件：`docs/tasks/point-cloud-io.md`  
> 所属阶段：Phase 1  
> 模块状态：进行中（IO-001 至 IO-005 已完成；IO-006 至 IO-009 与模块级验收未完成）
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
- [ ] **IO-006 实现 PLY 元数据打开**
- [ ] **IO-007 实现 PLY 批次转换**
- [ ] **IO-008 接入 I/O 并发与背压**
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
- **未解决问题**：PLY Reader（IO-006、IO-007）、Creator/Factory、I/O 并发/背压（IO-008）、进度与错误事件转换（IO-009）及模块级验收仍未完成。
- **追踪**：10.2、FR-DATA-003/004
### IO-006 实现 PLY 元数据打开

- **状态**：未开始
- **目标**：使用 PCL 读取标准 PLY 字段并校验。
- **前置任务**：IO-003, IO-001
- **预计文件**：`src/data/io/pcl/PlyReader.h`、`src/data/io/pcl/PlyReader.cpp`、`tests/integration/PlyReaderTests.cpp`
- **实现要求**：只映射标准字段，不猜测其他业务属性。
- **验收检查**：有效 XYZ/RGB/intensity PLY 可打开；损坏和缺坐标失败。
- **测试要求**：ASCII/binary fixture、未知属性和损坏测试。
- **追踪**：FR-DATA-001、10.2

### IO-007 实现 PLY 批次转换

- **状态**：未开始
- **目标**：输出规范化 PointBatch 并支持取消。
- **前置任务**：IO-006, point-cloud-data/PD-004
- **预计文件**：`src/data/io/pcl/PlyReader.cpp`、`tests/integration/PlyBatchTests.cpp`
- **实现要求**：转换规则与 PCD 一致；第三方类型不得逃逸。
- **验收检查**：批次长度、属性和结束语义正确。
- **测试要求**：跨批次、颜色、intensity、无效点、取消测试。
- **追踪**：FR-DATA-003/004、NFR-PORT-003

### IO-008 接入 I/O 并发与背压

- **状态**：未开始
- **目标**：将 Reader 任务接入 ConcurrencyGate 和 BackpressureController。
- **前置任务**：IO-005, IO-007, task-system/TS-008
- **预计文件**：`src/data/io/PointCloudLoadTask.h`、`src/data/io/PointCloudLoadTask.cpp`、`tests/integration/PointCloudLoadTaskTests.cpp`
- **实现要求**：同时读取最多配置值；下游拥塞暂停；取消优先。
- **验收检查**：加载不占 UI 线程，取消后停止产生新批次。
- **测试要求**：可控 Fake Reader 验证并发峰值、背压和取消延迟。
- **追踪**：NFR-PERF-003/004、FR-DATA-004

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
- 关键变更：完成 IO-001 至 IO-004 的 Reader 公共契约、Registry、PCL 私有 Target 和 Header-only PCD 元数据打开；完成 IO-005 的 PCD 惰性全量数据体读取、严格字段/长度校验、私有 SoA 批次转换、坐标有效性过滤、RGBA 规范化和全文件 intensity 量化。PCL 类型、include 与链接依赖仍限制在 `dzc_data_pcl`；Reader 不修改源文件，Point Cloud I/O 模块保持进行中状态。
- 未解决问题：PLY Reader（IO-006、IO-007）、Creator/Factory、I/O 并发/背压（IO-008）、进度和错误事件转换（IO-009）以及模块级验收仍未完成。
- 测试命令与结果：IO-001 使用 MSVC 19.51.36246.0、NMake Makefiles、OpenGL-only Debug 配置成功全量构建；`ctest --test-dir build-io001-clean --output-on-failure` 为 50/50 通过，其中 `dzc_reader_contract` 通过。IO-002 使用相同工具链完成干净全量构建；`ctest --test-dir build-io002-clean -R "^dzc_reader_registry$" --output-on-failure` 为 1/1 通过，完整 `ctest --test-dir build-io002-clean --output-on-failure` 为 51/51 通过。IO-003 使用相同工具链与 PCL 1.15.1/io 完成干净全量构建；`dzc_data_pcl`、Target 边界检查、Reader 回归均通过，完整 CTest 为 51/51 通过；`git diff --check` 通过。IO-004 使用相同工具链和干净 OpenGL-only Debug 配置，`dzc_pcd_reader` 1/1、Reader 回归 2/2、`dzc_target_boundary` 1/1 通过，完整 CTest 为 52/52 通过；`git diff --check` 通过。IO-005 使用显式记录的 NMake 路径完成干净全量构建；`dzc_pcd_reader`、`dzc_pcd_batch`、Reader 回归和 Target 边界专项为 5/5 通过，完整 CTest 为 53/53 通过；最终 `git diff --check` 通过。
- 关联提交：未提交（按用户要求不创建提交）。

## 8. 变更约束

若实现需要改变公共接口、模块依赖、已确认参数或需求行为，必须先更新需求与设计文档，不得在编码任务中自行改变。
