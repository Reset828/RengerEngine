# OpenGL Renderer 任务清单

> 文件：`docs/tasks/opengl-renderer.md`  
> 所属阶段：Phase 1  
> 模块状态：已完成（2026-09-02）
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
- [x] **GL-009 实现四种着色和点大小**
- [x] **GL-010 实现 resize 和投影适配**
- [x] **GL-011 实现 GPU Timer Query**
- [x] **GL-012 完成 OpenGL 后端集成测试**

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
- **实现边界**：增加已链接 Program 的真实 block/member 反射查询入口，但不创建 Context、不初始化 GLAD、不上传 Buffer；绘制和完整着色已在后续 GL-008/GL-009 实现。
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
- **生命周期边界**：`update()` 校验并保存完整 `RenderFrame`，`render()` 的逐 Chunk 绘制由 GL-008/GL-009 扩展；`resize()` 当前只更新尺寸。投影适配、平台 Context 创建和 GLAD loader 初始化仍不属于本阶段。
- **验收检查**：Fake 覆盖初始化成功/能力和 Context 失败/重复初始化/多 Chunk 与替换/upload 失败/帧更新与 render 占位/resize/线程拒绝/release/shutdown 失败重试/移动语义/析构安全。
- **测试要求**：`dzc_opengl_backend_lifecycle` 已通过；`dzc_opengl_backend_real_context` 返回 `77` 并由 CTest 明确标记 Skipped，真实 Context 生命周期留给后续集成层。
- **历史交接（GL-007 完成时）**：当时 GL-011 至 GL-012 尚未完成，OpenGL Renderer 模块整体保持“进行中”，模块级验收不得标记完成；当前状态见 GL-012 与模块级验收。
- **追踪**：4.3、14.2、NFR-REL-002
### GL-008 实现逐 Chunk 点绘制

- **状态**：已完成（2026-08-26）
- **目标**：每可见 Chunk 一次 `glDrawArrays(GL_POINTS)`。
- **前置任务**：GL-007、grid-chunking/GC-009
- **实际文件**：`src/render/opengl/OpenGLBackend.cpp`、`src/render/opengl/GlDrawOperations.h`、`src/render/opengl/GlDrawOperations.cpp`、`tests/graphics/OpenGLDrawTests.cpp`
- **实现结果**：在外部 Context、GLAD 和能力检查成功后，Backend 持久创建 Frame UBO、单记录 Chunk SSBO 和默认点云 Shader；FrameData 绑定到 binding 0，ChunkData 绑定到 binding 1。`render()` 先对全部 `RenderFrame.draws` 做资源有效性、点数一致性和 `GLsizei` 范围预检查，再按 draws 顺序上传 FrameData/ChunkData、清除当前帧颜色、绑定 Shader/VAO，并对每个可见 Chunk 提交一次 `GL_POINTS`。未列入 draws 的已上传 Chunk 不提交。
- **错误与统计**：缺少资源、点数不一致、资源无效或注入式 GL 操作失败均返回 `RenderFailed`；预检查失败时不发生任何绘制操作，运行时操作失败不更新成功统计。`drawCount()` 和 `submittedPointCount()` 表示最近一次成功 render 的统计，失败保留上一成功帧，空 draws 是零统计成功帧。
- **测试结果**：新增 `dzc_opengl_draw` Fake 操作表测试，覆盖资源创建、单/多 Chunk Draw、空和不可见列表、缺少资源、点数不一致、FrameData/ChunkData、binding 0/1、relative origin、失败统计保留；OpenGL-only 相关 CTest 共 14 项，其中 8 项通过、6 个真实 Context 测试按约定返回 77 并由 CTest 标记 `Skipped`。
- **历史交接（GL-008 完成时）**：GL-008 不创建真实 Context，不引入 Qt/WGL/EGL；当时 GL-009 着色、GL-010 resize 投影适配、GL-011 timer query 和 GL-012 集成测试仍由各自任务负责，OpenGL Renderer 模块保持进行中；当前这些任务均已完成。
- **追踪**：FR-REN-001、DDD-014、AC-P1-008

