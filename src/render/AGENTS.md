# Render Abstraction Guide

## OVERVIEW
- Owns backend-neutral render contracts and concrete backend subdirectories.
- `dzc_render_api` is an interface target.
- `dzc_render_opengl` is concrete only when `DZC_ENABLE_OPENGL=ON`.
- Disabled OpenGL still provides an interface placeholder linked to `dzc_render_api`.
- `dzc_render_vulkan` is currently an interface boundary linked to `dzc_render_api` and platform.

## Where to work
- `common/RenderBackendFactory.h`: `IRenderBackend` and factory seam.
- `common/RenderBackendTypes.h`: uploads, visible draws, frames, context operations.
- `common/ShaderData.h`: CPU-to-shader ABI records and bindings.
- `opengl/`: OpenGL 4.5 implementation.
- `vulkan/`: reserved backend boundary; do not make OpenGL assumptions there.

## Lifecycle contract
- `init(RenderBackendConfig)` establishes backend/context-owned resources.
- `upload(UploadBatch)` transfers chunk CPU data to backend resources.
- `update(RenderFrame)` supplies the next renderable frame.
- `render()` consumes a successfully updated frame.
- `resize(RenderSize)` changes render-target dimensions.
- `release(ChunkId)` drops a chunk resource without throwing.
- `shutdown()` is noexcept and must tolerate partial initialization.
- Platform/application code owns context creation, currentness, function loading, release.
- `IRenderContextOperations` remains the only context-control seam here.

## Frame invariants
- `DrawChunk.chunkId` identifies one uploaded chunk resource.
- `DrawChunk.pointCount` must match the backend resource point count.
- `relativeOrigin` is camera-relative, not source-world precision data.
- `RenderFrame` carries camera matrices, size, clear color, point size, shading, ranges, draws.
- `ChunkUpload` couples `ChunkMetadata` with matching `ChunkCpuData`.
- Keep upload schemas and stream contents consistent.

## Shader ABI
- `FrameData` is std140, 16-byte aligned, and fixed at 208 bytes.
- `ChunkData` is std430, 16-byte aligned, and fixed at 16 bytes.
- Preserve explicit offsets and static assertions in `ShaderData.h`.
- Matrices are copied in GLM/GLSL column-major order.
- Uniform binding is `frameDataBinding == 0`.
- storage binding is `chunkDataBinding == 1`.
- Convert `ShadingMode` only through fixed-width shader data helpers.

## Dependency boundary
- `dzc_render_api` depends on `dzc_engine_api` and `dzc_data_core`.
- Render libraries must not depend on `dzc_app`.
- UI context ownership stays above render; Qt Widgets stays in `dzc_app` only.
- PCL belongs to `dzc_data_pcl`, never render.

## Test map
- Contract factory coverage: `dzc_factory_contract`.
- Shader layout coverage: `dzc_shader_layout` when OpenGL is enabled.
- Backend integration: `dzc_opengl_render_pipeline`.
- OpenGL component coverage lives in `tests/graphics/`.

## ANTI-PATTERNS
- Do not leak API-specific handles through `IRenderBackend` or public render structs.
- Do not make rendering reach into Qt widgets or application state.
- Do not mutate GPU layout constants without matching GLSL and shader-layout coverage.
- Do not render stale data before `update()` succeeds.
- Do not use source-space doubles as a silent replacement for relative GPU coordinates.
