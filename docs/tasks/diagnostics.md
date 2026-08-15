# Diagnostics 任务清单

> 文件：`docs/tasks/diagnostics.md`  
> 所属阶段：公共基础  
> 模块状态：完成
> 前置模块：[project-foundation](./project-foundation.md)  
> 输入基线：[需求文档](../requirements/spec.md)、[概要设计](../design/architectureDesign.md)、[详细设计](../design/detailDesign.md)、[项目规范](../../agent.md)

## 1. 模块目标

实现不依赖 Qt 或图形 API 的日志、指标、FPS 统计以及 CSV/Markdown 性能报告基础设施。

## 2. 范围边界

**包含：** 日志级别和记录；异步日志队列；20 MiB/10 文件轮转；CPU 指标和 FPS；CSV 明细；Markdown 摘要。  
**不包含：** OpenGL/Vulkan GPU Query 实现；最终基准硬件数据；Camera 性能路径。

## 3. 完成规则

只有同时满足以下条件，模块才可以在 [progress.md](./progress.md) 中标记完成：

- 本文所有非阻塞必需任务均已勾选；
- 所有自动化测试通过，能力缺失用例只能明确标记为 Skipped，不能伪造通过；
- 对应公共接口和私有实现符合 `agent.md` 的命名、Pimpl、RAII 和依赖边界；
- 相关需求、设计和测试文档已经同步；

## 4. 子任务 Checklist

- [x] **DG-001 定义日志记录与 Sink 接口**
- [x] **DG-002 实现 UTF-8 文本文件 Sink**
- [x] **DG-003 实现日志轮转**
- [x] **DG-004 实现异步 Logger**
- [x] **DG-005 实现帧统计聚合器**
- [x] **DG-006 定义通用指标快照**
- [x] **DG-007 实现 CSV 性能写出**
- [x] **DG-008 实现 Markdown 性能摘要**

## 5. 子任务说明

### DG-001 定义日志记录与 Sink 接口

- **状态**：已完成
- **目标**：建立 LogLevel、LogRecord、ILogSink 和结构化上下文。
- **前置任务**：project-foundation/PF-005
- **预计文件**：`src/diagnostics/LogTypes.h`、`src/diagnostics/ILogSink.h`
- **实现要求**：记录时间、级别、模块、错误码和可选 Dataset/Chunk/Frame；不依赖 Qt。
- **验收检查**：内存 Sink 可接收并格式化 UTF-8 单行日志。
- **测试要求**：测试字段缺省、特殊字符转义和 UTF-8 内容。
- **追踪**：FR-UI-005、21.2
- **完成记录（2026-08-14）**：实现 `LogTypes.h` 与 `ILogSink.h`；使用 `Trace`/`Debug`/`Info`/`Warn`/`Error` 五级枚举；记录 `system_clock::time_point`、模块、`uint32_t` 错误码、可选 `uint64_t` Dataset/Chunk/Frame、消息及有序键值上下文；提供 UTF-8 单行格式化和约定的特殊字符转义；Sink 通过 `bool` 报告接收结果，线程安全由实现类负责。
- **验证**：`dzc_log_types` 测试覆盖缺省字段、特殊字符转义、UTF-8 内容、结构化键值字段和内存 Sink；完整 CTest 8/8 通过。

### DG-002 实现 UTF-8 文本文件 Sink

- **状态**：已完成
- **目标**：实现线程安全追加和明确写入失败返回。
- **前置任务**：DG-001
- **预计文件**：`src/diagnostics/TextFileSink.h`、`src/diagnostics/TextFileSink.cpp`、`tests/unit/TextFileSinkTests.cpp`
- **实现要求**：使用 RAII 文件句柄；时间格式包含时区；不得逐点输出。
- **验收检查**：写入内容符合详细设计示例格式；关闭后资源释放。
- **测试要求**：临时目录测试创建、追加、关闭和不可写路径。
- **追踪**：NFR-MAIN-002、FR-UI-005

### DG-002 完成记录（2026-08-15）

- 实现 `TextFileSink.h`/`TextFileSink.cpp`，使用 RAII 文件流、互斥保护追加写入，并启动每 3 秒执行一次 `flush()` 的后台线程。
- 构造函数立即尝试以二进制追加模式打开 UTF-8 文件，不创建父目录、不写 BOM，每条记录追加单个 LF。
- `dzc_text_file_sink` 覆盖创建与追加、UTF-8 内容、单个 LF、无 BOM、关闭后资源释放、父目录不存在、目录路径失败、并发写入和 3 秒后台刷新。
- 完整 CTest 9/9 通过。
### DG-003 实现日志轮转