### GL-009 实现四种着色和点大小

- **状态**：已完成（2026-08-26）
- **目标**：完成 OriginalColor/FixedColor/Height/Intensity 四种着色，并使 pointSize 在后续帧生效。
- **前置任务**：GL-004, GL-008
- **实际文件**：`src/render/common/RenderBackendTypes.h`、`src/render/opengl/OpenGLBackend.*`、`src/render/opengl/GlDrawOperations.*`、`shaders/opengl/point_cloud.vert`、`shaders/opengl/point_cloud.frag`、`tests/graphics/OpenGLShadingTests.cpp`
- **实现结果**：`RenderFrame` 新增 `heightRange`/`intensityRange` 两个 `glm::vec2` 字段；`update()` 校验有限值和 `min <= max`，`FrameData` 将范围写入 vec4 前两个分量。Vertex Shader 使用 FrameData.pointSize 设置 `gl_PointSize`，传递原始 Color、Intensity 和 Chunk 相对高度；Fragment Shader 实现四种模式及蓝—青—绿—黄—红五段线性伪彩色带，退化范围固定归一化为 `0.5`，Height/Intensity 输出 alpha 为 `1.0`，OriginalColor/FixedColor 保留输入 alpha。
- **缺失属性语义**：OriginalColor 缺少 Color、Intensity 模式缺少 Intensity 时使用 `fixedColor`，不读取无效属性；通过注入的 `dzc::diagnostics::ILogSink` 按属性类别只记录一次 Warn，记录 OpenGL 模块、ChunkId、FrameId 和回退原因；告警去重状态在初始化后生效并在 shutdown 清理。
- **操作表和错误语义**：私有 `IGlDrawOperations`/GLAD 实现新增 per-draw uint uniform 设置，继续保持 UBO binding 0、SSBO binding 1、VAO 和逐 Chunk `GL_POINTS` 顺序。FrameData、uniform、ChunkData、VAO 或 Draw 任一操作失败时返回 `RenderFailed`，上一成功帧统计保持不变。
- **验收检查**：Fake 覆盖四种模式、pointSize 后续帧、范围上传、退化范围 Shader 契约、固定五段色带 Shader 契约、缺失属性回退及一次告警、uniform 失败、Shader 初始化失败清理和 GL-008 绘制统计回归；真实 Context 用例返回 77 并由 CTest 标记 Skipped。
- **测试要求与历史状态**：`dzc_opengl_shading` 通过；`dzc_opengl_shading_real_context` 明确 Skipped。该任务完成时 GL-011 至 GL-012 尚未完成，OpenGL Renderer 模块级验收尚未完成；当前状态见 GL-012 与模块级验收。
- **追踪**：FR-REN-002/003、AC-P1-007
### GL-010 实现 resize、viewport 与投影适配

