# Point Cloud I/O 任务清单

> 文件：`docs/tasks/point-cloud-io.md`  
> 所属阶段：Phase 1  
> 模块状态：进行中（IO-001 已完成）
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
- [ ] **IO-002 实现扩展名 Reader Registry**
- [ ] **IO-003 建立 PCL 私有 Target**
- [ ] **IO-004 实现 PCD 元数据打开**
- [ ] **IO-005 实现 PCD 批次转换**
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

- **状态**：未开始
- **目标**：按大小写无关的 .pcd/.ply 选择工厂。
- **前置任务**：IO-001
- **预计文件**：`src/data/io/PointCloudReaderRegistry.h`、`src/data/io/PointCloudReaderRegistry.cpp`、`tests/unit/ReaderRegistryTests.cpp`
- **实现要求**：未知扩展名明确失败；不通过文件内容猜测未支持格式。
- **验收检查**：PCD/PLY 路由正确，未知格式返回 DataFormat。
- **测试要求**：扩展名大小写、空路径和未知格式测试。
- **追踪**：FR-DATA-001

### IO-003 建立 PCL 私有 Target

- **状态**：未开始
- **目标**：配置 dzc_data_pcl 并限制 PCL 头/链接可见性。
- **前置任务**：project-foundation/PF-003
- **预计文件**：`src/data/io/pcl/CMakeLists.txt`、`src/data/io/pcl/PclReaderFactory.h`
- **实现要求**：PCL 只以 PRIVATE 依赖链接；导出头不得出现 pcl:: 类型。
- **验收检查**：其他 Target 不需要包含 PCL 即可编译。
- **测试要求**：CMake 依赖边界检查。
- **追踪**：ADR-003、PCL 仅 I/O

### IO-004 实现 PCD 元数据打开

- **状态**：未开始
- **目标**：使用 PCL 读取字段、声明点数和基础校验。
- **前置任务**：IO-003, IO-001
- **预计文件**：`src/data/io/pcl/PcdReader.h`、`src/data/io/pcl/PcdReader.cpp`、`tests/integration/PcdReaderTests.cpp`
- **实现要求**：缺 x/y/z 失败；捕获 PCL 异常并转 Error；不修改源文件。
- **验收检查**：有效 XYZ/RGB/intensity PCD 能打开，损坏和缺字段不崩溃。
- **测试要求**：小型 fixture 覆盖 ASCII/binary（以 PCL 支持为准）和损坏文件。
- **追踪**：FR-DATA-001、FR-DATA-005、NFR-REL-001

### IO-005 实现 PCD 批次转换

- **状态**：未开始
- **目标**：将 PCL 数据转换为 PointBatch 并过滤无效坐标。
- **前置任务**：IO-004, point-cloud-data/PD-004
- **预计文件**：`src/data/io/pcl/PcdReader.cpp`、`tests/integration/PcdBatchTests.cpp`
- **实现要求**：颜色优先 rgba 后 rgb；alpha 缛失为 255；NaN/Inf 跳过并计数。
- **验收检查**：批次不超过 maximumPoints，schema 与流长度一致。
- **测试要求**：跨批次、颜色、intensity、无效点和取消测试。
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

- 完成日期：2026-08-21
- 完成人：Codex
- 关键变更：完成 IO-001 Reader 公共内部契约、PointCloudSourceInfo 与 Fake Reader 契约测试；统一详细设计的 Task 错误码表；Point Cloud I/O 模块进入进行中状态。
- 未解决问题：真实 PCD/PLY Reader、Registry、PCL 私有 Target、字段映射、I/O 并发/背压、进度和错误转换尚未实施，留给 IO-002 至 IO-009。
- 测试命令与结果：使用 MSVC 19.51.36246.0、NMake Makefiles、OpenGL-only Debug 配置成功全量构建；`ctest --test-dir build-io001-clean --output-on-failure` 为 50/50 通过，其中 `dzc_reader_contract` 通过；`git diff --check` 通过。
- 关联提交：未提交（按用户要求不创建提交）。

## 8. 变更约束

若实现需要改变公共接口、模块依赖、已确认参数或需求行为，必须先更新需求与设计文档，不得在编码任务中自行改变。
