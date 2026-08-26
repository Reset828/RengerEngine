# OpenGL Renderer 任务清单

> 文件：`docs/tasks/opengl-renderer.md`  
> 所属阶段：Phase 1  
> 模块状态：进行中
> 前置模块：[engine-core](./engine-core.md)、[point-cloud-data](./point-cloud-data.md)、[grid-chunking](./grid-chunking.md)、[camera-abstraction](./camera-abstraction.md)、[diagnostics](./diagnostics.md)  
> 输入基线：[需求文档](../requirements/spec.md)、[概要设计](../design/architectureDesign.md)、[详细设计](../design/detailDesign.md)、[项目规范](../../agent.md)

## 1. 模块目标

实现 OpenGL 4.5 Core 后端、Chunk GPU 资源、统一 Shader 逻辑布局和逐可见 Chunk 点绘制。

## 2. 范围边界

**包含：** OpenGL 能力检查；GLAD；RAII GL 对象；Chunk 上传；Frame/Chunk Buffer；Shader；绘制；resize；GPU 时间。  
**不包含：** CUDA-GL 注册；Vulkan；MultiDrawIndirect 强制优化；Qt 控件。

## 3. 完成规则

只有同时满足以下条件，模块才可以在 [progress.md](./progress.md) 中标记完成：

- 本文所有非阻塞必需任务均已勾选；
- 所有自动化测试通过，能力缺失用例只能明确标记为 Skipped，不能伪造通过；
- 对应公共接口和私有实现符合 `agent.md` 的命名、Pimpl、RAII 和依赖边界；
- 相关需求、设计和测试文档已经同步；

## 4. 子任务 Checklist

- [x] **GL-001 配置 OpenGL 和 GLAD 私有依赖**
- [x] **GL-002 实现 OpenGL 4.5 能力检查**
- [x] **GL-003 实现 GL Buffer/VAO RAII**
- [ ] **GL-004 实现 GLSL 编译与链接**
- [ ] **GL-005 实现统一 FrameData/ChunkData 布局**
- [ ] **GL-006 实现 Chunk GPU 上传**
- [ ] **GL-007 实现 OpenGLBackend 生命周期**
- [ ] **GL-008 实现逐 Chunk 点绘制**
- [ ] **GL-009 实现四种着色和点大小**
- [ ] **GL-010 实现 resize 和投影适配**
- [ ] **GL-011 实现 GPU Timer Query**
- [ ] **GL-012 完成 OpenGL 后端集成测试**

## 5. 子任务说明

### GL-001 配置 OpenGL 和 GLAD 私有依赖

- **状态**：已完成
- **目标**：建立 dzc_render_opengl Target 和函数加载。
- **前置任务**：project-foundation/PF-003
- **预计文件**：`src/render/opengl/CMakeLists.txt`、`cmake/FindOrFetchGlad.cmake`
- **实现要求**：依赖缺失时 CMake 明确失败；GLAD/OpenGL 不进入公共头。
- **验收检查**：OpenGL-only 配置能生成 Target。
- **测试要求**：CMake 配置测试。
- **追踪**：FR-GL-001、ADR-009

### GL-002 实现 OpenGL 4.5 能力检查

- **状态**：已完成
- **目标**：查询 Core 版本、点大小、UBO/SSBO 对齐和 Buffer 限制。
- **前置任务**：GL-001, diagnostics/DG-001
- **预计文件**：`src/render/opengl/OpenGLCapabilities.h`、`src/render/opengl/OpenGLCapabilities.cpp`、`tests/graphics/OpenGLCapabilitiesTests.cpp`
- **实现要求**：低于 4.5 返回 UnsupportedVersion；记录 GPU/驱动信息；真实路径要求调用方先完成 GLAD 加载和 Context 激活。
- **验收检查**：Fake 查询覆盖兼容/不兼容版本、Core Profile、能力字段和设备信息；真实 Context 用例明确 Skipped，等待 GL-007 提供 Context 基础设施。
- **测试要求**：Fake 查询单元测试通过；真实 Context 测试未创建 Context，明确 Skipped。
- **追踪**：FR-COM-002、AC-P1-002

### GL-003 实现 GL Buffer/VAO RAII

- **状态**：已完成
- **目标**：封装创建、移动、销毁和标签。
- **前置任务**：GL-002
- **预计文件**：`src/render/opengl/GlBuffer.h`、`src/render/opengl/GlBuffer.cpp`、`src/render/opengl/GlVertexArray.h`、`src/render/opengl/GlVertexArray.cpp`、`tests/graphics/GlResourceTests.cpp`
- **实现要求**：对象仅在当前 Context 线程创建销毁；析构不抛；资源操作通过项目自有接口隔离 GLAD。
- **验收检查**：移动后只有新对象持有资源；显式 `reset()` 在正确 Context 线程完成释放；不安全析构跳过 GL 删除并标记 `releasePending`。
- **测试要求**：创建、移动、重复释放、标签、错误、线程令牌和跨线程析构 Fake 测试；真实 Context 用例明确 Skipped，留给 GL-007。
- **追踪**：NFR-MAIN-002、14.1

