# Integration Test Guide

## OVERVIEW
- End-to-end seams across data readers, batching, tasks, visibility, and optional OpenGL pipeline.
- Configure every executable in `tests/integration/CMakeLists.txt`.
- Use temporary files/directories and generated fixture bytes; do not require external datasets.
- All ordinary integration executables compile as C++17.

## Target map
- `PcdReaderTests.cpp` -> `dzc_pcd_reader` -> PCL/data/tasks/engine API.
- `PcdBatchTests.cpp` -> `dzc_pcd_batch` -> PCL/data/tasks/engine API.
- `PlyReaderTests.cpp` -> `dzc_ply_reader` -> PCL/data/tasks/engine API.
- `PlyBatchTests.cpp` -> `dzc_ply_batch` -> PCL/data/tasks/engine API.
- `PointCloudLoadTaskTests.cpp` -> `dzc_point_cloud_load_task` -> data/tasks/engine API.
- `ReaderProgressTests.cpp` -> `dzc_reader_progress` -> data/tasks/engine API.
- `GridVisibilityTests.cpp` -> `dzc_grid_visibility` -> engine core/data/API.
- `OpenGLRenderPipelineTests.cpp` is conditional: `dzc_opengl_render_pipeline`.
- Pipeline test links `dzc_render_opengl` and `dzc_data_core`.

## Reader fixtures
- Build PCD/PLY source files inside a temporary directory.
- Preserve the source bytes when `open()` only inspects metadata.
- Cover ASCII and binary layouts where the reader supports them.
- Cover position-only, color, alpha/RGBA, intensity, and unknown/ignored properties.
- Validate declared point count and attribute schema after open.
- Header validation must report malformed metadata as `DataFormat` corruption.
- Zero-point input is valid when the format header permits it.
- Metadata loading is deferred; do not assume `open()` consumes point batches.
- EOF is stable: subsequent reads keep returning no batch.
- Invalid reader lifecycle and maximum-point requests are explicit negative cases.

## Data/task invariants
- Batched results must preserve point count and expected attributes.
- RGB fallback must provide opaque alpha when the contract requires RGBA.
- Progress tracks consumption without exceeding source semantics.
- Cancellation/failure paths must leave observable state coherent.
- Visibility uses data/engine contracts rather than rendering implementation details.

## OpenGL pipeline convention
- Use injected fake context, capability queries, chunk operations, draw operations, shader operations.
- Fakes must model `makeCurrent`, loading, currentness, and release state explicitly.
- The normal pipeline test remains deterministic and does not require a driver.
- `--real-context` registers a sibling CTest name with labels `integration;graphics;opengl;gl012;skipped`.
- Return `77` when a real context cannot be established; never convert that to a failure.
- Keep pipeline assertions on upload/update/render/resize and error propagation.

## ANTI-PATTERNS
- Do not read repository-local or machine-local point-cloud files as fixtures.
- Do not mutate fixture content after taking a before/after source-byte assertion.
- Do not hide corrupted-header behavior behind generic I/O failures.
- Do not force PCL dependency into unrelated integration targets.
- Do not use a live OpenGL context for ordinary coverage when injected operations suffice.