- **状态**：已完成（2026-08-27；该任务完成时 OpenGL Renderer 模块仍为“进行中”，当前模块已完成）
- **目标**：按逻辑 `RenderSize` 和 DPR 设置物理 viewport，并同步 resize、零尺寸暂停和帧尺寸校验；投影矩阵继续由相机层生成。
- **前置任务**：GL-007、camera-abstraction/CA-004
- **实际文件**：`src/render/opengl/GlDrawOperations.h`、`src/render/opengl/GlDrawOperations.cpp`、`src/render/opengl/OpenGLBackend.h`、`src/render/opengl/OpenGLBackend.cpp`、`tests/graphics/OpenGLResizeTests.cpp`
- **实现结果**：新增私有 `setViewport(x, y, width, height)` 操作表；逻辑尺寸按 `round(width * devicePixelRatio)` / `round(height * devicePixelRatio)` 转换为物理 viewport，拒绝非有限或非正 DPR、舍入为零及超出 `GLsizei` 的物理尺寸。初始化在持久资源创建成功后设置一次初始 viewport；有效 resize 只有 viewport 操作成功后才更新 Backend 尺寸。
- **暂停与帧同步**：宽或高为零且 DPR 有效时进入零尺寸暂停，保留 GPU 资源、清除缓存帧且不提交 viewport；暂停期间 `update()` 失败，`render()` 成功跳过所有 OpenGL 提交并将最近成功统计置零。恢复有效尺寸后必须按新逻辑尺寸重新生成相机矩阵并 `update()`；非暂停 `update()` 要求 `RenderFrame.size` 与当前 Backend 逻辑尺寸完全一致。
- **投影职责**：Backend 不重算、不转换投影矩阵，直接上传调用方通过 `ICameraController::matrices(RenderSize)` 生成的、采用 OpenGL `NegativeOneToOne` 深度约定的矩阵；逻辑 aspect 由相机使用逻辑宽高计算，不受 DPR 改变。
- **测试结果**：新增 `dzc_opengl_resize` 及真实 Context Skipped 用例，Fake 覆盖初始/重复 viewport、DPR 物理尺寸、`OrbitCameraController` 投影原样上传、尺寸同步、viewport 失败回滚、零宽/零高暂停恢复、物理尺寸边界和初始化资源清理。使用 OpenGL-only 配置（OpenGL=ON、Vulkan/CUDA=OFF、Tests=ON）构建成功；`ctest --test-dir build-dg010 -C Debug -L graphics --output-on-failure` 共 17 项通过，其中 8 项真实 Context 用例按 `SKIP_RETURN_CODE 77` 明确 Skipped；已在验证后清理 `build-dg010`。
- **历史交接（GL-010 完成时）**：不引入 Qt/WGL/EGL 或真实 Context 创建设施；当时 GL-012 OpenGL 后端集成测试尚未完成，模块级验收不得标记完成；当前 GL-012 与模块级验收均已完成。
### GL-011 实现 GPU Timer Query

- **状态**：已完成（2026-09-02；该任务完成时 OpenGL Renderer 模块仍为“进行中”，当前模块已完成）
- **目标**：使用固定查询池延迟读取 GPU 帧时间，不同步等待当前帧。
- **前置任务**：GL-008, diagnostics/DG-006
- **实际文件**：`src/render/opengl/GlTimerQueryPool.h`、`src/render/opengl/GlTimerQueryPool.cpp`、`src/render/opengl/OpenGLBackend.h`、`src/render/opengl/OpenGLBackend.cpp`、`include/dzc/EngineSnapshot.h`、`src/diagnostics/MetricsRegistry.h`、`src/diagnostics/MetricsRegistry.cpp`、`tests/graphics/FakeTimerQueryOperations.h`、`tests/graphics/GlTimerQueryTests.cpp` 及相关 CMake/回归测试文件。
- **实现结果**：新增固定 3 帧延迟的 `GlTimerQueryPool`。当前帧只执行 begin/end，只检查至少 3 帧前的槽位；结果未就绪时返回空值并保留槽位，该帧跳过新的计时但继续正常绘制。实现不调用 `glFinish`，GLAD 类型和查询句柄仅存在于 `.cpp` 默认操作表中。
- **Backend 接入**：`OpenGLBackend` 在 Context、GLAD 与能力检查成功后创建查询池，支持注入 Fake 操作表；每次有效渲染先解析延迟结果，再在绘制前后 begin/end，并通过 `latestGpuFrameMilliseconds()` 暴露 `std::optional<double>`。零尺寸暂停帧清空最新值；shutdown 在 Context 所属线程释放查询资源，失败记录既有 Backend 错误。
- **指标语义**：`PerformanceMetrics::gpuFrameMilliseconds` 与公共 `PerformanceSnapshot::gpuFrameMilliseconds` 统一为 `std::optional<double>`；`nullopt` 表示未就绪，已完成结果只接受有限且非负的毫秒值。CSV 与 Markdown Writer 保持空值输出语义。
- **测试结果**：OpenGL-only 构建成功；GL-011 Fake 覆盖 3 帧延迟、未就绪、创建/begin/end/可用性/读取/释放失败、线程令牌、移动和跨线程析构，并覆盖 Backend 初始化、渲染、暂停及 shutdown。相关专项/诊断/Writer 回归 13 项全部通过，其中 4 项真实 Context 用例按返回码 77 明确 Skipped；完整 graphics 回归 19 项全部通过，其中 9 项真实 Context 用例明确 Skipped。
- **限制与交接**：未创建 Qt/WGL/EGL 或真实 OpenGL Context 基础设施；真实 Context 验证继续以返回 77 的 Skipped 用例明确表达能力限制。GL-012 已完成，OpenGL Renderer 模块级验收已通过。
- **追踪**：FR-STAT-001、15

