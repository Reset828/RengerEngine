# Camera Abstraction 任务清单

> 文件：`docs/tasks/camera-abstraction.md`  
> 所属阶段：Phase 1（部分阻塞）  
> 模块状态：进行中
> 前置模块：[project-foundation](./project-foundation.md)、[point-cloud-data](./point-cloud-data.md)  
> 输入基线：[需求文档](../requirements/spec.md)、[概要设计](../design/architectureDesign.md)、[详细设计](../design/detailDesign.md)、[项目规范](../../agent.md)

## 1. 模块目标

建立 CameraState、CameraMatrices、ViewFrustum、InputEvent 和 ICameraController 抽象，并实现已确认交互规则的 GLM-only 透视轨道控制器；Engine/Qt 集成和性能路径继续独立推进。

## 2. 范围边界

**包含：** 相机数据结构；抽象输入事件；控制器接口；矩阵/视锥体验证工具；Fake Controller；`OrbitCameraController` 透视轨道交互与相机相对矩阵；仅测试使用的内存确定性相机路径回放。
**不包含：** Engine Controller 注入/命令转发；Qt 到抽象输入事件映射；GPU/渲染器集成；键盘交互；惯性/速度模型；文件序列化或正式 FPS 性能采集。

## 3. 完成规则

只有同时满足以下条件，模块才可以在 [progress.md](./progress.md) 中标记完成：

- 本文所有非阻塞必需任务均已勾选；
- 所有自动化测试通过，能力缺失用例只能明确标记为 Skipped，不能伪造通过；
- 对应公共接口和私有实现符合 `agent.md` 的命名、Pimpl、RAII 和依赖边界；
- 相关需求、设计和测试文档已经同步；
- CA-001 至 CA-007 已完成；但 Engine Controller 注入、命令转发、Qt 映射及正式 Renderer 性能验收仍未完成，因此本模块继续保持“进行中”。

## 4. 子任务 Checklist

- [x] **CA-001 定义 CameraState 和 CameraMatrices**
- [x] **CA-002 定义 ViewFrustum 和平面工具**
- [x] **CA-003 定义 InputEvent**
- [x] **CA-004 定义 ICameraController 和 Fake**
- [x] **CA-005 分析用户提供的相机参考源码**
- [x] **CA-006 实现确认后的具体 Camera Controller**
- [x] **CA-007 定义可重复性能相机路径**

## 5. 子任务说明

### CA-001 定义 CameraState 和 CameraMatrices

- **状态**：已完成（2026-08-20）
- **目标**：实现详细设计 19.1 的纯数据结构。
- **前置任务**：project-foundation/PF-004, point-cloud-data/PD-002
- **实际文件**：`include/dzc/CameraTypes.h`、`tests/unit/CameraTypesTests.cpp`、`src/CMakeLists.txt`、`src/data/CMakeLists.txt`、`tests/unit/CMakeLists.txt`
- **实现结果**：新增 `CameraState` 与 `CameraMatrices` 两个 `final` 值类型。`CameraState` 保存 double position/orientation 与 FOV、near/far 参数；`CameraMatrices` 保存 float view/projection 矩阵与 double cameraOrigin。
- **默认值语义**：默认值严格来自详细设计 19.1（零位置/原点、单位四元数、单位矩阵、零 FOV/near/far），仅表示可复制的数据初值，不声称构成可用相机配置，也不执行有效性校验或矩阵推导。
- **依赖边界**：公共头文件仅使用 GLM，不含 Qt、渲染后端或具体控制器行为；GLM 作为 `dzc_engine_api` 的传递公共依赖，数据目标继续复用该包。
- **验收检查**：默认值、精确成员类型、默认/复制/移动构造和赋值，以及 double position/cameraOrigin 数据保持均由断言测试覆盖。
- **验证结果**：MSVC 19.51.36246.0 x64 / Visual Studio 18 2026 OpenGL-only Debug，使用 `D:\vcpkg\vcpkg\installed\x64-windows` 的 GLM CMake 包配置；全量构建成功；`dzc_camera_types` 专项测试 1/1 通过；设置 `CMAKE_PREFIX_PATH=D:\vcpkg\vcpkg\installed\x64-windows` 后完整 CTest 44/44 通过；`git diff --check` 通过；任务专用 `build-ca001` 已清理。
- **边界**：不实现 ViewFrustum、InputEvent、ICameraController、Fake Controller、具体键位/鼠标行为、速度、初始视图、重置语义或性能运动路径。
- **追踪**：DDD-016、FR-CAM-001

