# Project Foundation 任务清单

> 文件：`docs/tasks/project-foundation.md`  
> 所属阶段：公共基础  
> 模块状态：已完成
> 前置模块：无  
> 输入基线：[需求文档](../requirements/spec.md)、[概要设计](../design/architectureDesign.md)、[详细设计](../design/detailDesign.md)、[项目规范](../../agent.md)

## 1. 模块目标

建立可持续扩展的 C++17/CMake 工程骨架、公共类型和后端装配入口，为后续模块提供稳定且不泄漏底层 API 的基础。

## 2. 范围边界

**包含：** 根目录与模块目录；CMake 选项和 Target 骨架；公共基础类型；后端工厂与启动配置模型；基础构建冒烟测试。  
**不包含：** 具体渲染实现；Qt 界面实现；点云读取算法；Camera 具体交互。

## 3. 完成规则

只有同时满足以下条件，模块才可以在 [progress.md](./progress.md) 中标记完成：

- 本文所有非阻塞必需任务均已勾选；
- 所有自动化测试通过，能力缺失用例只能明确标记为 Skipped，不能伪造通过；
- 对应公共接口和私有实现符合 `agent.md` 的命名、Pimpl、RAII 和依赖边界；
- 相关需求、设计和测试文档已经同步；

## 4. 子任务 Checklist

- [x] **PF-001 创建源码与测试目录骨架**
- [x] **PF-002 定义根 CMake 选项**
- [x] **PF-003 创建模块 Target 骨架**
- [x] **PF-004 实现强类型 ID 和基础值类型**
- [x] **PF-005 实现 Result 和 Error 基础模型**
- [x] **PF-006 定义公共 Engine 配置类型**
- [x] **PF-007 创建后端无关工厂契约**
- [x] **PF-008 添加基础构建冒烟测试**

## 5. 子任务说明

### PF-001 创建源码与测试目录骨架

- **状态**：已完成
- **目标**：按详细设计 2.1 和 24.1 创建空目录与最小占位文件。
- **前置任务**：无
- **预计文件**：`CMakeLists.txt`、`cmake/`、`include/dzc/`、`src/`、`shaders/`、`tests/`、`samples/`
- **实现要求**：只创建已确认的模块目录；不得加入业务实现或未确认第三方库。
- **验收检查**：所有设计约定目录存在，空目录使用说明文件保留，目录名称与设计一致。
- **测试要求**：运行目录结构检查脚本；确认无拼音命名。
- **追踪**：ADR-003、NFR-MAIN-005

### PF-002 定义根 CMake 选项

- **状态**：已完成
- **目标**：加入 OpenGL/Vulkan/CUDA/Tests 四个构建开关和 C++17 基线。
- **前置任务**：PF-001
- **预计文件**：`CMakeLists.txt`、`cmake/DzcOptions.cmake`
- **实现要求**：默认 OpenGL=ON、Vulkan=OFF、CUDA=OFF、Tests=ON；至少一个图形后端开启；开启依赖缺失时明确失败。
- **验收检查**：CMake 配置输出所选后端；无图形后端配置失败；仅 OpenGL 配置不要求 Vulkan/CUDA。
- **测试要求**：执行默认配置、全关闭失败配置、OpenGL-only 配置。
- **追踪**：FR-COM-001、ADR-002、24.2

### PF-003 创建模块 Target 骨架

- **状态**：已完成
- **目标**：建立详细设计 24.1 中的独立 CMake Target 和单向依赖。
- **前置任务**：PF-002
- **预计文件**：`src/CMakeLists.txt`、`src/*/CMakeLists.txt`
- **实现要求**：公共 API、Engine、Data、Render、Compute、Tasks、Diagnostics、Platform、App 必须分 Target；具体后端不得反向依赖 UI。
- **依赖延迟约束（主人确认，2026-08-14）**：PF-003 只创建 `INTERFACE` Target 和内部依赖图，不强制查找或链接 Qt Widgets/PCL；后续实现时仅允许 `dzc_app` 查找并链接 Qt Widgets，仅允许 `dzc_data_pcl` 查找并链接 PCL，其他 Target 不得引入这两类依赖。
- **验收检查**：CMake 生成成功；依赖图中 PCL 只位于 dzc_data_pcl，Qt Widgets 只位于 dzc_app。
- **测试要求**：添加 CMake 配置测试或脚本检查 Target 与链接边界。
- **追踪**：ADR-003、NFR-MAIN-004

### PF-004 实现强类型 ID 和基础值类型