### GL-012 完成 OpenGL 后端集成测试

- **状态**：已完成（2026-09-02）
- **目标**：从规范化 Chunk 到注入式 OpenGL 后端渲染跑通公共功能。
- **前置任务**：GL-009, GL-010, GL-011
- **实际文件**：`tests/integration/OpenGLRenderPipelineTests.cpp`、`tests/integration/CMakeLists.txt`
- **实现结果**：使用测试专用 Context、OpenGL 4.5 Core 能力、绘制、Shader 与 Timer Query Fake，覆盖 `GridBucket → GridChunkBuilder → CpuResident Chunk → UploadBatch → OpenGLBackend` 完整链路；未增加公共 API，也未引入 Qt、WGL、EGL 或真实 Context。
- **验收检查**：3 点 Position/Color/Intensity Chunk 的三条 VBO、VAO location 0/1/2、FrameData UBO binding 0、ChunkData SSBO binding 1 均已验证；OriginalColor、FixedColor、Height、Intensity 四帧各提交一次等价 `GL_POINTS` 绘制，Fake 累计 4 次绘制和 12 点，Backend 每帧统计为 1 次绘制和 3 点。
- **resize 与计时**：验证 `320×240@1.0` 到 `640×360@1.5` 后物理 viewport 为 `960×540`；固定三帧延迟查询返回有限、非负的 2.5 ms GPU 帧时间，无同步等待或回读。
- **资源与错误**：显式 `release(chunkId)` 释放 Chunk VAO/VBO；`shutdown()` 释放 Program、Shader、UBO、SSBO 和三枚 Timer Query。错误 Shader 场景通过 `IGlShaderOperations` 注入编译失败，保留诊断并验证初始化创建的持久 Buffer 与临时 Shader 全部回滚，Timer Query 尚未创建，后续 `shutdown()` 安全且无资源残留。
- **测试结果**：`dzc_opengl_render_pipeline` 通过；`dzc_opengl_render_pipeline_real_context` 输出项目尚无真实 Context 基础设施并返回 77，CTest 明确显示 Skipped。完整 `opengl` 标签回归 22/22 无失败，其中 10 项真实 Context 用例明确 Skipped；构建目录未发现或复制临时 DLL。
- **限制**：本任务的“离屏渲染”验收为注入式 Fake 完整管线验证，不包含真实 framebuffer 像素读取、截图或图像哈希。
- **追踪**：AC-P1-002/006/007/010/013

## 6. 模块级验收

- [x] OpenGL 4.5 Core 后端可独立初始化和关闭
- [x] VAO/VBO/UBO/SSBO 与统一 Shader 布局正确
- [x] 可见 Chunk 一块一次 GL_POINTS 绘制且四种着色可用
- [x] GPU 计时和 resize 测试通过且无同步回读

## 7. 交接记录