- **状态**：已完成
- **目标**：实现单文件 20 MiB、保留最近 10 个文件。
- **前置任务**：DG-002
- **预计文件**：`src/diagnostics/RotatingFileSink.h`、`src/diagnostics/RotatingFileSink.cpp`、`tests/unit/LogRotationTests.cpp`
- **实现要求**：阈值可测试配置覆盖；轮转失败回退现有 Sink 或 stderr。
- **验收检查**：超过阈值后文件数量和顺序正确，不丢失 Error。
- **测试要求**：用小阈值测试多次轮转、清理和失败回退。
- **追踪**：DDD-015、21.2

### DG-003 完成记录（2026-08-15）

- 新增 `RotatingFileSink`，继承 `ILogSink`，默认单文件阈值为 20 MiB，默认最多保留 10 个文件（当前文件加 `.1` 至 `.9`）。
- 支持构造函数注入最大字节数、最大文件数和 `std::shared_ptr<ILogSink>` 备用 Sink；构造时立即尝试打开当前文件。
- 按当前文件加本条 UTF-8 格式化日志行和单个 LF 的总字节数，在写入前执行轮转；单条超过阈值的记录完整写入，不拆分、不丢弃。
- 轮转顺序为删除最旧文件、从旧到新重命名、当前文件改名为 `.1`、重新创建当前文件；轮转失败时优先继续写当前可用文件，当前文件不可用时写备用 Sink 或 `stderr`。
- 沿用 DG-002 的 RAII、线程安全、幂等 `close()`、关闭时 flush、后台每 3 秒 flush、无 BOM 和单 LF 行尾行为。
- 不新增 `Fatal` 日志级别；DG-003 的高优先级日志验收统一按 `Error` 处理。
- `dzc_log_rotation` 覆盖小阈值多次轮转、文件清理、已有超限文件、超大单条记录、轮转失败继续写当前文件、备用 Sink 回退、Error 保留和并发写入。
- 完整 CTest：**10/10 通过**。
### DG-004 实现异步 Logger

- **状态**：已完成
- **目标**：加入有界队列和日志线程，低级日志拥塞时可丢弃。
- **前置任务**：DG-003
- **预计文件**：`src/diagnostics/Logger.h`、`src/diagnostics/Logger.cpp`、`tests/unit/LoggerTests.cpp`
- **实现要求**：队列满时允许丢弃 Trace/Debug/Info；Warn 等待队列空间；Error 队列满时同步写备用 Sink；shutdown 排空队列并汇合线程。
- **验收检查**：多线程写入无数据竞争；关闭后不接受新记录；Error 在队列拥塞时仍可通过备用 Sink 诊断。
- **测试要求**：并发压力、FIFO、队列满、Warn 保留、Error 备用回退、失败传播和幂等关闭测试。
- **追踪**：NFR-REL-002、21.2

### DG-004 完成记录（2026-08-15）

- 新增 `Logger` 公共接口：主 Sink、可选备用 Sink、可配置有界容量（默认 1024）、`write()` 和幂等 `shutdown()`。
- Logger 构造时启动单个后台线程，按 FIFO 顺序串行调用主 Sink；不扩展 `ILogSink`，shutdown 的 flush 语义为排空 Logger 队列并汇合线程。
- 队列满时 Trace/Debug/Info 返回 `false` 并允许丢弃；Warn 阻塞等待空间；Error 同步写备用 Sink，备用 Sink 缺失或失败时返回 `false`。
- 容量为 0 时不接受异步入队，Warn/Error 同步尝试备用 Sink；shutdown 后拒绝新记录；主 Sink 写失败不会停止日志线程或自动转发备用 Sink。
- 不新增 `Fatal` 日志级别；DG-004 中原有 `Error/Fatal` 统一按 `Error` 处理。
- `dzc_logger` 覆盖 FIFO、并发写入、低级日志丢弃、Warn 等待、Error 备用回退、容量边界、主 Sink 写失败、空主 Sink和并发幂等 shutdown。
- Debug 构建完整 CTest：**11/11 通过**。

### DG-005 实现帧统计聚合器

