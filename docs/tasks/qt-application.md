# Qt Application 任务清单

> 文件：`docs/tasks/qt-application.md`  
> 所属阶段：Phase 1/Phase 2 公共 UI  
> 模块状态：进行中（QT-001 至 QT-004 已完成；QT-005 至 QT-012 未完成）
> 前置模块：[engine-core](./engine-core.md)、[camera-abstraction](./camera-abstraction.md)、[diagnostics](./diagnostics.md)  
> 输入基线：[需求文档](../requirements/spec.md)、[概要设计](../design/architectureDesign.md)、[详细设计](../design/detailDesign.md)、[项目规范](../../agent.md)

## 1. 模块目标

实现 Qt 5.15.19 主窗口、Engine 适配、设置、状态与日志界面，并为 OpenGL/Vulkan 提供独立视图宿主。

## 2. 范围边界

**包含：** QApplication/QMainWindow；命令行；QSettings/INI；文件选择；参数面板；状态栏；日志面板；EngineUiAdapter；OpenGL Widget；Vulkan QWindow 容器骨架。  
**不包含：** Engine 业务逻辑；PCL/GPU 资源访问；具体 Camera 键位；Vulkan 后端内部。

## 3. 完成规则

只有同时满足以下条件，模块才可以在 [progress.md](./progress.md) 中标记完成：

- 本文所有非阻塞必需任务均已勾选；
- 所有自动化测试通过，能力缺失用例只能明确标记为 Skipped，不能伪造通过；
- 对应公共接口和私有实现符合 `agent.md` 的命名、Pimpl、RAII 和依赖边界；
- 相关需求、设计和测试文档已经同步；

## 4. 子任务 Checklist

- [x] **QT-001 配置 Qt 5.15.19 App Target**
- [x] **QT-002 实现命令行配置解析**
- [x] **QT-003 实现 QSettings/INI 控制器**
- [x] **QT-004 创建 MainWindow 布局**
- [ ] **QT-005 实现 EngineUiAdapter**
- [ ] **QT-006 实现文件加载与取消 UI**
- [ ] **QT-007 实现渲染参数 UI**
- [ ] **QT-008 实现状态与日志显示**
- [ ] **QT-009 实现 OpenGLRenderWidget 宿主**
- [ ] **QT-010 实现 VulkanRenderWindow 宿主骨架**
- [ ] **QT-011 实现 View Reset 入口但不定义行为**
- [ ] **QT-012 完成 UI 响应集成测试**

## 5. 子任务说明

### QT-001 配置 Qt 5.15.19 App Target

- **状态**：已完成（2026-09-03）
- **目标**：建立 dzc_app 并链接所需 Qt 模块。
- **前置任务**：project-foundation/PF-003
- **预计文件**：`src/app/CMakeLists.txt`、`src/app/main.cpp`
- **实现要求**：Qt 只在 App Target；Engine 公共库不链接 Qt。
- **验收检查**：最小空窗口可构建启动。
- **测试要求**：CMake 依赖边界和 GUI 冒烟测试。
- **追踪**：FR-UI-001、NFR-PORT-001

### QT-001 实施记录

- **实际文件**：`src/app/CMakeLists.txt`、`src/app/main.cpp`、`tests/ui/CMakeLists.txt`、`tests/ui/QtApplicationSmokeTests.cpp`，以及顶层 `CMakeLists.txt` 和 `src/CMakeLists.txt` 的 Target 注册/依赖调整。
- **Qt 版本与 Target**：通过 `find_package(Qt5 5.15.19 CONFIG REQUIRED COMPONENTS Widgets)` 发现 Qt 5.15.19；`dzc_app` 已从接口占位 Target 转为私有链接 `Qt5::Widgets` 的可执行程序。
- **应用行为**：`main.cpp` 仅创建 `QApplication`、标题为 `Dzc-RenderEngine` 的标准空 `QMainWindow`，显示后进入事件循环；未设置固定尺寸；QT-002 解析器作为独立 App 内部组件实现，但尚未接入 `main.cpp`，后续 UI/Engine/渲染宿主功能未实现。
- **冒烟测试**：新增独立 `dzc_qt_application_smoke`，使用 `QT_QPA_PLATFORM=offscreen` 验证 `QApplication`、`QMainWindow` 显示和事件循环自动退出，标签为 `ui;qt;qt001`；测试不链接 `dzc_app`。
- **边界与 CUDA**：Qt 依赖只加入 App/测试 Target，公共 Engine API 未修改；本阶段 CUDA 全部暂缓，构建/验证使用 `DZC_ENABLE_CUDA=OFF`。
### QT-002 实现命令行配置解析