- 完成日期：2026-08-26
- 完成人：Codex（按主人确认执行）
- 关键变更：GL-001 至 GL-003 保持已完成。GL-004 新增不暴露 OpenGL/GLAD 类型的 `GlShaderProgram` 和独立 `IGlShaderOperations`，支持源码与文件入口、Vertex/Fragment 运行时编译、Program 链接、阶段化错误日志、移动语义、显式 reset 和线程令牌约束。新增 `#version 450 core` 的最小点云 Vertex/Fragment shader fixture，固定 location 0/1/2 与 binding 0/1；GLAD 真实操作仅在 `.cpp` 内实现。
- 未解决问题（该任务完成时）：GL-007 至 GL-012 尚未开始；GL-002、GL-003、GL-004、GL-005 的真实 Context 测试均明确 Skipped，GLAD loader 初始化、OpenGL Backend 生命周期和正常 shutdown 资源清理留给 GL-007；这些后续任务与 OpenGL Renderer 模块级验收现已全部完成。
- 测试命令与结果：GL-001 至 GL-003 的既有配置、能力和资源回归保持通过。GL-004 使用 MSVC 19.51/NMake 配置 `build-gl004`（OpenGL=ON、Vulkan/CUDA=OFF、Tests=ON）；`cmake --build build-gl004 --target dzc_gl_shader_tests` 成功，包含 `GlShaderProgram.cpp` 和 GLAD 的 `dzc_render_opengl` 构建成功；`ctest --test-dir build-gl004 -R "^dzc_gl_shader$|^dzc_gl_shader_real_context$" --output-on-failure` 为 `1/1` 通过、真实 Context 用例明确 Skipped。已执行 `git diff --check`，未创建 Git commit。
- 关联提交：未提交（未创建 Git commit）。

### GL-005（2026-08-26）

- 关键变更：新增后端无关 `dzc::render::FrameData` 和 `ChunkData`，CPU 侧使用 `std::array`、标量和显式 `alignas(16)`/填充，不把 `glm::mat4` 或 `glm::vec4` 作为 GPU ABI 成员。FrameData 按 std140 binding 0 固定为 208 字节，ChunkData 按 std430 binding 1 固定为 16 字节数组 stride；矩阵辅助函数按 GLM 列主序写入。
- OpenGL 实现：新增 `OpenGLShaderData` 的布局验证和已链接 Program 反射查询入口，使用 GLAD 查询 UBO block size/offset、SSBO block binding 和 buffer variable offset/array stride。未创建 Context，未初始化 GLAD loader，未实现 Buffer 上传、绘制或完整着色。
- Shader 同步：`point_cloud.vert` 使用完整 FrameData 字段、view/projection 变换和 `relativeChunkOrigin[0]`；固定 `#version 450 core`、location 0/1/2、binding 0/1；当时 Fragment Shader 仅输出输入 Color；四种着色已在后续 GL-009 实现。
- 验证结果：MSVC 19.51.36246.0 x64 / NMake Makefiles、OpenGL=ON、Vulkan/CUDA=OFF、Tests=ON 配置成功；`dzc_render_opengl` 和 `dzc_shader_layout_tests` 构建成功；`dzc_shader_layout` 通过，真实 Context 用例明确 Skipped；`git diff --check` 已执行并通过。
- 未解决问题（该任务完成时）：真实 OpenGL Context、GLAD loader 初始化和运行时 block 反射验证留给 GL-007；当时 GL-007 至 GL-012 尚未开始；这些后续任务与 OpenGL Renderer 模块级验收现已全部完成，真实 Context 限制仍保留。
- 关联提交：未提交（未创建 Git commit）。

### GL-006（2026-08-26）

