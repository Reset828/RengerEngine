# Project Foundation 任务清单

> 文件：`docs/tasks/project-foundation.md`  
> 所属阶段：公共基础  
> 模块状态：未开始  
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

- [ ] **PF-001 创建源码与测试目录骨架**
- [ ] **PF-002 定义根 CMake 选项**
- [ ] **PF-003 创建模块 Target 骨架**
- [ ] **PF-004 实现强类型 ID 和基础值类型**
- [ ] **PF-005 实现 Result 和 Error 基础模型**
- [ ] **PF-006 定义公共 Engine 配置类型**
- [ ] **PF-007 创建后端无关工厂契约**
- [ ] **PF-008 添加基础构建冒烟测试**

## 5. 子任务说明

### PF-001 创建源码与测试目录骨架

- **状态**：未开始
- **目标**：按详细设计 2.1 和 24.1 创建空目录与最小占位文件。
- **前置任务**：无
- **预计文件**：`CMakeLists.txt`、`cmake/`、`include/dzc/`、`src/`、`shaders/`、`tests/`、`samples/`
- **实现要求**：只创建已确认的模块目录；不得加入业务实现或未确认第三方库。
- **验收检查**：所有设计约定目录存在，空目录使用说明文件保留，目录名称与设计一致。
- **测试要求**：运行目录结构检查脚本；确认无拼音命名。
- **追踪**：ADR-003、NFR-MAIN-005

### PF-002 定义根 CMake 选项

- **状态**：未开始
- **目标**：加入 OpenGL/Vulkan/CUDA/Tests 四个构建开关和 C++17 基线。
- **前置任务**：PF-001
- **预计文件**：`CMakeLists.txt`、`cmake/DzcOptions.cmake`
- **实现要求**：默认 OpenGL=ON、Vulkan=OFF、CUDA=OFF、Tests=ON；至少一个图形后端开启；开启依赖缺失时明确失败。
- **验收检查**：CMake 配置输出所选后端；无图形后端配置失败；仅 OpenGL 配置不要求 Vulkan/CUDA。
- **测试要求**：执行默认配置、全关闭失败配置、OpenGL-only 配置。
- **追踪**：FR-COM-001、ADR-002、24.2

### PF-003 创建模块 Target 骨架

- **状态**：未开始
- **目标**：建立详细设计 24.1 中的独立 CMake Target 和单向依赖。
- **前置任务**：PF-002
- **预计文件**：`src/CMakeLists.txt`、`src/*/CMakeLists.txt`
- **实现要求**：公共 API、Engine、Data、Render、Compute、Tasks、Diagnostics、Platform、App 必须分 Target；具体后端不得反向依赖 UI。
- **验收检查**：CMake 生成成功；依赖图中 PCL 只位于 dzc_data_pcl，Qt Widgets 只位于 dzc_app。
- **测试要求**：添加 CMake 配置测试或脚本检查 Target 与链接边界。
- **追踪**：ADR-003、NFR-MAIN-004

### PF-004 实现强类型 ID 和基础值类型

- **状态**：未开始
- **目标**：实现 DatasetId、ChunkId、FrameId、TaskId、RenderSize、ColorRgba。
- **前置任务**：PF-003
- **预计文件**：`include/dzc/EngineTypes.h`、`src/engine/EngineTypes.cpp`、`tests/unit/EngineTypesTests.cpp`
- **实现要求**：0 为无效 ID；类型间禁止隐式转换；路径仍使用 UTF-8 std::string；公共头不含 Qt/PCL/GPU 类型。
- **验收检查**：编译期不能混用不同 ID；默认值与比较行为符合详细设计。
- **测试要求**：单元测试默认值、比较、类型不可转换静态断言。
- **追踪**：3.1、3.4、NFR-MAIN-004

### PF-005 实现 Result 和 Error 基础模型

- **状态**：未开始
- **目标**：提供 ErrorDomain、Error、Result<T> 和 Result<void>。
- **前置任务**：PF-004
- **预计文件**：`include/dzc/Result.h`、`include/dzc/Error.h`、`tests/unit/ResultTests.cpp`
- **实现要求**：使用 C++17 值语义；异常不得跨公共接口；访问错误分支必须有可诊断保护。
- **验收检查**：成功与失败结果均可构造和读取；Result<void> 行为一致。
- **测试要求**：覆盖 value/error 分支、移动类型和 Result<void>。
- **追踪**：FR-COM-002、ADR-012、NFR-REL-001

### PF-006 定义公共 Engine 配置类型

- **状态**：未开始
- **目标**：实现 RenderBackendType、OptionalFeatureMode、ShadingMode、ThreadConfig、MemoryBudgetConfig、CacheConfig、EngineConfig。
- **前置任务**：PF-005
- **预计文件**：`include/dzc/EngineConfig.h`、`tests/unit/EngineConfigTests.cpp`
- **实现要求**：默认 OpenGL、CUDA auto、队列容量 1024、I/O 并发 2；0 表示自动计算。
- **验收检查**：默认构造值与 DDD-004/005/006 一致；公共接口无实现句柄。
- **测试要求**：单元测试全部默认值和非法容量校验入口。
- **追踪**：DDD-004、DDD-005、DDD-006

### PF-007 创建后端无关工厂契约

- **状态**：未开始
- **目标**：定义仅供 Composition Root 使用的 Render/Compute 工厂输入和失败语义。
- **前置任务**：PF-006
- **预计文件**：`src/render/common/RenderBackendFactory.h`、`src/compute/common/ComputeBackendFactory.h`、`src/app/ApplicationComposition.h`
- **实现要求**：显式后端不可用时返回失败；CUDA auto 可降级；不得使用全局单例。
- **验收检查**：工厂能用 Fake 实现完成装配；接口不出现 Qt/PCL/GPU 句柄。
- **测试要求**：Fake 工厂测试显式失败和 auto 降级。
- **追踪**：ADR-002、FR-COM-001、FR-CUDA-001

### PF-008 添加基础构建冒烟测试

- **状态**：未开始
- **目标**：为默认和 OpenGL-only 配置建立可重复构建检查。
- **前置任务**：PF-007
- **预计文件**：`tests/cmake/ConfigureSmoke.cmake`、`CTestConfig.cmake`
- **实现要求**：测试只验证工程结构和依赖选择，不要求尚未实现模块通过链接。
- **验收检查**：CTest 可运行配置用例，并准确报告失败原因。
- **测试要求**：执行 cmake configure 和 ctest 对应标签。
- **追踪**：AC-P1-001、NFR-TEST-002

## 6. 模块级验收

- [ ] 默认 CMake 配置成功且使用 C++17
- [ ] 无图形后端时配置明确失败
- [ ] 公共头文件不含 Qt、PCL、OpenGL、Vulkan、CUDA 类型
- [ ] 基础类型和 Result 单元测试通过

## 7. 交接记录

- 完成日期：
- 完成人：
- 关键变更：
- 未解决问题：
- 测试命令与结果：
- 关联提交：

## 8. 变更约束

若实现需要改变公共接口、模块依赖、已确认参数或需求行为，必须先更新需求与设计文档，不得在编码任务中自行改变。
