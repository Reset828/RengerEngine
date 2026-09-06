# OpenGL Backend Guide

## OVERVIEW
- Implements static target `dzc_render_opengl`.
- Enabled only by `DZC_ENABLE_OPENGL`; requires configured GLAD.
- Target sources include generated GLAD plus all resource, shader, capability, timer, draw, backend files.
- Public link: `dzc_render_api`; private links: `OpenGL::GL`, `dzc_diagnostics`.
- Shader directory compile definition resolves to `shaders/opengl`.

## Entry points
- `OpenGLBackend`: `IRenderBackend` implementation and state/lifecycle coordinator.
- `OpenGLCapabilities`: OpenGL 4.5-core capability inspection and limits.
- `GlDrawOperations`: GLAD-backed operation table; keep it injectable.
- `GlBuffer` / `GlVertexArray`: move-only, context-thread-owned handles.
- `GlShaderProgram`: compile/link from source or files; move-only program ownership.
- `GlChunkResource`: VAO plus position/color/intensity streams for one chunk.
- `GlTimerQueryPool`: delayed elapsed-time query rotation.
- `OpenGLShaderData`: backend-facing shader data adaptation.

## Context and threading
- Acquire current context before GL initialization or operations.
- Load GL functions through `IRenderContextOperations`, not direct UI code.
- `GlContextThreadToken::current()` captures the creating/current thread.
- Creation, reset, and explicit deletion require a token for the owner thread.
- Cross-thread reset returns an `InvalidThreadToken` failure and marks release pending.
- Noexcept destructors must skip GL deletion on a foreign thread.
- Move operations transfer ownership and leave the source harmless.
- Shutdown must tolerate failed or partial resource initialization.

## Resource invariants
- ID zero means invalid/uncreated resource.
- Label only valid objects and preserve diagnostic labels when supported.
- Chunk upload requires position data; color/intensity streams follow `AttributeSchema`.
- Chunk stats must reflect byte counts, schema, point count, and validity.
- Replacing a chunk deletes its prior owned GL resources exactly once.
- VAO/array-buffer binding and unbinding must balance on every success/failure path.
- Draw chunks must refer to existing valid resources.
- Draw point count must equal uploaded resource point count.
- Reject point counts exceeding `GLsizei` range.

## Backend sequencing
- `init` validates context/capabilities and creates draw resources.
- `upload` creates or replaces resources keyed by chunk ID.
- `update` stores a valid frame for the next render.
- `render` fails if no successful update supplied a frame.
- Render resolves prior GPU timing before beginning its next query.
- Begin/end timer pairing must be cleaned up when drawing fails.
- `resize` validates size/context and updates backend state.
- `release` and `shutdown` must not throw through the render boundary.

## Shader/ABI rules
- Preserve std140 `FrameData` and std430 `ChunkData` sizes, alignment, offsets, bindings.
- Bind frame UBO at 0 and chunk SSBO at 1.
- Use `GL_DYNAMIC_DRAW` through the injected draw operation contract.
- Keep shader source file failures distinguishable: unreadable, empty, compile, link.
- Report OpenGL errors through `Result` with meaningful context.

## Test map
- Capabilities: `dzc_opengl_capabilities` / `gl002`.
- Resources: `dzc_gl_resource` / `gl003`.
- Shader programs: `dzc_gl_shader` / `gl004`.
- Chunk upload: `dzc_gl_chunk_upload` / `gl006`.
- Lifecycle/draw/shading/resize/timer: `gl007` through `gl011`.
- End-to-end pipeline: `dzc_opengl_render_pipeline` / `gl012`.

## ANTI-PATTERNS
- Do not call raw GL directly outside the relevant injected GL operation implementation.
- Do not delete GL resources from arbitrary threads or destructors without owner checks.
- Do not claim function availability merely because a context exists.
- Do not ignore failed unbind, upload, shader, or timer operations.
- Do not silently clamp inconsistent chunk counts or substitute missing attribute streams.
- Do not bypass real-context skip semantics in graphics/integration tests.