### GL-004 实现 GLSL 编译与链接

- **状态**：未开始
- **目标**：实现运行时加载、编译、链接和诊断日志。
- **前置任务**：GL-003
- **预计文件**：`src/render/opengl/GlShaderProgram.h`、`src/render/opengl/GlShaderProgram.cpp`、`shaders/opengl/point_cloud.vert`、`shaders/opengl/point_cloud.frag`、`tests/graphics/GlShaderTests.cpp`
- **实现要求**：固定 location 0/1/2 和 binding 0/1；失败报告文件和阶段。
- **验收检查**：有效 Shader 链接；故意错误 Shader 返回稳定 Error。
- **测试要求**：真实 Context 编译测试和错误日志检查。
- **追踪**：FR-GL-003、ADR-011

### GL-005 实现统一 FrameData/ChunkData 布局

- **状态**：未开始
- **目标**：定义 CPU 侧 std140/std430 兼容结构和布局断言。
- **前置任务**：GL-004, point-cloud-data/PD-008
- **预计文件**：`src/render/common/ShaderData.h`、`src/render/opengl/OpenGLShaderData.cpp`、`tests/unit/ShaderLayoutTests.cpp`
- **实现要求**：字段偏移显式；不得直接假设 glm 默认对齐；与 Shader 声明一致。
- **验收检查**：静态断言和反射/查询验证 binding 与大小。
- **测试要求**：布局 Golden 值和 OpenGL block 查询测试。
- **追踪**：DDD-014、13.2

### GL-006 实现 Chunk GPU 上传

- **状态**：未开始
- **目标**：按 schema 创建 Position/Color/Intensity VBO 和 VAO。
- **前置任务**：GL-003, GL-005
- **预计文件**：`src/render/opengl/GlChunkResource.h`、`src/render/opengl/GlChunkResource.cpp`、`tests/graphics/GlChunkUploadTests.cpp`
- **实现要求**：上传只能在 Context 线程；缺失流使用固定属性；不回读点数据。
- **验收检查**：不同 schema 均可上传并记录准确字节/点数。
- **测试要求**：XYZ、XYZRGB、XYZI、全属性上传测试。
- **追踪**：FR-GL-002、14.3

### GL-007 实现 OpenGLBackend 生命周期

- **状态**：未开始
- **目标**：实现内部 IRenderBackend init/upload/update/resize/release/shutdown。
- **前置任务**：GL-002, GL-006, engine-core/EC-006
- **预计文件**：`src/render/opengl/OpenGLBackend.h`、`src/render/opengl/OpenGLBackend.cpp`、`tests/graphics/OpenGLBackendLifecycleTests.cpp`
- **实现要求**：检查调用线程；资源表按 ChunkId；显式 shutdown 在 Context 销毁前。
- **验收检查**：生命周期和非法线程/状态错误符合设计。
- **测试要求**：真实 Context 生命周期和故障注入测试。
- **追踪**：4.3、14.2、NFR-REL-002

### GL-008 实现逐 Chunk 点绘制

- **状态**：未开始
- **目标**：每可见 Chunk 一次 glDrawArrays(GL_POINTS)。
- **前置任务**：GL-007, grid-chunking/GC-009
- **预计文件**：`src/render/opengl/OpenGLBackend.cpp`、`tests/graphics/OpenGLDrawTests.cpp`
- **实现要求**：更新 Frame UBO 和 Chunk SSBO；不可见块不提交；首版不要求 MDI。
- **验收检查**：捕获/统计 draw 次数等于可见 Chunk 数，点数正确。
- **测试要求**：小型离屏渲染和 Draw 统计测试。
- **追踪**：FR-REN-001、DDD-014、AC-P1-008

### GL-009 实现四种着色和点大小

- **状态**：未开始
- **目标**：完成 OriginalColor/FixedColor/Height/Intensity 分支。
- **前置任务**：GL-004, GL-008
- **预计文件**：`shaders/opengl/point_cloud.vert`、`shaders/opengl/point_cloud.frag`、`tests/graphics/OpenGLShadingTests.cpp`
- **实现要求**：缺属性时按设计回退并一次警告；高度退化用 0.5。
- **验收检查**：四种模式像素/统计结果可区分，point size 更新生效。
- **测试要求**：离屏小图 Golden/采样测试。
- **追踪**：FR-REN-002/003、AC-P1-007

### GL-010 实现 resize 和投影适配

