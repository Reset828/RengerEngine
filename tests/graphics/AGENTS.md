# OpenGL Graphics Test Guide

## OVERVIEW
- Tests `dzc_render_opengl` behavior through injected GL/context operation interfaces.
- This suite is added only when `DZC_ENABLE_OPENGL=ON`.
- Each executable uses C++17 and CTest labels `graphics;opengl;glNNN`.
- Standard tests are fake-operation deterministic; real-context siblings are optional probes.

## Target map
- `OpenGLCapabilitiesTests.cpp` -> `dzc_opengl_capabilities` -> `gl002`.
- `GlResourceTests.cpp` -> `dzc_gl_resource` -> `gl003`.
- `GlShaderTests.cpp` -> `dzc_gl_shader` -> `gl004`.
- `GlChunkUploadTests.cpp` -> `dzc_gl_chunk_upload` -> `gl006`.
- `OpenGLBackendLifecycleTests.cpp` -> `dzc_opengl_backend_lifecycle` -> `gl007`.
- `OpenGLDrawTests.cpp` -> `dzc_opengl_draw` -> `gl008`.
- `OpenGLShadingTests.cpp` -> `dzc_opengl_shading` -> `gl009`.
- `OpenGLResizeTests.cpp` -> `dzc_opengl_resize` -> `gl010`.
- `GlTimerQueryTests.cpp` -> `dzc_gl_timer_query` -> `gl011`.
- Shader tests define `DZC_SOURCE_DIR` for repository shader fixture paths.
- Timer tests may include this directory privately for shared fake support.

## Fake-operation design
- Implement the full relevant interface, not a partial raw-GL shortcut.
- Record resource IDs, deleted IDs, bind/upload calls, uniforms, viewport, clear, and draw calls.
- Add controllable failure flags for every error branch under test.
- Context fake must track currentness and function-loader state separately.
- Capability fake supplies version, profile, limits, and device strings.
- Shader fake supplies source, compile, link, delete, and diagnostic behavior.
- Timer fake supplies creation, availability, result, begin/end, and delete behavior.

## Resource/thread invariants
- Capture `GlContextThreadToken::current()` on the resource-owning thread.
- Valid reset/delete uses the owner token and deletes exactly once.
- Move construction/assignment transfers ownership; moved-from state is harmless.
- Foreign-thread reset fails and sets release-pending behavior.
- Foreign-thread noexcept destruction skips deletion; assert no fake delete call.
- Zero ID and uninitialized pool paths stay safe.
- Partial creation failure cleans up objects already created.

## Draw/pipeline invariants
- OpenGL 4.5 core is accepted; unsupported version/profile is rejected.
- Chunk upload requires position data and schema-consistent optional streams.
- Buffer byte statistics and vertex attributes match point count/schema.
- Visible draw chunk must resolve to valid uploaded resource.
- Requested draw count must equal uploaded point count and fit `GLsizei`.
- Rendering requires a successful prior `update()`.
- Resize validates context/state and propagates viewport/resource errors.
- Shading tests preserve enum-to-uniform/ABI mapping.
- Timer queries use delayed slots (`queryDelayFrames == 3`) and never overlap active queries.
- Failure after a timer begin must still close/recover the active query.

## Real-context convention
- Sibling tests pass `--real-context`.
- Label them with `skipped` and set `SKIP_RETURN_CODE 77`.
- A missing driver/context returns `77`; it is not an ordinary test failure.
- Keep fake coverage authoritative for CI and developer machines without GL context setup.

## ANTI-PATTERNS
- Do not call real GL from a fake test path.
- Do not treat cross-thread destruction as permission to delete GL handles.
- Do not assert only boolean success; inspect calls, IDs, state, and error codes.
- Do not hardcode a local shader path; use `DZC_SOURCE_DIR` when fixture files are needed.
- Do not remove the real-context skip contract to make a local environment appear healthy.