- **状态**：已完成（2026-09-03）
- **目标**：解析 backend/cuda/log/cache/budget 等参数。
- **前置任务**：QT-001, project-foundation/PF-006
- **实际文件**：`src/app/CommandLineOptions.h`、`src/app/CommandLineOptions.cpp`、`tests/unit/CommandLineOptionsTests.cpp`、`src/app/CMakeLists.txt`、`tests/unit/CMakeLists.txt`。
- **实现行为**：提供 App 内部 `CommandLineOptions` 解析器，使用 `--name=value` 语法支持 `backend`、`cuda`、`log-level`、`cache-directory`、CPU/GPU 十进制字节预算；严格匹配小写枚举值，预算 0 保留自动计算语义，缓存目录仅保存 UTF-8 路径。
- **错误策略**：非法值、缺少值、缺少等号、未知选项、位置参数和重复参数均返回 `ErrorDomain::Configuration` / `InvalidValue(1)`，错误中包含具体原因和完整 usage；显式 backend 值不静默回退。解析器不向 stderr 输出。
- **日志边界**：`EngineConfig` 当前没有日志级别字段，因此日志级别保存在 App 内部 `Parsed` 结果中；未修改公共 Engine API。
- **范围边界**：本任务仅实现解析器和单元测试，未接入 `main.cpp`、Engine、QSettings/INI 或后续 UI；QT-003 至 QT-012 保持未开始。
- **验收测试**：`dzc_command_line_options` 覆盖默认值、全部支持参数、合法枚举、UTF-8 路径、预算边界/非法值、重复参数、未知参数、位置参数和 usage 错误诊断；`build-qt002` 配置、`dzc_app`、`dzc_qt_application_smoke` 与 `dzc_command_line_options_tests` 构建成功，`dzc_command_line_options`、`dzc_target_boundary` 以及既有 `dzc_result`、`dzc_engine_config`、`dzc_log_types` 回归测试通过；Qt smoke 未运行：仓库根目录 `dll/` 缺少 Qt 运行时 DLL，按 `agent.md` 停止，未从其他位置复制。
- **追踪**：FR-COM-001、DDD-004

### QT-003 实现 QSettings/INI 控制器

- **状态**：已完成（2026-09-03）
- **目标**：建立 App 内部配置模型、显式 INI 读写和命令行覆盖基础设施。
- **前置任务**：QT-002
- **实际文件**：`src/app/ApplicationConfig.h`、`src/app/SettingsController.h`、`src/app/SettingsController.cpp`、`src/app/CommandLineOptions.h`、`src/app/CommandLineOptions.cpp`、`tests/unit/SettingsControllerTests.cpp`、`src/app/CMakeLists.txt`、`tests/unit/CMakeLists.txt`。
- **实现行为**：`AppConfig` 仅存在于 `src/app`，组合 `EngineConfig` 和 App 内部 `diagnostics::LogLevel`；`AppConfigOverrides` 用 optional 表示命令行显式出现字段。`SettingsController` 使用显式 `QSettings(iniPath, QSettings::IniFormat)`，持久化 `engine/backend`、`engine/cuda`、`engine/logLevel`、`cache/directory`、`memory/gpuCacheBytes` 和 `memory/cpuCacheBytes`。
- **默认值与损坏值**：缺失文件/键使用既有 EngineConfig 默认值和日志 `info`；空 `cache/directory` 合法；枚举或 uint64 十进制预算损坏时逐字段回退默认并在返回的 `warnings` 中说明原因，不写 stderr 或全局日志；`cache.enabled`、线程数和队列容量不持久化。
- **错误与优先级**：INI 读错误返回 `ErrorDomain::FileIo`/1，写入或 `sync()` 错误返回 `FileIo`/3，并携带路径、QSettings 状态和上下文；命令行显式字段按“命令行 > INI > 默认值”覆盖，非法命令行仍返回 `Configuration`/1，不返回部分合并结果。
- **范围边界**：本任务仅实现配置控制器和单元测试，未接入 `main.cpp`、Engine、MainWindow 或 UI；Qt/QSettings 依赖只存在于 App 和配置测试目标，未修改公共 Engine API。
- **验收测试**：注册 `dzc_settings_controller`（`unit;app;qt;qt003`），覆盖默认值、INI 往返、UTF-8 路径、枚举/预算、空路径、损坏值 warning、FileIo 错误和命令行优先级。`build-qt003` 配置成功，`dzc_settings_controller_tests` 与 `dzc_app` 目标构建成功；`dzc_target_boundary` 通过。由于仓库根目录 `dll/` 缺少 Qt5Core 及相关 Qt 运行时 DLL，按 `agent.md` 未运行 Qt CTest，不能伪造通过。
- **追踪**：DDD-015、21.1
### QT-004 创建 MainWindow 布局