- **状态**：已完成
- **目标**：同时计算最近最多 120 个有效帧样本和最近 1 秒时间窗口的 FPS 与平均帧时。
- **前置任务**：DG-001
- **实现文件**：`src/diagnostics/IClock.h`、`src/diagnostics/FrameStatistics.h`、`src/diagnostics/FrameStatistics.cpp`、`tests/unit/FrameStatisticsTests.cpp`
- **公共接口**：注入 `std::shared_ptr<IClock>`，调用方传入 `std::chrono::nanoseconds` frame delta；返回 `Snapshot`，包含 `frameWindow` 和 `timeWindow` 两组 `WindowStats`。
- **窗口规则**：帧数窗口保留最近 `frameWindow` 个样本；时间窗口使用包含两端的 `[now - timeWindow, now]`，`snapshot()` 也会按当前时钟淘汰过期样本。
- **统计规则**：FPS 使用窗口样本数除以窗口首尾时间跨度；平均帧时使用有效样本的算术平均值，输出单位为毫秒；零样本返回零值。
- **异常处理**：非正 frame delta 和时钟回退样本返回 `false` 且不更新基线；相同时间点允许接收；空时钟使用系统 `steady_clock`。
- **边界行为**：`frameWindow == 0` 或 `timeWindow <= 0` 只使对应窗口无效；两个窗口独立统计；两个窗口均无效时拒绝样本；`reset()` 清空样本和时钟基线。
- **线程安全**：`addFrame()`、`snapshot()` 和 `reset()` 由互斥量保护，快照为一致副本。
- **测试**：`dzc_frame_statistics` 覆盖固定时间序列、帧数/时间边界、无新帧过期、零样本、异常 delta、时钟回退、无效窗口、reset 和并发调用。
- **验证**：Debug 配置完整 CTest **12/12 通过**。
- **追踪**：FR-STAT-001、6.3
### DG-006 定义通用指标快照

- **状态**：已完成
- **目标**：实现线程安全计数器和每帧 `MetricsSnapshot`。
- **前置任务**：DG-005
- **实现文件**：`src/diagnostics/MetricsRegistry.h`、`src/diagnostics/MetricsRegistry.cpp`、`tests/unit/MetricsRegistryTests.cpp`
- **最终接口**：`MetricsRegistry` 使用 Pimpl，提供 `beginFrame()`、`reset()`、`snapshot()`，以及固定字段的 `addXxx`/`setXxx` 更新方法；快照按性能、几何、传输、内存、LOD、运行时、录制和计算分组。
- **生命周期**：`beginFrame(frameId)` 原样记录帧号并清零帧级指标，保留驻留/预算、队列深度和 I/O 活跃数等状态指标；`reset()` 清零全部字段并恢复帧号 0。
- **数值规则**：无符号整数累加采用饱和语义；FPS、帧耗时、录制耗时和 CUDA 同步耗时拒绝负数及非有限值；录制聚合耗时合法输入累加并在 double 上溢时钳制到最大值。
- **线程安全**：所有公开操作由统一互斥量保护；`snapshot()` 返回一致的值副本，不暴露内部状态。
- **测试**：覆盖初始化、固定字段更新、帧切换、状态保留、reset、整数饱和、double 校验、录制/CUDA 指标、快照副本和多线程并发。
- **追踪**：FR-STAT-001、NFR-TEST-002
- **验证**：`cmake --build build-dg006 --config Debug` 成功；完整 CTest **13/13 通过**，新增 `dzc_metrics_registry` 测试通过。

### DG-007 实现 CSV 性能写出