- **状态**：已完成
- **目标**：实现 DatasetId、ChunkId、FrameId、TaskId、RenderSize、ColorRgba。
- **前置任务**：PF-003
- **预计文件**：`include/dzc/EngineTypes.h`、`src/engine/EngineTypes.cpp`、`tests/unit/EngineTypesTests.cpp`、`tests/unit/CMakeLists.txt`、根 `CMakeLists.txt` 测试接入
- **实际变更**：新增六个后端无关公共值类型；ID 默认值为 0；为六个类型提供相等/不等比较；公共路径继续使用 UTF-8 `std::string`。
- **实现要求**：0 为无效 ID；类型间禁止隐式转换；路径仍使用 UTF-8 std::string；公共头不含 Qt/PCL/GPU 类型。
- **验收检查**：编译期不能混用不同 ID；默认值与比较行为符合详细设计。
- **测试要求**：单元测试默认值、比较、类型不可转换静态断言。
- **测试接入决策（主人确认，2026-08-14）**：本任务采用自包含的最小测试程序并接入 CMake/CTest；不提前创建通用轻量测试框架。后续测试框架实现时可复用本测试用例，不改变公共类型接口。
- **验收结果**：Vulkan-only 配置、构建和 CTest 均通过，`2/2` 测试通过。
- **追踪**：3.1、3.4、NFR-MAIN-004

### PF-005 实现 Result 和 Error 基础模型

- **状态**：已完成
- **目标**：提供 ErrorDomain、Error、Result<T> 和 Result<void>。
- **前置任务**：PF-004
- **预计文件**：`include/dzc/Result.h`、`include/dzc/Error.h`、`tests/unit/ResultTests.cpp`、`tests/unit/CMakeLists.txt`
- **实际变更**：新增 12 类错误域、Error 值类型、基于 C++17 `std::variant` 的 `Result<T>` 及 `Result<void>` 特化；保留值语义并支持移动类型。
- **实现要求**：使用 C++17 值语义；异常不得跨公共接口；访问错误分支必须有可诊断保护。
- **访问保护决策（主人确认，2026-08-14）**：错误分支访问在 Debug 构建中通过 `assert` 报告，在 Release 构建中调用 `std::terminate()`；不让 `std::bad_variant_access` 跨越公共接口。
- **验收检查**：成功与失败结果均可构造和读取；Result<void> 行为一致。
- **测试要求**：覆盖 value/error 分支、移动类型和 Result<void>。
- **验收结果**：默认 OpenGL 配置、构建和 CTest 均通过，`3/3` 测试通过。
- **追踪**：FR-COM-002、ADR-012、NFR-REL-001

### PF-006 定义公共 Engine 配置类型

- **状态**：已完成
- **目标**：实现 RenderBackendType、OptionalFeatureMode、ShadingMode、ThreadConfig、MemoryBudgetConfig、CacheConfig、EngineConfig。
- **前置任务**：PF-005
- **预计文件**：`include/dzc/EngineConfig.h`、`tests/unit/EngineConfigTests.cpp`、`tests/unit/CMakeLists.txt`
- **实际变更**：新增三个后端/能力枚举及 ThreadConfig、MemoryBudgetConfig、CacheConfig、EngineConfig；默认 OpenGL、CUDA auto、队列容量 1024、I/O 并发 2；线程数和 CPU/GPU 预算中的 0 保持自动计算语义。
- **实现要求**：默认 OpenGL、CUDA auto、队列容量 1024、I/O 并发 2；0 表示自动计算。
- **容量校验决策（主人确认，2026-08-14）**：command/event 队列容量必须大于 0；`EngineConfig::hasValidQueueCapacities()` 返回无异常校验结果。启动时将非法配置转换为 Configuration 错误的逻辑保留给后续 Engine/工厂任务。
- **验收检查**：默认构造值与 DDD-004/005/006 一致；公共接口无实现句柄。
- **测试要求**：单元测试全部默认值和非法容量校验入口。
- **验收结果**：默认 OpenGL 配置、构建和 CTest 均通过，`4/4` 测试通过；公共头仅包含项目头和标准库头，未包含 Qt、PCL 或 GPU SDK 头。
- **追踪**：DDD-004、DDD-005、DDD-006

### PF-007 创建后端无关工厂契约

- **状态**：已完成
- **目标**：定义仅供 Composition Root 使用的 Render/Compute 工厂输入和失败语义。
- **前置任务**：PF-006
- **预计文件**：`src/render/common/RenderBackendFactory.h`、`src/compute/common/ComputeBackendFactory.h`、`src/app/ApplicationComposition.h`
- **实际变更**：新增后端无关的 `IRenderBackend`、`IComputeBackend` 最小抽象，注入式 Render/Compute creator，`ApplicationComposition` 组装入口，以及 `DisabledComputeBackend` 和 CUDA 降级结果标记；未实现具体 OpenGL、Vulkan、CUDA 后端。
- **实现要求**：显式后端不可用时返回失败；CUDA auto 可降级；不得使用全局单例。
- **主人确认方案（2026-08-14）**：PF-007 只定义供 Composition Root 使用的内部契约；工厂使用 `EngineConfig` 输入和 `Result<std::unique_ptr<...>>` 失败语义；通过可注入 Fake creator 测试，不引入 Qt、PCL、OpenGL/Vulkan/CUDA SDK 头、GPU 句柄或全局单例。CUDA `Off` 使用 Disabled Compute，`On` 创建失败返回错误，`Auto` 创建失败返回 Disabled Compute 并标记 `degraded=true`。
- **兼容性补充**：为支持工厂和 Composition Root 转移 `std::unique_ptr`，`Result<T>` 增加非 const `value()` 重载；保留原有 const 访问接口和错误访问保护语义。
- **验收检查**：工厂使用 Fake 实现完成装配；Render 显式失败转发；CUDA `On` 显式失败；CUDA `Auto` 降级；接口不出现 Qt/PCL/GPU 句柄。
- **测试要求**：Fake 工厂测试覆盖 Render 成功/失败转发、CUDA `Off` 不调用 creator、CUDA `On` 失败和 `Auto` 降级、Composition Root Fake 装配。
- **验收结果**：默认 OpenGL 配置、构建和 CTest 均通过，`5/5` 测试通过（含 Target 边界检查）。
- **追踪**：DDD-004、DDD-005、DDD-006、ADR-002、FR-COM-001、FR-CUDA-001