- 关键变更：新增 `GlChunkResource` 和继承 GL-003 资源操作的 `IGlChunkUploadOperations`，将 `ChunkCpuData` 的 Position/Color/Intensity SoA 显式打包为三条 `GL_ARRAY_BUFFER`/`GL_STATIC_DRAW` VBO，并创建/配置对应 VAO。固定属性为 location 0（3×Float32）、location 1（4×normalized UInt8）、location 2（1×normalized UInt16）。
- Schema 分支：支持 XYZ、XYZRGB、XYZI 和 XYZRGBI；缺失 Color 生成 RGBA `(255,255,255,255)`，缺失 Intensity 生成 `65535`；颜色不依赖主机端序，所有流字节数和统计由包装器显式计算。
- 生命周期：上传、替换和 `reset()` 受 `GlContextThreadToken` 约束，临时资源先完成上传后再替换旧资源；失败时保留旧资源并尽力清理临时资源；资源包装器保持不可复制、可移动、析构 `noexcept` 和待释放状态。
- 验证结果：MSVC 19.51.36246.0 x64 / NMake Makefiles、OpenGL=ON、Vulkan/CUDA=OFF、Tests=ON 配置和构建成功；`dzc_render_opengl` 与 `dzc_gl_chunk_upload_tests` 构建成功，`dzc_gl_chunk_upload` Fake 测试通过；未创建真实 Context。
- 未解决问题（该任务完成时）：ChunkData SSBO、FrameData UBO、绘制、Backend 生命周期、真实 Context 和 GLAD loader 初始化留给 GL-007；当时 GL-007 至 GL-012 尚未开始；这些后续任务与 OpenGL Renderer 模块级验收现已全部完成，真实 Context 限制仍保留。
- 关联提交：未提交（未创建 Git commit）。

### GL-011（2026-09-02）

- 关键变更：新增固定 3 帧延迟的 `GlTimerQueryPool`、私有 GLAD 操作表和 Fake 操作表；接入 `OpenGLBackend` 的初始化、逐帧解析/begin/end、零尺寸暂停与 shutdown 生命周期，并增加 `latestGpuFrameMilliseconds()` 可选结果接口。
- 公共指标：`PerformanceMetrics` 与 `PerformanceSnapshot` 的 GPU 帧时间统一为 `std::optional<double>`；未就绪清空，已就绪结果仅接受有限且非负值；CSV 与 Markdown Writer 空值回归已补齐。
- 验证结果：OpenGL-only 相关目标构建成功；GL-011/Backend/诊断/快照/Writer 回归 13 项全部通过，4 项真实 Context 用例明确 Skipped；完整 graphics 回归 19 项全部通过，9 项真实 Context 用例明确 Skipped；未复制仓库外 DLL。
- 未解决问题：未创建真实 OpenGL Context；相关用例继续按返回码 77 明确 Skipped。GL-012 与 OpenGL Renderer 模块级验收已于 2026-09-02 完成。
- 关联提交：未提交（未创建 Git commit）。

### GL-012 / OpenGL Renderer 完成（2026-09-02）

- 关键变更：新增 `OpenGLRenderPipelineTests.cpp` 与两项 integration CTest，使用注入式 Fake 验证真实 `GridChunkBuilder` 输出进入 `OpenGLBackend` 后的上传、四种着色、resize/DPR、逐 Chunk 绘制、统计、三帧延迟 GPU 计时和完整资源生命周期。
- 错误路径：Shader 编译失败保留稳定诊断，初始化期间已创建的 Buffer 与临时 Shader 全部回滚，Timer Query 不创建；后续 `shutdown()` 安全且 Fake 活动资源集合为空。
- 验证结果：GL-012 专项 Fake 通过，真实 Context 用例按 77 明确 Skipped；完整 `opengl` 标签回归 22/22 无失败，其中 10 项 Skipped。未发现或复制临时 DLL。
- 模块结论：GL-001 至 GL-012 全部完成，四项模块级验收全部通过，OpenGL Renderer 状态更新为“已完成”；Phase 1 阶段门禁仍未完成。
- 真实 Context 限制：项目尚无 Qt/WGL/EGL/headless/offscreen Context 基础设施，未伪造真实渲染成功，也未执行 framebuffer 像素读取。
- 关联提交：未提交（按主人要求不创建 Git commit）。

## 8. 变更约束

若实现需要改变公共接口、模块依赖、已确认参数或需求行为，必须先更新需求与设计文档，不得在编码任务中自行改变。
