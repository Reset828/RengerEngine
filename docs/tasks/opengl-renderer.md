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
- [x] **GL-004 实现 GLSL 编译与链接**
- [x] **GL-005 实现统一 FrameData/ChunkData 布局**
- [x] **GL-006 实现 Chunk GPU 上传**
- [x] **GL-007 实现 OpenGLBackend 生命周期**
- [x] **GL-008 实现逐 Chunk 点绘制**
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

- **状态**：已完成
- **目标**：实现运行时加载、编译、链接和诊断日志。
- **前置任务**：GL-003
- **预计文件**：`src/render/opengl/GlShaderProgram.h`、`src/render/opengl/GlShaderProgram.cpp`、`shaders/opengl/point_cloud.vert`、`shaders/opengl/point_cloud.frag`、`tests/graphics/GlShaderTests.cpp`
- **实现要求**：固定 location 0/1/2 和 binding 0/1；失败报告文件和阶段。
- **验收检查**：有效 Shader 链接；故意错误 Shader 返回稳定 Error。
- **测试要求**：真实 Context 编译测试和错误日志检查。
- **追踪**：FR-GL-003、ADR-011

### GL-005 实现统一 FrameData/ChunkData 布局

- **状态**：已完成（2026-08-26）
- **目标**：定义 CPU 侧 std140/std430 兼容结构和布局断言。
- **前置任务**：GL-004, point-cloud-data/PD-008
- **实际文件**：`src/render/common/ShaderData.h`、`src/render/opengl/OpenGLShaderData.h`、`src/render/opengl/OpenGLShaderData.cpp`、`tests/unit/ShaderLayoutTests.cpp`
- **实现结果**：新增不依赖 GLM 默认对齐的自有标量/数组 Shader 数据结构；FrameData 固定为 std140 binding 0、208 字节，ChunkData 固定为 std430 binding 1、16 字节数组 stride。矩阵按 GLM 列主序转换，Chunk 相对原点由 PD-008 计算后写入 vec4。
- **布局契约**：FrameData 字段偏移为 view 0、projection 64、fixedColor 128、heightRange 144、intensityRange 160、pointSize 176、shadingMode 180、reservedPadding 184、reservedExtension 192；ChunkData relativeChunkOrigin 偏移 0。
- **实现边界**：增加已链接 Program 的真实 block/member 反射查询入口，但不创建 Context、不初始化 GLAD、不上传 Buffer；绘制和完整着色留给 GL-008/GL-009。
- **验收检查**：静态断言、Golden 布局验证、矩阵列主序和 Shader 契约检查通过；真实 block 查询测试因缺少统一 Context 基础设施明确 Skipped。
- **测试要求**：`dzc_shader_layout` Fake/静态测试通过；`dzc_shader_layout_real_context` 使用返回码 77 并由 CTest 标记 Skipped。
- **追踪**：DDD-014、13.2
### GL-006 实现 Chunk GPU 上传

- **状态**：已完成（2026-08-26）
- **目标**：按 schema 创建 Position/Color/Intensity VBO 和 VAO。
- **前置任务**：GL-003, GL-005
- **实际文件**：`src/render/opengl/GlChunkResource.h`、`src/render/opengl/GlChunkResource.cpp`、`tests/graphics/GlChunkUploadTests.cpp`
- **实现结果**：新增 `GlChunkResource`，显式将 Chunk SoA 数据打包为 Position/Color/Intensity 三条 VBO，并配置 VAO location 0/1/2；Color 按 RGBA 高位到低位拆分，缺失 Color 使用白色，缺失 Intensity 使用 65535。
- **资源策略**：上传仅在 Context 线程执行，采用临时 VAO/VBO 完成上传和属性配置，成功后原子替换旧资源；失败时尽力清理临时资源并保留旧资源。统计记录点数、Schema、各流和总 GPU 字节数。
- **实现边界**：未实现 ChunkData SSBO、FrameData UBO、绘制、真实 Context、GLAD loader 初始化或 CUDA-GL 互操作，均留给 GL-007 及后续任务。
- **验收检查**：Fake 覆盖 XYZ/XYZRGB/XYZI/全属性、字节打包、固定属性、VAO 格式/stride/normalized、错误输入、溢出、失败清理、旧资源保留、reset、移动和线程令牌。
- **测试要求**：`dzc_gl_chunk_upload` 已注册并通过；未创建真实 Context。
- **追踪**：FR-GL-002、14.3

### GL-007 实现 OpenGLBackend 生命周期