### CA-002 定义 ViewFrustum 和平面工具

- **状态**：已完成（2026-08-20）
- **目标**：实现六平面结构、规范化和有限性校验。
- **前置任务**：CA-001
- **实际文件**：`include/dzc/Bounds3d.h`、`include/dzc/ViewFrustum.h`、`src/data/chunk/Bounds3d.cpp`、`src/camera/ViewFrustum.cpp`、`tests/unit/Bounds3dTests.cpp`、`tests/unit/ViewFrustumTests.cpp`、`src/engine/CMakeLists.txt`、`tests/unit/CMakeLists.txt`。
- **实现结果**：`Bounds3d` 已提升为公共值类型；新增 `FrustumPlane`、`ViewFrustum` 和 `ClipDepthRange`。平面内侧统一为 `ax + by + cz + d >= 0`，固定下标顺序为 Left、Right、Bottom、Top、Near、Far；`normalized()` 返回完整新值，不修改源对象。
- **矩阵与可见性语义**：`fromViewProjection(const glm::mat4&, ClipDepthRange)` 显式支持 `NegativeOneToOne` 和 `ZeroToOne` 两种裁剪深度，按 GLM 列主序转换数学行后提取并规范化。`intersects(const Bounds3d&)` 使用每平面正顶点的保守 AABB 测试；接触平面视为可见，返回 `true` 表示相交或在内，`false` 表示完全在外。
- **错误语义**：非有限矩阵/平面、零法线、规范化计算非有限、非法裁剪深度、非法 Bounds 或交集计算非有限均返回 `ErrorDomain::DataFormat`、错误码 `2`（`CorruptData`），且用户信息、诊断信息和上下文非空；失败不返回部分结果。
- **边界**：不实现具体控制器、相机矩阵生成、Qt、渲染后端、GPU、Chunk 调度或输入行为；零法线之外不引入任意 epsilon 退化阈值。
- **验收检查**：平面/视锥体值语义、规范化、两种深度矩阵提取、平面顺序与符号、Bounds 全内/全外/接触/相交以及全部错误路径均由断言测试覆盖；原有 `Bounds3d` 行为测试保留并改为公共头文件包含。
- **验证结果**：MSVC 19.51.36246.0 x64 / Visual Studio 18 2026 生成器 OpenGL-only Debug（本机 Ninja 不在 PATH，故未使用 Ninja），使用 `D:\vcpkg\vcpkg\installed\x64-windows` 的 GLM CMake 包配置；全量构建成功；`dzc_view_frustum` 专项测试 1/1 通过；设置 `CMAKE_PREFIX_PATH=D:\vcpkg\vcpkg\installed\x64-windows` 后完整 CTest 45/45 通过；`git diff --check` 通过；任务专用 `build-ca002` 已清理。
- **追踪**：FR-VIS-002、19.1

### CA-003 定义 InputEvent

- **状态**：已完成（2026-08-21）
- **目标**：实现详细设计 19.1 已定义的后端无关输入事件值类型。
- **前置任务**：CA-001
- **实际文件**：`include/dzc/InputEvent.h`、`tests/unit/InputEventTests.cpp`、`tests/unit/CMakeLists.txt`。
- **实现结果**：新增底层类型为 `std::uint8_t` 的 `InputEventType`，固定顺序为 `PointerMove`、`PointerButton`、`Wheel`、`Key`、`Focus`、`ResetRequest`；新增 `InputEvent final`，字段严格为 `type`、`code`、`valueX`、`valueY`、`pressed`、`modifiers`，并保持详细设计规定的默认值。
- **数值与依赖边界**：`code`、`modifiers` 仅为抽象 `uint32_t` 数值，`valueX/valueY` 仅为原样保存的 `double`。类型不验证、拒绝或规范化 NaN、无穷、负零、数值范围或字段组合；公共头仅包含 `<cstdint>`，不依赖 Qt、GLM、平台或引擎实现。
- **边界**：不实现 Qt 映射、事件分发、EngineCommand 接入、矩阵更新、控制器、具体键位/鼠标语义、速度、初始视图或重置行为。
- **验收检查**：枚举底层类型及六个固定值、精确成员类型、默认/复制/移动值语义、全部事件类型、整数与 double 边界（含 NaN、正负无穷和负零）的原样保持均由断言测试覆盖。
- **验证结果**：MSVC 19.51.36246.0 x64 / Visual Studio 18 2026 开发环境下的 Ninja OpenGL-only Debug，使用 `D:\vcpkg\vcpkg\installed\x64-windows` 的 GLM CMake 包配置；全量构建成功；`dzc_input_event` 专项测试 1/1 通过；设置环境变量 `CMAKE_PREFIX_PATH=D:\vcpkg\vcpkg\installed\x64-windows` 后完整 CTest 46/46 通过；`git diff --check` 通过；任务专用 `build-ca003` 已清理。
- **追踪**：FR-CAM-001

