# Qt Application 任务清单

> 文件：`docs/tasks/qt-application.md`  
> 所属阶段：Phase 1/Phase 2 公共 UI  
> 模块状态：未开始  
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

- [ ] **QT-001 配置 Qt 5.15.19 App Target**
- [ ] **QT-002 实现命令行配置解析**
- [ ] **QT-003 实现 QSettings/INI 控制器**
- [ ] **QT-004 创建 MainWindow 布局**
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

- **状态**：未开始
- **目标**：建立 dzc_app 并链接所需 Qt 模块。
- **前置任务**：project-foundation/PF-003
- **预计文件**：`src/app/CMakeLists.txt`、`src/app/main.cpp`
- **实现要求**：Qt 只在 App Target；Engine 公共库不链接 Qt。
- **验收检查**：最小空窗口可构建启动。
- **测试要求**：CMake 依赖边界和 GUI 冒烟测试。
- **追踪**：FR-UI-001、NFR-PORT-001

### QT-002 实现命令行配置解析

- **状态**：未开始
- **目标**：解析 backend/cuda/log/cache/budget 等参数。
- **前置任务**：QT-001, project-foundation/PF-006
- **预计文件**：`src/app/CommandLineOptions.h`、`src/app/CommandLineOptions.cpp`、`tests/unit/CommandLineOptionsTests.cpp`
- **实现要求**：命令行优先；非法值显示用法；显式后端失败不静默切换。
- **验收检查**：默认值和覆盖值生成正确 EngineConfig。
- **测试要求**：所有参数、非法值和重复参数测试。
- **追踪**：FR-COM-001、DDD-004

### QT-003 实现 QSettings/INI 控制器

- **状态**：未开始
- **目标**：保存 UI 设置并转换成标准配置。
- **前置任务**：QT-002
- **预计文件**：`src/app/SettingsController.h`、`src/app/SettingsController.cpp`、`tests/unit/SettingsControllerTests.cpp`
- **实现要求**：Engine 不依赖 QSettings；命令行覆盖设置。
- **验收检查**：往返保存后配置一致，损坏值回退默认并警告。
- **测试要求**：临时 INI 往返和优先级测试。
- **追踪**：DDD-015、21.1

### QT-004 创建 MainWindow 布局

- **状态**：未开始
- **目标**：实现菜单、视图区域、参数 Dock、日志 Dock 和状态栏。
- **前置任务**：QT-001
- **预计文件**：`src/app/MainWindow.h`、`src/app/MainWindow.cpp`、`src/app/MainWindow.ui`、`tests/ui/MainWindowSmokeTests.cpp`
- **实现要求**：UI 只显示/发送数据；不解析文件或持有 GPU 对象。
- **验收检查**：主要控件存在，窗口可显示和关闭。
- **测试要求**：Qt offscreen 冒烟测试。
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

- 完成日期：
- 完成人：
- 关键变更：
- 未解决问题：
- 测试命令与结果：
- 关联提交：

## 8. 变更约束

若实现需要改变公共接口、模块依赖、已确认参数或需求行为，必须先更新需求与设计文档，不得在编码任务中自行改变。
