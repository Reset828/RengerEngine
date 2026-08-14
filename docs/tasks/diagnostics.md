# Diagnostics 任务清单

> 文件：`docs/tasks/diagnostics.md`  
> 所属阶段：公共基础  
> 模块状态：未开始  
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

- [ ] **DG-001 定义日志记录与 Sink 接口**
- [ ] **DG-002 实现 UTF-8 文本文件 Sink**
- [ ] **DG-003 实现日志轮转**
- [ ] **DG-004 实现异步 Logger**
- [ ] **DG-005 实现帧统计聚合器**
- [ ] **DG-006 定义通用指标快照**
- [ ] **DG-007 实现 CSV 性能写出**
- [ ] **DG-008 实现 Markdown 性能摘要**

## 5. 子任务说明

### DG-001 定义日志记录与 Sink 接口

- **状态**：未开始
- **目标**：建立 LogLevel、LogRecord、ILogSink 和结构化上下文。
- **前置任务**：project-foundation/PF-005
- **预计文件**：`src/diagnostics/LogTypes.h`、`src/diagnostics/ILogSink.h`
- **实现要求**：记录时间、级别、模块、错误码和可选 Dataset/Chunk/Frame；不依赖 Qt。
- **验收检查**：内存 Sink 可接收并格式化 UTF-8 单行日志。
- **测试要求**：测试字段缺省、特殊字符转义和 UTF-8 内容。
- **追踪**：FR-UI-005、21.2

### DG-002 实现 UTF-8 文本文件 Sink

- **状态**：未开始
- **目标**：实现线程安全追加和明确写入失败返回。
- **前置任务**：DG-001
- **预计文件**：`src/diagnostics/TextFileSink.h`、`src/diagnostics/TextFileSink.cpp`、`tests/unit/TextFileSinkTests.cpp`
- **实现要求**：使用 RAII 文件句柄；时间格式包含时区；不得逐点输出。
- **验收检查**：写入内容符合详细设计示例格式；关闭后资源释放。
- **测试要求**：临时目录测试创建、追加、关闭和不可写路径。
- **追踪**：NFR-MAIN-002、FR-UI-005

### DG-003 实现日志轮转

- **状态**：未开始
- **目标**：实现单文件 20 MiB、保留最近 10 个文件。
- **前置任务**：DG-002
- **预计文件**：`src/diagnostics/RotatingFileSink.h`、`src/diagnostics/RotatingFileSink.cpp`、`tests/unit/LogRotationTests.cpp`
- **实现要求**：阈值可测试配置覆盖；轮转失败回退现有 Sink 或 stderr。
- **验收检查**：超过阈值后文件数量和顺序正确，不丢失 Error/Fatal。
- **测试要求**：用小阈值测试多次轮转、清理和失败回退。
- **追踪**：DDD-015、21.2

### DG-004 实现异步 Logger

- **状态**：未开始
- **目标**：加入有界队列和日志线程，低级日志拥塞时可丢弃。
- **前置任务**：DG-003
- **预计文件**：`src/diagnostics/Logger.h`、`src/diagnostics/Logger.cpp`、`tests/unit/LoggerTests.cpp`
- **实现要求**：Error/Fatal 队列满时同步写备用 Sink；shutdown 必须 flush 并汇合线程。
- **验收检查**：多线程写入无数据竞争；关闭后不接受新记录。
- **测试要求**：并发压力、队列满、Error 保留、幂等关闭测试。
- **追踪**：NFR-REL-002、21.2

### DG-005 实现帧统计聚合器

- **状态**：未开始
- **目标**：计算最近 120 帧或 1 秒窗口的 FPS 和平均帧时。
- **前置任务**：DG-001
- **预计文件**：`src/diagnostics/FrameStatistics.h`、`src/diagnostics/FrameStatistics.cpp`、`tests/unit/FrameStatisticsTests.cpp`
- **实现要求**：使用注入时钟以便确定性测试；未填满窗口按实际样本。
- **验收检查**：固定时间序列得到预期 FPS/平均值；零样本安全。
- **测试要求**：覆盖窗口帧数边界、时间边界和异常 delta。
- **追踪**：FR-STAT-001、6.3

### DG-006 定义通用指标快照

- **状态**：未开始
- **目标**：实现线程安全计数器和每帧 DiagnosticsSnapshot。
- **前置任务**：DG-005
- **预计文件**：`src/diagnostics/MetricsRegistry.h`、`src/diagnostics/MetricsRegistry.cpp`、`tests/unit/MetricsRegistryTests.cpp`
- **实现要求**：包含设计 21.3 的点数、块数、驻留、上传、LOD、队列和录制工作量字段；不复制点云。
- **验收检查**：多线程更新后快照数值一致，帧级计数可重置。
- **测试要求**：并发计数和帧切换测试。
- **追踪**：FR-STAT-001、NFR-TEST-002

### DG-007 实现 CSV 性能写出

- **状态**：未开始
- **目标**：按固定列头和 C locale 写 UTF-8 CSV。
- **前置任务**：DG-006
- **预计文件**：`src/diagnostics/PerformanceCsvWriter.h`、`src/diagnostics/PerformanceCsvWriter.cpp`、`tests/unit/PerformanceCsvWriterTests.cpp`
- **实现要求**：缺失值留空；逗号、引号字段正确转义；失败不终止渲染。
- **验收检查**：生成文件列头、列数和数值格式稳定。
- **测试要求**：Golden file 测试正常、缺失值和写失败。
- **追踪**：FR-STAT-002、DDD-015

### DG-008 实现 Markdown 性能摘要

- **状态**：未开始
- **目标**：输出环境、数据集、配置和统计摘要，并保留 TBD。
- **前置任务**：DG-007
- **预计文件**：`src/diagnostics/PerformanceSummaryWriter.h`、`src/diagnostics/PerformanceSummaryWriter.cpp`、`tests/unit/PerformanceSummaryWriterTests.cpp`
- **实现要求**：基准硬件、低帧率百分位、Camera 路径未确认时必须写 TBD。
- **验收检查**：摘要包含设计 21.4 全部字段，不虚构结果。
- **测试要求**：Golden file 测试完整环境和缺失环境。
- **追踪**：NFR-TEST-001、ADR-014

## 6. 模块级验收

- [ ] 日志为 UTF-8 且轮转策略通过测试
- [ ] Error/Fatal 在队列拥塞时仍可诊断
- [ ] FPS 和指标聚合测试通过
- [ ] CSV 与 Markdown Golden file 测试通过

## 7. 交接记录

- 完成日期：
- 完成人：
- 关键变更：
- 未解决问题：
- 测试命令与结果：
- 关联提交：

## 8. 变更约束

若实现需要改变公共接口、模块依赖、已确认参数或需求行为，必须先更新需求与设计文档，不得在编码任务中自行改变。