### CA-004 定义 ICameraController 和 Fake

- **状态**：已完成（2026-08-21）
- **目标**：实现详细设计 19.1 已定义的后端无关相机控制器纯抽象接口，并提供仅供测试使用的 Fake。
- **前置任务**：CA-002, CA-003
- **实际文件**：`include/dzc/ICameraController.h`、`tests/fakes/FakeCameraController.h`、`tests/unit/CameraControllerContractTests.cpp`、`tests/unit/CMakeLists.txt`。
- **实现结果**：新增纯虚 `ICameraController`，严格声明 `submitInput`、`update`、`state`、`matrices`、`frustum`、`reset` 六项详细设计接口，并提供虚析构函数。接口使用既有 `InputEvent`、`Bounds3d`、`CameraState`、`CameraMatrices`、`RenderSize`、`ViewFrustum` 和 `Result<void>` 公共类型。
- **Fake 契约**：`FakeCameraController` 仅记录每种调用的次数和最近一次输入参数，可预置 state、matrices、frustum 及 submit/update/reset 的成功或失败 `Result<void>`；它不推导矩阵、不改变相机状态、不定义输入或重置语义。
- **依赖与边界**：公共接口和 Fake 均不依赖 Qt、平台、渲染后端或具体控制器。本任务按已确认边界不修改 `Engine`，不增加 Controller 注入、`SubmitInputCommand`、Reset 转发或任何所有权模型；具体键位、鼠标、速度、初始视图、near/far 策略和重置效果仍未实现。
- **验收检查**：契约测试覆盖多态调用、完整方法签名（包括 `state()` 的 const/noexcept）、虚析构、预置成功/失败结果、返回值保持以及调用参数记录。原“Engine 可注入 Fake”的检查属于尚未确认的后续 Engine 集成任务；模块级“Fake Controller 可驱动 Engine 与剔除测试”继续保持未勾选。
- **验证结果**：MSVC 19.51.36246.0 x64 / Visual Studio 18 2026 开发环境下的 Ninja OpenGL-only Debug，使用 `D:\vcpkg\vcpkg\installed\x64-windows` 的 GLM CMake 包配置；全量构建成功；`dzc_camera_controller_contract` 专项测试 1/1 通过；设置环境变量 `CMAKE_PREFIX_PATH=D:\vcpkg\vcpkg\installed\x64-windows` 后完整 CTest 47/47 通过；`git diff --check` 通过；任务专用 `build-ca004` 已清理。
- **追踪**：FR-CAM-001/003、DDD-016

### CA-005 分析用户提供的相机参考源码

- **状态**：已完成（2026-08-21）
- **目标**：仅从用户提供的 `camera.cpp` 提取透视轨道球交互、旋转限制、平移、缩放、reset 和裁剪面规则，并适配当前公共接口。
- **实际文件**：`docs/design/cameraInteractionDesign.md`、`docs/requirements/spec.md`、`docs/design/architectureDesign.md`、`docs/design/detailDesign.md`、本任务清单和 `progress.md`。
- **确认结果**：采用右手、世界 `+Y` 向上的透视轨道相机；左键轨迹球、右键屏幕平面平移、滚轮 0.9/1.1 固定倍率、45° FOV、`[0.1, 1000]` 仅滚轮距离限制、`worldZ.y >= -1e-6` 与 16 次二分防翻转、Bounds 包围球 1.05 倍自动框选、动态 near/far 均已记录。`ResetRequest` 在下一次 `update()` 延迟执行；首次无有效尺寸的 reset 失败；resize 保持当前视角。
- **边界**：不复用参考 Vulkan/渲染器/模型变换接口。CA-006 已独立实现 Controller、矩阵/视锥和固定 OpenGL `NegativeOneToOne` 深度约定；Engine 注入或命令转发、Qt 映射、GPU/渲染器集成及性能路径仍不在范围内。
- **验收检查**：用户已明确确认交互细节；FR-CAM-002/003、概要设计和详细设计已同步；文档交叉审查与 `git diff --check` 见本任务交接记录。
- **测试要求**：文档审查已完成；其 Golden/行为测试已由 CA-006 `dzc_orbit_camera_controller` 覆盖。
- **追踪**：FR-CAM-002、FR-CAM-003、TBD-004