- **状态**：已完成（2026-08-26）
- **目标**：实现内部 `IRenderBackend` 的 `init/upload/update/render/resize/release/shutdown` 生命周期。
- **前置任务**：GL-002、GL-006、engine-core/EC-006
- **实际文件**：`src/render/common/RenderBackendTypes.h`、`src/render/common/RenderBackendFactory.h`、`src/render/opengl/OpenGLBackend.h`、`src/render/opengl/OpenGLBackend.cpp`、`tests/graphics/OpenGLBackendLifecycleTests.cpp`
- **实现结果**：补齐后端无关生命周期接口；`OpenGLBackend` 使用 `Uninitialized/Initialized/Shutdown` 状态机，通过外部 Context/loader、能力查询和 Chunk 上传操作表注入完成初始化；初始化检查当前 Context、GLAD functions loaded、OpenGL 4.5 Core 能力，并保存初始 `RenderSize`。
- **资源管理**：按 `ChunkId` 使用 `std::unordered_map` 管理 `GlChunkResource`；批量 `upload()` 逐项处理，同 ID 只有新资源完整上传成功后才替换旧资源；`release()` 仅释放指定 Chunk，`shutdown()` 在当前 Context 线程释放全部资源，失败时保留资源并通过 `lastError()` 记录阶段和 ChunkId。
- **生命周期边界**：`update()` 只校验并保存完整 `RenderFrame`，`render()` 仅在有效帧存在时返回占位成功，`resize()` 只更新尺寸；未实现 FrameData UBO、ChunkData SSBO、绘制、投影适配、平台 Context 创建或 GLAD loader 初始化。
- **验收检查**：Fake 覆盖初始化成功/能力和 Context 失败/重复初始化/多 Chunk 与替换/upload 失败/帧更新与 render 占位/resize/线程拒绝/release/shutdown 失败重试/移动语义/析构安全。
- **测试要求**：`dzc_opengl_backend_lifecycle` 已通过；`dzc_opengl_backend_real_context` 返回 `77` 并由 CTest 明确标记 Skipped，真实 Context 生命周期留给后续集成层。
- **后续范围**：GL-009 至 GL-012 仍未开始；OpenGL Renderer 模块整体保持“进行中”，模块级验收不得标记完成。
- **追踪**：4.3、14.2、NFR-REL-002
### GL-008 实现逐 Chunk 点绘制

- **状态**：已完成（2026-08-26）
- **目标**：每可见 Chunk 一次 `glDrawArrays(GL_POINTS)`。
- **前置任务**：GL-007、grid-chunking/GC-009
- **实际文件**：`src/render/opengl/OpenGLBackend.cpp`、`src/render/opengl/GlDrawOperations.h`、`src/render/opengl/GlDrawOperations.cpp`、`tests/graphics/OpenGLDrawTests.cpp`
- **实现结果**：在外部 Context、GLAD 和能力检查成功后，Backend 持久创建 Frame UBO、单记录 Chunk SSBO 和默认点云 Shader；FrameData 绑定到 binding 0，ChunkData 绑定到 binding 1。`render()` 先对全部 `RenderFrame.draws` 做资源有效性、点数一致性和 `GLsizei` 范围预检查，再按 draws 顺序上传 FrameData/ChunkData、清除当前帧颜色、绑定 Shader/VAO，并对每个可见 Chunk 提交一次 `GL_POINTS`。未列入 draws 的已上传 Chunk 不提交。
- **错误与统计**：缺少资源、点数不一致、资源无效或注入式 GL 操作失败均返回 `RenderFailed`；预检查失败时不发生任何绘制操作，运行时操作失败不更新成功统计。`drawCount()` 和 `submittedPointCount()` 表示最近一次成功 render 的统计，失败保留上一成功帧，空 draws 是零统计成功帧。
- **测试结果**：新增 `dzc_opengl_draw` Fake 操作表测试，覆盖资源创建、单/多 Chunk Draw、空和不可见列表、缺少资源、点数不一致、FrameData/ChunkData、binding 0/1、relative origin、失败统计保留；OpenGL-only 相关 CTest 共 14 项，其中 8 项通过、6 个真实 Context 测试按约定返回 77 并由 CTest 标记 `Skipped`。
- **限制**：GL-008 不创建真实 Context，不引入 Qt/WGL/EGL，不实现 GL-009 着色逻辑、GL-010 resize 投影适配、GL-011 timer query 或 GL-012 集成测试；OpenGL Renderer 模块仍保持进行中。
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
- 关键变更：GL-001 至 GL-003 保持已完成。GL-004 新增不暴露 OpenGL/GLAD 类型的 `GlShaderProgram` 和独立 `IGlShaderOperations`，支持源码与文件入口、Vertex/Fragment 运行时编译、Program 链接、阶段化错误日志、移动语义、显式 reset 和线程令牌约束。新增 `#version 450 core` 的最小点云 Vertex/Fragment shader fixture，固定 location 0/1/2 与 binding 0/1；GLAD 真实操作仅在 `.cpp` 内实现。
- 未解决问题：GL-007 至 GL-012 尚未开始；GL-002、GL-003、GL-004、GL-005 的真实 Context 测试均明确 Skipped，GLAD loader 初始化、OpenGL Backend 生命周期和正常 shutdown 资源清理留给 GL-007；OpenGL Renderer 模块级验收尚未开始。
- 测试命令与结果：GL-001 至 GL-003 的既有配置、能力和资源回归保持通过。GL-004 使用 MSVC 19.51/NMake 配置 `build-gl004`（OpenGL=ON、Vulkan/CUDA=OFF、Tests=ON）；`cmake --build build-gl004 --target dzc_gl_shader_tests` 成功，包含 `GlShaderProgram.cpp` 和 GLAD 的 `dzc_render_opengl` 构建成功；`ctest --test-dir build-gl004 -R "^dzc_gl_shader$|^dzc_gl_shader_real_context$" --output-on-failure` 为 `1/1` 通过、真实 Context 用例明确 Skipped。已执行 `git diff --check`，未创建 Git commit。
- 关联提交：未提交（未创建 Git commit）。

