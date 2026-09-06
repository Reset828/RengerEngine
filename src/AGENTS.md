# src scope

## OVERVIEW
C++17 engine modules beneath `src`; composition stays at the application edge.

## TARGETS
- `dzc_app`: Qt executable; only application target links Qt Widgets.
- `dzc_engine_api`: public interface target over `include/dzc`.
- `dzc_engine_core`: engine loop, dataset session, scene, camera.
- `dzc_data_core`: chunking, dataset model, reader orchestration.
- `dzc_data_pcl`: concrete PCD/PLY readers; sole PCL boundary.
- `dzc_tasks`: worker execution, cancellation, bounded flow control.
- `dzc_diagnostics`: logs, metrics, frame/performance writers.
- `dzc_render_*`, `dzc_compute_*`, `dzc_platform`: backend capability targets.

## DOMAIN FLOW
- UI event -> `EngineUiAdapter` -> validated `EngineCommand`.
- `Engine` drains commands and task completions through `EngineCoordinator` stages.
- Reader task emits opened/progress/batch/error events into the engine-facing flow.
- Chunk/data code turns batches into resident `Chunk`/`Dataset` state.
- Scene visibility describes draws; render backends upload and render visible chunks.
- Diagnostics records frame, transfer, residency, LOD, task, and compute metrics.
- Snapshots/events are the read boundary consumed by Qt presentation code.

## WHERE TO LOOK
- `src/app`: CLI/settings, Qt window, adapter, OpenGL widget.
- `src/data`: dataset assembly; see `chunk` and `io` child guidance.
- `src/engine`: lifecycle, commands, coordinator, dataset session.
- `src/tasks`: execution primitives shared by engine and data loading.
- `src/diagnostics`: asynchronous logs and performance output.
- `src/render`, `src/compute`, `src/platform`: backend implementations and factories.
- `src/scene`, `src/camera`: frame visibility and camera math linked into engine core.

## BUILD BOUNDARIES
- `src/CMakeLists.txt` creates and validates the foundation target graph.
- `dzc_app` links selected OpenGL/Vulkan/CUDA capabilities at the composition root.
- `dzc_engine_core` depends on APIs/data/tasks/diagnostics, not Qt or PCL.
- `dzc_data_pcl` is the only target expected to discover/link PCL.
- Backend SDK handles stay behind render/compute interfaces and private implementations.

## ANTI-PATTERNS (THIS TREE)
- Do not move UI or Qt types into engine, data, task, or backend-core interfaces.
- Do not make a module reach around its target dependency direction via includes.
- Do not bypass command/event/completion queues with shared mutable cross-thread state.
- Do not make PCL a transitive dependency of `dzc_data_core` or `dzc_engine_core`.
- Do not expose `GLuint`, Vulkan handles, CUDA handles, or Qt widgets across APIs.
- Do not silently drop cancellation, queue-full, backpressure, or structured errors.
- Do not add a second ad-hoc application composition path beside `ApplicationComposition`.