### CA-006 实现确认后的具体 Camera Controller

- **状态**：已完成（2026-08-21）
- **目标**：将 CA-005 已确认的轨迹球规则实现为后端无关的 `OrbitCameraController`。
- **前置任务**：CA-005
- **实际文件**：`include/dzc/OrbitCameraController.h`、`src/camera/OrbitCameraController.cpp`、`src/engine/CMakeLists.txt`、`tests/unit/OrbitCameraControllerTests.cpp`、`tests/unit/CMakeLists.txt`。
- **实现结果**：新增 GLM-only、Pimpl 封装、不可复制可移动的 `OrbitCameraController final`，并仅通过既有 `ICameraController` 提供输入、更新、状态、矩阵、视锥和 reset。默认 target 为零、distance 为 3、单位方向、45° 垂直 FOV、near/far 为 0.001/1000；相机局部 `-Z` 指向 target。
- **交互与错误语义**：实现左键虚拟球 270° 灵敏度、`world +Z.y >= -1e-6` 的固定 16 次二分防翻转、右键屏幕 right/up 平移、滚轮 0.9/1.1 与 `[0.1,1000]` 交互限制、Focus 取消拖拽及延迟 `ResetRequest`。无效抽象输入返回 `General/1`；非法 Bounds、尺寸前置条件或数值计算返回 `DataFormat/2`；失败均保留原状态。
- **矩阵、视锥与 reset**：使用 OpenGL `glm::perspectiveRH_NO`/`ClipDepthRange::NegativeOneToOne`。view 只保存 orientation 的逆旋转且不窄化绝对位置；`cameraOrigin` 保持 double。相机相对视锥转换为全局世界空间，因此可直接测试全局 `Bounds3d`。reset 使用 Bounds 包围球、45° FOV 与当前 aspect 同时框选，自动距离允许超过 1000；update 无动画并刷新动态 near/far。
- **验收检查**：Golden/行为测试覆盖默认姿态、矩阵/视锥、旋转限制、平移、释放/失焦、滚轮与原子失败、reset/pending reset、动态裁剪面、尺寸缓存、resize 保持视角和全局 Bounds 剔除。
- **验证结果**：MSVC 19.51.36246.0 x64 / Visual Studio 18 2026 开发环境下的 Ninja OpenGL-only Debug，使用 `D:\vcpkg\vcpkg\installed\x64-windows` 的 GLM CMake 包与 `CMAKE_PREFIX_PATH` 配置；全量构建成功；`dzc_orbit_camera_controller` 专项测试 1/1 通过；完整 CTest 48/48 通过；`git diff --check` 通过；任务专用 `build-ca006` 已清理。
- **边界**：不改 Engine Controller 注入、所有权或命令转发；不实现 Qt 输入映射、GPU/渲染器、键盘控制、惯性/时间驱动运动或 CA-007 性能路径。
- **追踪**：FR-CAM-002/003、DDD-016
### CA-007 定义可重复性能相机路径

- **状态**：已完成（2026-08-21）
- **目标**：提供仅测试/性能路径验证使用的内存 C++ `InputEvent` 回放。
- **实际文件**：`tests/performance/CameraPath.h`、`CameraPath.cpp`、`CameraPathTests.cpp`、`CMakeLists.txt`、`docs/testing/cameraPath.md`。
- **实现结果**：每次回放从默认 `OrbitCameraController` 开始，先缓存有效尺寸并建立 Bounds；绝对时间戳转换为相邻 `update()` 的 delta，逐步采样 state/matrices。结构错误为 `DataFormat/2`；控制器输入错误原样传播且不返回部分结果。
- **验收检查**：基础轨迹球/平移/滚轮/失焦/reset 与无输入步、两次回放逐帧比较已覆盖；double 使用 1e-12 绝对+相对容差，float 使用 1e-5。
- **验证**：MSVC 19.51.36246.0 x64 / Ninja OpenGL-only Debug 全量构建成功；`dzc_camera_path_replay` 专项 CTest 1/1 通过；设置 `CMAKE_PREFIX_PATH=D:\vcpkg\vcpkg\installed\x64-windows` 后完整 CTest 49/49 通过；`git diff --check` 通过；任务专用 `build-ca007` 已清理。
- **边界**：不提供 JSON/CSV、真实 FPS、Renderer/GPU、Engine 注入或 Qt 映射；`AC-P2-011` 仍未完成。
- **追踪**：NFR-TEST-001、已解决的 TBD-004
## 6. 模块级验收