- **状态**：已完成（2026-09-03）
- **目标**：实现菜单、视图区域、参数 Dock、日志 Dock 和状态栏。
- **前置任务**：QT-001
- **实际文件**：`src/app/MainWindow.h`、`src/app/MainWindow.cpp`、`src/app/main.cpp`、`tests/ui/MainWindowSmokeTests.cpp`、`tests/ui/CMakeLists.txt`。
- **实现行为**：使用 C++ + Pimpl 创建 `QMainWindow`，标题固定为 `Dzc-RenderEngine`；以普通 `QWidget` 作为 `renderViewPlaceholder` 中央视图，创建 `datasetDock`、`renderParametersDock` 和 `logDock` 三个初始可见 Dock。数据集 Dock 提供打开/取消、状态、进度和点数静态占位；参数 Dock 提供 Point Size、Shading Mode、Fixed Color、Background Color、CUDA 和 Camera Parameters 静态字段；日志 Dock 使用只读文本占位，状态栏显示 `Ready`。
- **菜单与动作**：创建 `File`、`View`、`Help` 菜单，以及稳定命名的 `openDatasetAction`、`exitAction`、`resetViewAction`、`aboutAction` 和三个 Dock toggle action；QT-004 阶段不连接文件 I/O、退出、Engine 或 ResetView 业务行为。
- **范围边界**：不创建 `QOpenGLWidget`、Vulkan `QWindow`、Engine、PCL 或 CUDA 资源；不接入命令行、SettingsController 或启动流程；公共 Engine API 未修改。
- **验收测试**：独立 `dzc_main_window_smoke` 使用 `QT_QPA_PLATFORM=offscreen` 检查窗口标题、中央占位、三 Dock、菜单、动作、可见性和事件循环自动退出，标签为 `ui;qt;qt004`。
- **验证结果**：使用 Qt 5.15.19、OpenGL ON、Vulkan OFF、CUDA OFF、Tests ON 配置 `build-qt004`；`dzc_main_window_smoke` 和 `dzc_app` 构建成功，CTest `dzc_main_window_smoke` 通过；`dzc_target_boundary` 通过。旧 QT-001 至 QT-003 测试在本目录未重新构建，不能记为本次回归通过。
- **追踪**：FR-UI-001/003/004/005
### QT-005 实现 EngineUiAdapter

- **状态**：未开始
- **目标**：转换 QString/QColor/Qt 输入为标准命令，读取 Snapshot/Event。
- **前置任务**：QT-004, engine-core/EC-008, camera-abstraction/CA-003
- **预计文件**：`src/app/EngineUiAdapter.h`、`src/app/EngineUiAdapter.cpp`、`tests/unit/EngineUiAdapterTests.cpp`
- **实现要求**：Qt 信号槽止于适配器；不传 QEvent/QKeyEvent 到 Engine。
- **验收检查**：文件、颜色、参数和输入均产生预期命令。
- **测试要求**：Fake Engine/command sink 转换测试。
- **追踪**：FR-DATA-002、FR-CAM-001、ADR-004

### QT-006 实现文件加载与取消 UI

- **状态**：未开始
- **目标**：接入文件对话框、进度、取消和错误显示。
- **前置任务**：QT-005
- **预计文件**：`src/app/MainWindow.cpp`、`tests/ui/DatasetLoadUiTests.cpp`
- **实现要求**：只允许需求支持的 PCD/PLY 过滤；加载异步，UI 不冻结。
- **验收检查**：进度更新、取消按钮和后续重新加载可用。
- **测试要求**：Fake Snapshot/Event 驱动 UI 测试。
- **追踪**：FR-DATA-002/003/004、NFR-PERF-003

### QT-007 实现渲染参数 UI

- **状态**：未开始
- **目标**：接入点大小、着色、固定色、背景色和 CUDA 模式。
- **前置任务**：QT-005
- **预计文件**：`src/app/MainWindow.cpp`、`tests/ui/RenderParametersUiTests.cpp`
- **实现要求**：控件变化只 enqueue 命令；属性不可用状态由 Snapshot 提示。
- **验收检查**：各控件产生正确命令且可从设置恢复。
- **测试要求**：UI 信号和配置恢复测试。
- **追踪**：FR-REN-002/003/004、FR-CUDA-001

### QT-008 实现状态与日志显示

- **状态**：未开始
- **目标**：显示后端、FPS、点/块、驻留、加载状态和日志。
- **前置任务**：QT-005, diagnostics/DG-004
- **预计文件**：`src/app/StatusPresenter.h`、`src/app/StatusPresenter.cpp`、`src/app/LogPanelModel.h`、`src/app/LogPanelModel.cpp`、`tests/ui/StatusUiTests.cpp`
- **实现要求**：持续状态读 Snapshot，离散错误读 Event；不复制点云。
- **验收检查**：状态字段格式正确，日志严重级别可区分。
- **测试要求**：Snapshot/Event 输入的 UI 模型测试。
- **追踪**：FR-UI-004/005、FR-STAT-001