### GL-005（2026-08-26）

- 关键变更：新增后端无关 `dzc::render::FrameData` 和 `ChunkData`，CPU 侧使用 `std::array`、标量和显式 `alignas(16)`/填充，不把 `glm::mat4` 或 `glm::vec4` 作为 GPU ABI 成员。FrameData 按 std140 binding 0 固定为 208 字节，ChunkData 按 std430 binding 1 固定为 16 字节数组 stride；矩阵辅助函数按 GLM 列主序写入。
- OpenGL 实现：新增 `OpenGLShaderData` 的布局验证和已链接 Program 反射查询入口，使用 GLAD 查询 UBO block size/offset、SSBO block binding 和 buffer variable offset/array stride。未创建 Context，未初始化 GLAD loader，未实现 Buffer 上传、绘制或完整着色。
- Shader 同步：`point_cloud.vert` 使用完整 FrameData 字段、view/projection 变换和 `relativeChunkOrigin[0]`；固定 `#version 450 core`、location 0/1/2、binding 0/1；Fragment Shader 仍仅输出输入 Color，四种着色留给 GL-009。
- 验证结果：MSVC 19.51.36246.0 x64 / NMake Makefiles、OpenGL=ON、Vulkan/CUDA=OFF、Tests=ON 配置成功；`dzc_render_opengl` 和 `dzc_shader_layout_tests` 构建成功；`dzc_shader_layout` 通过，真实 Context 用例明确 Skipped；`git diff --check` 已执行并通过。
- 未解决问题：真实 OpenGL Context、GLAD loader 初始化和运行时 block 反射验证留给 GL-007；GL-007 至 GL-012 尚未开始；OpenGL Renderer 模块级验收未完成。
- 关联提交：未提交（未创建 Git commit）。

### GL-006（2026-08-26）

- 关键变更：新增 `GlChunkResource` 和继承 GL-003 资源操作的 `IGlChunkUploadOperations`，将 `ChunkCpuData` 的 Position/Color/Intensity SoA 显式打包为三条 `GL_ARRAY_BUFFER`/`GL_STATIC_DRAW` VBO，并创建/配置对应 VAO。固定属性为 location 0（3×Float32）、location 1（4×normalized UInt8）、location 2（1×normalized UInt16）。
- Schema 分支：支持 XYZ、XYZRGB、XYZI 和 XYZRGBI；缺失 Color 生成 RGBA `(255,255,255,255)`，缺失 Intensity 生成 `65535`；颜色不依赖主机端序，所有流字节数和统计由包装器显式计算。
- 生命周期：上传、替换和 `reset()` 受 `GlContextThreadToken` 约束，临时资源先完成上传后再替换旧资源；失败时保留旧资源并尽力清理临时资源；资源包装器保持不可复制、可移动、析构 `noexcept` 和待释放状态。
- 验证结果：MSVC 19.51.36246.0 x64 / NMake Makefiles、OpenGL=ON、Vulkan/CUDA=OFF、Tests=ON 配置和构建成功；`dzc_render_opengl` 与 `dzc_gl_chunk_upload_tests` 构建成功，`dzc_gl_chunk_upload` Fake 测试通过；未创建真实 Context。
- 未解决问题：ChunkData SSBO、FrameData UBO、绘制、Backend 生命周期、真实 Context 和 GLAD loader 初始化留给 GL-007；GL-007 至 GL-012 尚未开始；OpenGL Renderer 模块仍为进行中，模块级验收未完成。
- 关联提交：未提交（未创建 Git commit）。

## 8. 变更约束

若实现需要改变公共接口、模块依赖、已确认参数或需求行为，必须先更新需求与设计文档，不得在编码任务中自行改变。