- **状态**：已完成
- **目标**：按固定列头和 C locale 写线程安全 UTF-8 CSV。
- **前置任务**：DG-006
- **实现文件**：`src/diagnostics/PerformanceCsvWriter.h`、`src/diagnostics/PerformanceCsvWriter.cpp`、`tests/unit/PerformanceCsvWriterTests.cpp`
- **最终接口**：`PerformanceCsvRow` 固定包含 UTC 时间、帧号、后端、尺寸、CPU/GPU 帧耗时、FPS、几何/内存/传输/LOD 丢失和录制 worker 字段；`PerformanceCsvWriter` 提供构造、`write()`、`close()` 和 `isOpen()`，使用 Pimpl、RAII 和互斥量。
- **固定表头**：`utcTime,frameId,backend,width,height,cpuFrameMs,gpuFrameMs,fps,visiblePoints,submittedPoints,visibleChunks,cpuResidentBytes,gpuResidentBytes,uploadBytes,lodMisses,recordingWorkers`，不扩展 DG-006 未列入的字段。
- **格式规则**：二进制 UTF-8、无 BOM、表头和记录以单个 LF 结束；UTC 格式为 `YYYY-MM-DDTHH:MM:SS.mmmZ`；整数使用十进制；浮点使用 classic/C locale 固定 6 位小数；`std::nullopt` 输出空字段。
- **CSV 与失败语义**：字符串字段按逗号、双引号、CR/LF 规则转义；NaN/正负无穷拒绝整行；父目录不自动创建；打开、写入和 flush 失败通过 `false` 报告，不抛异常、不写 stderr；`close()` 幂等并 flush 后关闭，析构自动关闭。
- **并发规则**：`write()`、`close()`、`isOpen()` 由互斥量保护；并发写入不会交错，关闭后所有写入失败；不创建后台刷新线程。
- **指标映射**：调用方组装行时使用 `lodMisses = max(lod.requests - lod.hits, 0)`；Writer 不直接依赖 `MetricsRegistry` 或 `FrameStatistics`。
- **验收检查**：固定表头、列数、UTC/浮点格式、缺失值、CSV 转义、失败返回和关闭行为均稳定。
- **测试**：`dzc_performance_csv_writer` 覆盖 Golden file、固定表头、无 BOM/单 LF、UTC 毫秒、classic locale、缺失值、字符串转义、非法浮点整行拒绝、打开失败、幂等关闭、析构和并发写入/关闭。
- **追踪**：FR-STAT-002、DDD-015
- **验证**：`cmake -S . -B build-dg007 -DDZC_ENABLE_OPENGL=ON -DDZC_ENABLE_VULKAN=OFF -DDZC_ENABLE_CUDA=OFF -DDZC_BUILD_TESTS=ON`；`cmake --build build-dg007 --config Debug`（MSVC PDB 并发问题后以 `/m:1` 完成）；`ctest --test-dir build-dg007 -C Debug --output-on-failure`；完整 CTest **14/14 通过**。
### DG-008 实现 Markdown 性能摘要

- **状态**：已完成
- **目标**：输出环境、数据集、配置、统计和错误摘要，并保留未确认信息的 TBD 标记。
- **前置任务**：DG-007
- **实现文件**：`src/diagnostics/PerformanceSummaryWriter.h`、`src/diagnostics/PerformanceSummaryWriter.cpp`、`tests/unit/PerformanceSummaryWriterTests.cpp`
- **最终接口**：固定 `PerformanceSummary` 数据结构和 Pimpl `PerformanceSummaryWriter`；Writer 只允许首次成功写出一份完整摘要。
- **固定模板**：`Performance Summary` 下依次输出 `Environment`、`Dataset`、`Configuration`、`Statistics`、`Errors` 五个章节，字段顺序与详细设计 21.4 一致。
- **格式**：二进制写入 UTF-8、无 BOM、单 LF；整数使用十进制；double 使用 classic/C locale 和固定 6 位小数。
- **缺失值**：普通缺失字段输出空值；`benchmarkHardware`、`cameraPath`、`lowFrameRatePercentile` 缺失时输出 `TBD`。
- **Markdown 转义**：字符串中的反斜杠转义为 `\\`，`|` 转义为 `\|`，CR/LF 转为空格，确保表格结构和单行约束不被破坏。
- **失败语义**：父目录不自动创建；打开、非法浮点、写入和关闭失败通过返回值报告，不抛异常；非法 optional double 拒绝整份摘要且不写入；`close()` flush 后关闭并幂等。
- **线程安全**：`write()`、`close()` 和 `isOpen()` 由互斥量保护；关闭后拒绝写入；并发首次写入最多一个线程成功。
- **测试结果**：`dzc_performance_summary_writer` 覆盖完整/缺失 Golden file、TBD、编码、换行、数值格式、转义、非法浮点、生命周期、打开失败及并发行为。
- **追踪**：NFR-TEST-001、ADR-014

## 6. 模块级验收

- [x] 日志为 UTF-8 且轮转策略通过测试
- [x] Error 在队列拥塞时仍可诊断
- [x] FPS 和指标聚合测试通过
- [x] CSV 与 Markdown Golden file 及格式测试通过

## 7. 交接记录

- 完成日期：2026-08-14（DG-001）
- 完成人：Codex
- 关键变更：新增 `src/diagnostics/LogTypes.h`、`src/diagnostics/ILogSink.h`、`tests/unit/LogTypesTests.cpp`，并接入 `dzc_diagnostics` 测试目标。
- 未解决问题：DG-007、DG-008 尚未实现；Diagnostics 模块不可标记为完成。
- 测试命令与结果：`cmake --build build-dg001 --config Debug`；`ctest --test-dir build-dg001 -C Debug --output-on-failure`；8/8 通过。
- 关联提交：尚未创建 Git 提交。
### DG-002（2026-08-15）