- **状态**：未开始
- **目标**：更新 viewport、RenderSize 和 OpenGL 深度约定矩阵。
- **前置任务**：GL-007, camera-abstraction/CA-004
- **预计文件**：`src/render/opengl/OpenGLBackend.cpp`、`tests/graphics/OpenGLResizeTests.cpp`
- **实现要求**：零尺寸安全跳过；不让 UI 构造 API 特定矩阵。
- **验收检查**：多次 resize 后无拉伸、GL error 或失效资源。
- **测试要求**：离屏尺寸变更和零尺寸测试。
- **追踪**：FR-REN-005、AC-P1-006

### GL-011 实现 GPU Timer Query

- **状态**：未开始
- **目标**：使用查询池延迟读取 GPU 帧时间。
- **前置任务**：GL-008, diagnostics/DG-006
- **预计文件**：`src/render/opengl/GlTimerQueryPool.h`、`src/render/opengl/GlTimerQueryPool.cpp`、`tests/graphics/GlTimerQueryTests.cpp`
- **实现要求**：不得同步等待当前帧；缺结果时指标留空。
- **验收检查**：若干帧后获得非负时间，帧循环无 glFinish。
- **测试要求**：查询延迟和不可用路径测试。
- **追踪**：FR-STAT-001、15

### GL-012 完成 OpenGL 后端集成测试

- **状态**：未开始
- **目标**：从规范化 Chunk 到离屏渲染跑通公共功能。
- **前置任务**：GL-009, GL-010, GL-011
- **预计文件**：`tests/integration/OpenGLRenderPipelineTests.cpp`
- **实现要求**：覆盖 resize、模式切换、资源释放和错误 Shader。
- **验收检查**：OpenGL 能力环境下全部通过；缺环境明确 Skipped。
- **测试要求**：CTest graphics/opengl 标签。
- **追踪**：AC-P1-002/006/007/010/013

## 6. 模块级验收

- [ ] OpenGL 4.5 Core 后端可独立初始化和关闭
- [ ] VAO/VBO/UBO/SSBO 与统一 Shader 布局正确
- [ ] 可见 Chunk 一块一次 GL_POINTS 绘制且四种着色可用
- [ ] GPU 计时和 resize 测试通过且无同步回读

## 7. 交接记录

- 完成日期：2026-08-26
- 完成人：Codex（按主人确认执行）
- 关键变更：GL-001 已完成。将 GLAD 1.0.36（生成目标 OpenGL 4.6 Core）纳入 `third_party/glad`；OpenGL 开启时通过内置默认路径或 `DZC_GLAD_ROOT` 覆盖路径检查 GLAD 文件并创建真实 `dzc_render_opengl` 静态库，GLAD include 目录保持私有；OpenGL 关闭时保留兼容的接口占位 Target。GL-002 新增不暴露 OpenGL/GLAD 类型的 `OpenGLCapabilities` 能力查询接口和 `IOpenGLCapabilityQueries` Fake 注入接口，验证 OpenGL 4.5 Core 最低版本与 Core Profile，并记录点大小、UBO/SSBO 对齐、最大 SSBO Block Buffer、GPU/驱动/GLSL 信息。GL-003 在 `dzc_render_opengl` 中新增 `GlBuffer`、`GlVertexArray` 及共享 `IGlResourceOperations`/`GlContextThreadToken`，实现不可复制、可移动的 RAII 创建、显式 reset、标签转发和 CPU 标签保存；析构为 `noexcept`，错误线程或操作不可用时跳过 GL 删除并设置 `releasePending`。头文件不暴露 OpenGL/GLAD 类型。
- 未解决问题：GL-004 至 GL-012 尚未实现；GL-002/GL-003 的真实 Context 测试、GLAD loader 初始化、OpenGL Backend 生命周期和正常 shutdown 资源清理留给 GL-007；OpenGL Renderer 模块级验收尚未开始。
- 测试命令与结果：GL-001 的 OpenGL-only 配置、`dzc_render_opengl` 构建及相关 CTest 为 `4/4` 通过；GL-002 的 Fake 能力测试通过，真实 Context 用例明确 Skipped。GL-003 使用 MSVC 19.51/NMake 配置 `build-gl003`（OpenGL=ON、Vulkan/CUDA=OFF、Tests=ON），`cmake --build build-gl003 --target dzc_render_opengl`、`cmake --build build-gl003 --target dzc_gl_resource_tests` 成功；`ctest --test-dir build-gl003 -R "^(dzc_gl_resource|dzc_opengl_capabilities|dzc_gl001_configure|dzc_target_boundary|dzc_configure_smoke_opengl_only)$" --output-on-failure` 为 `5/5` 通过；`ctest --test-dir build-gl003 -R "^dzc_opengl_capabilities_real_context$|^dzc_gl_resource_real_context$" --output-on-failure` 为 `2/2` Skipped。
- 关联提交：未提交（未创建 Git commit）。

## 8. 变更约束

若实现需要改变公共接口、模块依赖、已确认参数或需求行为，必须先更新需求与设计文档，不得在编码任务中自行改变。