- [x] Camera 公共类型和接口不依赖 Qt
- [ ] Fake Controller 可驱动 Engine 与剔除测试
- [x] 已确认的 GLM-only `OrbitCameraController` 实现左/右键交互、滚轮、reset、动态裁剪面与防翻转限制；未引入未确认键位或速度模型
- [x] 参考源码规则已确认并由 CA-006 Golden/行为测试覆盖

## 7. 交接记录
### CA-007（2026-08-21）

- 完成人：Codex
- 关键变更：新增内存 `CameraPath`/`CameraPathReplayer` 和独立 `dzc_camera_path_replay` CTest；回放真实调用默认 `OrbitCameraController` 的 `submitInput()`、`update()` 与 `matrices()`，不使用 Fake 或内部状态注入。
- 验证结果：MSVC 19.51.36246.0 x64 / Ninja OpenGL-only Debug 全量构建成功；`dzc_camera_path_replay` 专项 CTest 1/1 通过；设置 `CMAKE_PREFIX_PATH=D:\vcpkg\vcpkg\installed\x64-windows` 后完整 CTest 49/49 通过；`git diff --check` 通过；任务专用 `build-ca007` 已清理。本任务不采集 FPS。
- 后续状态：CA-001 至 CA-007 已完成；Camera Abstraction 模块仍为进行中，Engine Controller 注入、Qt 映射、Renderer/GPU 集成和正式性能验收仍未完成。
- 关联提交：未提交。
### CA-006（2026-08-21）

- 完成人：Codex
- 关键变更：新增 GLM-only `OrbitCameraController`（Pimpl），实现已确认的轨迹球旋转、防翻转二分限制、右键平移、滚轮缩放、延迟 reset、动态裁剪面，以及相机相对矩阵和全局世界空间视锥；新增 `dzc_orbit_camera_controller` CTest。
- 验证结果：MSVC 19.51.36246.0 x64 / Ninja OpenGL-only Debug 全量构建成功；`dzc_orbit_camera_controller` 专项测试 1/1 通过；设置 `CMAKE_PREFIX_PATH=D:\vcpkg\vcpkg\installed\x64-windows` 后完整 CTest 48/48 通过；`git diff --check` 通过；任务专用 `build-ca006` 已清理。
- 后续未解决问题：Engine 的 Controller 注入/所有权/命令转发及 Qt 输入映射仍是后续集成工作；CA-007 已在后续任务完成内存确定性回放。
- 后续任务：Engine Controller 注入/所有权/命令转发、Qt 输入映射与正式 Renderer 性能验收；Camera Abstraction 模块继续进行中。
- 关联提交：未提交。
### CA-005（2026-08-21）

- 完成人：Codex
- 关键变更：新增 `docs/design/cameraInteractionDesign.md`，仅基于用户提供的未跟踪 `camera.cpp` 固化透视轨道球交互：左键虚拟球旋转及防翻转二分限制、右键屏幕平面平移、滚轮距离倍率、Bounds 自动框选、动态裁剪面、Focus/ResetRequest 和异常输入处理。
- 验证结果：完成 FR-CAM-002/003、概要设计、详细设计、任务清单和进度文档的交叉审查；`git diff --check` 通过；任务专用 `build-ca007` 已清理。CA-005 是设计任务，不新增构建或 C++ 行为测试。
- 后续状态：CA-006 已实现具体 Controller、矩阵/视锥和 Golden/行为测试，CA-007 已实现内存确定性回放，固定采用 OpenGL `NegativeOneToOne`；Engine 的 Controller 注入/所有权/命令转发及 Qt 映射仍未实现。
- 后续任务：Engine Controller 注入/所有权/命令转发、Qt 输入映射与正式 Renderer 性能验收；Camera Abstraction 模块继续进行中。
- 关联提交：未提交。