- 完成人：Codex
- 关键变更：新增 `src/diagnostics/TextFileSink.h`、`src/diagnostics/TextFileSink.cpp`、`tests/unit/TextFileSinkTests.cpp`，并将 `dzc_diagnostics` 接入静态库实现。
- 未解决问题：DG-007、DG-008 尚未实现；Diagnostics 模块不可标记为完成。
- 测试命令与结果：`cmake --build build-dg002 --config Debug`；`ctest --test-dir build-dg002 -C Debug --output-on-failure`；9/9 通过。
- 关联提交：尚未创建 Git 提交。

### DG-006（2026-08-15）

- 完成人：Codex
- 关键变更：新增固定字段、分组快照和 Pimpl 实现的 `MetricsRegistry`，接入 `dzc_diagnostics`；新增并注册 `MetricsRegistryTests.cpp`。
- 行为：所有公开操作线程安全；`beginFrame()` 清零帧级指标并保留状态指标；`reset()` 全量清零；整数累加饱和；非法 double 被拒绝；快照返回一致副本。
- 测试命令与结果：`cmake -S . -B build-dg006 -DDZC_ENABLE_OPENGL=ON -DDZC_ENABLE_VULKAN=OFF -DDZC_ENABLE_CUDA=OFF -DDZC_BUILD_TESTS=ON`；`cmake --build build-dg006 --config Debug`；`ctest --test-dir build-dg006 -C Debug --output-on-failure`；13/13 通过。
- 未解决问题：DG-007、DG-008 尚未实现；Diagnostics 模块不可标记为完成。
- 关联提交：尚未创建 Git 提交。
### DG-007（2026-08-15）

- 完成人：Codex
- 关键变更：新增固定字段的 `PerformanceCsvRow` 与线程安全 Pimpl `PerformanceCsvWriter`；接入 `dzc_diagnostics`；新增并注册 `PerformanceCsvWriterTests.cpp`。
- 行为：UTF-8 无 BOM、单 LF、UTC 毫秒格式、classic locale 固定 6 位浮点、缺失值留空、CSV 字符串转义、非法浮点拒绝整行、父目录不创建、`close()` flush 且幂等。
- 测试命令与结果：`cmake -S . -B build-dg007 -DDZC_ENABLE_OPENGL=ON -DDZC_ENABLE_VULKAN=OFF -DDZC_ENABLE_CUDA=OFF -DDZC_BUILD_TESTS=ON`；`cmake --build build-dg007 --config Debug`；`ctest --test-dir build-dg007 -C Debug --output-on-failure`；完整 CTest **14/14 通过**。由于 MSVC 并行 PDB 锁冲突，最终构建以 `cmake --build build-dg007 --config Debug -- /m:1` 完成。
- 未解决问题：DG-008 尚未实现；Diagnostics 模块不可标记为完成。
- 下一任务：DG-008 实现 Markdown 性能摘要。
- 关联提交：尚未创建 Git 提交。
### DG-008（2026-08-15）

- 完成人：Codex
- 关键变更：新增固定 `PerformanceSummary` 与线程安全 Pimpl `PerformanceSummaryWriter`；接入 `dzc_diagnostics`；新增并注册 `PerformanceSummaryWriterTests.cpp`。
- 行为：固定 Markdown 五章节和字段顺序；UTF-8 无 BOM、单 LF、classic locale 固定 6 位浮点、普通缺失值为空、未确认字段为 TBD；Markdown 字符串执行反斜杠/管道转义并将 CR/LF 转为空格；一次性写出、关闭 flush 且幂等、失败通过返回值报告。
- 测试命令与结果：`cmake -S . -B build-dg008 -DDZC_ENABLE_OPENGL=ON -DDZC_ENABLE_VULKAN=OFF -DDZC_ENABLE_CUDA=OFF -DDZC_BUILD_TESTS=ON`；`cmake --build build-dg008 --config Debug -- /m:1`；`ctest --test-dir build-dg008 -C Debug --output-on-failure`；完整 CTest 15/15 通过。
- 未解决问题：Diagnostics 模块已完成；下一任务待用户指定。
- 关联提交：尚未创建 Git 提交。

## 8. 变更约束

若实现需要改变公共接口、模块依赖、已确认参数或需求行为，必须先更新需求与设计文档，不得在编码任务中自行改变。