### PF-008 添加基础构建冒烟测试

- **状态**：已完成
- **目标**：为默认和 OpenGL-only 配置建立可重复构建检查。
- **前置任务**：PF-007
- **预计文件**：`tests/cmake/ConfigureSmoke.cmake`、`CTestConfig.cmake`
- **主人确认方式（2026-08-14）**：`CTestConfig.cmake` 作为项目级 CTest 配置入口，由根 `CMakeLists.txt` 在启用测试时引入；`tests/cmake/ConfigureSmoke.cmake` 执行隔离的子配置并验证结果。
- **实际变更**：注册 `dzc_configure_smoke_default` 与 `dzc_configure_smoke_opengl_only` 两个 `configure;smoke` 标签测试；子配置关闭内部测试以避免链接未实现模块，验证 OpenGL/Vulkan/CUDA 选项缓存值和生成的 Target manifest；配置失败时输出退出码、stdout 和 stderr。为保证 CTest 子进程下可重复执行，显式传递外层 generator、make 程序和 C++ 编译器，并使用静态库 try-compile 避免链接阶段依赖。
- **实现要求**：测试只验证工程结构和依赖选择，不要求尚未实现模块通过链接。
- **验收检查**：CTest 可运行默认和显式 OpenGL-only 配置用例；失败消息包含退出码、stdout 和 stderr。
- **测试要求**：执行 cmake configure、完整 ctest 以及 `configure` 标签 ctest。
- **验收结果**：使用 CMake 4.3.3、Visual Studio 18 2026/NMake Makefiles，默认 OpenGL 配置、构建和完整 CTest 均通过，`7/7` 测试通过；`ctest -L configure` 的两个 PF-008 冒烟用例均通过，`2/2` 通过。
- **追踪**：AC-P1-001、NFR-TEST-002

## 6. 模块级验收

- [x] 默认 CMake 配置成功且使用 C++17
- [x] 无图形后端时配置明确失败
- [x] 公共头文件不含 Qt、PCL、OpenGL、Vulkan、CUDA 类型
- [x] 基础类型和 Result 单元测试通过
- [x] PF-007 Fake 工厂装配、显式失败和 CUDA auto 降级测试通过
- [x] PF-007 工厂与 Composition Root 接口不包含 Qt、PCL 或 GPU 句柄
- [x] PF-008 默认与显式 OpenGL-only 配置冒烟测试可通过 CTest 标签运行

## 7. 交接记录

- 完成日期：2026-08-14
- 完成人：Codex（按主人确认执行）
- 关键变更：PF-001 至 PF-008 已全部完成。工程已具备 CMake 3.21/C++17 基线、可选 OpenGL/Vulkan/CUDA 配置、模块 Target 边界、公共基础类型和错误模型、Engine 配置、后端无关工厂契约及 Composition Root；PF-008 新增项目级 CTest 配置入口和默认/显式 OpenGL-only 的隔离配置冒烟测试。
- 未解决问题：Project Foundation 模块无未完成必需任务。Qt Widgets/PCL 依赖发现、具体 OpenGL/Vulkan/CUDA 后端、队列非法容量转换为 Configuration 错误均属于后续模块任务，未在本模块提前实现。
- 测试命令与结果：使用 CMake 4.3.3、Visual Studio 18 2026/NMake Makefiles 执行 `cmake -S . -B .pf008-build -G "NMake Makefiles" -DDZC_BUILD_TESTS=ON`、`cmake --build .pf008-build`、`ctest --test-dir .pf008-build --output-on-failure`，完整 CTest `7/7` 通过；`ctest --test-dir .pf008-build -L configure --output-on-failure`，PF-008 标签测试 `2/2` 通过。
- 关联提交：未提交

## 8. 变更约束

若实现需要改变公共接口、模块依赖、已确认参数或需求行为，必须先更新需求与设计文档，不得在编码任务中自行改变。