### CA-004（2026-08-21）

- 完成人：Codex
- 关键变更：新增纯抽象公共 `ICameraController` 和仅测试使用的 `FakeCameraController`；Fake 可预置三类操作结果与相机返回值，并记录输入、更新、矩阵、视锥体和重置调用参数；新增 `dzc_camera_controller_contract` CTest。
- 验证结果：全量构建成功；`dzc_camera_controller_contract` 专项测试 1/1 通过；设置 `CMAKE_PREFIX_PATH=D:\vcpkg\vcpkg\installed\x64-windows` 后完整 CTest 47/47 通过；`git diff --check` 通过；任务专用 `build-ca004` 已清理。
- 后续状态：CA-005 的交互规则已随后确认，CA-006 与 CA-007 已实现；Engine 的 Controller 注入、所有权和命令转发及 Qt 映射仍未实现。
- 后续任务：CA-005 的已确认规则由 CA-006 实现具体 Camera Controller；Camera Abstraction 模块继续进行中。
- 关联提交：未提交。
### CA-003（2026-08-21）

- 完成人：Codex
- 关键变更：新增无 Qt/GLM 依赖的公共 `InputEventType` 与 `InputEvent` 值类型；六种抽象输入事件按详细设计的固定顺序表达，新增 `dzc_input_event` CTest。
- 验证结果：全量构建成功；`dzc_input_event` 专项测试 1/1 通过；设置 `CMAKE_PREFIX_PATH=D:\vcpkg\vcpkg\installed\x64-windows` 后完整 CTest 46/46 通过；`git diff --check` 通过；任务专用 `build-ca003` 已清理。
- 当时未解决问题：CA-004 尚未实现；CA-005 至 CA-007 当时仍等待参考源码。现状以 CA-007 交接记录为准：CA-006 与 CA-007 已实现。
- 后续任务：CA-004 定义 ICameraController 和 Fake；Camera Abstraction 模块继续进行中。
- 关联提交：未提交。
### CA-002（2026-08-20）

- 完成人：Codex
- 关键变更：提升 `Bounds3d` 为公共值类型；新增双精度 `FrustumPlane`/`ViewFrustum`、显式 `ClipDepthRange`、平面规范化、view-projection 平面提取与保守 Bounds 可见性测试；新增 `dzc_view_frustum` CTest。
- 验证结果：全量构建成功；`dzc_view_frustum` 专项测试 1/1 通过；设置 `CMAKE_PREFIX_PATH=D:\vcpkg\vcpkg\installed\x64-windows` 后完整 CTest 45/45 通过；`git diff --check` 通过；任务专用 `build-ca002` 已清理。
- 当时未解决问题：CA-003、CA-004 尚未实现；CA-005 至 CA-007 当时仍等待参考源码。现状以 CA-007 交接记录为准：CA-006 与 CA-007 已实现。
- 后续任务：CA-003 定义 InputEvent；Camera Abstraction 模块继续进行中。
- 关联提交：未提交。
### CA-001（2026-08-20）

- 完成人：Codex
- 关键变更：新增 GLM-only 的 `CameraState` 与 `CameraMatrices` 公共值类型；将 GLM 配置为 `dzc_engine_api` 的传递公共依赖；新增 `dzc_camera_types` CTest。
- 验证结果：全量构建成功；`dzc_camera_types` 专项测试 1/1 通过；设置 `CMAKE_PREFIX_PATH=D:\vcpkg\vcpkg\installed\x64-windows` 后完整 CTest 44/44 通过；`git diff --check` 通过；任务专用 `build-ca001` 已清理。测试覆盖默认值、成员精确类型、复制/移动和 double 精度字段保持。
- 当时未解决问题：CA-003、CA-004 尚未实现；CA-005 至 CA-007 当时仍等待参考源码。现状以 CA-007 交接记录为准：CA-006 与 CA-007 已实现。
- 后续任务：CA-003 定义 InputEvent；Camera Abstraction 模块继续进行中。
- 关联提交：未提交。

## 8. 变更约束

若实现需要改变公共接口、模块依赖、已确认参数或需求行为，必须先更新需求与设计文档，不得在编码任务中自行改变。