### QT-009 实现 OpenGLRenderWidget 宿主

- **状态**：未开始
- **目标**：在 initializeGL/resizeGL/paintGL 驱动 Phase 1 Engine。
- **前置任务**：QT-005, opengl-renderer/GL-007
- **预计文件**：`src/app/OpenGLRenderWidget.h`、`src/app/OpenGLRenderWidget.cpp`、`tests/ui/OpenGLRenderWidgetTests.cpp`
- **实现要求**：GL 生命周期调用线程正确；最小化/隐藏时降低刷新。
- **验收检查**：窗口显示、resize、关闭无 Context 资源错误。
- **测试要求**：Qt+OpenGL 集成测试；无图形环境 Skipped。
- **追踪**：FR-UI-002、4.3、20.2

### QT-010 实现 VulkanRenderWindow 宿主骨架

- **状态**：未开始
- **目标**：创建普通 QWindow、QWidget 容器和线程安全窗口事件。
- **前置任务**：QT-005
- **预计文件**：`src/app/VulkanRenderWindow.h`、`src/app/VulkanRenderWindow.cpp`、`src/app/RenderWindowContainer.h`、`src/app/RenderWindowContainer.cpp`、`tests/ui/VulkanWindowHostTests.cpp`
- **实现要求**：不创建 Vulkan 句柄；只传平台装配描述和 expose/resize/close 控制。
- **验收检查**：QWindow 可嵌入、尺寸事件正确、关闭先请求协调线程停止。
- **测试要求**：无 Vulkan 实现的 Fake host 测试。
- **追踪**：ADR-010、20.3

### QT-011 实现 View Reset 入口但不定义行为

- **状态**：未开始
- **目标**：菜单/按钮只发送 ResetViewCommand。
- **前置任务**：QT-005, camera-abstraction/CA-004
- **预计文件**：`src/app/MainWindow.cpp`、`tests/ui/ViewResetUiTests.cpp`
- **实现要求**：不得在 UI 中计算相机位置或固定快捷键。
- **验收检查**：触发入口只产生一次 ResetViewCommand。
- **测试要求**：UI 命令测试。
- **追踪**：FR-CAM-003、DDD-016

### QT-012 完成 UI 响应集成测试

- **状态**：未开始
- **目标**：在长任务 Fake 场景下验证窗口持续处理事件。
- **前置任务**：QT-006, QT-008, QT-009
- **预计文件**：`tests/integration/UiResponsivenessTests.cpp`
- **实现要求**：测试加载、取消、日志和状态轮询；不得调用真实大数据。
- **验收检查**：长任务期间定时器/交互继续响应，取消可达。
- **测试要求**：Qt event loop 集成测试。
- **追踪**：AC-P1-009/010/013

## 6. 模块级验收

- [ ] Qt 依赖仅存在于 dzc_app
- [ ] 加载、取消、参数、状态和日志 UI 均通过 Fake Engine 测试
- [ ] OpenGL Widget 生命周期正确，Vulkan QWindow 宿主边界已建立
- [ ] UI 不访问 PCL 或任何图形/CUDA 句柄

## 7. 交接记录

- 完成日期：2026-09-03
- 完成人：Codex（按主人确认方案实施）
- 关键变更：完成 QT-001 最小 Qt 5.15.19 Widgets App Target、QT-002 App 内部命令行配置解析器及 QT-003 `SettingsController`/INI 配置基础设施与测试；QT-004 至 QT-012 保持未开始。
- 未解决问题：Qt Application 模块其余任务和模块级验收尚未完成。
- 测试命令与结果：`build-qt003` 按 Qt 5.15.19、OpenGL ON、Vulkan/CUDA OFF、Tests ON 配置成功；`dzc_settings_controller_tests` 与 `dzc_app` 目标构建成功；`dzc_target_boundary` 通过。`dzc_settings_controller` 未运行：仓库根目录 `dll` 缺少 Qt5Core 及相关 Qt 运行时 DLL，按 `agent.md` 规则停止，不能从 Qt 安装目录、其他构建目录或 PATH 复制规避。完整构建仍受既有 PCL/Boost 头文件环境问题影响。
- 关联提交：无（按要求不创建或修改 Git commit）。

## 8. 变更约束

若实现需要改变公共接口、模块依赖、已确认参数或需求行为，必须先更新需求与设计文档，不得在编码任务中自行改变。
